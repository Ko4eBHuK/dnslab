package dnslab.android.model

/**
 * Question section entry: what is being asked.
 */
data class DnsQuestion(
    val name: String,
    val qtype: Int,
    val qclass: Int,
) {

    /**
     * Known record type for [qtype], or null if the app does not support it.
     */
    val type: DnsRecordType? get() = DnsRecordType.entries.find { it.code == qtype }
}
