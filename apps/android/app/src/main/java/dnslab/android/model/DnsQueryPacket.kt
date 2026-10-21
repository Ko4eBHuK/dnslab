package dnslab.android.model

/**
 * DNS query packet decoded from raw wire bytes: header plus the single question.
 */
data class DnsQueryPacket(
    val header: DnsHeader,
    val question: DnsQuestion,
    val size: Int,
) {

    companion object {

        /**
         * Decodes [bytes] produced by the native query builder.
         */
        fun parse(bytes: ByteArray): DnsQueryPacket {
            val header = DnsHeader(
                id = bytes.u16(0),
                flags = bytes.u16(2),
                qdcount = bytes.u16(4),
                ancount = bytes.u16(6),
                nscount = bytes.u16(8),
                arcount = bytes.u16(10),
            )

            // The name is a sequence of length-prefixed labels ending with a zero byte
            var pos = DnsHeader.SIZE
            val labels = mutableListOf<String>()
            while (bytes[pos].toInt() != 0) {
                val length = bytes[pos].toInt() and 0xFF
                labels += String(bytes, pos + 1, length, Charsets.US_ASCII)
                pos += 1 + length
            }
            pos++

            val question = DnsQuestion(
                name = labels.joinToString(separator = ".", postfix = "."),
                qtype = bytes.u16(pos),
                qclass = bytes.u16(pos + 2),
            )

            return DnsQueryPacket(header, question, bytes.size)
        }
    }
}

private fun ByteArray.u16(offset: Int): Int =
    ((this[offset].toInt() and 0xFF) shl 8) or (this[offset + 1].toInt() and 0xFF)
