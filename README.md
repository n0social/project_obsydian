# Project Obsydian

**Started:** July 30, 2026

Android client for **Vanilla World of Warcraft 1.12**, built on the open-source [WoWee](https://github.com/Kelsidavis/WoWee) engine (World of Warcraft Engine Experiment).

Early-stage port: SDLActivity host, CMake link of native `libwowee.so`, crash-report hooks, and device/emulator scripts. **Not a playable tablet client yet.** Living status: [STATUS.md](STATUS.md).

## Based on WoWee

[WoWee](https://github.com/Kelsidavis/WoWee) is a clean-room C++ WoW client (Vulkan/SDL2) that talks to classic private-server backends. Obsydian does **not** replace WoWee; it ports and wraps that engine for Android.

| Item | Choice |
|------|--------|
| Engine | [Kelsidavis/WoWee](https://github.com/Kelsidavis/WoWee) |
| Expansion target | Vanilla **1.12.1** only |
| Input | USB / Bluetooth keyboard + mouse |
| Assets | BYO extracted `Data/` (no Blizzard assets shipped here) |
| Servers | External private 1.12 realms (client-only) |

## Repo layout

```
android/                         Obsydian Android app (SDLActivity + NDK → libwowee.so)
scripts/                         setup / build / install / emulator / data-push
patches/wowee-android-port.patch
STATUS.md                        have vs should-have (source of truth)
TODO.md                          remaining work
OBSIDIAN_NOTES.md                planning background
```

WoWee is **not vendored**. Clone it into this tree and apply the Android patch:

```powershell
.\scripts\setup-wowee.ps1
```

Equivalent manual steps:

```powershell
git clone --recurse-submodules https://github.com/Kelsidavis/WoWee.git WoWee
git -C WoWee apply ..\patches\wowee-android-port.patch
```

## Build

Prerequisites: Android SDK + NDK **30.0.15729638**, vcpkg with `arm64-android` (and `x64-android` for the Windows emulator) packages: openssl, sdl2, glm, zlib, libiconv. Set `VCPKG_ROOT`.

```powershell
.\scripts\setup-wowee.ps1
.\scripts\extract-classic.ps1          # RetroWoW 1.12.1 MPQs → WoWee\Data\expansions\classic
.\scripts\build-debug.ps1
.\scripts\install-and-debug.ps1
.\scripts\push-game-data.ps1 -SmokeTest
```

APK: `android\app\build\outputs\apk\debug\app-debug.apk`

ABIs: **arm64-v8a** (device) and **x86_64** (Windows emulator).

## Status

- [x] Project planning / roadmap
- [x] Android app shell + SDL2 `SDLActivity` host
- [x] CMake wiring to build WoWee as `libwowee.so` (needs local `WoWee/` + patch)
- [x] Crash report hooks (Java)
- [ ] WoWee cloned and Android patch applied in this workspace
- [ ] Debug APK that boots on device/emulator
- [ ] Relative-mouse look verified with USB/BT peripherals
- [ ] Reliable smoke boot with extracted Data + realm

See [STATUS.md](STATUS.md) for the full have vs should-have table.

## Legal

This project ships **engine and app code only**. You must supply your own legally obtained 1.12 client data (extracted). No MPQs, no download links, no Blizzard assets.

`retro_wow/` (if present) is a local original-client tree for your own use. It is gitignored. WoWee still needs a **loose-file extract** (`Data/expansions/classic/manifest.json`), not raw MPQs.

## License

Android shell and project docs: see repo files as they land.
WoWee remains under its upstream license — see [WoWee](https://github.com/Kelsidavis/WoWee) (MIT).
