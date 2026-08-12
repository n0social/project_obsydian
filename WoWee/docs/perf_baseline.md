# Performance Baseline — WoWee

> Phase 0.3 deliverable. Measurements taken before any optimization work.
> Re-run after each phase to quantify improvement.

## Tracy Profiler Integration

Tracy is integrated under the `WOWEE_ENABLE_TRACY` CMake option (default: OFF).
When enabled, zero-cost zone markers instrument the following critical paths.

> **Setup**: Tracy is *not* vendored in this repo. `CMakeLists.txt` looks for
> `extern/tracy/public/TracyClient.cpp` and `extern/tracy/public/` headers
> (lines 1045-1050). Clone Tracy into that path before configuring with
> `-DWOWEE_ENABLE_TRACY=ON`:
>
> ```bash
> git clone --depth 1 --branch v0.11.1 https://github.com/wolfpld/tracy extern/tracy
> ```

### Instrumented Zones

| Zone Name | File | Purpose |
|-----------|------|---------|
| `Application::run` | src/core/application.cpp | Main loop entry |
| `Application::update` | src/core/application.cpp | Per-frame game logic |
| `Renderer::beginFrame` | src/rendering/renderer.cpp | Vulkan frame begin |
| `Renderer::endFrame` | src/rendering/renderer.cpp | Post-process + present |
| `Renderer::update` | src/rendering/renderer.cpp | Renderer per-frame update |
| `Renderer::renderWorld` | src/rendering/renderer.cpp | Main world draw call |
| `Renderer::renderShadowPass` | src/rendering/renderer.cpp | Shadow depth pass |
| `PostProcess::execute` | src/rendering/post_process_pipeline.cpp | FSR/FXAA post-process |
| `M2::computeBoneMatrices` | src/rendering/m2_renderer_internal.h | CPU skeletal animation |
| `M2Renderer::update` | src/rendering/m2_renderer_render.cpp | M2 instance update + culling |
| `TerrainManager::update` | src/rendering/terrain_manager.cpp | Terrain streaming logic |
| `TerrainManager::processReadyTiles` | src/rendering/terrain_manager.cpp | GPU tile uploads |
| `ADTLoader::load` | src/pipeline/adt_loader.cpp | ADT binary parsing |
| `AssetManager::loadTexture` | src/pipeline/asset_manager.cpp | BLP texture loading |
| `AssetManager::loadDBC` | src/pipeline/asset_manager.cpp | DBC data file loading |
| `WorldSocket::update` | src/network/world_socket.cpp | Network packet dispatch |

`FrameMark` placed at frame boundary in Application::update to track FPS.

### How to Profile

```bash
# Build with Tracy enabled
mkdir -p build_tracy && cd build_tracy
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWOWEE_ENABLE_TRACY=ON
cmake --build . --parallel $(nproc)

# Run the client — Tracy will broadcast on default port (8086)
cd bin && ./wowee

# Connect with Tracy profiler GUI (separate download from https://github.com/wolfpld/tracy/releases)
# Or capture from CLI: tracy-capture -o trace.tracy
```

## Baseline Scenarios

> **TODO:** Record measurements once profiler is connected to a running instance.
> Each scenario should record: avg FPS, frame time (p50/p95/p99), and per-zone timings.

### Scenario 1: Stormwind (Heavy M2/WMO)
- **Location:** Stormwind City center
- **Load:** Dense M2 models (NPCs, doodads), multiple WMO interiors
- **Avg FPS:** _pending_
- **Frame time (p50/p95/p99):** _pending_
- **Top zones:** _pending_

### Scenario 2: The Barrens (Heavy Terrain)
- **Location:** Central Barrens
- **Load:** Many terrain tiles loaded, sparse M2, large draw distance
- **Avg FPS:** _pending_
- **Frame time (p50/p95/p99):** _pending_
- **Top zones:** _pending_

### Scenario 3: Dungeon Instance (WMO-only)
- **Location:** Any dungeon instance (e.g., Deadmines entrance)
- **Load:** WMO interior rendering, no terrain
- **Avg FPS:** _pending_
- **Frame time (p50/p95/p99):** _pending_
- **Top zones:** _pending_

## Notes

- When `WOWEE_ENABLE_TRACY` is OFF (default), all `ZoneScopedN` / `FrameMark` macros expand to nothing — zero runtime overhead.
- Tracy requires a network connection to capture traces. Run the Tracy profiler GUI or `tracy-capture` CLI alongside the client.
- Debug builds are significantly slower due to -Og and no LTO; use RelWithDebInfo for representative measurements.
