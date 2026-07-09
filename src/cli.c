/*
 * cli.c — implementation of the dnslab CLI state machine.
 *
 * Owns:      argument parsing, dispatch, usage/help output, and
 *            per-mode precondition checks (e.g. "domain required").
 * Owns NOT:  any DNS or networking logic — that lives in command/.
 *
 * Flow:  cli_run() -> parse_args() -> switch(args.mode) -> cmd_*().
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cli.h"
#include "command/command.h"
#include "net/net.h"   /* for DNS_DEFAULT_TIMEOUT_MS, DNS_DEFAULT_RETRIES */

/* ================================================================
 * Internal state of a parsed command line
 * ================================================================ */
typedef struct {
    app_mode_t  mode;
    const char *domain;     /* positional argument (NULL if absent)    */
    const char *error;      /* human-readable reason, when MODE_ERROR  */
    const char *bad_token;  /* offending token (e.g. unknown flag)     */

    /* Optional flags */
    const char *server;     /* --server <ip> (NULL = auto-detect)     */
    int         timeout_ms; /* --timeout <ms> (0 = default)            */
    int         retries;    /* --retries <n> (0 = default)             */
    int         verbose;    /* --verbose / -v                          */
} cli_args_t;

/* ================================================================
 * parse_args
 *
 * Walk argv and fill a cli_args_t. Rules:
 *   - At most one mode flag may appear (else ERROR).
 *   - At most one positional argument (the domain) may appear.
 *   - Any unrecognized "--foo" / "-x" token is an ERROR.
 *   - The domain is the first token that does not start with '-'.
 *
 * The parser does NOT check that a domain is required for a given
 * mode — that check lives in the dispatcher, so error messages can
 * be mode-specific ("--compose-request requires a <domain>").
 * ================================================================ */
static cli_args_t parse_args(int argc, char *argv[]) {
    cli_args_t args;
    args.mode      = MODE_DEFAULT;
    args.domain    = NULL;
    args.error     = NULL;
    args.bad_token = NULL;
    args.server    = NULL;
    args.timeout_ms = 0;
    args.retries    = 0;
    args.verbose    = 0;

    int mode_flags = 0;
    int domains    = 0;

    for (int i = 1; i < argc; i++) {
        const char *tok = argv[i];

        if (strcmp(tok, "--compose-request") == 0) {
            args.mode = MODE_COMPOSE;
            mode_flags++;
        } else if (strcmp(tok, "--details") == 0) {
            args.mode = MODE_DETAILS;
            mode_flags++;
        } else if (strcmp(tok, "-h") == 0 || strcmp(tok, "--help") == 0) {
            args.mode = MODE_HELP;
            mode_flags++;
        } else if (strcmp(tok, "--server") == 0) {
            /* Consume next argument as the server address */
            if (i + 1 >= argc) {
                args.mode      = MODE_ERROR;
                args.error     = "--server requires an IP address argument";
                args.bad_token = tok;
                return args;
            }
            args.server = argv[++i];
        } else if (strcmp(tok, "--timeout") == 0) {
            if (i + 1 >= argc) {
                args.mode      = MODE_ERROR;
                args.error     = "--timeout requires a value in milliseconds";
                args.bad_token = tok;
                return args;
            }
            args.timeout_ms = atoi(argv[++i]);
            if (args.timeout_ms <= 0) {
                args.mode      = MODE_ERROR;
                args.error     = "--timeout must be a positive number";
                args.bad_token = tok;
                return args;
            }
        } else if (strcmp(tok, "--retries") == 0) {
            if (i + 1 >= argc) {
                args.mode      = MODE_ERROR;
                args.error     = "--retries requires a number";
                args.bad_token = tok;
                return args;
            }
            args.retries = atoi(argv[++i]);
            if (args.retries < 0) {
                args.mode      = MODE_ERROR;
                args.error     = "--retries must be >= 0";
                args.bad_token = tok;
                return args;
            }
        } else if (strcmp(tok, "--verbose") == 0 || strcmp(tok, "-v") == 0) {
            args.verbose = 1;
        } else if (tok[0] == '-' && tok[1] != '\0') {
            /* Unrecognized flag (e.g. "--foo", "-x"). Bare "-" is NOT
             * treated as a flag: it would be a weird domain, but we
             * let it through as a positional argument. */
            args.mode      = MODE_ERROR;
            args.error     = "unknown flag";
            args.bad_token = tok;
            return args;
        } else {
            /* Positional argument: treat as the domain name. */
            if (domains == 0) {
                args.domain = tok;
            }
            domains++;
        }
    }

    if (mode_flags > 1) {
        args.mode       = MODE_ERROR;
        args.error      = "multiple mode flags given (use only one of "
                          "--compose-request / --details / --help)";
        args.bad_token  = NULL;
        return args;
    }
    if (domains > 1) {
        args.mode       = MODE_ERROR;
        args.error      = "multiple domain arguments given (expected one)";
        args.bad_token  = NULL;
        return args;
    }

    return args;
}

/* ================================================================
 * print_usage
 * ================================================================ */
static void print_usage(const char *prog) {
    printf("Usage: %s [FLAG] <domain>\n\n", prog);
    printf("An educational DNS tool. The chosen FLAG selects a mode of operation.\n\n");
    printf("Modes:\n");
    printf("  (none)              Build a query, send it over UDP/53, and\n");
    printf("                      resolve <domain> to an IP address.\n");
    printf("  --compose-request   Only build the query and display it\n");
    printf("                      (no network).\n");
    printf("  --details           Full flow with verbose output at every step.\n\n");
    printf("Options:\n");
    printf("  --server <ip>       DNS server address (default: auto-detect).\n");
    printf("  --timeout <ms>      Receive timeout per attempt (default: %d).\n",
           DNS_DEFAULT_TIMEOUT_MS);
    printf("  --retries <n>       Number of retries on timeout (default: %d).\n",
           DNS_DEFAULT_RETRIES);
    printf("  --verbose, -v       Detailed output for lookup mode.\n");
    printf("  -h, --help          Show this help message.\n\n");
    printf("Arguments:\n");
    printf("  <domain>            Domain name to query (e.g. example.com).\n\n");
    printf("Examples:\n");
    printf("  %s example.com\n", prog);
    printf("  %s --verbose example.com\n", prog);
    printf("  %s --server 8.8.8.8 example.com\n", prog);
    printf("  %s --timeout 3000 --retries 1 example.com\n", prog);
    printf("  %s --compose-request example.com\n", prog);
    printf("  %s --details example.com\n", prog);
}

/* ================================================================
 * cli_run — parse + dispatch
 *
 * Read the parsed state and switch into exactly one branch. Each
 * branch validates its own precondition (e.g. presence of <domain>)
 * before running, so messages stay specific to the chosen mode.
 * ================================================================ */
int cli_run(int argc, char *argv[]) {
    cli_args_t args = parse_args(argc, argv);

    switch (args.mode) {
        case MODE_HELP:
            print_usage(argv[0]);
            return 0;

        case MODE_ERROR:
            fprintf(stderr, "Error: %s", args.error);
            if (args.bad_token != NULL) {
                fprintf(stderr, " \"%s\"", args.bad_token);
            }
            fprintf(stderr, "\n\n");
            print_usage(argv[0]);
            return 1;

        case MODE_COMPOSE:
            if (args.domain == NULL) {
                fprintf(stderr, "Error: --compose-request requires a <domain>\n\n");
                print_usage(argv[0]);
                return 1;
            }
            return cmd_compose(args.domain);

        case MODE_DETAILS: {
            if (args.domain == NULL) {
                fprintf(stderr, "Error: --details requires a <domain>\n\n");
                print_usage(argv[0]);
                return 1;
            }
            cmd_options_t opts = {
                .server    = args.server,
                .timeout_ms = args.timeout_ms,
                .retries   = args.retries,
                .verbose   = args.verbose,
            };
            return cmd_details(args.domain, &opts);
        }

        case MODE_DEFAULT: {
            if (args.domain == NULL) {
                /* No arguments at all -> show help and exit cleanly. */
                print_usage(argv[0]);
                return 0;
            }
            cmd_options_t opts = {
                .server    = args.server,
                .timeout_ms = args.timeout_ms,
                .retries   = args.retries,
                .verbose   = args.verbose,
            };
            return cmd_lookup(args.domain, &opts);
        }

        default:
            /* Unreachable: every enum value is handled above. */
            fprintf(stderr, "Internal error: unknown mode\n");
            return 1;
    }
}
