package com.obsidian.client

import android.content.Context
import android.os.Build
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Java-side crash capture. Writes a report under files/crash_reports/ and
 * chains to the previous default handler so the system dialog still appears.
 */
object CrashReporter {
    private const val TAG = "ObsidianCrash"
    private const val DIR = "crash_reports"

    @Volatile
    private var installed = false

    fun install(context: Context) {
        if (installed) return
        installed = true
        val app = context.applicationContext
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                writeReport(app, thread, throwable)
            } catch (t: Throwable) {
                Log.e(TAG, "Failed to write crash report", t)
            }
            previous?.uncaughtException(thread, throwable)
        }
        Log.i(TAG, "UncaughtExceptionHandler installed")
    }

    fun writeBreadcrumb(context: Context, message: String) {
        try {
            val dir = File(context.filesDir, DIR).apply { mkdirs() }
            File(dir, "last_breadcrumb.txt").writeText(
                "${timestamp()} $message\n"
            )
        } catch (_: Throwable) {
        }
    }

    fun latestReport(context: Context): File? {
        val dir = File(context.filesDir, DIR)
        if (!dir.isDirectory) return null
        return dir.listFiles()?.filter { it.name.endsWith(".txt") }?.maxByOrNull { it.lastModified() }
    }

    private fun writeReport(context: Context, thread: Thread, throwable: Throwable) {
        val dir = File(context.filesDir, DIR).apply { mkdirs() }
        val name = "crash_${SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())}.txt"
        val file = File(dir, name)
        val sw = StringWriter()
        throwable.printStackTrace(PrintWriter(sw))
        val body = buildString {
            appendLine("=== Obsidian Crash Report ===")
            appendLine("time=${timestamp()}")
            appendLine("thread=${thread.name} id=${thread.id}")
            appendLine("package=${context.packageName}")
            appendLine("version=${runCatching { context.packageManager.getPackageInfo(context.packageName, 0).versionName }.getOrNull()}")
            appendLine("sdk=${Build.VERSION.SDK_INT} release=${Build.VERSION.RELEASE}")
            appendLine("device=${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE})")
            appendLine("abi=${Build.SUPPORTED_ABIS.joinToString()}")
            appendLine()
            appendLine(sw.toString())
        }
        file.writeText(body)
        Log.e(TAG, "Crash report written: ${file.absolutePath}")
        Log.e(TAG, body)
    }

    private fun timestamp(): String =
        SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS Z", Locale.US).format(Date())
}
