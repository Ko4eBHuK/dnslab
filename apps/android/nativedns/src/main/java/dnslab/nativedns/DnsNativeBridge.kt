package dnslab.nativedns

/**
 * Kotlin entry point to the dnslib C core. Only raw bytes cross the JNI boundary.
 */
object DnsNativeBridge {

    init {
        System.loadLibrary("nativedns")
    }


    /**
     * Builds a DNS query packet without sending it.
     *
     * @param domain domain name, e.g. "example.com"
     * @param qtype DNS record type code (1 = A, 28 = AAAA)
     * @return raw packet bytes, or null if the core rejected the input
     */
    external fun buildQuery(domain: String, qtype: Int): ByteArray?
}
