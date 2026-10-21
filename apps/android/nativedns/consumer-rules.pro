# JNI symbol names are derived from the class name, so R8 must not rename it.
-keep class dnslab.nativedns.DnsNativeBridge { *; }
