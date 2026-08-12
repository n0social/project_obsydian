# WoWee Testing Guide

This document covers everything needed to build, run, lint, and extend the WoWee test suite.

---

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Test Suite Layout](#test-suite-layout)
4. [Building the Tests](#building-the-tests)
   - [Release Build (normal)](#release-build-normal)
   - [Debug + ASAN/UBSan Build](#debug--asanubsan-build)
5. [Running Tests](#running-tests)
   - [test.sh — the unified entry point](#testsh--the-unified-entry-point)
   - [Running directly with ctest](#running-directly-with-ctest)
6. [Lint (clang-tidy)](#lint-clang-tidy)
   - [Running lint](#running-lint)
   - [Applying auto-fixes](#applying-auto-fixes)
   - [Configuration (.clang-tidy)](#configuration-clang-tidy)
7. [ASAN / UBSan](#asan--ubsan)
8. [Adding New Tests](#adding-new-tests)
9. [CI Reference](#ci-reference)

---

## Overview

WoWee uses **Catch2 v3** (amalgamated) for unit testing and **clang-tidy** for static analysis. The `test.sh` script is the single entry point for both.

| Command | What it does |
|---|---|
| `./test.sh` | Runs both unit tests (Release) and lint |
| `./test.sh --test` | Runs unit tests only (Release build) |
| `./test.sh --lint` | Runs clang-tidy only |
| `./test.sh --asan` | Runs unit tests under ASAN + UBSan (Debug build) |
| `FIX=1 ./test.sh --lint` | Applies clang-tidy auto-fixes in-place |

All commands exit non-zero on any failure.

---

## Prerequisites

The test suite requires the same base toolchain used to build the project. See [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) for platform-specific dependency installation.

### Linux (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config git \
  libssl-dev \
  clang-tidy
```

### Linux (Arch)

```bash
sudo pacman -S --needed base-devel cmake pkgconf git openssl clang
```

### macOS

```bash
brew install cmake openssl@3 llvm
# Add LLVM tools to PATH so clang-tidy is found:
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

### Windows (MSYS2)

Install the full toolchain as described in `BUILD_INSTRUCTIONS.md`, then add:

```bash
pacman -S --needed mingw-w64-x86_64-clang-tools-extra
```

---

## Test Suite Layout

```
tests/
  CMakeLists.txt                          — CMake test configuration

  # Core
  test_packet.cpp                         — Network packet encode/decode
  test_srp.cpp                            — SRP-6a authentication math (requires OpenSSL)
  test_opcode_table.cpp                   — Opcode registry lookup
  test_entity.cpp                         — ECS entity basics
  test_dbc_loader.cpp                     — DBC binary file parsing
  test_m2_structs.cpp                     — M2 model struct layout / alignment
  test_blp_loader.cpp                     — BLP texture file parsing
  test_frustum.cpp                        — View-frustum culling math

  # Animation
  test_animation_ids.cpp                  — Animation ID constants
  test_locomotion_fsm.cpp                 — Locomotion state machine transitions
  test_combat_fsm.cpp                     — Combat animation state machine
  test_activity_fsm.cpp                   — Activity state machine
  test_anim_capability.cpp                — Animation capability queries
  test_indoor_shadows.cpp                 — Indoor shadow rendering

  # Transport & Spline
  test_spline.cpp                         — CatmullRomSpline math (interpolation, binary search, looping)
  test_transport_components.cpp           — Transport clock sync and animator
  test_transport_path_repo.cpp            — TransportPathRepository (DBC loading, path inference)

  # World Map
  test_world_map.cpp                      — World map integration tests
  test_world_map_coordinate_projection.cpp — UV projection, zone/continent spatial lookups
  test_world_map_exploration_state.cpp    — Server exploration mask, local tracking
  test_world_map_map_resolver.cpp         — Cross-map navigation (Outland, Northrend)
  test_world_map_view_state_machine.cpp   — COSMIC→WORLD→CONTINENT→ZONE transitions
  test_world_map_zone_metadata.cpp        — Zone level ranges and faction labels

  # Chat
  test_chat_markup_parser.cpp             — Item link and markup parsing
  test_chat_tab_completer.cpp             — Tab-completion for names and commands
  test_gm_commands.cpp                    — GM command data table and dispatch
  test_macro_evaluator.cpp                — Macro conditional evaluation
```

The Catch2 v3 amalgamated source lives at:

```
extern/catch2/
  catch_amalgamated.hpp
  catch_amalgamated.cpp
```

---

## Building the Tests

Tests **are** built by default (`option(WOWEE_BUILD_TESTS "Build tests" ON)` in `CMakeLists.txt`). Pass `-DWOWEE_BUILD_TESTS=OFF` to skip them; the snippets below pass `-DWOWEE_BUILD_TESTS=ON` only to be explicit.

### Release Build (normal)

> **Note:** Per project rules, always use `rebuild.sh` for a full clean build. Direct `cmake --build` is fine for test-only incremental builds.

```bash
# Configure (only needed once)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DWOWEE_BUILD_TESTS=ON

# Build all test targets
cmake --build build --parallel $(nproc)

# Or build specific test targets
cmake --build build --target test_packet test_spline test_world_map
```

Or simply run a full rebuild (builds everything including the main binary):

```bash
./rebuild.sh      # ~10 minutes — see BUILD_INSTRUCTIONS.md
```

### Debug + ASAN/UBSan Build

A separate CMake build directory is used so ASAN flags do not pollute the Release binary.

```bash
cmake -B build_asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWOWEE_ENABLE_ASAN=ON \
  -DWOWEE_BUILD_TESTS=ON

cmake --build build_asan --parallel $(nproc)
```

CMake will print: `Test targets: ASAN + UBSan ENABLED` when configured correctly.

---

## Running Tests

### test.sh — the unified entry point

`test.sh` is the recommended way to run tests and/or lint. It handles build-directory discovery, dependency checking, and exit-code aggregation across both steps.

```bash
# Run everything (tests + lint) — default when no flags are given
./test.sh

# Tests only (Release build)
./test.sh --test

# Tests only under ASAN+UBSan (Debug build — requires build_asan/)
./test.sh --asan

# Lint only
./test.sh --lint

# Both tests and lint explicitly
./test.sh --test --lint

# Usage summary
./test.sh --help
```

**Exit codes:**

| Outcome | Exit code |
|---|---|
| All tests passed, lint clean | `0` |
| Any test failed | `1` |
| Any lint diagnostic | `1` |
| Both test failure and lint issues | `1` |

### Running directly with ctest

```bash
# Release build
cd build
ctest --output-on-failure

# ASAN build
cd build_asan
ctest --output-on-failure

# Run one specific test suite by name
ctest --output-on-failure -R srp

# Verbose output (shows every SECTION and REQUIRE)
ctest --output-on-failure -V
```

You can also run a test binary directly for detailed Catch2 output:

```bash
./build/bin/test_srp
./build/bin/test_srp --reporter console
./build/bin/test_srp "[authentication]"    # run only tests tagged [authentication]
```

---

## Lint (clang-tidy)

The project uses clang-tidy to enforce C++20 best practices on all first-party sources under `src/`. Third-party code (anything in `extern/`) and generated files are excluded.

### Running lint

```bash
./test.sh --lint
```

Under the hood the script:

1. Locates `clang-tidy` (tries versions 14–18, then `clang-tidy`).
2. Uses `run-clang-tidy` for parallel execution when available; falls back to sequential.
3. Reads `build/compile_commands.json` (generated by CMake) for compiler flags.
4. Feeds GCC stdlib include paths as `-isystem` extras so clang-tidy can resolve `<vector>`, `<string>`, etc. when the compile-commands were generated with GCC.

`compile_commands.json` is regenerated automatically by any CMake configure step. If you only want to update it without rebuilding:

```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### Applying auto-fixes

Some clang-tidy checks can apply fixes automatically (e.g. `modernize-*`, `readability-*`):

```bash
FIX=1 ./test.sh --lint
```

> **Caution:** Review the diff before committing — automatic fixes occasionally produce non-idiomatic results in complex template code.

### Configuration (.clang-tidy)

The active check set is defined in [.clang-tidy](.clang-tidy) at the repository root.

**Enabled check categories:**

| Category | What it catches |
|---|---|
| `bugprone-*` | Common bug patterns (signed overflow, misplaced `=`, etc.) |
| `clang-analyzer-*` | Deep flow-analysis: null dereferences, memory leaks, dead stores |
| `performance-*` | Unnecessary copies, inefficient STL usage |
| `modernize-*` (subset) | Pre-C++11 patterns that should use modern equivalents |
| `readability-*` (subset) | Control-flow simplification, redundant code |

**Notable suppressions** (see `.clang-tidy` for details):

| Suppressed check | Reason |
|---|---|
| `bugprone-easily-swappable-parameters` | High false-positive rate in graphics/math APIs |
| `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` | Intentional low-level buffer code in rendering |
| `performance-avoid-endl` | `std::endl` is used intentionally for logger flushing |

To suppress a specific warning inline, use:

```cpp
// NOLINT(bugprone-narrowing-conversions)
uint8_t byte = static_cast<uint8_t>(value); // NOLINT
```

---

## ASAN / UBSan

AddressSanitizer (ASAN) and Undefined Behaviour Sanitizer (UBSan) are applied to all test targets when `WOWEE_ENABLE_ASAN=ON`.

Both the test executables **and** the `catch2_main` static library are recompiled with:

```
-fsanitize=address,undefined -fno-omit-frame-pointer
```

This means any heap overflow, stack buffer overflow, use-after-free, null dereference, signed integer overflow, or misaligned access detected during a test will abort the process and print a human-readable report to stderr.

### Workflow

```bash
# 1. Configure once (only needs to be re-run when CMakeLists.txt changes)
cmake -B build_asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWOWEE_ENABLE_ASAN=ON \
  -DWOWEE_BUILD_TESTS=ON

# 2. Build test binaries (fast incremental after the first build)
cmake --build build_asan --target test_packet test_srp   # etc.

# 3. Run
./test.sh --asan
```

### Interpreting ASAN output

A failing ASAN report looks like:

```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000010
READ of size 4 at 0x602000000010 thread T0
    #0 0x... in PacketBuffer::read_uint32 src/network/packet.cpp:42
    #1 0x... in test_packet tests/test_packet.cpp:88
```

Address the issue in the source file and re-run. Do **not** suppress ASAN reports without a code fix.

---

## Adding New Tests

1. **Create** `tests/test_<name>.cpp` with a standard Catch2 v3 structure:

```cpp
#include "catch_amalgamated.hpp"

TEST_CASE("SomeFeature does X", "[tag]") {
    REQUIRE(1 + 1 == 2);
}
```

2. **Register** the test in `tests/CMakeLists.txt` following the existing pattern:

```cmake
# ── test_<name> ──────────────────────────────────────────────
add_executable(test_<name>
    test_<name>.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/<module>/<file>.cpp   # source under test
)
target_include_directories(test_<name> PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_<name> SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_<name> PRIVATE catch2_main)
add_test(NAME <name> COMMAND test_<name>)
register_test_target(test_<name>)   # required — enables ASAN propagation
```

3. **Build** and verify:

```bash
cmake --build build --target test_<name>
./test.sh --test
```

The `register_test_target()` macro call is **mandatory** — without it the new test will not receive ASAN/UBSan flags when `WOWEE_ENABLE_ASAN=ON`.

---

## CI Reference

The following commands map to typical CI jobs:

| Job | Command |
|---|---|
| Unit tests (Release) | `./test.sh --test` |
| Unit tests (ASAN+UBSan) | `./test.sh --asan` |
| Lint | `./test.sh --lint` |
| Full check (tests + lint) | `./test.sh` |

**Configuring the ASAN job in CI:**

```yaml
- name: Configure ASAN build
  run: |
    cmake -B build_asan \
      -DCMAKE_BUILD_TYPE=Debug \
      -DWOWEE_ENABLE_ASAN=ON \
      -DWOWEE_BUILD_TESTS=ON

- name: Build test targets
  run: cmake --build build_asan --parallel $(nproc)

- name: Run ASAN tests
  run: ./test.sh --asan
```

> See [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) for full platform dependency installation steps required before any CI job.
