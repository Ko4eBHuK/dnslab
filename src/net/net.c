/*
 * net/net.c — DNS transport layer (UDP).
 *
 * Implements dns_send_udp(): resolve server -> socket -> sendto ->
 * recvfrom (with timeout + retry) -> return raw response.
 */

#include "net/net.h"
#include "engine/dns.h"    /* for DNS_HEADER_SIZE, dns_header_t, ntohs */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>

/* ================================================================
 * dns_net_strerror
 * ================================================================ */
const char *dns_net_strerror(dns_net_status_t s) {
    switch (s) {
        case DNS_NET_OK:        return "success";
        case DNS_NET_BAD_SERVER: return "cannot resolve server address";
        case DNS_NET_SOCKET:    return "socket creation failed";
        case DNS_NET_SEND:      return "send failed";
        case DNS_NET_TIMEOUT:   return "no response after all retries";
        case DNS_NET_REFUSED:   return "server unreachable (connection refused)";
        case DNS_NET_TRUNCATED: return "response too short";
        default:                return "unknown error";
    }
}

/* ================================================================
 * dns_send_udp
 *
 * Steps:
 *   1. Resolve server address with getaddrinfo (SOCK_DGRAM).
 *   2. Create a UDP socket.
 *   3. Set SO_RCVTIMEO for per-attempt timeout.
 *   4. Loop up to (retries + 1) times:
 *        sendto(query)
 *        recvfrom(response)
 *        - if EAGAIN/EWOULDBLOCK → continue (retry)
 *        - if ECONNREFUSED       → break (server unreachable)
 *        - if response too short → break (truncated)
 *        - check resp.ID == req.ID, if not → continue (ignore stale)
 *        - on success → return response length
 *   5. Close the socket in all cases.
 * ================================================================ */
int dns_send_udp(const uint8_t *req, size_t req_len,
                 uint8_t *resp, size_t resp_cap,
                 const char *server, uint16_t port,
                 int timeout_ms, int retries,
                 dns_net_status_t *status) {
    /* Default to auto-detect if no server given. */
    const char *server_str = server;
    if (server_str == NULL) {
        /* We'll handle fallback logic in the command layer (resolv.c).
         * If none given here, fall back to 8.8.8.8 directly. */
        server_str = "8.8.8.8";
    }

    /* ---- Step 1: resolve server address ---- */
    char port_str[8];
    snprintf(port_str, sizeof port_str, "%u", (unsigned)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;    /* IPv4 + IPv6 */
    hints.ai_socktype = SOCK_DGRAM;   /* UDP */
    hints.ai_flags    = 0;
    hints.ai_protocol = 0;

    struct addrinfo *ai;
    int rc = getaddrinfo(server_str, port_str, &hints, &ai);
    if (rc != 0) {
        if (status) *status = DNS_NET_BAD_SERVER;
        return -1;
    }

    /* ---- Step 2: create socket ---- */
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(ai);
        if (status) *status = DNS_NET_SOCKET;
        return -1;
    }

    /* ---- Step 3: set receive timeout ---- */
    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }

    /* ---- Step 4: send + receive loop ---- */
    int result = -1;
    dns_net_status_t local_status = DNS_NET_OK;

    /* retries = number of ADDITIONAL attempts on top of the first.
     * So we loop up to retries + 1 times. */
    int max_attempts = (retries < 0 ? 1 : retries + 1);

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        /* Send the query */
        ssize_t sent = sendto(fd, req, req_len, 0,
                              ai->ai_addr, ai->ai_addrlen);
        if (sent < 0) {
            local_status = DNS_NET_SEND;
            result = -1;
            break;
        }

        /* Receive the response */
        ssize_t n = recvfrom(fd, resp, resp_cap, 0, NULL, NULL);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Timeout — retry if we have attempts left */
                local_status = DNS_NET_TIMEOUT;
                continue;
            }
            if (errno == ECONNREFUSED) {
                local_status = DNS_NET_REFUSED;
                result = -1;
                break;
            }
            /* Other recvfrom error — treat as send failure */
            local_status = DNS_NET_SEND;
            result = -1;
            break;
        }

        /* Check for truncated response */
        if (n < DNS_HEADER_SIZE) {
            local_status = DNS_NET_TRUNCATED;
            result = -1;
            break;
        }

        /* Check that the response ID matches the request ID */
        const dns_header_t *req_hdr  = (const dns_header_t *)req;
        const dns_header_t *resp_hdr = (const dns_header_t *)resp;
        if (ntohs(resp_hdr->id) != ntohs(req_hdr->id)) {
            /* Stale/foreign response — wait for the correct one.
             * This does NOT count as a failed attempt. */
            continue;
        }

        /* Success */
        local_status = DNS_NET_OK;
        result = (int)n;
        break;
    }

    if (result < 0 && local_status == DNS_NET_TIMEOUT) {
        /* All attempts timed out */
        local_status = DNS_NET_TIMEOUT;
    }

    /* ---- Step 5: cleanup ---- */
    close(fd);
    freeaddrinfo(ai);

    if (status) *status = local_status;
    return result;
}
