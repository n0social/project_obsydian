# Clone WoWee into this tree and apply patches/wowee-android-port.patch.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$wowee = Join-Path $root "WoWee"
$patch = Join-Path $root "patches\wowee-android-port.patch"

if (-not (Test-Path $patch)) {
    throw "Android port patch missing: $patch"
}

if (-not (Test-Path (Join-Path $wowee "CMakeLists.txt"))) {
    $zip = Join-Path $root "WoWee-master.zip"
    if (Test-Path $zip) {
        Write-Host "== unpacking $zip into $wowee =="
        $tmp = Join-Path $root "tools-cache\wowee-src-unpack"
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $tmp | Out-Null
        tar -xf $zip -C $tmp
        $unpacked = Join-Path $tmp "WoWee-master"
        if (-not (Test-Path (Join-Path $unpacked "CMakeLists.txt"))) {
            throw "Zip did not contain WoWee-master/CMakeLists.txt"
        }
        Move-Item $unpacked $wowee
    } else {
        Write-Host "== cloning Kelsidavis/WoWee into $wowee =="
        git clone --recurse-submodules https://github.com/Kelsidavis/WoWee.git $wowee
    }
} else {
    Write-Host "== WoWee already present at $wowee =="
}

$cmake = Get-Content (Join-Path $wowee "CMakeLists.txt") -Raw
if ($cmake -match "OBSIDIAN_ANDROID") {
    Write-Host "Android port patch already applied (OBSIDIAN_ANDROID present)."
    exit 0
}

Write-Host "== applying patches\wowee-android-port.patch =="
Push-Location $wowee
try {
    git apply --whitespace=nowarn $patch
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Patch did not apply cleanly to this WoWee revision. The Android shared-library port needs a refreshed patch before libwowee.so will build. Extraction (extract-classic.ps1) does not need the patch."
        exit 1
    }
} finally {
    Pop-Location
}
Write-Host "Done. Next: .\scripts\extract-classic.ps1  then  .\scripts\build-debug.ps1"
