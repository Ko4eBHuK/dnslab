#ifndef DNSLAB_NET_H
#define DNSLAB_NET_H

/*
 * net/net.h — DNS transport layer (UDP).
 *
 * Sends a pre-built DNS query over UDP, waits for a response with
 * configurable timeout and retry logic. The engine module knows
 * nothing about sockets — that separation lives here.
 *
 * Usage:
 *   uint8_t resp[DNS_MAX_PACKET];
 *   dns_net_status_t status;
 *   int n = dns_send_udp(req, req_len, resp, sizeof(resp),
 *                        "8.8.8.8", 53, 2000, 2, &status);
 *   if (n >= 0) { ... parse response ... }
 */

#include <stdint.h>
#include <stddef.h>

#define DNS_PORT                53
#define DNS_DEFAULT_TIMEOUT_MS  2000
#define DNS_DEFAULT_RETRIES     2

/* Status codes returned via the out-parameter of dns_send_udp(). */
typedef enum {
    DNS_NET_OK = 0,            /* response received, length returned       */
    DNS_NET_BAD_SERVER,        /* cannot resolve server hostname           */
    DNS_NET_SOCKET,            /* socket creation failed                   */
    DNS_NET_SEND,              /* sendto failed                            */
    DNS_NET_TIMEOUT,           /* all retries exhausted, no response       */
    DNS_NET_REFUSED,           /* ECONNREFUSED (ICMP port unreachable)     */
    DNS_NET_TRUNCATED          /* response too short (< 12 bytes)          */
} dns_net_status_t;

/*
 * Send a DNS query over UDP and receive a raw response.
 *
 *   req        — assembled query (from dns_build_query)
 *   req_len    — length of the query
 *   resp       — buffer for the response (>= DNS_MAX_PACKET)
 *   resp_cap   — capacity of the response buffer
 *   server     — IP/hostname string ("8.8.8.8"), or NULL for auto-detect
 *   port       — destination port (usually DNS_PORT)
 *   timeout_ms — per-attempt receive timeout in milliseconds
 *   retries    — number of retries on timeout (0 = single attempt)
 *   status     — out: detailed status code
 *
 * Returns the length of the received response on success, or -1 on error.
 * On error, *status indicates what went wrong.
 *
 * The caller must check that resp.ID matches req.ID for transaction
 * integrity (the transport layer does this internally for retries, but
 * the final response's ID is not verified — the caller may do so).
 */
int dns_send_udp(const uint8_t *req, size_t req_len,
                 uint8_t *resp, size_t resp_cap,
                 const char *server, uint16_t port,
                 int timeout_ms, int retries,
                 dns_net_status_t *status);

/*
 * Return a human-readable string for a dns_net_status_t value.
 * The returned string is statically allocated — do not free.
 */
const char *dns_net_strerror(dns_net_status_t s);

#endif /* DNSLAB_NET_H */
