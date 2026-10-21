/*
 * details.c — implementation of cmd_details (--details).
 *
 * Full flow with verbose output at every step:
 *   1. Compose output (hex + structured + 3 schemas).
 *   2. Network step with server/attempt logging.
 *   3. Raw response hex dump + structured + schemes.
 *   4. Decode RCODE, flags, answer RRs.
 *   5. Summary: domain -> IP (A or AAAA depending on --ipv6).
 */

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "engine/dns.h"
#include "decor/print.h"
#include "net/net.h"
#include "command/command.h"

int cmd_details(const char *domain, const cmd_options_t *options) {
    uint16_t qtype = (options && options->qtype) ? options->qtype : DNS_TYPE_A;
    const char *type_name = (qtype == DNS_TYPE_AAAA) ? "AAAA (IPv6)" : "A (IPv4)";

    const char *explicit_server = (options && options->server) ? options->server : NULL;
    char *server = dns_resolve_server(explicit_server);

    int timeout = (options && options->timeout_ms > 0) ? options->timeout_ms : DNS_DEFAULT_TIMEOUT_MS;
    int retries = (options && options->retries > 0)   ? options->retries    : DNS_DEFAULT_RETRIES;

    printf("=== DNS Full Details ===\n");
    printf("Domain: %s\n", domain);
    printf("Type:   %s\n", type_name);
    printf("Server: %s:53\n", server);
    printf("Timeout: %d ms, retries: %d\n\n", timeout, retries);

    /* ---- Step 1: Compose the request ---- */
    uint8_t packet[DNS_MAX_PACKET];
    int pkt_len = compose_request(domain, qtype, packet, sizeof(packet), 1);
    if (pkt_len < 0) {
        free(server);
        return 1;
    }
    putchar('\n');

    /* ---- Step 2: Send over network ---- */
    uint8_t resp[DNS_MAX_PACKET];
    dns_net_status_t net_status;
    int resp_len = -1;

    int max_attempts = retries + 1;
    for (int attempt = 1; attempt <= max_attempts; attempt++) {
        printf("--- Attempt %d/%d: sending to %s:53 ---\n", attempt, max_attempts, server);

        resp_len = dns_send_udp(packet, (size_t)pkt_len,
                                resp, sizeof(resp),
                                server, DNS_PORT,
                                timeout, 0,  /* retries=0, we handle the loop */
                                &net_status);

        if (resp_len >= 0) {
            printf("  Response received (%d bytes)\n", resp_len);
            break;
        }

        if (net_status == DNS_NET_TIMEOUT && attempt < max_attempts) {
            printf("  Timeout — retrying...\n");
            continue;
        }

        break;
    }

    if (resp_len < 0) {
        printf("  Network error: %s\n", dns_net_strerror(net_status));
        free(server);
        return 1;
    }
    putchar('\n');

    /* ---- Step 3: Raw response visualisation ---- */
    printf("--- Raw Response: Hex dump ---\n");
    dns_print_hex(resp, (size_t)resp_len);
    putchar('\n');

    printf("--- Raw Response: Structured ---\n");
    dns_print_structured(resp, (size_t)resp_len);
    putchar('\n');

    printf("--- Raw Response: Schema (mode 0) ---\n");
    dns_print_scheme(resp, (size_t)resp_len, domain, 0);
    putchar('\n');

    printf("--- Raw Response: Binary (mode 1) ---\n");
    dns_print_scheme(resp, (size_t)resp_len, domain, 1);
    putchar('\n');

    printf("--- Raw Response: Human-readable (mode 2) ---\n");
    dns_print_scheme(resp, (size_t)resp_len, domain, 2);
    putchar('\n');

    /* ---- Step 4: Parse and decode response ---- */
    dns_response_t dns_resp;
    if (dns_parse_response(resp, (size_t)resp_len, &dns_resp) < 0) {
        printf("Error: malformed response packet\n");
        free(server);
        return 1;
    }

    printf("--- Parsed Response ---\n");
    dns_print_response(&dns_resp);
    putchar('\n');

    /* ---- Step 5: Summary ---- */
    printf("--- Result ---\n");
    if (dns_resp.rcode == DNS_RCODE_NXDOMAIN) {
        printf("  %s -> NXDOMAIN (domain does not exist)\n", domain);
    } else if (dns_resp.rcode != DNS_RCODE_NOERROR) {
        printf("  %s -> RCODE=%d (server error)\n", domain, dns_resp.rcode);
    } else {
        int found = 0;
        for (size_t i = 0; i < dns_resp.nanswers; i++) {
            const dns_answer_rr_t *rr = &dns_resp.answers[i];

            if (qtype == DNS_TYPE_AAAA) {
                if (rr->type == DNS_TYPE_AAAA && rr->rdlength == 16) {
                    found = 1;
                    char ip6_str[INET6_ADDRSTRLEN];
                    if (inet_ntop(AF_INET6, rr->rdata, ip6_str, sizeof(ip6_str))) {
                        printf("  %s -> AAAA %s (TTL=%u)\n", domain, ip6_str, rr->ttl);
                    }
                }
            } else {
                if (rr->type == DNS_TYPE_A && rr->rdlength == 4) {
                    found = 1;
                    printf("  %s -> A %u.%u.%u.%u (TTL=%u)\n",
                           domain,
                           rr->rdata[0], rr->rdata[1],
                           rr->rdata[2], rr->rdata[3],
                           rr->ttl);
                }
            }
        }
        if (!found) {
            const char *rec_type = (qtype == DNS_TYPE_AAAA) ? "AAAA" : "A";
            printf("  %s -> no %s records in response\n", domain, rec_type);
        }
    }
    printf("\n");
    if (qtype == DNS_TYPE_AAAA) {
        printf("Hint: compare with `dig +short %s AAAA`\n", domain);
    } else {
        printf("Hint: compare with `dig +short %s A`\n", domain);
    }

    free(server);
    return 0;
}
