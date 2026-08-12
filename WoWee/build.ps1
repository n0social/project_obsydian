<#
.SYNOPSIS
    Builds the wowee project (Windows equivalent of build.sh).

.DESCRIPTION
    Creates a build directory, runs CMake configure + build, and creates a
    directory junction for the Data folder so the binary can find assets.
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

function Ensure-Fsr2Sdk {
    $sdkDir = Join-Path $ScriptDir "extern\FidelityFX-FSR2"
    $sdkHeader = Join-Path $sdkDir "src\ffx-fsr2-api\ffx_fsr2.h"
    if (Test-Path $sdkHeader) { return }

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Warning "git not found; cannot auto-fetch AMD FSR2 SDK."
        return
    }

    Write-Host "Fetching AMD FidelityFX FSR2 SDK into $sdkDir ..."
    New-Item -ItemType Directory -Path (Join-Path $ScriptDir "extern") -Force | Out-Null
    & git clone --depth 1 https://github.com/GPUOpen-Effects/FidelityFX-FSR2.git $sdkDir
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to clone AMD FSR2 SDK. Build will use internal fallback path."
    }
}

function Ensure-FidelityFxSdk {
    $sdkDir = Join-Path $ScriptDir "extern\FidelityFX-SDK"
    $sdkHeader = Join-Path $sdkDir "sdk\include\FidelityFX\host\ffx_frameinterpolation.h"
    $sdkRepo = if ($env:WOWEE_FFX_SDK_REPO) { $env:WOWEE_FFX_SDK_REPO } else { "https://github.com/Kelsidavis/FidelityFX-SDK.git" }
    $sdkRef = if ($env:WOWEE_FFX_SDK_REF) { $env:WOWEE_FFX_SDK_REF } else { "main" }
    if (Test-Path $sdkHeader) { return }

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Warning "git not found; cannot auto-fetch AMD FidelityFX SDK."
        return
    }

    Write-Host "Fetching AMD FidelityFX SDK ($sdkRef from $sdkRepo) into $sdkDir ..."
    New-Item -ItemType Directory -Path (Join-Path $ScriptDir "extern") -Force | Out-Null
    & git clone --depth 1 --branch $sdkRef $sdkRepo $sdkDir
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to clone AMD FidelityFX SDK. FSR3 framegen extern will be unavailable."
    }
}

Write-Host "Building wowee..."
Ensure-Fsr2Sdk
Ensure-FidelityFxSdk

# Create build directory if it doesn't exist
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}
Set-Location "build"

# Configure with CMake
Write-Host "Configuring with CMake..."
& cmake .. -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Build with all cores
$numProcs = $env:NUMBER_OF_PROCESSORS
if (-not $numProcs) { $numProcs = 4 }
Write-Host "Building with $numProcs cores..."
& cmake --build . --parallel $numProcs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Ensure Data junction exists in bin directory
$binData = Join-Path (Get-Location) "bin\Data"
if (-not (Test-Path $binData)) {
    $target = (Resolve-Path (Join-Path (Get-Location) "..\Data")).Path
    cmd /c mklink /J "$binData" "$target"
}

Write-Host ""
Write-Host "Build complete! Binary: build\bin\wowee.exe"
Write-Host "Run with: cd build\bin && .\wowee.exe"
