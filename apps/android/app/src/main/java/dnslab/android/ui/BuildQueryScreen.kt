package dnslab.android.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dnslab.android.model.DnsQueryPacket
import dnslab.android.model.DnsRecordType
import dnslab.android.render.hexDump
import dnslab.nativedns.DnsNativeBridge

/**
 * Builds a DNS query via the native core and shows its hex dump and decoded fields.
 */
@Composable
fun BuildQueryScreen(modifier: Modifier = Modifier) {
    var domain by remember { mutableStateOf("example.com") }
    var bytes by remember { mutableStateOf<ByteArray?>(null) }

    Column(
        modifier = modifier
            .padding(16.dp)
            .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        OutlinedTextField(
            value = domain,
            onValueChange = { domain = it },
            label = { Text("Domain") },
            singleLine = true,
        )

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            DnsRecordType.entries.forEach { type ->
                Button(onClick = { bytes = DnsNativeBridge.buildQuery(domain, type.code) }) {
                    Text(type.label)
                }
            }
        }

        bytes?.let {
            MonospaceText(hexDump(it))
            MonospaceText(packetDescription(DnsQueryPacket.parse(it)))
        }
    }
}

@Composable
private fun MonospaceText(text: String) {
    Text(
        text = text,
        fontFamily = FontFamily.Monospace,
        fontSize = 12.sp,
        modifier = Modifier.horizontalScroll(rememberScrollState()),
    )
}

private fun packetDescription(packet: DnsQueryPacket): String = with(packet) {
    """
    --- DNS Header ---
      ID:      ${hex16(header.id)} (${header.id})
      Flags:   ${hex16(header.flags)}
        QR:     ${header.qr} (${if (header.qr == 0) "query" else "response"})
        OPCODE: ${header.opcode}
        AA:     ${header.aa}
        TC:     ${header.tc}
        RD:     ${header.rd}
        RA:     ${header.ra}
        RCODE:  ${header.rcode}
      QDCOUNT: ${header.qdcount}
      ANCOUNT: ${header.ancount}
      NSCOUNT: ${header.nscount}
      ARCOUNT: ${header.arcount}

    --- Question ---
      Name:   ${question.name}
      QTYPE:  ${hex16(question.qtype)} (${question.type?.name ?: "?"})
      QCLASS: ${hex16(question.qclass)}

    --- End of packet ($size bytes) ---
    """.trimIndent()
}

private fun hex16(value: Int): String = "0x%04x".format(value)
