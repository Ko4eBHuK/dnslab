package dnslab.android.model

/**
 * DNS packet header (first 12 bytes). Flag bits are decoded from [flags].
 */
data class DnsHeader(
    val id: Int,
    val flags: Int,
    val qdcount: Int,
    val ancount: Int,
    val nscount: Int,
    val arcount: Int,
) {

    companion object {

        /**
         * Header size on the wire, in bytes.
         */
        const val SIZE = 12
    }


    /**
     * 0 = query, 1 = response.
     */
    val qr: Int get() = (flags shr 15) and 1

    /**
     * Kind of query, 0 = standard query.
     */
    val opcode: Int get() = (flags shr 11) and 0x0F

    /**
     * Authoritative answer.
     */
    val aa: Int get() = (flags shr 10) and 1

    /**
     * Truncated message.
     */
    val tc: Int get() = (flags shr 9) and 1

    /**
     * Recursion desired.
     */
    val rd: Int get() = (flags shr 8) and 1

    /**
     * Recursion available.
     */
    val ra: Int get() = (flags shr 7) and 1

    /**
     * Response code, 0 = no error.
     */
    val rcode: Int get() = flags and 0x0F
}
