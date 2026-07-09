#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "engine/dns.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    printf("  %-45s ", name); \
} while (0)

#define PASS() do { \
    printf("ok\n"); \
    tests_passed++; \
} while (0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
    tests_failed++; \
} while (0)

/* ================================================================
 * Test: dns_encode_name
 * ================================================================ */
static void test_encode_simple(void) {
    TEST("encode_name: \"example.com\"");

    uint8_t buf[256];
    int len = dns_encode_name("example.com", buf, sizeof(buf));

    if (len != 13) {
        FAIL("expected length 13");
        return;
    }

    /* Expected: \x07example\x03com\x00 */
    const uint8_t expected[] = {
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm',
        0x00
    };

    if (memcmp(buf, expected, (size_t)len) != 0) {
        FAIL("byte mismatch");
        return;
    }

    PASS();
}

static void test_encode_root(void) {
    TEST("encode_name: \"\"");

    uint8_t buf[256];
    int len = dns_encode_name("", buf, sizeof(buf));

    if (len != 1) {
        FAIL("expected length 1");
        return;
    }

    if (buf[0] != 0x00) {
        FAIL("expected just zero byte");
        return;
    }

    PASS();
}

static void test_encode_multi_label(void) {
    TEST("encode_name: \"www.example.com\"");

    uint8_t buf[256];
    int len = dns_encode_name("www.example.com", buf, sizeof(buf));

    if (len != 17) {
        FAIL("expected length 17");
        return;
    }

    /* Expected: \x03www\x07example\x03com\x00 */
    const uint8_t expected[] = {
        0x03, 'w', 'w', 'w',
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm',
        0x00
    };

    if (memcmp(buf, expected, (size_t)len) != 0) {
        FAIL("byte mismatch");
        return;
    }

    PASS();
}

static void test_encode_buffer_too_small(void) {
    TEST("encode_name: buffer too small");

    uint8_t buf[2];  /* way too small */
    int len = dns_encode_name("example.com", buf, sizeof(buf));

    if (len != -1) {
        FAIL("expected -1 for too-small buffer");
        return;
    }

    PASS();
}

/* ================================================================
 * Test: dns_build_query
 * ================================================================ */
static void test_build_query_example_a(void) {
    TEST("build_query: \"example.com\" A-record");

    uint8_t buf[DNS_MAX_PACKET];
    int len = dns_build_query(0x1234, "example.com",
                              DNS_TYPE_A, buf, sizeof(buf));

    if (len != 29) {
        FAIL("expected packet length 29");
        return;
    }

    /* Check header fields */
    /* ID = 0x1234 */
    if (buf[0] != 0x12 || buf[1] != 0x34) {
        FAIL("ID mismatch");
        return;
    }

    /* Flags = 0x0100 (standard query, RD=1) */
    if (buf[2] != 0x01 || buf[3] != 0x00) {
        FAIL("Flags mismatch");
        return;
    }

    /* QDCOUNT = 1 */
    if (buf[4] != 0x00 || buf[5] != 0x01) {
        FAIL("QDCOUNT mismatch");
        return;
    }

    /* ANCOUNT = 0 */
    if (buf[6] != 0x00 || buf[7] != 0x00) {
        FAIL("ANCOUNT mismatch");
        return;
    }

    /* NSCOUNT = 0 */
    if (buf[8] != 0x00 || buf[9] != 0x00) {
        FAIL("NSCOUNT mismatch");
        return;
    }

    /* ARCOUNT = 0 */
    if (buf[10] != 0x00 || buf[11] != 0x00) {
        FAIL("ARCOUNT mismatch");
        return;
    }

    /* Check domain name encoding: \x07example\x03com\x00 */
    const uint8_t expected_name[] = {
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm',
        0x00
    };
    if (memcmp(buf + 12, expected_name, sizeof(expected_name)) != 0) {
        FAIL("domain encoding mismatch");
        return;
    }

    /* QTYPE = 0x0001 (A) */
    if (buf[25] != 0x00 || buf[26] != 0x01) {
        FAIL("QTYPE mismatch");
        return;
    }

    /* QCLASS = 0x0001 (IN) */
    if (buf[27] != 0x00 || buf[28] != 0x01) {
        FAIL("QCLASS mismatch");
        return;
    }

    PASS();
}

static void test_build_query_different_id(void) {
    TEST("build_query: ID = 0xABCD");

    uint8_t buf[DNS_MAX_PACKET];
    int len = dns_build_query(0xABCD, "test.com",
                              DNS_TYPE_A, buf, sizeof(buf));

    if (len < 0) {
        FAIL("build failed");
        return;
    }

    if (buf[0] != 0xAB || buf[1] != 0xCD) {
        FAIL("ID mismatch");
        return;
    }

    PASS();
}

static void test_build_query_buffer_too_small(void) {
    TEST("build_query: buffer too small");

    uint8_t buf[4];  /* way too small */
    int len = dns_build_query(0x1234, "example.com",
                              DNS_TYPE_A, buf, sizeof(buf));

    if (len != -1) {
        FAIL("expected -1 for too-small buffer");
        return;
    }

    PASS();
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void) {
    printf("=== DNS Query Builder Tests ===\n\n");

    /* dns_encode_name tests */
    printf("--- dns_encode_name ---\n");
    test_encode_simple();
    test_encode_root();
    test_encode_multi_label();
    test_encode_buffer_too_small();
    putchar('\n');

    /* dns_build_query tests */
    printf("--- dns_build_query ---\n");
    test_build_query_example_a();
    test_build_query_different_id();
    test_build_query_buffer_too_small();
    putchar('\n');

    /* Summary */
    printf("Results: %d passed, %d failed out of %d\n",
           tests_passed, tests_failed,
           tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
