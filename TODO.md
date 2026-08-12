# Obsidian — remaining work

Scope locked: **K&M only**, **BYO assets**, **external private servers**, **no Blizzard asset distribution**.

Living status: [STATUS.md](STATUS.md). Planning background: [OBSIDIAN_NOTES.md](OBSIDIAN_NOTES.md).

---

## Done (shell)

- [x] Android app module `com.obsidian.client` under `android/`
- [x] `MainActivity` extends SDL2 `SDLActivity`, loads `libwowee.so`
- [x] Vendored `org.libsdl.app` Java glue
- [x] Landscape, keep-screen-on, soft keyboard hidden, cleartext LAN
- [x] `WOW_DATA_PATH` → app-private `files/Data/`
- [x] Java crash reporter → `files/crash_reports/`
- [x] CMake `add_subdirectory(WoWee)` shared-library mode
- [x] `patches/wowee-android-port.patch` (Vulkan 1.1, SDL JNI, no X11)
- [x] Scripts: setup, build, install, tablet AVD, data push
- [x] ABI: arm64-v8a (device) + x86_64 (Windows emulator)

---

## Next (make it boot)

### 0. Toolchain + engine tree

- [ ] Run `.\scripts\setup-wowee.ps1` (clone WoWee + apply patch)
- [x] NDK **30.0.15729638** (retargeted; already installed)
- [x] Install vcpkg; set `VCPKG_ROOT`; triplets `arm64-android` and `x64-android` with openssl, sdl2, glm, zlib, libiconv
- [x] `.\scripts\build-debug.ps1` produces `app-debug.apk`
- [ ] `.\scripts\install-and-debug.ps1` — process stays up; logcat has no FATAL

**Exit:** APK installs; native lib loads; SDL window appears (login or a clear missing-data failure, not a silent crash).

### 1. Desktop known-good path (do on PC in parallel)

- [ ] Build WoWee **desktop** once and confirm it runs
- [x] `.\scripts\extract-classic.ps1` — unpack `retro_wow\...\Data\*.MPQ` into `WoWee\Data\expansions\classic` (79887 files, 0 failed)
- [ ] Local vMaNGOS / MaNGOS; test account + character
- [ ] Desktop client: auth → realm → char select → world

**Exit:** Same extract + server will be reused by the tablet. `retro_wow/` MPQs are **not** a substitute for the extract.

### 2. Assets on device

- [ ] First launch: if no `manifest.json`, show a setup screen (no download button)
- [ ] `.\scripts\push-game-data.ps1 -Full` from `WoWee\Data\expansions\classic`
- [ ] Engine finds `WOW_DATA_PATH` and reaches the login UI

### 3. Input (K&M)

- [ ] Relative mouse (`SDL_SetRelativeMouseMode` / pointer capture) — 360° look, no edge stop
- [ ] USB/BT keys: WASD, 1–0, F-keys, Tab, Shift, Alt, Space, Enter, Escape
- [ ] Chat/login via `SDL_TEXTINPUT`; soft keyboard stays hidden
- [ ] UI scale for tablet DPI

### 4. Network

- [ ] Wi-Fi TCP to LAN vMaNGOS (cleartext already allowed)
- [ ] Lock expansion profile to Vanilla 1.12
- [ ] Auth / realmlist / char list / enter world
- [ ] In-app or file realm address (equivalent of `realmlist.wtf`)

### 5. Lifecycle + tablet stability

- [ ] Pause/resume: destroy/recreate Vulkan surface; no leak on Home
- [ ] Texture/streaming caps for Adreno + unified memory (cities)
- [ ] Cap FPS / don’t spin full-speed while backgrounded
- [ ] Unicorn arm64 only if a target realm requires Warden; else document kicks
- [ ] 15–30 min session without LMK

### 6. Polish (still BYO)

- [ ] First-run legal disclaimer
- [ ] Settings: data path, realm, UI scale, graphics preset
- [ ] Third-party license notices (WoWee MIT, SDL2, …)
- [ ] **Do not** add asset download buttons or pirate links

---

## Out of v1

Touch overlays, armeabi-v7a, 2019 Classic / CASC, on-device server, GLES-as-primary (fallback only if Vulkan fails).

---

## v1 done when

1. APK runs on arm64 Android tablet
2. User-supplied extracted 1.12.1 `Data/` loads from internal storage
3. USB/BT keyboard + mouse: relative look, WASD, hotkeys, chat/login text
4. Connects to an external Vanilla 1.12 private server
5. Enter world, move, basic interact; survives pause/resume
6. No Blizzard assets shipped inside the APK
