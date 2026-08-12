# Contributing to Wowee

## Build Setup

See [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) for full platform-specific details.
The short version: CMake + Make on Linux/macOS, MSYS2 on Windows.

```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
make -C build -j$(nproc)
```

## Code Style

- **C++20**. Use `#pragma once` for include guards.
- Namespaces: `wowee::game`, `wowee::rendering`, `wowee::rendering::world_map`, `wowee::ui`, `wowee::ui::chat`, `wowee::math`, `wowee::core`, `wowee::network`.
- Conventional commit messages in imperative mood:
  - `feat:` new feature
  - `fix:` bug fix
  - `refactor:` code restructuring with no behavior change
  - `perf:` performance improvement
- Prefer `constexpr` over `static const` for compile-time data.
- Mark functions whose return value should not be ignored with `[[nodiscard]]`.

## Pull Request Process

1. Branch from `master`.
2. Keep commits focused -- one logical change per commit.
3. Describe *what* changed and *why* in the PR description.
4. Ensure the project compiles cleanly before submitting.
5. Manual testing against a WoW 3.3.5a server (e.g. AzerothCore/ChromieCraft) is expected
   for gameplay-affecting changes.

## Architecture Overview

See [docs/architecture.md](docs/architecture.md) for the full picture. Key namespaces:

| Namespace | Responsibility |
|---|---|
| `wowee::game` | Game state, packet handling (`GameHandler`), opcode dispatch, spline parsing |
| `wowee::rendering` | Vulkan renderer, M2/WMO/terrain, sky system |
| `wowee::rendering::world_map` | Modular world map (16 components: facade, compositor, layers, etc.) |
| `wowee::ui` | ImGui windows and HUD (`GameScreen`) |
| `wowee::ui::chat` | Modular chat system (15+ components: commands, markup, macros, etc.) |
| `wowee::math` | Reusable math modules (CatmullRomSpline) |
| `wowee::core` | Coordinates, math, utilities |
| `wowee::network` | Connection, `Packet` read/write API |

## Packet Handlers

The standard pattern for adding a new server packet handler:

1. Define a `struct FooData` holding the parsed fields.
2. Write `void GameHandler::handleFoo(network::Packet& packet)` to parse into `FooData`.
3. Register it in the dispatch table: `registerHandler(LogicalOpcode::SMSG_FOO, &GameHandler::handleFoo)`.

Helper variants: `registerWorldHandler` (requires `isInWorld()`), `registerSkipHandler` (discard),
`registerErrorHandler` (log warning).

## Testing

31 unit-test suites cover core systems, animation, transport/spline, world map, and chat.
See [TESTING.md](TESTING.md) for the full guide. Run with `./test.sh --test`.
Manual testing against WoW 3.3.5a private servers (primarily ChromieCraft/AzerothCore)
is expected for gameplay-affecting changes.

## Expansion Config Files

`Data/expansions/<id>/` holds the per-expansion config (`expansion.json`,
`opcodes.json`, `update_fields.json`, `dbc_layouts.json`, plus the
`db/` CSV fallback and DBC overlay).

- `opcodes.json` supports `_extends` / `_remove` (see
  `src/game/opcode_table.cpp` `loadOpcodeJsonRecursive`). The turtle
  profile uses this to inherit from classic.
- `update_fields.json` does **not** currently support `_extends` — the
  classic and turtle files must be kept byte-identical by hand. Update
  both together when changing vanilla field indices.
- Authoritative source for vanilla 1.12 field indices: vmangos
  `UpdateFields_1_12_1.h`. For 2.4.3 and 3.3.5a, use vmangos
  `UpdateFields_2_4_3.h` and AzerothCore / TrinityCore `UpdateFields.h`
  respectively.

## Key Files for New Contributors

| File / Directory | What it does |
|---|---|
| `include/game/game_handler.hpp` | Central game state and all packet handler declarations |
| `src/game/game_handler.cpp` | Packet dispatch registration and handler implementations |
| `include/network/packet.hpp` | `Packet` class -- the read/write API every handler uses |
| `include/ui/game_screen.hpp` | Main gameplay UI screen (ImGui) |
| `src/ui/chat/` | Modular chat system (commands, markup, macros, tab completion) |
| `src/rendering/world_map/` | Modular world map (facade, compositor, layers, coordinate projection) |
| `src/math/spline.cpp` | Reusable CatmullRomSpline math |
| `src/game/spline_packet.cpp` | Unified spline packet parsing for all expansions |
| `src/rendering/m2_renderer.cpp` | M2 model loading and rendering |
| `docs/architecture.md` | High-level system architecture reference |
