/*
 * resolv.c — DNS server address resolution.
 *
 * Provides dns_resolve_server(): determines which DNS server to use,
 * checking in order:
 *   1. Explicit --server argument (if non-NULL)
 *   2. /etc/resolv.conf (first "nameserver" line)
 *   3. Fallback to "8.8.8.8"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command/command.h"  /* for dns_resolve_server declaration */

/* Maximum line length in /etc/resolv.conf (RFC 1035 §6) */
#define MAX_LINE 1024

/*
 * Determine the DNS server address to use.
 *
 *   explicit — value from --server flag, or NULL to auto-detect
 *
 * Returns a malloc-allocated string that the caller must free,
 * or NULL on error (should not happen — fallback always succeeds).
 */
char *dns_resolve_server(const char *explicit) {
    /* 1. Explicit override */
    if (explicit != NULL) {
        return strdup(explicit);
    }

    /* 2. Try /etc/resolv.conf */
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (f != NULL) {
        char line[MAX_LINE];
        while (fgets(line, sizeof line, f)) {
            /* Trim leading whitespace */
            const char *p = line;
            while (*p == ' ' || *p == '\t') p++;

            /* Check for "nameserver" keyword */
            if (strncmp(p, "nameserver", 10) == 0) {
                p += 10;
                /* Skip whitespace between keyword and address */
                while (*p == ' ' || *p == '\t') p++;

                /* Extract the IP address (up to newline or space) */
                const char *start = p;
                while (*p && *p != '\n' && *p != ' ' && *p != '\t') p++;

                size_t len = (size_t)(p - start);
                if (len > 0) {
                    char *addr = (char *)malloc(len + 1);
                    if (addr) {
                        memcpy(addr, start, len);
                        addr[len] = '\0';
                        fclose(f);
                        return addr;
                    }
                }
            }
        }
        fclose(f);
    }

    /* 3. Fallback to public resolver */
    return strdup("8.8.8.8");
}
