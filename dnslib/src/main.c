/*
 * main.c — dnslab entry point.
 *
 * main() does nothing except hand control to the CLI state machine.
 * All argument parsing, dispatch, and command logic live in cli.c /
 * command/ (one cmd_*.c per action). Keeping this file trivial makes
 * obvious from the directory layout alone:
 *
 *   src/main.c              -> here (just calls cli_run)
 *   src/cli.{c,h}           -> state machine: parse argv + dispatch
 *   src/command/            -> one cmd_*.c file per user action
 *   src/dns.{c,h}           -> DNS query library
 */

#include "cli.h"

int main(int argc, char *argv[]) {
    return cli_run(argc, argv);
}
