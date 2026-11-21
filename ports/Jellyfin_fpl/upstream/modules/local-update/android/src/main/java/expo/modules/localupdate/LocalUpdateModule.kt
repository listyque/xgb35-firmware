package expo.modules.localupdate

import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import expo.modules.kotlin.functions.Queues
import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition
import java.io.File
import java.security.DigestOutputStream
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.zip.ZipEntry
import java.util.zip.ZipFile

class LocalUpdateFileProvider : FileProvider()

class LocalUpdateModule : Module() {
  private val context
    get() = requireNotNull(appContext.reactContext)

  private val apkFile
    get() = File(context.cacheDir, "streamyfin-update/Streamyfin-Local.apk")

  override fun definition() = ModuleDefinition {
    Name("StreamyfinLocalUpdate")

    AsyncFunction("prepare") { archiveUri: String, versionCode: Int, sha256: String ->
      val archive = File(requireNotNull(Uri.parse(archiveUri).path)).canonicalFile
      check(archive.parentFile == context.cacheDir.canonicalFile)
      require(sha256.matches(Regex("[a-f0-9]{64}")))
      require(versionCode > readVersion(installedPackage()))

      val apk = apkFile
      apk.parentFile?.mkdirs()
      try {
        ZipFile(archive).use { zip ->
          val signature = requireNotNull(zip.getEntry("META-INF/streamyfin-update.sig"))
          require(signature.size == 384L)
          val key = context.resources.openRawResource(R.raw.streamyfin_update_key).use {
            KeyFactory.getInstance("RSA").generatePublic(X509EncodedKeySpec(it.readBytes()))
          }
          val verifier = Signature.getInstance("SHA256withRSA")
          verifier.initVerify(key)
          verifier.update("$versionCode\n$sha256".toByteArray(Charsets.UTF_8))
          check(zip.getInputStream(signature).use { verifier.verify(it.readBytes()) })

          val payload = requireNotNull(zip.getEntry("META-INF/Streamyfin-Local.apk"))
          require(payload.size in 1..536_870_912L && payload.method == ZipEntry.STORED)
          val digest = MessageDigest.getInstance("SHA-256")
          val bytes = zip.getInputStream(payload).use { input ->
            DigestOutputStream(apk.outputStream(), digest).use { input.copyTo(it) }
          }
          check(bytes == payload.size)
          check(digest.digest().joinToString("") { "%02x".format(it.toInt() and 255) } == sha256)
          verifyApk(apk, versionCode)
        }
      } catch (error: Exception) {
        apk.delete()
        throw error
      }
    }

    AsyncFunction("install") { versionCode: Int ->
      val apk = apkFile
      verifyApk(apk, versionCode)
      if (!context.packageManager.canRequestPackageInstalls()) {
        context.startActivity(
          Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${context.packageName}"))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        )
        return@AsyncFunction false
      }

      val uri = FileProvider.getUriForFile(context, "${context.packageName}.local-update", apk)
      context.startActivity(
        Intent(Intent.ACTION_VIEW)
          .setDataAndType(uri, "application/vnd.android.package-archive")
          .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_GRANT_READ_URI_PERMISSION)
      )
      true
    }.runOnQueue(Queues.MAIN)
  }

  @Suppress("DEPRECATION")
  private fun signingFlags() = if (Build.VERSION.SDK_INT >= 28) {
    PackageManager.GET_SIGNING_CERTIFICATES
  } else {
    PackageManager.GET_SIGNATURES
  }

  @Suppress("DEPRECATION")
  private fun installedPackage() =
    context.packageManager.getPackageInfo(context.packageName, signingFlags())

  @Suppress("DEPRECATION")
  private fun readVersion(info: PackageInfo) =
    if (Build.VERSION.SDK_INT >= 28) info.longVersionCode else info.versionCode.toLong()

  @Suppress("DEPRECATION")
  private fun signers(info: PackageInfo) =
    (if (Build.VERSION.SDK_INT >= 28) info.signingInfo?.apkContentsSigners else info.signatures)
      ?.toSet().orEmpty()

  @Suppress("DEPRECATION")
  private fun verifyApk(file: File, versionCode: Int) {
    check(context.packageName == "com.fredrikburmester.streamyfin.local")
    val current = installedPackage()
    val candidate = requireNotNull(context.packageManager.getPackageArchiveInfo(file.path, signingFlags()))
    check(candidate.packageName == context.packageName)
    check(readVersion(candidate) == versionCode.toLong() && versionCode > readVersion(current))
    val expectedSigners = signers(current)
    check(expectedSigners.isNotEmpty() && signers(candidate) == expectedSigners)
  }
}
