package dnslab.android.render

/**
 * Classic hex dump: offset, 16 bytes in hex, ASCII column. Same layout as the CLI.
 */
fun hexDump(bytes: ByteArray): String = buildString {
    for (offset in bytes.indices step 16) {
        append("%04x  ".format(offset))

        for (j in 0 until 16) {
            val i = offset + j
            append(if (i < bytes.size) "%02x ".format(bytes[i].toInt() and 0xFF) else "   ")
            if (j == 7) append(' ')
        }

        append(' ')
        for (i in offset until minOf(offset + 16, bytes.size)) {
            val b = bytes[i].toInt() and 0xFF
            append(if (b in 32..126) b.toChar() else '.')
        }
        append('\n')
    }
}
