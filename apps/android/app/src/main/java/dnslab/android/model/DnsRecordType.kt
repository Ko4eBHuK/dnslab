package dnslab.android.model

/**
 * DNS record types supported by the app. [code] is the IANA QTYPE value.
 */
enum class DnsRecordType(val code: Int, val label: String) {
    A(1, "A (IPv4)"),
    AAAA(28, "AAAA (IPv6)"),
}
