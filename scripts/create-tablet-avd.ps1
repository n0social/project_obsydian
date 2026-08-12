# Create Pixel Tablet-style AVD for Obsidian.
# On Windows x86_64 hosts use x86_64 images (arm64 AVDs are rejected by QEMU2).
param(
    [string]$AvdName = "Obsidian_Tablet_API34",
    [string]$Package = "system-images;android-34;google_apis;x86_64",
    [string]$Device = "pixel_tablet"
)

$ErrorActionPreference = "Stop"
$sdk = "$env:LOCALAPPDATA\Android\Sdk"
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk
$env:PATH = "$sdk\cmdline-tools\latest\bin;$sdk\platform-tools;$sdk\emulator;$env:JAVA_HOME\bin;$env:PATH"

$imgPath = Join-Path $sdk ($Package -replace ';','\')
if (-not (Test-Path $imgPath)) {
    Write-Host "Installing $Package ..."
    $yes = ("y`n" * 50)
    $yes | sdkmanager --licenses | Out-Null
    sdkmanager $Package "platforms;android-34" "emulator"
}

Write-Host "Creating AVD $AvdName (device=$Device, package=$Package)"
# echo no = don't start UI; force overwrite
"no" | avdmanager create avd -n $AvdName -k $Package -d $Device --force

$config = Join-Path $env:USERPROFILE ".android\avd\${AvdName}.avd\config.ini"
if (Test-Path $config) {
    $extra = @"

# Obsidian tablet tuning
hw.ramSize=4096
vm.heapSize=512
disk.dataPartition.size=8G
hw.keyboard=yes
hw.mainKeys=no
hw.gpu.enabled=yes
hw.gpu.mode=host
hw.lcd.density=320
"@
    Add-Content -Path $config -Value $extra
    Write-Host "Updated $config"
}

Write-Host "AVD ready: $AvdName"
& "$sdk\emulator\emulator.exe" -list-avds
