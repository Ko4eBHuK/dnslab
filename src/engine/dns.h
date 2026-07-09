#ifndef DNSLAB_DNS_H
#define DNSLAB_DNS_H

/*
 * engine/dns.h — core DNS protocol library (no presentation).
 *
 * This module knows how to *build* DNS packets on the wire: domain
 * name encoding, header assembly, byte-order handling. It deliberately
 * knows nothing about how packets are *shown* to the user — all
 * printing/visualization lives in src/decor/print.{h,c}.
 *
 * Keeping "protocol" and "presentation" separate means you can reuse
 * this engine to drive a resolver (network mode) and a viewer (compose
 * mode) from the same code.
 */

#include <stdint.h>
#include <stddef.h>

/* ---------- DNS constants ---------- */

/* Record types */
#define DNS_TYPE_A      1
#define DNS_TYPE_NS     2
#define DNS_TYPE_CNAME  5
#define DNS_TYPE_MX     15
#define DNS_TYPE_AAAA   28
#define DNS_TYPE_ANY    255

/* Record classes */
#define DNS_CLASS_IN    1

/* Sizes of fixed fields in the question section */
#define DNS_QTYPE_SIZE  2
#define DNS_QCLASS_SIZE 2

/* Maximum DNS packet size on wire (UDP) */
#define DNS_MAX_PACKET  512

/* DNS header is always 12 bytes */
#define DNS_HEADER_SIZE 12

/* ---------- Types ---------- */

/* DNS header (12 bytes, network byte order) */
typedef struct __attribute__((packed)) {
    uint16_t id;       /* Transaction ID */
    uint16_t flags;    /* Flags (QR, OPCODE, AA, TC, RD, RA, Z, RCODE) */
    uint16_t qdcount;  /* Number of questions */
    uint16_t ancount;  /* Number of answer RRs */
    uint16_t nscount;  /* Number of authority RRs */
    uint16_t arcount;  /* Number of additional RRs */
} dns_header_t;

/* ---------- Query builder ---------- */

/*
 * Encode a domain name into DNS wire format.
 *
 * "www.example.com" -> \x03www\x07example\x03com\x00
 *
 * Returns the number of bytes written, or -1 on error (buf too small).
 */
int dns_encode_name(const char *domain, uint8_t *buf, size_t bufsize);

/*
 * Build a standard DNS query packet (A record by default).
 *
 *   id      - transaction ID (host byte order, e.g. 0x1234)
 *   domain  - domain name to query (e.g. "example.com")
 *   qtype   - query type (e.g. DNS_TYPE_A)
 *   buf     - output buffer (at least DNS_MAX_PACKET bytes)
 *   bufsize - buffer size
 *
 * Returns the total packet size, or -1 on error.
 *
 * Assumptions:
 *   - qdcount = 1 (single question)
 *   - ancount = nscount = arcount = 0 (query, not response)
 *   - flags = 0x0100 (standard query, recursion desired)
 *   - qclass = DNS_CLASS_IN (Internet)
 */
int dns_build_query(uint16_t id, const char *domain,
                    uint16_t qtype, uint8_t *buf, size_t bufsize);

/* ---------- Response parser ---------- */

/* RCODE values from header flags. */
#define DNS_RCODE_NOERROR  0
#define DNS_RCODE_NXDOMAIN 3

/* A single answer Resource Record (A-record focused for now). */
typedef struct {
    uint16_t type;            /* DNS_TYPE_A, etc. */
    uint16_t class_;          /* DNS_CLASS_IN */
    uint32_t ttl;
    uint8_t  rdata[16];       /* up to 16 bytes of RDATA (4 for A) */
    uint8_t  rdlength;
} dns_answer_rr_t;

/* Parsed DNS response (header + up to 16 A-record answers). */
typedef struct {
    uint16_t id;
    uint16_t flags;
    int      rcode;            /* decoded from flags bits [0..3] */
    uint16_t qdcount, ancount, nscount, arcount;
    dns_answer_rr_t answers[16];
    size_t          nanswers;
} dns_response_t;

/*
 * Parse a raw DNS response packet into a structured form.
 *
 *   buf    — raw packet bytes
 *   len    — length of the packet
 *   out    — output structure to fill
 *
 * Handles name compression pointers safely (with a jump limit to
 * prevent infinite loops from malformed packets).
 *
 * Returns 0 on success, -1 if the packet is too short or malformed.
 */
int dns_parse_response(const uint8_t *buf, size_t len, dns_response_t *out);

#endif /* DNSLAB_DNS_H */
