package games.polyworld.app

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Log

/** Opens http(s) links in the system browser. */
object PwBrowser {
    private const val TAG = "PolyWorld"

    @JvmStatic
    fun openUrl(context: Context, url: String): Boolean {
        return try {
            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url)).apply {
                addCategory(Intent.CATEGORY_BROWSABLE)
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
            context.startActivity(intent)
            true
        } catch (e: ActivityNotFoundException) {
            Log.e(TAG, "No browser for $url", e)
            false
        } catch (t: Throwable) {
            Log.e(TAG, "openUrl failed: $url", t)
            false
        }
    }
}
