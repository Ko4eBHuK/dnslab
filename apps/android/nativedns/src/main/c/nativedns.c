/*
 * nativedns.c — JNI bridge between Kotlin (DnsNativeBridge) and dnslib.
 *
 * Only marshals data across the boundary: Kotlin types in, raw packet
 * bytes out. All DNS logic stays in dnslib/src/engine.
 */

#include <jni.h>
#include <stdint.h>

#include "engine/dns.h"

/* Transaction ID for composed queries (same as DEFAULT_QUERY_ID in the CLI). */
#define BRIDGE_QUERY_ID 0x1234

JNIEXPORT jbyteArray JNICALL
Java_dnslab_nativedns_DnsNativeBridge_buildQuery(JNIEnv *env, jobject thiz,
                                                 jstring jdomain, jint qtype) {
    (void)thiz;

    const char *domain = (*env)->GetStringUTFChars(env, jdomain, NULL);

    uint8_t buf[DNS_MAX_PACKET];
    int len = dns_build_query(BRIDGE_QUERY_ID, domain, (uint16_t)qtype,
                              buf, sizeof buf);

    (*env)->ReleaseStringUTFChars(env, jdomain, domain);

    if (len < 0) {
        return NULL;
    }

    jbyteArray result = (*env)->NewByteArray(env, len);
    (*env)->SetByteArrayRegion(env, result, 0, len, (const jbyte *)buf);
    return result;
}
