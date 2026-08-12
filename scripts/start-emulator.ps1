# Start the Obsidian tablet AVD (creates it if missing).
param(
    [string]$AvdName = "Obsidian_Tablet_API34",
    [switch]$ColdBoot,
    [switch]$NoWindow  # headless (-no-window) for CI-style runs
)

$ErrorActionPreference = "Stop"
$sdk = "$env:LOCALAPPDATA\Android\Sdk"
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk
$env:PATH = "$sdk\cmdline-tools\latest\bin;$sdk\platform-tools;$sdk\emulator;$env:JAVA_HOME\bin;$env:PATH"

$emulator = Join-Path $sdk "emulator\emulator.exe"
if (-not (Test-Path $emulator)) {
    throw "Android Emulator not installed. Run: sdkmanager emulator"
}

$avds = & $emulator -list-avds
if ($avds -notcontains $AvdName) {
    Write-Host "AVD '$AvdName' not found. Creating..."
    & "$PSScriptRoot\create-tablet-avd.ps1" -AvdName $AvdName
}

# Prefer WHPX on Hyper-V hosts; fall back to AEHD if present.
$args = @(
    "-avd", $AvdName,
    "-gpu", "host",
    "-no-boot-anim",
    "-netdelay", "none",
    "-netspeed", "full"
)
if ($ColdBoot) { $args += "-no-snapshot-load" }
if ($NoWindow) { $args += "-no-window" }

Write-Host "Starting emulator: $AvdName"
Write-Host "UI: the Emulator window should appear. Or open Android Studio > Device Manager."
Write-Host "Command: emulator $($args -join ' ')"
Start-Process -FilePath $emulator -ArgumentList $args

Write-Host "Waiting for boot..."
$deadline = (Get-Date).AddMinutes(8)
do {
    Start-Sleep -Seconds 5
    $boot = adb -e shell getprop sys.boot_completed 2>$null
    $devs = adb devices
    Write-Host ("  " + ($devs -join " | ") + " boot=$boot")
    if ($boot -match "1") { break }
} while ((Get-Date) -lt $deadline)

if (-not ((adb -e shell getprop sys.boot_completed 2>$null) -match "1")) {
    throw "Emulator did not finish booting in time. Check the Emulator window for errors."
}

Write-Host "Emulator ready:"
adb -e devices -l
adb -e shell getprop ro.product.model
adb -e shell getprop ro.build.version.release
adb -e shell getprop ro.product.cpu.abi
