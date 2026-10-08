package dnslab.nativedns

class NativeLib {

    /**
     * A native method that is implemented by the 'nativedns' native library,
     * which is packaged with this application.
     */
    external fun stringFromJNI(): String

    external fun nativeSum(x: Int, y: Int): Int

    companion object {
        // Used to load the 'nativedns' library on application startup.
        init {
            System.loadLibrary("nativedns")
        }
    }
}