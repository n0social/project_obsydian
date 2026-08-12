# Install Obsidian debug APK, launch, and stream useful logcat lines.
param(
    [string]$Apk = "$PSScriptRoot\..\android\app\build\outputs\apk\debug\app-debug.apk",
    [int]$Seconds = 20
)

$ErrorActionPreference = "Stop"
$sdk = "$env:LOCALAPPDATA\Android\Sdk"
$env:PATH = "$sdk\platform-tools;$env:PATH"

if (-not (Test-Path $Apk)) {
    throw "APK not found: $Apk - build with android\gradlew.bat :app:assembleDebug first"
}

Write-Host "== devices =="
adb devices -l
Write-Host "== install =="
adb install -r -g -t $Apk
adb logcat -c
Write-Host "== launch =="
adb shell am start -n com.obsidian.client/.LauncherActivity
Start-Sleep -Seconds $Seconds
Write-Host "== pid =="
adb shell pidof com.obsidian.client
Write-Host "== filtered logcat =="
adb logcat -d -v threadtime |
    Select-String -Pattern "Obsidian|ObsidianCrash|SDL|Wowee|wowee|AndroidRuntime|FATAL|DEBUG|Unsatisfied|Vulkan|WOW_DATA|native_crash|libwowee" |
    Select-Object -Last 120
Write-Host "== pull crash reports (if any) =="
$out = Join-Path $PSScriptRoot "..\tools-cache\device-crash-reports"
New-Item -ItemType Directory -Force -Path $out | Out-Null
adb shell run-as com.obsidian.client ls files/crash_reports 2>$null
adb exec-out run-as com.obsidian.client sh -c "cd files/crash_reports && tar cf - ." 2>$null |
    tar -xf - -C $out 2>$null
Get-ChildItem $out -ErrorAction SilentlyContinue | Format-Table Name, Length, LastWriteTime
