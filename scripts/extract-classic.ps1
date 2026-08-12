# Extract RetroWoW 1.12.1 MPQs into WoWee's classic Data tree.
#
# Source:  retro_wow\RetroWoW 1.12.1\Data\*.MPQ
# Output:  WoWee\Data\expansions\classic\  (+ manifest.json)
#
# Prefers a prebuilt asset_extract.exe (release zip or tools-cache).
# Falls back to WoWee\extract_assets.ps1 which builds the tool (needs CMake + StormLib).
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mpqDir = Join-Path $root "retro_wow\RetroWoW 1.12.1\Data"
$wowee = Join-Path $root "WoWee"
$outDir = Join-Path $wowee "Data"
$woweeExtract = Join-Path $wowee "asset_extract.exe"
$cacheExtract = Get-ChildItem -Path (Join-Path $root "tools-cache") -Recurse -Filter "asset_extract.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName

if (-not (Test-Path $mpqDir)) {
    throw "MPQ directory not found: $mpqDir"
}
$mpqs = @()
$mpqs += @(Get-ChildItem -Path $mpqDir -Filter "*.MPQ" -ErrorAction SilentlyContinue)
$mpqs += @(Get-ChildItem -Path $mpqDir -Filter "*.mpq" -ErrorAction SilentlyContinue)
if ($mpqs.Count -eq 0) {
    throw "No .MPQ files in $mpqDir"
}

if (-not (Test-Path (Join-Path $wowee "CMakeLists.txt"))) {
    throw "WoWee source not found at $wowee. Finish the clone or run scripts\setup-wowee.ps1 first."
}

$extractor = $null
foreach ($candidate in @($woweeExtract, $cacheExtract)) {
    if ($candidate -and (Test-Path $candidate)) {
        $extractor = $candidate
        break
    }
}

$mpqGb = [math]::Round((($mpqs | Measure-Object Length -Sum).Sum / 1GB), 2)
Write-Host ("MPQ source : {0} ({1} archives, {2} GB)" -f $mpqDir, $mpqs.Count, $mpqGb)
Write-Host ("Output     : {0}\expansions\classic" -f $outDir)
Write-Host "Expansion  : classic"

if ($extractor) {
    Write-Host ("Extractor  : {0}" -f $extractor)
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    # Run from the extractor directory so bundled StormLib/zlib DLLs resolve.
    Push-Location (Split-Path $extractor)
    try {
        & $extractor --mpq-dir $mpqDir --output $outDir --expansion-subdir --expansion classic
        if ($LASTEXITCODE -ne 0) { throw "asset_extract failed with exit $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "Extractor  : building via WoWee\extract_assets.ps1 (needs CMake + StormLib)"
    $vsCmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    if (Test-Path (Join-Path $vsCmake "cmake.exe")) {
        $env:PATH = "$vsCmake;$env:PATH"
    }
    & (Join-Path $wowee "extract_assets.ps1") $mpqDir classic
    if ($LASTEXITCODE -ne 0) { throw "extract_assets.ps1 failed with exit $LASTEXITCODE" }
}

$manifest = Join-Path $outDir "expansions\classic\manifest.json"
if (-not (Test-Path $manifest)) {
    throw "Extraction finished but manifest missing: $manifest"
}
Write-Host "OK: $manifest"
Write-Host "Next: .\scripts\push-game-data.ps1 -Full   (after the APK is installed)"
