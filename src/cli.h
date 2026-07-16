#ifndef DNSLAB_CLI_H
#define DNSLAB_CLI_H

/*
 * cli.h — command-line state machine for dnslab.
 *
 * The CLI is a "parse -> dispatch" state machine. cli_run() does both:
 *   1. Parses argv into a (mode, domain) pair.
 *   2. Dispatches to the matching command handler in command/.
 * It also handles --help and all argument errors, so command handlers
 * receive only validated inputs.
 *
 * main() is a one-liner:  return cli_run(argc, argv);
 *
 * Adding a new mode is a three-step change: add an enum value here
 * (well, in cli.c — the enum is internal), add a strcmp() branch in
 * parse_args(), add a dispatch case in cli_run().
 */

/*
 * Which action the user selected via command-line flags.
 *
 * NOTE: declared here so command/ could in principle read the mode, but
 * in practice the enum is only used inside cli.c. It lives in the
 * header only to document the set of modes to the rest of the program.
 */
typedef enum {
    MODE_DEFAULT,   /* no flag         -> cmd_lookup   (resolve -> IP) */
    MODE_COMPOSE,   /* --compose-request -> cmd_compose                */
    MODE_DETAILS,   /* --details        -> cmd_details                 */
    MODE_HELP,      /* -h / --help                                     */
    MODE_ERROR      /* invalid combination of arguments                */
} app_mode_t;

/*
 * Query type (A = IPv4 or AAAA = IPv6), selected by --ipv6 flag.
 */
typedef enum {
    QTYPE_A    = 1,    /* DNS_TYPE_A    — IPv4 address */
    QTYPE_AAAA = 28    /* DNS_TYPE_AAAA — IPv6 address */
} app_qtype_t;

/*
 * Run the CLI: parse argv and dispatch to the appropriate command.
 * Returns the command's exit code, or 1 for usage errors. Never
 * returns without having handled everything.
 */
int cli_run(int argc, char *argv[]);

#endif /* DNSLAB_CLI_H */
