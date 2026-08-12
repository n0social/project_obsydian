# Push extracted WoWee classic Data/ into the Obsidian app private storage.
#
# Full extract is ~6GB - use -SmokeTest for a tiny manifest-only push to validate paths,
# or -Full for the real client pack.
param(
    [string]$Source = (Join-Path (Split-Path $PSScriptRoot -Parent) "WoWee\Data\expansions\classic"),
    [switch]$SmokeTest,
    [switch]$Full
)

$ErrorActionPreference = "Stop"
$sdk = "$env:LOCALAPPDATA\Android\Sdk"
$env:PATH = "$sdk\platform-tools;$env:PATH"
$pkg = "com.obsidian.client"
$remoteRoot = "/data/data/$pkg/files/Data"

if (-not (adb devices | Select-String "device$")) {
    throw "No adb device connected"
}

# Ensure app is installed so run-as works.
$installed = adb shell pm path $pkg 2>$null
if (-not $installed) {
    throw "Package $pkg not installed - run scripts\install-and-debug.ps1 first"
}

if ($SmokeTest -or (-not $Full)) {
    Write-Host "== smoke-test data push (manifest stubs only) =="
    $tmp = Join-Path $env:TEMP "obsidian-smoke-data"
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    $classic = Join-Path $tmp "expansions\classic"
    New-Item -ItemType Directory -Force -Path $classic | Out-Null
    '{"format":"obsidian-smoke","note":"Replace with real WoWee extract via -Full"}' |
        Set-Content -Encoding utf8 (Join-Path $classic "manifest.json")
    '{"format":"obsidian-smoke"}' | Set-Content -Encoding utf8 (Join-Path $tmp "manifest.json")

    adb shell run-as $pkg mkdir -p files/Data/expansions/classic
    Push-Location $tmp
    try {
        tar -cf - . | adb exec-out run-as $pkg tar -xf - -C files/Data
    } finally {
        Pop-Location
    }
    Write-Host "Smoke data at device: $remoteRoot"
    Write-Host "Re-run with -Full to push real extract from:`n  $Source"
    if (-not $Full) { return }
}

if (-not (Test-Path (Join-Path $Source "manifest.json"))) {
    throw "Source missing manifest.json: $Source"
}

Write-Host "== full data push from $Source =="
Write-Host "This can take a long time (~6GB)."
adb shell run-as $pkg mkdir -p files/Data/expansions/classic

# Prefer adb push to public cache then run-as cp when tar is unavailable on device.
$staging = "/data/local/tmp/obsidian_data_stage"
adb shell rm -rf $staging
adb shell mkdir -p $staging
adb push $Source "$staging/classic"
adb shell run-as $pkg mkdir -p files/Data/expansions
# run-as cannot always read /data/local/tmp on all devices - fall back to tar stream from PC.
if ($LASTEXITCODE -ne 0) {
    Write-Host "staging copy may be restricted; streaming via tar..."
}
Push-Location (Split-Path $Source -Parent)
try {
    # Parent is expansions/; stream classic/
    tar -cf - classic | adb exec-out run-as $pkg tar -xf - -C files/Data/expansions
} finally {
    Pop-Location
}

# Also place a top-level pointer manifest if useful
adb shell run-as $pkg sh -c "cp files/Data/expansions/classic/manifest.json files/Data/manifest.json" 2>$null
Write-Host "Done. Verify:"
adb shell run-as $pkg ls -la files/Data/expansions/classic/manifest.json
