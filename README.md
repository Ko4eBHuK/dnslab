# dnslab

An educational DNS tool for learning how DNS works "from the bottom up" —
from constructing a raw binary query to sending it over UDP and parsing
the response.

## Quick Start

```bash
make
./build/bin/dnslab example.com           # A (IPv4) lookup
./build/bin/dnslab --ipv6 example.com    # AAAA (IPv6) lookup
make test                               # run unit tests
```

## Modes

| Mode | Flag | Description |
|------|------|-------------|
| Lookup | *(none)* | Build query → send via UDP/53 → print IP(s) |
| Compose | `--compose-request` | Build query only, display hex + structured + 3 visual schemes |
| Details | `--details` | Full flow with verbose logging at every step |

## Options

| Option | Description |
|--------|-------------|
| `--server <ip>` | DNS server address (default: auto-detect from `/etc/resolv.conf` or `8.8.8.8`) |
| `--timeout <ms>` | Receive timeout per attempt (default: 5000) |
| `--retries <n>` | Number of retries on timeout (default: 2) |
| `--verbose, -v` | Detailed output for lookup mode |
| `--ipv6, -6` | Query for AAAA (IPv6) records instead of A (IPv4) |
| `-h, --help` | Show help message |

## Examples

```bash
# Basic A (IPv4) lookup
./build/bin/dnslab example.com

# IPv6 lookup
./build/bin/dnslab --ipv6 google.com

# Verbose lookup with custom server
./build/bin/dnslab --verbose --server 1.1.1.1 example.com

# Compose mode — see the raw query bytes (no network)
./build/bin/dnslab --compose-request example.com

# Compose an AAAA query
./build/bin/dnslab --ipv6 --compose-request example.com

# Full debug flow
./build/bin/dnslab --details example.com

# Custom timeout and retries
./build/bin/dnslab --timeout 2000 --retries 1 example.com
```

## Project Structure

```
src/
├── main.c              Entry point
├── cli.c/h             Argument parsing & dispatch
├── engine/
│   └── dns.c/h         DNS protocol: query builder + response parser
├── decor/
│   └── print.c/h       Presentation: hex dump, structured view, 3 ASCII schemes
├── net/
│   └── net.c/h         UDP transport layer
├── command/
│   ├── command.h       Shared command declarations & types
│   ├── compose.c       --compose-request mode
│   ├── lookup.c        Default lookup mode
│   ├── details.c       --details mode
│   └── resolv.c        DNS server auto-detection
└── test/
    └── test_dns.c      Unit tests for query builder
```

## Comparison with dig / nslookup

| | dig | nslookup | dnslab |
|--|-----|----------|--------|
| Purpose | Professional DNS diagnostics | Interactive / simple queries | **Educational** |
| Record types | All | Common (A, AAAA, MX, NS, …) | A (IPv4), AAAA (IPv6) |
| Hex dump | Yes (`+qr` flag) | No | **Yes** (`--details`) |
| Packet visualization | No | No | **Yes** (3 ASCII representations) |
| DNSSEC | Yes | No | No |
| EDNS0 | Yes | Yes | No |

See [`comparison-dig-nslookup.md`](comparison-dig-nslookup.md) for a detailed
comparison.

## Build

```bash
make          # release build
make debug    # debug build (no optimizations, -O0)
make test     # build and run unit tests
make clean    # remove build artifacts
```

Requirements: C17 compiler (gcc/clang), GNU Make, POSIX sockets (macOS/Linux).
