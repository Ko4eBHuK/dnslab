/*
 * lookup.c — implementation of cmd_lookup (default mode, no flag).
 *
 * Build query -> send over UDP to resolver (port 53) ->
 * receive response -> extract A record -> print IP(s).
 *
 * Two output modes:
 *   - Brief  (options.verbose == 0): IP addresses only, one per line.
 *   - Verbose (options.verbose != 0): server info, RCODE, answer summary.
 */

#include <stdio.h>
#include <stdlib.h>

#include "engine/dns.h"
#include "decor/print.h"
#include "net/net.h"
#include "command/command.h"

int cmd_lookup(const char *domain, const cmd_options_t *options) {
    /* ---- Build the query (silently, no compose output) ---- */
    uint8_t packet[DNS_MAX_PACKET];
    int pkt_len = compose_request(domain, packet, sizeof(packet), 0);
    if (pkt_len < 0) {
        return 1;
    }

    /* ---- Resolve server ---- */
    const char *explicit_server = (options && options->server) ? options->server : NULL;
    char *server = dns_resolve_server(explicit_server);

    int timeout = (options && options->timeout_ms > 0) ? options->timeout_ms : DNS_DEFAULT_TIMEOUT_MS;
    int retries = (options && options->retries > 0)   ? options->retries    : DNS_DEFAULT_RETRIES;
    int verbose = (options && options->verbose);

    if (verbose) {
        printf("=== DNS Resolve (verbose) ===\n");
        printf("Domain: %s\n", domain);
        printf("Server: %s:53\n", server);
        printf("Timeout: %d ms, retries: %d\n\n", timeout, retries);
    }

    /* ---- Send query and receive response ---- */
    uint8_t resp[DNS_MAX_PACKET];
    dns_net_status_t net_status;
    int resp_len = dns_send_udp(packet, (size_t)pkt_len,
                                resp, sizeof(resp),
                                server, DNS_PORT,
                                timeout, retries,
                                &net_status);

    if (resp_len < 0) {
        if (verbose) {
            printf("Error: %s\n", dns_net_strerror(net_status));
        } else {
            fprintf(stderr, "Error: %s\n", dns_net_strerror(net_status));
        }
        free(server);
        return 1;
    }

    /* ---- Parse response ---- */
    dns_response_t dns_resp;
    if (dns_parse_response(resp, (size_t)resp_len, &dns_resp) < 0) {
        if (verbose) {
            printf("Error: malformed response packet\n");
        } else {
            fprintf(stderr, "Error: malformed response packet\n");
        }
        free(server);
        return 1;
    }

    /* ---- Handle RCODE ---- */
    if (dns_resp.rcode == DNS_RCODE_NXDOMAIN) {
        if (verbose) {
            printf("RCODE: %d (NXDOMAIN) — domain does not exist\n", dns_resp.rcode);
        } else {
            fprintf(stderr, "Error: domain does not exist (NXDOMAIN)\n");
        }
        free(server);
        return 1;
    }

    if (dns_resp.rcode != DNS_RCODE_NOERROR) {
        if (verbose) {
            printf("RCODE: %d — server returned an error\n", dns_resp.rcode);
        } else {
            fprintf(stderr, "Error: server returned RCODE %d\n", dns_resp.rcode);
        }
        free(server);
        return 1;
    }

    /* ---- Extract A records ---- */
    int found_a = 0;
    for (size_t i = 0; i < dns_resp.nanswers; i++) {
        const dns_answer_rr_t *rr = &dns_resp.answers[i];
        if (rr->type == DNS_TYPE_A && rr->rdlength == 4) {
            found_a = 1;
            if (verbose) {
                printf("Answer: A %u.%u.%u.%u  TTL=%u\n",
                       rr->rdata[0], rr->rdata[1],
                       rr->rdata[2], rr->rdata[3],
                       rr->ttl);
            } else {
                printf("%u.%u.%u.%u\n",
                       rr->rdata[0], rr->rdata[1],
                       rr->rdata[2], rr->rdata[3]);
            }
        }
    }

    if (!found_a) {
        if (verbose) {
            printf("No A records in response\n");
            dns_print_response(&dns_resp);
        } else {
            fprintf(stderr, "Error: no A records in response\n");
        }
        free(server);
        return 1;
    }

    if (verbose) {
        dns_print_response(&dns_resp);
    }

    free(server);
    return 0;
}
