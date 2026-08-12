# Build Obsidian debug APK (WoWee + SDLActivity).
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$wowee = Join-Path $root "WoWee"
$ndk = Join-Path $env:LOCALAPPDATA "Android\Sdk\ndk\30.0.15729638"
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = "C:\Users\donav\vcpkg" }

if (-not (Test-Path (Join-Path $wowee "CMakeLists.txt"))) {
    throw "WoWee not found at $wowee - run .\scripts\setup-wowee.ps1 first"
}
$cmake = Get-Content (Join-Path $wowee "CMakeLists.txt") -Raw
if ($cmake -notmatch "OBSIDIAN_ANDROID") {
    throw "WoWee is present but the Android patch is not applied - run .\scripts\setup-wowee.ps1"
}
if (-not (Test-Path $ndk)) {
    throw "NDK not found at $ndk. Install NDK 30.0.15729638 via Android SDK Manager."
}
if (-not (Test-Path $env:VCPKG_ROOT)) {
    throw "vcpkg not found at $($env:VCPKG_ROOT) - set VCPKG_ROOT and install arm64-android / x64-android triplets"
}

$env:JAVA_HOME = "C:\Program Files\Microsoft\jdk-17.0.20.8-hotspot"
$env:ANDROID_NDK_HOME = $ndk
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA "Android\Sdk"
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$javaBin = Join-Path $env:JAVA_HOME "bin"
$adbBin = Join-Path $env:ANDROID_HOME "platform-tools"
$env:PATH = "$javaBin;$adbBin;$env:PATH"

Set-Location (Join-Path $root "android")
& .\gradlew.bat :app:assembleDebug
if ($LASTEXITCODE -ne 0) { throw "Gradle assembleDebug failed with exit $LASTEXITCODE" }
$apk = Resolve-Path "app\build\outputs\apk\debug\app-debug.apk"
Write-Host "APK: $apk"
Get-Item $apk | Format-Table Name, @{N='MB';E={[math]::Round($_.Length/1MB,1)}}
