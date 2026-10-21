/*
 * engine/dns.c — core DNS protocol library (no presentation).
 *
 * Implements only the building of DNS query packets:
 *   - dns_encode_name:  "example.com" -> \x07example\x03com\x00
 *   - dns_build_query:  header (12 B) + question section
 *
 * All printing / visualization of packets lives in src/decor/print.c.
 */

#include "dns.h"   /* same-directory include; -Isrc also makes it work */

#include <string.h>
#include <arpa/inet.h>

/* ================================================================
 * dns_encode_name
 *
 * Encode "example.com" -> \x07example\x03com\x00
 * ================================================================ */
int dns_encode_name(const char *domain, uint8_t *buf, size_t bufsize) {
    size_t pos = 0;

    /* Quick early check: empty domain => just root label (0x00) */
    if (*domain == '\0') {
        if (bufsize < 1) return -1;
        buf[0] = 0x00;
        return 1;
    }

    /* Upfront estimate: encoded length = strlen(domain) + 2
     * Explanation: each label loses its dot but gains a 1-byte length prefix;
     * the final root label (0x00) adds 1 byte. Net: always +2 for non-empty.
     *   "example.com"  : strlen=11 -> encoded=13
     *   "a"            : strlen=1  -> encoded=3
     *   "www.ex.com"   : strlen=10 -> encoded=12
     * Reject obviously-too-small buffers early. */
    size_t max_encoded = strlen(domain) + 2;
    if (bufsize < max_encoded) {
        return -1;
    }

    while (*domain) {
        /* Find the end of the current label (until '.' or '\0') */
        const char *dot = strchr(domain, '.');
        size_t label_len = dot ? (size_t)(dot - domain) : strlen(domain);

        /* Per-step safety net: redundant after upfront check,
         * but guards against logic errors in label length calculation */
        if (pos + 1 + label_len >= bufsize) {
            return -1;
        }

        /* Write label length byte */
        buf[pos++] = (uint8_t)label_len;

        /* Copy label characters */
        memcpy(buf + pos, domain, label_len);
        pos += label_len;

        /* Advance to next label. NOTE: this modifies only the LOCAL copy
         * of the pointer — the caller's 'domain' pointer is unaffected
         * because C passes pointers by value. */
        domain = dot ? dot + 1 : domain + label_len;
    }

    /* Write terminating zero byte (root label) */
    if (pos + 1 >= bufsize) {
        return -1;
    }
    buf[pos++] = 0x00;

    /* Cast is safe: pos <= 255 (max DNS name length) << INT_MAX */
    return (int)pos;
}

/* ================================================================
 * dns_build_query
 *
 * Build a full DNS query: header (12 bytes) + question section.
 * ================================================================ */
int dns_build_query(uint16_t id, const char *domain, uint16_t qtype,
                    uint8_t *buf, size_t bufsize) {
    /* --- Upfront: calculate total minimum required size ---
     *   DNS header       : 12 bytes (fixed)
     *   Encoded name     : strlen(domain) + 2 (or 1 for empty)
     *   QTYPE + QCLASS   : 2 + 2 = 4 bytes
     * Single check at the start — fail fast if buffer is too small. */
    size_t name_encoded = (*domain == '\0') ? 1 : strlen(domain) + 2;
    size_t total_min = DNS_HEADER_SIZE + name_encoded
                     + DNS_QTYPE_SIZE + DNS_QCLASS_SIZE;

    if (bufsize < total_min) {
        return -1;
    }

    /* ---------- Build the header (uses first 12 bytes directly) ---------- */
    dns_header_t *hdr = (dns_header_t *)buf;
    hdr->id      = htons(id);
    hdr->flags   = htons(0x0100);  /* standard query, RD=1 */
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;

    size_t pos = DNS_HEADER_SIZE;

    /* ---------- Build the question section ---------- */

    /* Encode domain name (has its own defensive checks inside) */
    int name_len = dns_encode_name(domain, buf + pos, bufsize - pos);
    if (name_len < 0) {
        return -1;
    }
    pos += (size_t)name_len;

    /* Write QTYPE (2 bytes) — safety check, redundant after upfront */
    if (pos + DNS_QTYPE_SIZE > bufsize) {
        return -1;
    }
    uint16_t *qtype_ptr = (uint16_t *)(buf + pos);
    *qtype_ptr = htons(qtype);
    pos += DNS_QTYPE_SIZE;

    /* Write QCLASS (2 bytes) — safety check, redundant after upfront */
    if (pos + DNS_QCLASS_SIZE > bufsize) {
        return -1;
    }
    uint16_t *qclass_ptr = (uint16_t *)(buf + pos);
    *qclass_ptr = htons(DNS_CLASS_IN);
    pos += DNS_QCLASS_SIZE;

    /* Cast is safe: pos <= DNS_MAX_PACKET (512) << INT_MAX */
    return (int)pos;
}

/* ================================================================
 * Internal helpers: skip a wire-format domain name with pointer support
 *
 * Returns the number of bytes consumed (including the root label or
 * pointer), or -1 on error. Does NOT decode the name — only skips it.
 * ================================================================ */

/* Maximum number of pointer jumps to follow (protection against loops). */
#define MAX_POINTER_JUMPS 16

/* Decode a name from buf at offset *pos, write it into the output buffer.
 * Returns the number of bytes consumed from the wire (not the decoded length),
 * or -1 on error.  We don't need the decoded name here — but this is used
 * by the response parser to skip over names in the answer section. */
static int skip_name(const uint8_t *buf, size_t len, size_t *pos) {
    size_t jumps = 0;
    size_t consumed = 0;  /* bytes consumed in the flat sequence */
    size_t p = *pos;

    while (p < len) {
        uint8_t b = buf[p];

        if (b == 0) {
            /* Root label terminator — end of name */
            if (consumed == 0) consumed = p - *pos + 1;
            *pos = *pos + consumed;
            return (int)consumed;
        }

        if ((b & 0xC0) == 0xC0) {
            /* Compression pointer (2 bytes, high bits 11) */
            if (p + 2 > len) return -1;
            if (consumed == 0) consumed = p - *pos + 2;
            size_t offset = ((size_t)(b & 0x3F) << 8) | buf[p + 1];
            if (offset >= len) return -1;
            jumps++;
            if (jumps > MAX_POINTER_JUMPS) return -1;
            p = offset;
            continue;
        }

        /* Normal label: length byte followed by label characters */
        if ((b & 0xC0) != 0) {
            /* Reserved bits — malformed */
            return -1;
        }

        size_t label_len = b;
        if (p + 1 + label_len > len) return -1;
        p += 1 + label_len;
    }

    return -1;  /* unterminated name */
}

/* ================================================================
 * dns_parse_response
 *
 * Parse a raw response packet:
 *   1. Read and decode the 12-byte header.
 *   2. Skip over the question section.
 *   3. Parse up to `ancount` answer RRs (A-record only).
 * ================================================================ */
int dns_parse_response(const uint8_t *buf, size_t len, dns_response_t *out) {
    if (len < DNS_HEADER_SIZE) return -1;

    const dns_header_t *hdr = (const dns_header_t *)buf;
    out->id      = ntohs(hdr->id);
    out->flags   = ntohs(hdr->flags);
    out->rcode   = out->flags & 0x0F;
    out->qdcount = ntohs(hdr->qdcount);
    out->ancount = ntohs(hdr->ancount);
    out->nscount = ntohs(hdr->nscount);
    out->arcount = ntohs(hdr->arcount);
    out->nanswers = 0;

    /* Skip question section */
    size_t pos = DNS_HEADER_SIZE;
    for (uint16_t q = 0; q < out->qdcount; q++) {
        int ret = skip_name(buf, len, &pos);
        if (ret < 0) return -1;
        /* Skip QTYPE + QCLASS (4 bytes) */
        if (pos + 4 > len) return -1;
        pos += 4;
    }

    /* Parse answer RRs */
    size_t max_answers = sizeof(out->answers) / sizeof(out->answers[0]);
    for (uint16_t a = 0; a < out->ancount && a < max_answers; a++) {
        /* Skip the NAME field (may be a pointer) */
        int ret = skip_name(buf, len, &pos);
        if (ret < 0) return -1;

        /* Read TYPE (2), CLASS (2), TTL (4), RDLENGTH (2) */
        if (pos + 10 > len) return -1;

        dns_answer_rr_t *rr = &out->answers[out->nanswers];
        rr->type     = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
        rr->class_   = (uint16_t)((buf[pos + 2] << 8) | buf[pos + 3]);
        rr->ttl      = ((uint32_t)buf[pos + 4] << 24)
                     | ((uint32_t)buf[pos + 5] << 16)
                     | ((uint32_t)buf[pos + 6] <<  8)
                     | (uint32_t)buf[pos + 7];
        rr->rdlength = (uint16_t)((buf[pos + 8] << 8) | buf[pos + 9]);
        pos += 10;

        /* Read RDATA */
        if (pos + rr->rdlength > len) return -1;
        size_t copy_len = rr->rdlength < sizeof(rr->rdata) ? rr->rdlength : sizeof(rr->rdata);
        memcpy(rr->rdata, buf + pos, copy_len);
        pos += rr->rdlength;

        out->nanswers++;
    }

    return 0;
}
