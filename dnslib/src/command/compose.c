/*
 * compose.c — implementation of cmd_compose (--compose-request).
 *
 * Build a DNS query (A or AAAA) for <domain> and display it three ways:
 *   1. hex dump
 *   2. structured field breakdown
 *   3. three ASCII visualizations (schema / binary / human-readable)
 *
 * No network I/O happens here — pure local construction + display.
 */

#include <stdio.h>

#include "engine/dns.h"
#include "decor/print.h"
#include "command/command.h"

/* ================================================================
 * compose_request — shared helper: build a DNS query, optionally print
 *
 * When print_all is non-zero, prints the full compose output (hex dump,
 * structured breakdown, 3 schemes). Otherwise just builds silently.
 *
 * Returns packet length, or -1 on error.
 * ================================================================ */
int compose_request(const char *domain, uint16_t qtype,
                    uint8_t *buf, size_t cap, int print_all) {
    const uint16_t id = DEFAULT_QUERY_ID;

    int pkt_len = dns_build_query(id, domain, qtype, buf, cap);
    if (pkt_len < 0) {
        fprintf(stderr, "Error: failed to build DNS query for \"%s\"\n", domain);
        return -1;
    }

    if (print_all) {
        const char *type_name = (qtype == DNS_TYPE_AAAA) ? "AAAA (IPv6)" : "A (IPv4)";

        printf("=== DNS Compose Request ===\n");
        printf("Domain: %s\n", domain);
        printf("Type:   %s\n", type_name);
        printf("ID:     0x%04x\n\n", id);

        /* 1. Hex dump */
        printf("--- Hex dump ---\n");
        dns_print_hex(buf, (size_t)pkt_len);
        putchar('\n');

        /* 2. Structured breakdown */
        dns_print_structured(buf, (size_t)pkt_len);
        putchar('\n');

        /* 3. Visual representations */
        printf("--- Visual: Schema (mode 0) ---\n");
        dns_print_scheme(buf, (size_t)pkt_len, domain, 0);
        putchar('\n');

        printf("--- Visual: Binary (mode 1) ---\n");
        dns_print_scheme(buf, (size_t)pkt_len, domain, 1);
        putchar('\n');

        printf("--- Visual: Human-readable (mode 2) ---\n");
        dns_print_scheme(buf, (size_t)pkt_len, domain, 2);
        putchar('\n');
    }

    return pkt_len;
}

/* ================================================================
 * cmd_compose — user-facing command for --compose-request
 * ================================================================ */
int cmd_compose(const char *domain, uint16_t qtype) {
    uint8_t packet[DNS_MAX_PACKET];
    int pkt_len = compose_request(domain, qtype, packet, sizeof(packet), 1);
    return (pkt_len >= 0) ? 0 : 1;
}
