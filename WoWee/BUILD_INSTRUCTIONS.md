# WoWee Build Instructions

This document provides platform-specific build instructions for WoWee.

---

## 🐧 Linux (Ubuntu / Debian)

### Install Dependencies

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config git \
  libsdl2-dev libglm-dev \
  libssl-dev zlib1g-dev \
  libvulkan-dev vulkan-tools glslc \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  libx11-dev
```

Optional features:

```bash
sudo apt install -y libunicorn-dev  # Warden execution
sudo apt install -y libstorm-dev    # asset_extract
```

---

## 🐧 Linux (Arch)

### Install Dependencies

```bash
sudo pacman -S --needed \
  base-devel cmake pkgconf git \
  sdl2-compat glm openssl zlib libx11 \
  vulkan-headers vulkan-icd-loader vulkan-tools shaderc \
  ffmpeg
```

> **Note:** `vulkan-headers` provides the `vulkan/vulkan.h` development headers required
> at build time. `vulkan-devel` is a group that includes these on some distros but is not
> available by name on Arch — install `vulkan-headers` and `vulkan-icd-loader` explicitly.

Install `unicorn` for optional Warden execution. StormLib is available from the
AUR as `stormlib-git` if you need `asset_extract`.

---

## 🐧 Linux (All Distros)

### Clone Repository

Always clone with submodules:

```bash
git clone --recurse-submodules https://github.com/Kelsidavis/WoWee.git
cd WoWee
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### Asset Extraction (Linux)

After building, extract assets from your WoW client:

```bash
./extract_assets.sh /path/to/WoW/Data wotlk
```

Supports `classic`, `turtle`, `tbc`, `wotlk` targets (auto-detected if omitted).

---

## 🍎 macOS

### Install Dependencies

Vulkan on macOS uses the Vulkan loader plus MoltenVK's Vulkan-to-Metal driver.

```bash
brew install cmake pkg-config sdl2 glm openssl@3 zlib ffmpeg \
  vulkan-loader vulkan-headers molten-vk shaderc
```

Optional features:

```bash
brew install unicorn  # Warden execution
brew install stormlib # asset_extract
```

Optional (for creating redistributable `.app` bundles):

```bash
brew install dylibbundler
```

### Clone & Build

```bash
git clone --recurse-submodules https://github.com/Kelsidavis/WoWee.git
cd WoWee

BREW=$(brew --prefix)
export PKG_CONFIG_PATH="$BREW/lib/pkgconfig:$(brew --prefix ffmpeg)/lib/pkgconfig:$(brew --prefix openssl@3)/lib/pkgconfig:$(brew --prefix vulkan-loader)/lib/pkgconfig:$(brew --prefix shaderc)/lib/pkgconfig"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$BREW" \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
cmake --build build -j"$(sysctl -n hw.logicalcpu)"
```

### Asset Extraction (macOS)

The script will auto-build `asset_extract` if needed (requires `stormlib`).
It automatically detects Homebrew and passes the correct paths to CMake.

```bash
./extract_assets.sh /path/to/WoW/Data wotlk
```

Supports `classic`, `turtle`, `tbc`, `wotlk` targets (auto-detected if omitted).

### Running a downloaded macOS release

GitHub release DMGs are Developer ID signed, notarized by Apple, and stapled.
Gatekeeper should accept a release downloaded from the official WoWee GitHub
repository without an **Open Anyway** exception.

Maintainers can find the CI credential contract and verification commands in
[`docs/macos-distribution.md`](docs/macos-distribution.md).

---

## 🪟 Windows (MSYS2 — Recommended)

MSYS2 provides the normal client dependencies as pre-built packages. StormLib
is built separately only when the optional asset extractor is needed.

### Install MSYS2

Download and install from <https://www.msys2.org/>, then open a **MINGW64** shell.

### Install Dependencies

```bash
pacman -S --needed \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-SDL2 \
  mingw-w64-x86_64-glm \
  mingw-w64-x86_64-openssl \
  mingw-w64-x86_64-zlib \
  mingw-w64-x86_64-ffmpeg \
  mingw-w64-x86_64-unicorn \
  mingw-w64-x86_64-vulkan-loader \
  mingw-w64-x86_64-vulkan-headers \
  mingw-w64-x86_64-shaderc \
  git
```

The normal client does not require StormLib. To build `asset_extract`, install
the static-link dependencies and build StormLib using the same configuration as
CI:

```bash
pacman -S --needed \
  mingw-w64-x86_64-libtommath \
  mingw-w64-x86_64-libtomcrypt

git clone --depth 1 https://github.com/ladislav-zezula/StormLib.git /tmp/StormLib
cmake -S /tmp/StormLib -B /tmp/StormLib/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$MINGW_PREFIX" \
  -DBUILD_SHARED_LIBS=OFF
cmake --build /tmp/StormLib/build --parallel
cmake --install /tmp/StormLib/build
```

### Clone & Build

```bash
git clone --recurse-submodules https://github.com/Kelsidavis/WoWee.git
cd WoWee
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

---

## 🪟 Windows (Visual Studio 2022)

For users who prefer Visual Studio over MSYS2.

### Install

- Visual Studio 2022 with **Desktop development with C++** workload
- CMake tools for Windows (included in VS workload)
- [LunarG Vulkan SDK](https://vulkan.lunarg.com/) (provides Vulkan headers, loader, and glslc)

### vcpkg Dependencies

```powershell
vcpkg install sdl2 glm openssl zlib ffmpeg stormlib --triplet x64-windows
```

### Clone

```powershell
git clone --recurse-submodules https://github.com/Kelsidavis/WoWee.git
cd WoWee
```

### Build

Open the folder in Visual Studio (it will detect CMake automatically)
or build from Developer PowerShell:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="[vcpkg root]/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

---

## 🪟 Asset Extraction (Windows)

After building (via either MSYS2 or Visual Studio), extract assets from your WoW client:

```powershell
.\extract_assets.ps1 "C:\Games\WoW-3.3.5a\Data"
```

Or double-click `extract_assets.bat` and provide the path when prompted.
You can also specify an expansion: `.\extract_assets.ps1 "C:\Games\WoW\Data" wotlk`

---

## ⚠️ Notes

- Case matters on Linux (`WoWee` not `wowee`).
- Always use `--recurse-submodules` when cloning.
- If you encounter missing headers for ImGui, run:
  ```bash
  git submodule update --init --recursive
  ```
- AMD FSR2 SDK is fetched automatically by `build.sh` / `rebuild.sh` / `build.ps1` / `rebuild.ps1` from:
  - `https://github.com/GPUOpen-Effects/FidelityFX-FSR2.git`
  - target path: `extern/FidelityFX-FSR2`
- AMD backend is enabled when SDK headers and Vulkan permutation headers are available.
- If upstream SDK checkout is missing generated Vulkan permutation headers, CMake bootstraps them from:
  - `third_party/fsr2_vk_permutations`
- If SDK headers are missing, the build uses the internal FSR2 fallback path.
