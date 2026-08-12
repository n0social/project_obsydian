# Obsidian — Project Notes

> **Status:** this file is planning background from the original design conversations.
> What the tree actually contains vs v1 is tracked in [STATUS.md](STATUS.md).
> Remaining work: [TODO.md](TODO.md).

Notes from planning conversations (based on [WoWee](https://github.com/Kelsidavis/WoWee) / World of Warcraft Engine Experiment). Project codename: **Obsidian**.

---

## What is WoWee?

**WoWee** (World of Warcraft Engine Experiment) is an open-source, custom-built game client written from scratch in native C++. It is a **clean-room re-implementation** of the World of Warcraft client — not a line-by-line translation of WoW.exe — designed to run on modern systems and talk to classic private server backends.

### Key technical details

| Area | Details |
|------|---------|
| **Graphics** | Custom 3D pipeline on **Vulkan** and **OpenGL** (no old DirectX). Terrain, water, skyboxes, lighting, particles, M2 models, WMO world objects. |
| **Supported versions** | Classic expansions: Vanilla (**1.12**), TBC (**2.4.3**), WotLK (**3.3.5a**). |
| **Assets** | Repo contains **no** copyrighted Blizzard assets or code. Users run `extract_assets.sh` on a legally obtained WoW install to produce an extracted directory the client can read. |
| **Servers** | Connects to open-source emulators (AzerothCore, TrinityCore, MaNGOS) via **SRP6a** auth and standard packet opcodes. |
| **Extras** | Experimental Unicorn Engine x86 CPU emulation for legacy server/Warden-style checks. |

### Current WoWee status

Can authenticate, load the 3D world, render complex environments, basic combat/spell casting, inventory, movement, and chat. Still educational/experimental — much UI and edge-case logic under heavy development. Not a flawless drop-in replacement for the original executable.

### Why it matters

Historically private servers had custom backends but clients were stuck on Blizzard’s old 32-bit WoW.exe. An open-source native C++ client enables:

- **Linux** natively (no Wine/Proton required)
- **De-hardcoding** old 32-bit memory limits; modern graphics, camera, frame options
- **Moddability** — extend the client directly (UI, shaders, gameplay plumbing)

---

## Obsidian scope

| Decision | Choice |
|----------|--------|
| Project name | **Obsidian** |
| Base engine | WoWee C++ core |
| Target platform | **Android** (Galaxy Tab A9+ class devices in mind) |
| Expansion | **Classic Vanilla 1.12** only (not 2019 WoW Classic) |
| Input | **Keyboard + mouse** only (USB OTG / Bluetooth) — **no** touch virtual controls |
| Server model | **Client only** — connect to external private servers (not a bundled on-device server; not Blizzard official) |

Analogy: Obsidian is to Vanilla WoW what **AetherSX2** is to PS2 — an open-source client/engine; the user supplies the game data.

---

## Target game data: 2004-era 1.12.1 (not 2019 Classic)

WoWee / Obsidian use **original 2004–2006 era data**, specifically **Patch 1.12.1**. They do **not** use 2019 “WoW Classic” re-release data.

2019 Classic is a modern (Legion/BfA-era) engine with Classic rules layered on top. Using it would make an Android port far harder:

| Factor | 2004 (1.12) | 2019 WoW Classic | Android impact |
|--------|-------------|------------------|----------------|
| Storage | ~5 GB | ~60+ GB | 60GB impractical on most tablets |
| File system | `.MPQ` | CASC | CASC parser from scratch; MPQ simpler |
| Networking | SRP6a + legacy opcodes | Modern retail auth | Private Vanilla emulators speak 2004 protocols |

Sticking to 1.12 keeps footprint small, models GPU-friendly, and MaNGOS / vMaNGOS-style servers viable.

---

## Architecture (Android + K&M)

```
┌────────────────────────────────────────────────────────┐
│             Android Native Shell (Activity)            │
│   • Captures USB/BT Keyboard & Mouse Inputs            │
│   • Suppresses On-Screen Soft Keyboard                 │
│   • Enables Relative Pointer Locking                   │
└───────────────────────────┬────────────────────────────┘
                            │ Raw Input Events / SDL2
┌───────────────────────────▼────────────────────────────┐
│                    Obsidian Core C++                   │
│   • Desktop UI Engine (Native WoW Interface / ImGui)   │
│   • Vulkan / GLES3 Render Loop                         │
│   • Direct Camera Orbit & Keybind Handler              │
└────────────────────────────────────────────────────────┘
```

### Layers

1. **Android APK** (Kotlin/Java) — lifecycle, storage paths, soft-keyboard suppression, pointer capture
2. **JNI / SDL2** — window surface, Vulkan/GLES context, audio, raw K&M events
3. **Obsidian Core C++** — renderer, SRP6 network client, world/audio engine, desktop-style UI

---

## Input (keyboard + mouse on Android)

WoW expects desktop mouse look (right-click hold → orbit camera) and full hotkeys. Android must not treat the mouse as a finger tap.

### Requirements

1. **Relative mouse / pointer capture**
   - Use SDL2 `SDL_SetRelativeMouseMode(SDL_TRUE)` and/or Android API 26+ `requestPointerCapture()`
   - Without this, the invisible cursor hits screen edges and camera orbit stops

2. **Hardware key mapping**
   - Forward USB/BT scan codes (WASD, 1–0, F-keys, Tab, Shift, Alt, Space) to Obsidian’s existing input handler
   - Map Android keycodes (`AKEYCODE_W`, etc.) → Win/Linux scancodes via SDL2

3. **Soft keyboard suppression**
   - Chat/login text via `SDL_TEXTINPUT` / IME callbacks
   - Prevent Android soft keyboard from covering the screen when Enter opens chat

4. **UI scaling**
   - Desktop UI expects 1080p+ at monitor distance
   - Need UI scale for tablet aspect ratios (16:9, 16:10, 4:3) and DPI so bags/spellbook/nameplates stay readable

### Input flow

```
Android OS Event (AMotionEvent / AKeyEvent via USB OTG or Bluetooth)
        → SDL2 Input Hook (relative mouse + keycode translation)
        → Obsidian Input Engine (camera orbit, WASD movement opcodes, hotkeys)
```

---

## Classic 1.12 networking profile

- **Auth:** SRP6a
- **Headers:** 2-byte header size; RC4 client/server header encryption after `SMSG_AUTH_RESPONSE`
- **Opcodes:** 1.12 set (e.g. `CMSG_PLAYER_LOGIN`, `CMSG_MOVE_START_FORWARD`)
- **Mobile networks:** Socket handler should tolerate brief Wi-Fi ↔ LTE delays without tearing down the session

**Local testing:** Run vMaNGOS / MaNGOS (or similar Vanilla 1.12 emulator) on a PC; point tablet `realmlist` at the PC’s LAN IP (e.g. `192.168.1.50`).

**Public use:** Users point realmlist at any compatible Vanilla 1.12 private server (custom patches may still break compatibility).

---

## Assets pipeline

### What the engine needs

WoWee does **not** read `.MPQ` archives directly. It needs a **loose-file tree** plus `manifest.json`.

1. User obtains a **legal** Vanilla **1.12.1** client install on PC  
2. Run WoWee’s `extract_assets.sh` against that install’s Data folder (e.g. classic target)  
3. Script unpacks models (M2/WMO), textures (BLP), maps, etc., and writes `manifest.json`  
4. User copies the resulting **`Data/`** folder to the Android device  
5. App sets data path (e.g. `WOW_DATA_PATH`) and boots if `manifest.json` is found  

Extracted Classic assets are roughly **~3.5–5 GB**.

### Where to put files on Android (preferred)

**Prefer app internal storage**, not SD card:

| Location | Why |
|----------|-----|
| **Internal** `Android/data/<package>/files/Data/` | Sandboxed paths → normal C++ `ifstream`; no SAF; faster random reads (UFS/eMMC) |
| **External SD** | Scoped Storage / SAF hell; every texture load may need Java; slower → zone-load stutter |

Do **not** bake multi-GB assets into the APK/AAB.

### First-launch UX

- Scan configured path for `manifest.json`
- If missing → setup screen: “No game data found. Copy extracted 1.12.1 Data folder to …”
- If found → init Vulkan/GLES, load login UI/assets, show login

Optional community tools (e.g. **wow.export**) can extract assets visually, but WoWee’s script is tailored to produce the exact `manifest.json` the engine expects — stick to it unless a compatible index is generated.

---

## Storage & RAM reality check

Original 2004 WoW ran on ~512 MB RAM; that does **not** map cleanly to a modern reimplementation on Android:

1. **OS overhead** — Android may use 2–3 GB before the game starts  
2. **Unified memory** — CPU and GPU share RAM; textures/WMO/M2 load into system memory  
3. **Engine style** — Clean-room Vulkan/OpenGL + loose files vs Blizzard’s heavily tuned D3D9 + MPQ streaming  

So: internal storage for assets, and expect to tune texture streaming / terrain distances for the Tab A9+ (Adreno 619, Vulkan 1.1).

---

## Major work beyond keyboard/mouse

| Challenge | Why it matters | Approach |
|-----------|----------------|----------|
| **Android lifecycle & Vulkan** | Pause/background/rotation destroys surfaces | Hook native app glue (`APP_CMD_INIT_WINDOW` / `TERM_WINDOW`); destroy/recreate Vulkan surface |
| **Scoped storage / ~5 GB assets** | Desktop-style paths break on Android 13/14 | Prefer app-private internal dir; avoid SAF if possible |
| **Unicorn / Warden x86 emu** | Some servers need x86 crypto responses | Cross-compile `libunicorn` for **arm64-v8a** via NDK |
| **Mobile GPU memory** | OOM in dense zones (Ironforge/Orgrimmar) | ASTC / streaming limits; careful Vulkan allocators |

### Difficulty: **Hard / Advanced**

Hard part is **platform bridging and cross-compile**, not game logic. Compiling via CMake+NDK is ~30%; ~70% is Vulkan quirks, lifecycle, and not getting killed while streaming large asset trees.

---

## Build roadmap (high level)

1. **Prune desktop deps** — Replace GLFW/X11/desktop audio with **SDL2** Android bindings in CMake  
2. **Android Studio + NDK** — Native project; Gradle → CMake; target **arm64-v8a** (+ x86_64 emulator) → `libwowee.so`  
3. **Graphics** — Vulkan on device; **OpenGL ES 3.2** fallback if Vulkan drivers fail  
4. **Assets** — Point at internal `Data/` + `manifest.json`  
5. **Input** — Relative mouse + hardware keys; suppress soft keyboard  
6. **Verify** — Login to local 1.12 server over Wi-Fi; mouse-look, WASD, hotkeys, world load  

Next concrete focus discussed: **configure SDL2 mouse/keyboard hooks**.

---

## Client vs server options

| Option | Description | Verdict |
|--------|-------------|---------|
| **Bundled server on device** | MaNGOS/AzerothCore as Android service; client → `127.0.0.1` | Offline/GM power; **much harder** (DB + CPU + battery). Not recommended for Tab A9+. |
| **External private servers** | Client + realmlist IP | **Recommended** — develop against PC-local emulator; users point at public 1.12 realms |
| **Official Blizzard** | Retail / Classic live | **Impossible / illegal** — proprietary auth, Warden, data mismatch |

---

## Legal summary (not legal advice)

### Player connecting to private servers

- Primarily **EULA / ToS breach** (civil), not typically criminal for casual play  
- Risk: Battle.net bans; Blizzard rarely sues individual players for realmlist changes  
- Acquiring client files via torrents / unauthorized downloads can be copyright infringement  

### Server hosts

- High risk: distributing Blizzard client files, monetizing IP → DMCA / lawsuits (historical examples: Scapegaming, Project Ascension, etc.)

### Obsidian / WoWee-style client

**Safer path (emulator model):**

- Ship **only** clean-room C++ engine + UI — **no** Blizzard code or assets in the APK  
- Users supply their **own** legally obtained 1.12 data  
- Provide extraction tooling (`extract_assets.sh`) only  
- Clear disclaimer: no Blizzard assets hosted or distributed  

**Danger zone:**

- Bundling `.MPQ` / extracted Data in the APK  
- Hosting asset packs for download  
- In-app “Download game assets” buttons / deep links to Mega/Drive/torrents → **contributory copyright infringement** risk  
- Pointing users to specific pirate hosts  

**Clean-room defense:** Ideas/protocols aren’t copyrighted; expression (Blizzard’s source) is. Interoperability reverse engineering has DMCA carve-outs. Same general space as console emulators (PCSX2, DuckStation): engine OK; distributing ROMs/assets is not.

Suggested disclaimer style:

> Obsidian is a custom-built, open-source game engine. It does not contain, distribute, or host any copyrighted Blizzard Entertainment code or assets. Users must provide their own legally obtained game data to use this software.

---

## User setup (BYO assets)

1. Install Vanilla **1.12.1** on a PC  
2. Get `extract_assets.sh` from the WoWee repo; run against that install  
3. Wait for extracted `Data/` + `manifest.json`  
4. Copy `Data/` to tablet internal app storage (USB / network)  
5. Open Obsidian → it finds the manifest → login screen  

WoWee’s GitHub does **not** and will **not** host Blizzard files (explicitly prohibited: sharing client files, MPQs, CASC, proprietary assets).

---

## Recommended local test stack

- **Client:** Obsidian Android build (arm64-v8a, SDL2, Vulkan/GLES)  
- **Assets:** Extracted 1.12.1 `Data/` on internal storage  
- **Server:** vMaNGOS / MaNGOS (Vanilla 1.12) on desktop PC  
- **Network:** Same Wi-Fi; realmlist → PC LAN IP  

---

## Open / next steps

The Android **shell** (SDLActivity + CMake → `libwowee.so` + crash hooks) exists.
The engine tree, toolchain, APK, and playable vertical slice do **not**. See [TODO.md](TODO.md).

- [ ] `.\scripts\setup-wowee.ps1` then a debug APK that stays up
- [ ] SDL2 relative mouse + keyboard hooks verified on device
- [ ] Lifecycle-safe Vulkan surface create/destroy
- [ ] Internal `Data/` path + first-run “missing assets” screen
- [ ] Desktop extract + local 1.12 emulator (vMaNGOS) as the known-good path
- [ ] Cross-compile Unicorn for arm64 if required by target servers
- [ ] Memory/streaming tuning for tablet GPUs
- [ ] Document legal disclaimer + BYO-assets UX

---

*Document compiled from project planning discussion. Codename: Obsidian. Target: Android Vanilla 1.12 client based on WoWee, keyboard/mouse peripherals, external private servers, user-supplied extracted assets.*
