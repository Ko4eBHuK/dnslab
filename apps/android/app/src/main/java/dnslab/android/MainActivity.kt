package dnslab.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.ui.Modifier
import dnslab.android.ui.BuildQueryScreen
import dnslab.android.ui.theme.DnslabTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            DnslabTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    BuildQueryScreen(modifier = Modifier.padding(innerPadding))
                }
            }
        }
    }
}