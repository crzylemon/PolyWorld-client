package games.polyworld.app

import android.util.Log
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Protocol
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.util.concurrent.TimeUnit

/**
 * Plain OkHttp HTTPS — HTTP/1.1 only (required by PolyWorld / HAProxy).
 * Real URLs, system DNS. No LAN / gateway workarounds.
 */
object PwHttp {
    private const val TAG = "PolyWorld"

    @JvmField
    @Volatile
    var lastError: String = ""

    private val formType = "application/x-www-form-urlencoded".toMediaType()

    private val client: OkHttpClient by lazy {
        OkHttpClient.Builder()
            // Site requires HTTP/1.1 so do not negotiate h2/h3
            .protocols(listOf(Protocol.HTTP_1_1))
            .connectTimeout(10, TimeUnit.SECONDS)
            .readTimeout(15, TimeUnit.SECONDS)
            .writeTimeout(15, TimeUnit.SECONDS)
            .callTimeout(20, TimeUnit.SECONDS)
            .followRedirects(true)
            .followSslRedirects(true)
            .build()
    }

    @JvmStatic
    fun httpGet(url: String): ByteArray? = execute(url, "GET", null)

    @JvmStatic
    fun httpPost(url: String, body: String): ByteArray? = execute(url, "POST", body)

    private fun execute(url: String, method: String, body: String?): ByteArray? {
        lastError = ""
        return try {
            val reqBuilder = Request.Builder()
                .url(url)
                .header("User-Agent", "PolyWorld-Android/0.1")
                .header("Accept", "*/*")
                .header("Connection", "close")

            val request = if (method == "POST") {
                reqBuilder.post((body ?: "").toRequestBody(formType)).build()
            } else {
                reqBuilder.get().build()
            }

            client.newCall(request).execute().use { resp ->
                val bytes = resp.body?.bytes()
                Log.i(TAG, "http $method $url -> ${resp.code} proto=${resp.protocol}")
                if (!resp.isSuccessful) {
                    lastError = "HTTP ${resp.code}"
                    // Still return body so native can parse API JSON errors (e.g. version reject)
                    if (bytes != null && bytes.isNotEmpty()) return bytes
                    return null
                }
                if (bytes == null) {
                    lastError = "empty body"
                    return null
                }
                lastError = ""
                bytes
            }
        } catch (t: Throwable) {
            lastError = t.javaClass.simpleName + ": " + (t.message ?: "unknown")
            Log.e(TAG, "http $method $url failed: $lastError", t)
            null
        }
    }
}
