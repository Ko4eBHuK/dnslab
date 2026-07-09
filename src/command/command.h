#ifndef DNSLAB_COMMAND_H
#define DNSLAB_COMMAND_H

/*
 * command.h — dnslab command handlers (declarations).
 *
 * Each mode of the CLI maps to exactly one cmd_* function:
 *
 *   MODE_DEFAULT  -> cmd_lookup    (resolve domain -> IP)        [stub]
 *   MODE_COMPOSE  -> cmd_compose   (build + show query)          [full]
 *   MODE_DETAILS  -> cmd_details   (full flow + verbose decode)   [stub]
 *
 * A "command" is a complete user-visible action. It receives the
 * validated domain string and returns a process exit code. All
 * argument parsing and usage/help output stays in cli.c.
 *
 * To add a new mode:
 *   1. add a declaration here:    int cmd_foo(const char *domain);
 *   2. implement it in            command/foo.c
 *   3. wire it into cli.c         (enum + parse branch + dispatch case)
 *   4. add command/foo.c to the Makefile CMD_SRC list
 */

/* Default transaction ID used when composing DNS queries. */
#define DEFAULT_QUERY_ID 0x1234

/* ---------- Command options ---------- */

/*
 * Optional parameters that modify command behaviour.
 * Passed to cmd_lookup() and cmd_details().
 */
typedef struct {
    const char *server;   /* --server <ip>, or NULL for auto-detect  */
    int         timeout_ms; /* --timeout <ms>, or 0 for default       */
    int         retries;    /* --retries <n>, or 0 for default        */
    int         verbose;    /* --verbose flag (1 = detailed output)    */
} cmd_options_t;

/* ---------- Commands ---------- */

/*
 * Resolve <domain> to an IP address: build query, send over UDP/53,
 * receive response, extract A record, print "domain -> ip".
 *
 * Brief mode (options.verbose == 0): print only IP addresses, one per line.
 * Verbose mode (options.verbose != 0): print server info, RCODE, answer list.
 */
int cmd_lookup(const char *domain, const cmd_options_t *options);

/*
 * Build a DNS A-record query for <domain> and display it in three
 * visual representations (schema / binary / human-readable), plus a
 * hex dump and structured field breakdown. No network I/O.
 */
int cmd_compose(const char *domain);

/*
 * Full flow with verbose output at every step: compose output plus a
 * real network round-trip and a full decode of the response.
 */
int cmd_details(const char *domain, const cmd_options_t *options);

/* ---------- Shared utilities ---------- */

/*
 * Build a DNS A-record query and optionally print the compose output.
 *   domain    — domain to query (e.g. "example.com")
 *   buf       — output buffer (at least DNS_MAX_PACKET bytes)
 *   cap       — buffer capacity
 *   print_all — if non-zero, print hex + structured + 3 schemes (as --compose-request)
 *
 * Returns the packet length, or -1 on error.
 */
int compose_request(const char *domain, uint8_t *buf, size_t cap, int print_all);

/*
 * Resolve the DNS server address.
 *   explicit — value from --server flag, or NULL for auto-detect
 *
 * Returns a malloc-allocated string that the caller must free.
 */
char *dns_resolve_server(const char *explicit);

#endif /* DNSLAB_COMMAND_H */
