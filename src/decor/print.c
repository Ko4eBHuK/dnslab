/*
 * decor/print.c — presentation layer for DNS packets.
 *
 * Pure output: these functions never build or send packets, they only
 * render an existing packet to stdout. The protocol knowledge they
 * need (header layout, constants, ntohs) comes from engine/dns.h.
 *
 *   dns_print_hex        : classic hex dump
 *   dns_print_structured : field-by-field breakdown
 *   dns_print_scheme     : 3 ASCII visualizations
 */

#include "decor/print.h"
#include "engine/dns.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

/* ================================================================
 * dns_print_hex
 *
 * Standard hex dump: offset | hex bytes | ASCII
 * ================================================================ */
void dns_print_hex(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i += 16) {
        /* Print offset */
        printf("%04zx  ", i);

        /* Print hex bytes */
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                printf("%02x ", buf[i + j]);
            } else {
                printf("   ");
            }
            /* Extra space in the middle for readability */
            if (j == 7) {
                putchar(' ');
            }
        }

        /* Print ASCII representation */
        putchar(' ');
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t byte = buf[i + j];
            putchar(isprint(byte) ? byte : '.');
        }

        putchar('\n');
    }
}

/* ================================================================
 * dns_print_structured
 *
 * Human-readable breakdown of the DNS query packet.
 * ================================================================ */
void dns_print_structured(const uint8_t *buf, size_t len) {
    if (len < DNS_HEADER_SIZE) {
        printf("Packet too short (%zu bytes)\n", len);
        return;
    }

    const dns_header_t *hdr = (const dns_header_t *)buf;

    uint16_t id      = ntohs(hdr->id);
    uint16_t flags   = ntohs(hdr->flags);
    uint16_t qdcount = ntohs(hdr->qdcount);
    uint16_t ancount = ntohs(hdr->ancount);
    uint16_t nscount = ntohs(hdr->nscount);
    uint16_t arcount = ntohs(hdr->arcount);

    /* Decode flags */
    int qr    = (flags >> 15) & 1;
    int opcode = (flags >> 11) & 0x0F;
    int aa    = (flags >> 10) & 1;
    int tc    = (flags >> 9) & 1;
    int rd    = (flags >> 8) & 1;
    int ra    = (flags >> 7) & 1;
    int rcode = flags & 0x0F;

    printf("--- DNS Header ---\n");
    printf("  ID:      0x%04x (%u)\n", id, id);
    printf("  Flags:   0x%04x\n", flags);
    printf("    QR:     %d (%s)\n", qr, qr ? "response" : "query");
    printf("    OPCODE: %d\n", opcode);
    printf("    AA:     %d\n", aa);
    printf("    TC:     %d\n", tc);
    printf("    RD:     %d\n", rd);
    printf("    RA:     %d\n", ra);
    printf("    RCODE:  %d\n", rcode);
    printf("  QDCOUNT: %u\n", qdcount);
    printf("  ANCOUNT: %u\n", ancount);
    printf("  NSCOUNT: %u\n", nscount);
    printf("  ARCOUNT: %u\n", arcount);

    /* ---------- Parse question section ---------- */
    size_t pos = DNS_HEADER_SIZE;

    for (uint16_t q = 0; q < qdcount && q < 1; q++) {
        if (pos >= len) {
            printf("  [truncated question]\n");
            break;
        }

        printf("\n--- Question %u ---\n", q + 1);
        printf("  Name: ");

        /* Decode the domain name from wire format */
        size_t name_start = pos;
        while (pos < len) {
            uint8_t label_len = buf[pos];
            if (label_len == 0) {
                pos++;  /* skip the zero terminator */
                break;
            }
            /* Print label */
            pos++;
            if (pos + label_len > len) {
                printf("[truncated]");
                break;
            }
            for (size_t i = 0; i < label_len; i++) {
                putchar(isprint(buf[pos + i]) ? buf[pos + i] : '.');
            }
            putchar('.');
            pos += label_len;
        }
        putchar('\n');

        /* Show encoded bytes of the domain name */
        printf("  Encoded: ");
        for (size_t i = name_start; i < pos; i++) {
            printf("\\x%02x", buf[i]);
        }
        putchar('\n');

        /* Read QTYPE and QCLASS */
        if (pos + 4 > len) {
            printf("  [truncated QTYPE/QCLASS]\n");
            break;
        }

        uint16_t qtype  = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
        uint16_t qclass = (uint16_t)((buf[pos + 2] << 8) | buf[pos + 3]);

        const char *qtype_str = "unknown";
        switch (qtype) {
            case DNS_TYPE_A:     qtype_str = "A";     break;
            case DNS_TYPE_NS:    qtype_str = "NS";    break;
            case DNS_TYPE_CNAME: qtype_str = "CNAME"; break;
            case DNS_TYPE_MX:    qtype_str = "MX";    break;
            case DNS_TYPE_AAAA:  qtype_str = "AAAA";  break;
            case DNS_TYPE_ANY:   qtype_str = "ANY";   break;
        }

        const char *qclass_str = "unknown";
        if (qclass == DNS_CLASS_IN) {
            qclass_str = "IN";
        }

        printf("  QTYPE:  0x%04x (%s)\n", qtype, qtype_str);
        printf("  QCLASS: 0x%04x (%s)\n", qclass, qclass_str);

        pos += 4;
    }

    printf("\n--- End of packet (%zu bytes) ---\n", len);
}

/* ================================================================
 * dns_print_scheme
 *
 * Draw three visual representations of the DNS packet.
 *
 *   mode 0 = schema (field labels)
 *   mode 1 = binary (bits)
 *   mode 2 = human-readable (hex + ASCII chars)
 * ================================================================ */

/* Helper: print the ruler line */
static void print_ruler(void) {
    printf("  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F\n");
}

/* Helper: print the grid separator */
static void print_separator(void) {
    printf("+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+\n");
}

/* ================================================================
 * visualization helpers (shared by the three modes)
 *
 * The visualizations need a little protocol knowledge: where the
 * Question's labels and the QTYPE/QCLASS fields are. We parse that
 * once from the packet and let every mode caption itself from it.
 * ================================================================ */

/* One parsed label inside the Question section. */
typedef struct {
    size_t len_pos;      /* offset of the 1-byte length             */
    size_t char_pos;     /* offset of the first character byte       */
    size_t char_count;   /* number of character bytes                */
} dns_label_t;

/* Parsed Question section (a single question is assumed, as built by
 * dns_build_query). */
typedef struct {
    dns_label_t labels[63];   /* a DNS label is <= 63 bytes            */
    size_t      nlabels;
    size_t      term_pos;     /* offset of the 0x00 root terminator   */
    size_t      qtype_pos;    /* offset of QTYPE (2 bytes)             */
    size_t      qclass_pos;   /* offset of QCLASS (2 bytes)            */
    int         have_term;    /* 1 if the 0x00 terminator was found    */
} dns_question_t;

/* Walk the Question section and record where each label, the root
 * terminator and QTYPE/QCLASS sit. Never reads past len. */
static void parse_question(const uint8_t *buf, size_t len,
                           dns_question_t *q) {
    q->nlabels    = 0;
    q->have_term  = 0;
    q->term_pos   = len;
    q->qtype_pos  = len;
    q->qclass_pos = len;

    size_t p = DNS_HEADER_SIZE;
    while (p < len) {
        uint8_t lbl = buf[p];
        if (lbl == 0) {                       /* root terminator */
            q->term_pos  = p;
            q->have_term = 1;
            p++;
            break;
        }
        if (q->nlabels < sizeof(q->labels) / sizeof(q->labels[0]) &&
            p + 1 + (size_t)lbl <= len) {
            q->labels[q->nlabels].len_pos    = p;
            q->labels[q->nlabels].char_pos   = p + 1;
            q->labels[q->nlabels].char_count = lbl;
            q->nlabels++;
        }
        p += (size_t)1 + lbl;
    }

    q->qtype_pos  = p;                        /* right after the terminator */
    q->qclass_pos = p + DNS_QTYPE_SIZE;       /* right after QTYPE           */
}

/* Is the byte at offset p one of the domain-name characters? */
static int is_name_char(const dns_question_t *q, size_t p) {
    for (size_t i = 0; i < q->nlabels; i++) {
        if (p >= q->labels[i].char_pos &&
            p <  q->labels[i].char_pos + q->labels[i].char_count) {
            return 1;
        }
    }
    return 0;
}

static const char *qtype_name(uint16_t t) {
    switch (t) {
        case DNS_TYPE_A:     return "A";
        case DNS_TYPE_NS:    return "NS";
        case DNS_TYPE_CNAME: return "CNAME";
        case DNS_TYPE_MX:    return "MX";
        case DNS_TYPE_AAAA:  return "AAAA";
        case DNS_TYPE_ANY:   return "ANY";
        default:             return "unknown";
    }
}

static const char *qclass_name(uint16_t c) {
    return (c == DNS_CLASS_IN) ? "IN" : "unknown";
}

/* Print `label` centered in a field `width` chars wide. Labels that are
 * too long are truncated from the right so the grid alignment is never
 * broken; the extra space goes to the left, matching the "| QR|" style. */
static void print_centered(const char *label, int width) {
    int lablen = (int)strlen(label);
    if (lablen > width) {
        lablen = width;
    }
    int pad   = width - lablen;
    int left  = (pad + 1) / 2;
    int right = pad - left;
    for (int i = 0; i < left;  i++) putchar(' ');
    for (int i = 0; i < lablen; i++) putchar(label[i]);
    for (int i = 0; i < right; i++) putchar(' ');
}

/* One labelled field spanning `cells` cells of the 16-cell grid. */
typedef struct {
    const char *label;
    int         cells;
} field_t;

/* Render a full 16-cell row. The sum of cells over all fields must be 16. */
static void print_field_row(const field_t *fields, int n) {
    putchar('|');
    for (int i = 0; i < n; i++) {
        /* c merged cells give c*3 chars of content joined by (c-1) pipes,
         * i.e. inner width = c*4 - 1. */
        int width = fields[i].cells * 4 - 1;
        print_centered(fields[i].label, width);
        putchar('|');
    }
    putchar('\n');
}

/* Short human-readable description of one byte of the Question section. */
static void describe_qbyte(const uint8_t *buf, size_t len, size_t p,
                           const dns_question_t *q,
                           char *out, size_t outsz) {
    if (q->have_term && p == q->term_pos) {
        snprintf(out, outsz, "0x00 (end of name)");
        return;
    }
    for (size_t i = 0; i < q->nlabels; i++) {
        if (p == q->labels[i].len_pos) {
            const uint8_t *ch = buf + q->labels[i].char_pos;
            size_t cn = q->labels[i].char_count;
            snprintf(out, outsz,
                     "0x%02x = L%zu (%zu bytes, '%.*s')",
                     buf[p], i + 1, cn, (int)cn, (const char *)ch);
            return;
        }
    }
    if (is_name_char(q, p)) {
        snprintf(out, outsz, "'%c' (0x%02x)", (char)buf[p], buf[p]);
        return;
    }
    if (p < len) {
        snprintf(out, outsz, "0x%02x", buf[p]);
    } else {
        snprintf(out, outsz, "-");
    }
}

void dns_print_scheme(const uint8_t *buf, size_t len,
                      const char *domain, int mode) {
    (void)domain;

    if (len < DNS_HEADER_SIZE) {
        printf("Packet too short (%zu bytes)\n", len);
        return;
    }

    const dns_header_t *hdr = (const dns_header_t *)buf;
    uint16_t id      = ntohs(hdr->id);
    uint16_t flags   = ntohs(hdr->flags);
    uint16_t qdcount = ntohs(hdr->qdcount);
    uint16_t ancount = ntohs(hdr->ancount);
    uint16_t nscount = ntohs(hdr->nscount);
    uint16_t arcount = ntohs(hdr->arcount);

    /* Parse the single Question so every mode can caption itself. */
    dns_question_t q;
    parse_question(buf, len, &q);

    print_ruler();

    if (mode == 0) {
        /* ----- mode 0: schema (field labels) ----- */
        print_separator();
        printf("|                              ID                               |\n");
        print_separator();
        printf("| QR|     OPCODE    | AA| TC| RD| RA|     Z     |     RCODE     |\n");
        print_separator();
        printf("|                            QDCOUNT                            |\n");
        print_separator();
        printf("|                            ANCOUNT                            |\n");
        print_separator();
        printf("|                            NSCOUNT                            |\n");
        print_separator();
        printf("|                            ARCOUNT                            |\n");
        print_separator();
        /* Question section: derived from the actual packet, so each label
         * gets its own row (length byte + description) and the trailing
         * root terminator, QTYPE and QCLASS always line up with the grid. */
        for (size_t i = 0; i < q.nlabels; i++) {
            char name[8];
            snprintf(name, sizeof name, "L%zu", i + 1);
            const uint8_t *ch = buf + q.labels[i].char_pos;
            size_t cn = q.labels[i].char_count;
            char desc[128];
            snprintf(desc, sizeof desc,
                     "label '%.*s' (%zu bytes)", (int)cn, (const char *)ch, cn);
            const field_t row[] = { {name, 1}, {desc, 15} };
            print_field_row(row, 2);
            print_separator();
        }
        if (q.have_term &&
            q.qtype_pos + DNS_QTYPE_SIZE + DNS_QCLASS_SIZE <= len) {
            uint16_t qtype  = (uint16_t)((buf[q.qtype_pos]  << 8) | buf[q.qtype_pos + 1]);
            uint16_t qclass = (uint16_t)((buf[q.qclass_pos] << 8) | buf[q.qclass_pos + 1]);
            char qtype_lab[40], qclass_lab[40];
            snprintf(qtype_lab,  sizeof qtype_lab,
                     "QTYPE = 0x%04x (%s)",  qtype,  qtype_name(qtype));
            snprintf(qclass_lab, sizeof qclass_lab,
                     "QCLASS = 0x%04x (%s)", qclass, qclass_name(qclass));
            const field_t tail[] = { {"0x00", 2}, {qtype_lab, 7}, {qclass_lab, 7} };
            print_field_row(tail, 3);
            print_separator();
        }

    } else if (mode == 1) {
        /* ----- mode 1: binary (bits) ----- */
        /* 16 cells per row, each cell = 1 bit. Two bytes = 16 bits = 1 row. */
        size_t pos = 0;
        while (pos < len) {
            /* We'll show pairs of bytes (16 bits per row) */
            uint8_t b0 = (pos < len)     ? buf[pos]     : 0;
            uint8_t b1 = (pos + 1 < len) ? buf[pos + 1] : 0;

            print_separator();
            printf("|");
            for (int bit = 15; bit >= 0; bit--) {
                uint8_t byte = (bit >= 8) ? b0 : b1;
                int b = (bit >= 8) ? (bit - 8) : bit;
                printf(" %c ", (byte & (1 << b)) ? '1' : '0');
                if (bit > 0) printf("|");
            }
            printf("|");

            /* Comment per 2-byte row. */
            if (pos == 0)           printf("    ID = 0x%04x", id);
            else if (pos == 2)      printf("    Flags = 0x%04x", flags);
            else if (pos == 4)      printf("    QDCOUNT = %u", qdcount);
            else if (pos == 6)      printf("    ANCOUNT = %u", ancount);
            else if (pos == 8)      printf("    NSCOUNT = %u", nscount);
            else if (pos == 10)     printf("    ARCOUNT = %u", arcount);
            else if (pos == q.qtype_pos && pos + 1 < len) {
                uint16_t v = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
                printf("    QTYPE = 0x%04x (%s)", v, qtype_name(v));
            } else if (pos == q.qclass_pos && pos + 1 < len) {
                uint16_t v = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
                printf("    QCLASS = 0x%04x (%s)", v, qclass_name(v));
            } else if (pos >= DNS_HEADER_SIZE) {
                char c0[128], c1[128];
                describe_qbyte(buf, len, pos, &q, c0, sizeof c0);
                if (pos + 1 < len) {
                    describe_qbyte(buf, len, pos + 1, &q, c1, sizeof c1);
                    printf("    %s  %s", c0, c1);
                } else {
                    printf("    %s", c0);
                }
            }
            printf("\n");
            pos += 2;
        }
        print_separator();

    } else if (mode == 2) {
        /* ----- mode 2: human-readable (mixed hex/chars) ----- */
        /* Each cell = 1 byte. Only the domain-name characters are shown
         * as " c "; everything else (header, lengths, terminator, QTYPE
         * and QCLASS) is shown as hex "xNN", so e.g. ID 0x1234 reads as
         * "x12 x34" instead of a misleading "x12 4". */
        size_t pos = 0;
        while (pos < len) {
            print_separator();
            printf("|");
            for (int i = 0; i < 16 && pos + i < len; i++) {
                size_t p = pos + (size_t)i;
                uint8_t byte = buf[p];
                if (is_name_char(&q, p)) {
                    printf(" %c ", (char)byte);
                } else {
                    printf("x%02x", byte);
                }
                if (i < 15) {
                    printf("|");
                }
            }
            /* Pad remaining cells for rows with fewer than 16 bytes */
            for (size_t i = len - pos; i < 16; i++) {
                printf("   ");
                if (i < 15) printf("|");
            }
            printf("|");

            if (pos == 0)      printf("    Header + start of payload");
            else if (pos == 16) printf("    Payload continued + QTYPE + QCLASS");
            printf("\n");
            pos += 16;
        }
        print_separator();
    }
}

/* ================================================================
 * dns_print_response
 *
 * Print a human-readable summary of a parsed DNS response.
 * Shows: server info, RCODE, flags, answer RRs (IP + TTL).
 * ================================================================ */
void dns_print_response(const dns_response_t *resp) {
    /* Decode flags */
    int qr    = (resp->flags >> 15) & 1;
    int opcode = (resp->flags >> 11) & 0x0F;
    int aa    = (resp->flags >> 10) & 1;
    int tc    = (resp->flags >> 9) & 1;
    int rd    = (resp->flags >> 8) & 1;
    int ra    = (resp->flags >> 7) & 1;

    printf("  ID:     0x%04x\n", resp->id);
    printf("  Flags:  0x%04x\n", resp->flags);
    printf("    QR:     %d (response)\n", qr);
    if (opcode) printf("    OPCODE: %d\n", opcode);
    if (aa)     printf("    AA:     %d (authoritative)\n", aa);
    if (tc)     printf("    TC:     %d (truncated)\n", tc);
    printf("    RD:     %d\n", rd);
    if (ra)     printf("    RA:     %d (recursion available)\n", ra);

    /* RCODE */
    const char *rcode_str;
    switch (resp->rcode) {
        case 0:  rcode_str = "NOERROR";  break;
        case 1:  rcode_str = "FORMERR";  break;
        case 2:  rcode_str = "SERVFAIL"; break;
        case 3:  rcode_str = "NXDOMAIN"; break;
        case 4:  rcode_str = "NOTIMP";   break;
        case 5:  rcode_str = "REFUSED";  break;
        default: rcode_str = "unknown";  break;
    }
    printf("    RCODE:  %d (%s)\n", resp->rcode, rcode_str);

    printf("  QDCOUNT: %u\n", resp->qdcount);
    printf("  ANCOUNT: %u\n", resp->ancount);
    printf("  NSCOUNT: %u\n", resp->nscount);
    printf("  ARCOUNT: %u\n", resp->arcount);

    /* Print answer RRs */
    if (resp->nanswers > 0) {
        printf("\n  Answers (%zu):\n", resp->nanswers);
        for (size_t i = 0; i < resp->nanswers; i++) {
            const dns_answer_rr_t *rr = &resp->answers[i];
            printf("    %zu. ", i + 1);

            /* Format RDATA based on type */
            if (rr->type == DNS_TYPE_A && rr->rdlength == 4) {
                printf("A %u.%u.%u.%u",
                       rr->rdata[0], rr->rdata[1],
                       rr->rdata[2], rr->rdata[3]);
            } else {
                printf("TYPE=%u RDATA=", rr->type);
                for (int j = 0; j < rr->rdlength; j++) {
                    printf("%02x", rr->rdata[j]);
                }
            }
            printf(" TTL=%u", rr->ttl);
            putchar('\n');
        }
    } else if (resp->nanswers == 0 && resp->rcode == 0) {
        printf("\n  (no answer records)\n");
    }
}
