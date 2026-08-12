# Architecture Overview

## System Design

Wowee follows a modular architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────┐
│           Application (main loop)            │
│  - State management (auth/realms/game)      │
│  - Update cycle                             │
│  - Event dispatch                           │
└──────────────┬──────────────────────────────┘
               │
       ┌───────┴────────┐
       │                │
┌──────▼──────┐  ┌─────▼──────┐
│   Window    │  │   Input    │
│  (SDL2 +    │  │ (Keyboard/ │
│   Vulkan)   │  │   Mouse)   │
└──────┬──────┘  └─────┬──────┘
       │                │
       └───────┬────────┘
               │
    ┌──────────┴──────────┐
    │                     │
┌───▼────────┐   ┌───────▼──────┐
│  Renderer  │   │  UI Manager  │
│  (Vulkan)  │   │   (ImGui)    │
└───┬────────┘   └──────────────┘
    │
    ├─ Camera + CameraController
    ├─ TerrainRenderer (ADT streaming)
    ├─ WMORenderer (buildings, collision)
    ├─ M2Renderer (models, particles, ribbons)
    ├─ CharacterRenderer (skeletal animation)
    ├─ WaterRenderer (refraction, lava, slime)
    ├─ SkyBox + StarField + Weather
    ├─ LightingManager (Light.dbc volumes)
    └─ SwimEffects, ChargeEffect, Lightning
```

## Core Systems

### 1. Application Layer (`src/core/`)

**Application** (`application.hpp/cpp`) - Main controller
- Owns all subsystems (renderer, game handler, asset manager, UI)
- Manages application state (AUTH → REALM_SELECT → CHAR_SELECT → IN_WORLD)
- Runs update/render loop
- Populates `GameServices` struct and passes to `GameHandler` at construction

**Window** (`window.hpp/cpp`) - SDL2 + Vulkan wrapper
- Creates SDL2 window with Vulkan surface
- Owns `VkContext` (Vulkan device, swapchain, render passes)
- Handles resize events

**Input** (`input.hpp/cpp`) - Input management
- Keyboard state tracking (SDL scancodes)
- Mouse position, buttons (1-based SDL indices), wheel delta
- Per-frame delta calculation

**Logger** (`logger.hpp/cpp`) - Thread-safe logging
- Multiple log levels (DEBUG, INFO, WARNING, ERROR, FATAL)
- File output to `logs/wowee.log`
- Configurable via `WOWEE_LOG_LEVEL` env var

### 2. Rendering System (`src/rendering/`)

**Renderer** (`renderer.hpp/cpp`) - Main rendering coordinator
- Manages Vulkan pipeline state
- Coordinates frame rendering across all sub-renderers
- Owns camera, sky, weather, lighting, and all sub-renderers
- Shadow mapping with PCF filtering

**VkContext** (`vk_context.hpp/cpp`) - Vulkan infrastructure
- Device selection, queue families, swapchain
- Render passes, framebuffers, command pools
- Sampler cache (FNV-1a hashed dedup)
- Pipeline cache persistence for fast startup

**Camera** (`camera.hpp/cpp`) - View/projection matrices
- Position and orientation
- FOV, aspect ratio, near/far planes
- Sub-pixel jitter for TAA/FSR2 (column 2 NDC offset)
- Frustum extraction for culling

**TerrainRenderer** - ADT terrain streaming
- Async chunk loading within configurable radius
- 4-layer texture splatting with alpha blending
- Frustum + distance culling
- Vegetation/foliage placement via deterministic RNG

**WMORenderer** - World Map Objects (buildings)
- Multi-material batch rendering
- Portal-based visibility culling
- Floor/wall collision (normal-based classification)
- Interior glass transparency, doodad placement

**M2Renderer** - Models (creatures, doodads, spell effects)
- Skeletal animation with GPU bone transforms
- Particle emitters (WotLK FBlock format)
- Ribbon emitters (charge trails, enchant glows)
- Portal spin effects, foliage wind displacement
- Per-instance animation state

**CharacterRenderer** - Player/NPC character models
- GPU vertex skinning (256 bones)
- Race/gender-aware textures via CharSections.dbc
- Equipment rendering (geoset visibility per slot)
- Fallback textures (white/transparent/flat-normal) for missing assets

**WaterRenderer** - Terrain and WMO water
- Refraction/reflection rendering
- Magma/slime with multi-octave FBM noise flow
- Beer-Lambert absorption

**Skybox + StarField + Weather**
- Procedural sky dome with time-of-day lighting
- Star field with day/night fade (dusk 18:00–20:00, dawn 04:00–06:00)
- Rain/snow particle systems per zone (via zone weather table)

**LightingManager** - Light.dbc volume sampling
- Time-of-day color bands (half-minutes, 0–2879)
- Distance-weighted light volume blending
- Fog color/distance parameters

**World Map System** (`src/rendering/world_map/`) - Modular map architecture:
- `WorldMapFacade` - Public API (PIMPL pattern), composes all components
- `CompositeRenderer` - Vulkan tile pipeline + off-screen FBO compositing (1024×768 FBO, 1002×668 visible)
- `DataRepository` - DBC zone loading, ZMP pixel map, POI/overlay storage
- `CoordinateProjection` - UV projection, zone/continent spatial lookups
- `ExplorationState` - Server exploration mask + local fog-of-war tracking
- `ViewStateMachine` - COSMIC → WORLD → CONTINENT → ZONE navigation with transitions
- `InputHandler` - Keyboard/mouse input → `InputAction` mapping
- `OverlayRenderer` - Layer-based ImGui overlay system (Open/Closed Principle)
- `MapResolver` - Cross-map navigation (Outland, Northrend detection)
- `ZoneMetadata` - Zone level ranges and faction data for labels
- 9 overlay layers (each implements `IOverlayLayer`): player marker, party dot, taxi node, POI marker, quest POI, corpse marker, zone highlight, coordinate display, subzone tooltip

### 3. Networking (`src/network/`)

**TCPSocket** (`tcp_socket.hpp/cpp`) - Platform TCP
- Non-blocking I/O with per-frame recv budgets
- 4 KB recv buffer per call
- Portable across Linux/macOS/Windows

**WorldSocket** (`world_socket.hpp/cpp`) - WoW world connection
- RC4 header encryption (derived from SRP session key)
- Packet parsing with configurable per-frame budgets
- Compressed move packet handling

**Packet** (`packet.hpp/cpp`) - Binary data container
- Read/write primitives (uint8–uint64, float, string, packed GUID)
- Bounds-checked reads (return 0 past end)

### 4. Authentication (`src/auth/`)

**AuthHandler** - Auth server protocol (port 3724)
- SRP6a challenge/proof flow
- Security flags: PIN (0x01), Matrix (0x02), Authenticator (0x04)
- Realm list retrieval

**SRP** (`srp.hpp/cpp`) - Secure Remote Password
- SRP6a with 19-byte (152-bit) ephemeral
- OpenSSL BIGNUM math
- Session key generation (40 bytes)

**Integrity** - Client integrity verification
- Checksum computation for Warden compatibility

### 5. Game Logic (`src/game/`)

**GameHandler** (`game_handler.hpp/cpp`) - Central game state
- Dispatch table routing server opcodes to domain handlers (registrations live in `game_handler_packets.cpp`)
- Owns all domain handlers via composition
- Receives dependencies via `GameServices` struct (no singleton access)

**Domain Handlers** (SOLID decomposition from GameHandler):
- `EntityController` - UPDATE_OBJECT parsing, entity spawn/despawn
- `MovementHandler` - Movement packets, speed, taxi, swimming, flying
- `CombatHandler` - Damage, healing, death, auto-attack, threat
- `SpellHandler` - Spell casting, cooldowns, auras, talents, pet spells
- `InventoryHandler` - Equipment, bags, bank, mail, auction, vendors
- `QuestHandler` - Quest accept/complete, objectives, progress tracking
- `SocialHandler` - Party, guild, LFG, friends, who, duel, trade
- `ChatHandler` - Chat messages, channels, emotes, system messages
- `WardenHandler` - Anti-cheat module management

**OpcodeTable** - Expansion-agnostic opcode mapping
- `LogicalOpcode` enum → wire opcode via JSON config per expansion
- Runtime remapping for Classic/TBC/WotLK/Turtle protocol differences

**Entity / EntityManager** - Entity lifecycle
- Shared entity base class with update fields (uint32 array)
- Player, Unit, GameObject subtypes
- GUID-based lookup, field extraction (health, level, display ID, etc.)

**TransportManager** - Transport lifecycle and server sync
- Delegates path data to `TransportPathRepository`
- Delegates spline math to `math::CatmullRomSpline`
- Clock-based motion with `TransportClockSync`
- `transport_manager.cpp` reduced from ~1,200 to ~370 lines after decomposition (extracted modules: TransportPathRepository, TransportClockSync, TransportAnimator)

**TransportPathRepository** - Transport path data
- DBC loading (TransportAnimation.dbc, TaxiPathNode.dbc)
- Path inference heuristics for server spawn→DBC mapping
- Z-only elevator detection vs XY transport paths

**math::CatmullRomSpline** (`src/math/`) - Reusable spline module
- Catmull-Rom interpolation with O(log n) binary search segment lookup
- Fused position+tangent evaluation (single call per frame per transport)
- Time-closed (looping) and clamped (non-looping) path modes
- `orientationFromTangent()` for smooth transport/entity facing

**SplineBlockData** (`src/game/spline_packet.hpp/cpp`) - Unified spline parsing
- Consolidates 7 duplicated spline parsers into shared functions
- `parseMonsterMoveSplineBody()` (WotLK/TBC), `parseMonsterMoveSplineBodyVanilla()`
- `parseWotlkMoveUpdateSpline()`, `parseClassicMoveUpdateSpline()`
- Packed delta decoding (11+11+10-bit signed, ×0.25 scale)

**Expansion Helpers** (`game_utils.hpp`):
- `isActiveExpansion("classic")` / `isActiveExpansion("tbc")` / `isActiveExpansion("wotlk")`
- `isClassicLikeExpansion()` (Classic or Turtle WoW)
- `isPreWotlk()` (Classic, Turtle, or TBC)

### 6. Asset Pipeline (`src/pipeline/`)

**AssetManager** - Runtime asset access
- Extracted loose-file tree indexed by `Data/manifest.json`
- Layered resolution via optional overlay manifests (multi-expansion dedup)
- File cache with configurable budget (256 MB min, 12 GB max)
- PNG override support (checks for .png before .blp)

**asset_extract (tool)** - MPQ extraction
- Uses StormLib to extract MPQs into `Data/` and generate `manifest.json`
- Driven by `extract_assets.sh` / `extract_assets.ps1`

**BLPLoader** - Texture decompression
- DXT1/3/5 block compression (RGB565 color endpoints)
- Palette mode with 1/4/8-bit alpha
- Mipmap extraction

**M2Loader** - Model binary parsing
- Version-aware header (Classic v256 vs WotLK v264)
- Skeletal animation tracks (embedded vs external .anim files, flag 0x20)
- Compressed quaternions (int16 offset mapping)
- Particle emitters, ribbon emitters, attachment points
- Geoset support (group × 100 + variant encoding)

**WMOLoader** - World object parsing
- Multi-group rendering with portal visibility
- Doodad placement (24-bit name index + 8-bit flags packing)
- Liquid data, collision geometry

**ADTLoader** - Terrain parsing
- 64×64 tiles per map, 16×16 chunks per tile (MCNK)
- MCVT height grid (145 vertices: 9 outer + 8 inner per row × 9 rows)
- Texture layers (up to 4 with alpha blending, RLE-compressed alpha maps)
- Async loading to prevent frame stalls

**DBCLoader** - Database table parsing
- Binary DBC format (fixed 4-byte uint32 fields + string block)
- CSV fallback for pre-extracted data
- Expansion-aware field layout via `dbc_layouts.json`
- 20+ DBC files: Spell, Item, Creature, Faction, Map, AreaTable, etc.

### 7. UI System (`src/ui/`)

**UIManager** - ImGui coordinator
- ImGui initialization with SDL2/Vulkan backend
- Screen state management and transitions
- Event handling and input routing

**Screens:**
- `AuthScreen` - Login with username/password, server address, security code
- `RealmScreen` - Realm list with population and type indicators
- `CharacterScreen` - Character selection with 3D animated preview, keyboard navigation
- `CharacterCreateScreen` - Race/class/gender/appearance customization
- `GameScreen` - Main HUD: chat, action bar, target frame, minimap, nameplates, combat text, tooltips
- `InventoryScreen` - Equipment paper doll, backpack, bag windows, item tooltips with stats
- `SpellbookScreen` - Tabbed spell list with icons, drag-drop to action bar
- `QuestLogScreen` - Quest list with objectives, details, and rewards
- `TalentScreen` - Talent tree UI with point allocation
- `SettingsScreen` - Graphics presets (LOW/MEDIUM/HIGH/ULTRA), audio, keybindings

**Chat System** (`src/ui/chat/`) - Modular chat architecture:
- `ChatPanel` - Main chat UI (tabs, input, message display)
- `ChatInput` - Input handling and history
- `ChatTabManager` - Tab creation, switching, per-tab filters
- `ChatTabCompleter` - Tab-completion for player names, commands, channels
- `ChatCommandRegistry` - Slash command dispatch with `IChatCommand` interface
- `ChatMarkupParser` / `ChatMarkupRenderer` - Item link parsing and colored rich-text rendering
- `ChatBubbleManager` - Floating chat bubbles above entities
- `ChatSettings` - Per-channel color, font size, timestamp options
- `MacroEvaluator` - WoW-style macro conditional evaluation (`[mod:shift]`, `[target=focus]`, etc.)
- `GameStateAdapter` / `InputModifierAdapter` - Testable abstractions over game state
- `ItemTooltipRenderer` - Chat-embedded item tooltip rendering (510 LOC)
- `CastSequenceTracker` - `/castsequence` state tracking
- 11 command modules under `commands/`: channel, combat, emote, GM, group, guild, help, misc, social, system, target
- 190-command GM data table with dot-prefix interception and `/gmhelp`

### 8. Audio System (`src/audio/`)

**AudioEngine** - miniaudio-based playback
- WAV decode cache (256 entries, LRU eviction)
- 2D and 3D positional audio
- Sample rate preservation (explicit to avoid miniaudio pitch distortion)

**Sound Managers:**
- `AmbientSoundManager` - Wind, water, fire, birds, crickets, city ambience, bell tolls
- `ActivitySoundManager` - Swimming strokes, jumping, landing
- `MovementSoundManager` - Footsteps (terrain-aware), mount movement
- `MountSoundManager` - Mount-specific movement audio
- `MusicManager` - Zone music with day/night variants

### 9. Warden Anti-Cheat (`src/game/`)

4-layer architecture:
- `WardenHandler` - Packet handling (SMSG/CMSG_WARDEN_DATA)
- `WardenModuleManager` - Module lifecycle and caching
- `WardenModule` - 8-step pipeline: decrypt (RC4), strip RSA-2048 signature, decompress (zlib), parse PE headers, relocate, resolve imports, execute
- `WardenEmulator` - Unicorn Engine x86 CPU emulation with Windows API interception
- `WardenMemory` - PE image loading with bounds-checked reads, runtime global patching

## Threading Model

- **Main thread**: Window events, game logic update, rendering
- **Async terrain**: Non-blocking chunk loading (std::async)
- **Network I/O**: Non-blocking recv in main thread with per-frame budgets
- **Normal maps**: Background CPU generation with mutex-protected result queue
- **GPU uploads**: Second Vulkan queue for parallel texture/buffer transfers

## Memory Management

- **Smart pointers**: `std::unique_ptr` / `std::shared_ptr` throughout
- **RAII**: All Vulkan resources wrapped with proper destructors
- **VMA**: Vulkan Memory Allocator for GPU memory
- **Object pooling**: Weather particles, combat text entries
- **DBC caching**: Lazy-loaded mutable caches in const getters

## Build System

**CMake** with modular targets:
- `wowee` - Main executable
- `asset_extract` - MPQ extraction tool (requires StormLib)
- `dbc_to_csv` / `auth_probe` / `blp_convert` - Utility tools

**Dependencies:**
- SDL2, Vulkan SDK, OpenSSL, GLM, zlib (system)
- ImGui (submodule in extern/)
- VMA, vk-bootstrap, stb_image (vendored in extern/)
- StormLib (system, optional — only for asset_extract)
- Unicorn Engine (system, optional — only for Warden emulation)
- FFmpeg (system, optional — for video playback)

**CI**: GitHub Actions for Linux (x86-64, ARM64), Windows (MSYS2), macOS (ARM64)
**Container builds**: Docker cross-compilation for Linux, macOS (osxcross), Windows (LLVM-MinGW)

## Code Style

- **C++20 standard**
- **Namespaces**: `wowee::core`, `wowee::rendering`, `wowee::rendering::world_map`, `wowee::game`, `wowee::ui`, `wowee::ui::chat`, `wowee::math`, `wowee::network`, `wowee::auth`, `wowee::audio`, `wowee::pipeline`
- **Naming**: PascalCase for classes, camelCase for functions/variables, kPascalCase for constants
- **Headers**: `.hpp` extension, `#pragma once`
- **Commits**: Conventional style (`feat:`, `fix:`, `refactor:`, `docs:`, `perf:`)
