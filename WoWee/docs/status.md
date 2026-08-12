# Project Status

**Last updated**: 2026-07-09

## What This Repo Is

Wowee is a native C++ World of Warcraft client experiment focused on connecting to real emulator servers (online/multiplayer) with a custom renderer and asset pipeline.

## Current Code State

Implemented (working in normal use):

- Auth flow: SRP6a auth + realm list + world connect with header encryption
- Rendering: terrain, WMO/M2, water/magma/slime (FBM noise shaders), sky system, particles, shadow mapping, minimap/world map, loading video playback
- Instances: WDT parser, WMO-only dungeon maps, area trigger portals with glow/spin effects, zone transitions
- Character system: creation (including nonbinary gender), selection, 3D preview with equipment, character screen, per-instance NPC hair/skin textures
- Core gameplay: movement (with ACK responses), targeting (hostility-filtered tab-cycle), combat, action bar, inventory/equipment, chat (tabs/channels, emotes, item links)
- Quests: quest markers (! and ?) on NPCs/minimap, quest log with detail queries/retry, objective tracking, accept/complete flow, turn-in, quest item progress
- Trainers: spell trainer UI, buy spells, known/available/unavailable states
- Vendors, loot (including chest/gameobject loot), gossip dialogs (including buyback for most recently sold item)
- Bank: full bank support for all expansions, bag slots, drag-drop, right-click deposit
- Auction house: search with filters, pagination, sell picker, bid/buyout, tooltips
- Mail: item attachment support for sending
- Spellbook with specialty/general/profession/mount/companion tabs, drag-drop to action bar, spell icons, item use
- Talent tree UI with proper visuals and functionality
- Pet tracking (SMSG_PET_SPELLS), dismiss pet button
- Party: group invites, party list, out-of-range member health (SMSG_PARTY_MEMBER_STATS)
- Nameplates: NPC subtitles, guild names, elite/boss/rare borders, quest/raid indicators, cast bars, debuff dots
- Floating combat text: world-space damage/heal numbers above entities with 3D projection
- Target/focus frames: guild name, creature type, rank badges, combo points, cast bars
- Map exploration: subzone-level fog-of-war reveal
- Warden anti-cheat: full module execution via Unicorn Engine x86 emulation; module caching
- Audio: ambient, movement, combat, spell, and UI sound systems; NPC voice lines for all playable races (greeting/farewell/vendor/pissed/aggro/flee)
- Bag UI: independent bag windows (any bag closable independently), open-bag indicator on bag bar, server-synced bag sort, off-screen position reset, optional collapse-empty mode in aggregate view
- DBC auto-detection: CharSections.dbc field layout auto-detected at runtime (handles stock WotLK vs HD-textured clients)
- Multi-expansion: Classic/Vanilla, TBC, WotLK, and Turtle WoW (1.18) protocol and asset variants
- CI: GitHub Actions for Linux (x86-64, ARM64), Windows (MSYS2 x86-64 + ARM64), macOS (ARM64); container builds via Podman

Recent refactors (PRs #59-63, April 2026):

- Chat system decomposed into 15+ modules under `src/ui/chat/` with 11 command modules, GM command support, macro evaluator, and tab completion
- World map decomposed into 16 modules under `src/rendering/world_map/` with overlay layer system, view state machine, and ZMP-based hover detection
- TransportManager decomposed: spline math extracted to `src/math/`, path data to TransportPathRepository, 7 duplicated spline parsers consolidated into `spline_packet.cpp`
- Spell visual effects system with bone-tracked ribbons and particles
- Entity movement improvements: multi-segment path interpolation, terrain height clamping, walk/run animation fix
- 31 unit-test suites (up from 8), covering chat, world map, spline math, transport, and animation systems
- Code quality fix pass: 7 issues resolved across hover detection, null safety, buffer bounds, and coordinate validation

Recent fixes (July 2026):

- Login pipeline hardened: login-critical opcodes have hardcoded fallback when opcode table lookup fails; OpcodeTable::loadFromJson() is now safe against failed reloads (issue #87)
- Integrity hash is build-aware: Classic-era DLLs only required for builds <=6005 or Turtle; TBC/WotLK hash only the .exe
- Strafing reworked: torso-twist via SpineLow bone rotation instead of dedicated strafe animations
- Camera smoothing snaps 1:1 during active drag/keyboard turn to reduce input lag
- Mount strafing uses MOUNT_RUN_LEFT/RIGHT when available

In progress / known gaps:

- World map: zone hover detection has edge cases with some zone boundaries; cosmic highlight sizing is approximate
- Transports: M2 transports (trams) working with position-delta riding; WMO transports (ships, zeppelins) working with path following; some edge cases remain
- Quest GO interaction: CMSG_GAMEOBJ_USE + CMSG_LOOT sent correctly, but some AzerothCore/ChromieCraft servers don't grant quest credit for chest-type GOs (server-side limitation)
- Visual edge cases: some M2/WMO rendering gaps (some particle effects)
- Water refraction: enabled by default; srcAccessMask barrier fix (2026-03-18) resolved prior VK_ERROR_DEVICE_LOST on AMD/Mali GPUs
- Fixed 2026-05-15: classic/turtle update-field tables had multiple wrong indices (`UNIT_FIELD_BYTES_1`=133 colliding with `UNIT_FIELD_MOUNTDISPLAYID`=133; STAT0..4 at 138..142; RESISTANCES at 154; missing NATIVEDISPLAYID). Corrected against vmangos `UpdateFields_1_12_1.h`: BYTES_1=138, STAT0..4=150..154, RESISTANCES=155, added NATIVEDISPLAYID=132.

## Where To Look

- Entry point: `src/main.cpp`, `src/core/application.cpp`
- Networking/auth: `src/auth/`, `src/network/`, `src/game/game_handler.cpp`
- Rendering: `src/rendering/`
- Assets/extraction: `extract_assets.sh`, `tools/asset_extract/`, `src/pipeline/asset_manager.cpp`
