#!/bin/bash
# Wowee Clean Rebuild Script - Removes all build artifacts and rebuilds from scratch

set -e  # Exit on error

cd "$(dirname "$0")"

ensure_fsr2_sdk() {
    local sdk_dir="extern/FidelityFX-FSR2"
    local sdk_header="$sdk_dir/src/ffx-fsr2-api/ffx_fsr2.h"
    if [ -f "$sdk_header" ]; then
        return
    fi
    if ! command -v git >/dev/null 2>&1; then
        echo "Warning: git not found; cannot auto-fetch AMD FSR2 SDK."
        return
    fi
    echo "Fetching AMD FidelityFX FSR2 SDK into $sdk_dir ..."
    mkdir -p extern
    git clone --depth 1 https://github.com/GPUOpen-Effects/FidelityFX-FSR2.git "$sdk_dir" || {
        echo "Warning: failed to clone AMD FSR2 SDK. Build will use internal fallback path."
    }
}

ensure_fidelityfx_sdk() {
    local sdk_dir="extern/FidelityFX-SDK"
    local sdk_header="$sdk_dir/sdk/include/FidelityFX/host/ffx_frameinterpolation.h"
    local sdk_repo="${WOWEE_FFX_SDK_REPO:-https://github.com/Kelsidavis/FidelityFX-SDK.git}"
    local sdk_ref="${WOWEE_FFX_SDK_REF:-main}"
    if [ -f "$sdk_header" ]; then
        return
    fi
    if ! command -v git >/dev/null 2>&1; then
        echo "Warning: git not found; cannot auto-fetch AMD FidelityFX SDK."
        return
    fi
    echo "Fetching AMD FidelityFX SDK ($sdk_ref from $sdk_repo) into $sdk_dir ..."
    mkdir -p extern
    git clone --depth 1 --branch "$sdk_ref" "$sdk_repo" "$sdk_dir" || {
        echo "Warning: failed to clone AMD FidelityFX SDK. FSR3 framegen extern will be unavailable."
    }
}

echo "Clean rebuilding wowee..."
ensure_fsr2_sdk
ensure_fidelityfx_sdk

# Remove build directory completely
if [ -d "build" ]; then
    echo "Removing old build directory..."
    rm -rf build
fi

# Create fresh build directory
mkdir -p build
cd build

# Configure with cmake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build with all cores
NPROC=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
echo "Building with $NPROC cores..."
cmake --build . --parallel "$NPROC"

# Create Data symlink in bin directory
echo "Creating Data symlink..."
cd bin
if [ ! -e Data ]; then
    ln -s ../../Data Data
    echo "  Created Data -> ../../Data"
fi
cd ..

echo ""
echo "Clean build complete! Binary: build/bin/wowee"
echo "Run with: cd build/bin && ./wowee"
