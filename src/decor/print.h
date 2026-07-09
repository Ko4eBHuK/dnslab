#ifndef DNSLAB_PRINT_H
#define DNSLAB_PRINT_H

 /*
 * decor/print.h — presentation layer for DNS packets.
 *
 * These functions turn a binary DNS packet into human-facing output.
 * They depend on the engine (src/engine/dns.h) for the on-wire layout,
 * but the engine does NOT depend on them — print is a pure consumer.
 *
 *   dns_print_hex        : classic hex dump (offset | bytes | ASCII)
 *   dns_print_structured : field-by-field header + question breakdown
 *   dns_print_scheme     : three ASCII visualizations (schema/binary/human)
 */

#include <stdint.h>
#include <stddef.h>

#include "engine/dns.h"   /* for dns_response_t */

/*
 * Print a hex dump of the DNS packet.
 * Format: offset  |  hex bytes  |  ASCII
 */
void dns_print_hex(const uint8_t *buf, size_t len);

/*
 * Print a structured, human-readable breakdown of the DNS query.
 * Shows header fields and question section with explanations.
 */
void dns_print_structured(const uint8_t *buf, size_t len);

/*
 * Draw three visual representations of the DNS packet as ASCII scheme.
 *
 *   buf     — binary DNS packet
 *   len     — packet length
 *   domain  — original domain name (reserved for human-readable mode)
 *   mode    : 0 = schema (field labels), 1 = binary (bits), 2 = human-readable
 *
 * Output goes to stdout.
 */
void dns_print_scheme(const uint8_t *buf, size_t len,
                      const char *domain, int mode);

/*
 * Print a human-readable summary of a parsed DNS response:
 * RCODE, flags, server info, and answer RRs (name → A → ip → TTL).
 */
void dns_print_response(const dns_response_t *resp);

#endif /* DNSLAB_PRINT_H */
