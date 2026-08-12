# Threading Model

This document describes the threading architecture of WoWee, the synchronisation
primitives that protect shared state, and the conventions that new code must
follow.

---

## Thread Inventory

| #  | Name / Role            | Created At                                      | Lifetime                      |
|----|------------------------|-------------------------------------------------|-------------------------------|
| 1  | **Main thread**        | `Application::run()` (`main.cpp`)               | Entire session                |
| 2  | **Async network pump** | `WorldSocket::asyncPumpLoop()` started inside `WorldSocket::connect()` (`world_socket.cpp`) | Connect → disconnect          |
| 3  | **Terrain workers**    | `TerrainManager::initialize()` spawns the worker pool that runs `workerLoop()` (`terrain_manager.cpp`) | Map load → `stopWorkers()` on shutdown |
| 4  | **Watchdog**           | Inline lambda `std::thread` started in `Application::run()` (`application.cpp:642`) | After first frame → shutdown  |
| 5  | **Fire-and-forget**    | `std::async` / `std::thread(...).detach()` (various) | Task-scoped (bone anim, normal-map gen, warden crypto, world preload, entity model loading) |

### Thread Responsibilities

* **Main thread** — SDL event pumping, game logic (entity update, camera, UI),
  GPU resource upload/finalization, render command recording, Vulkan present.
* **Network pump** — `recv()` loop, header decryption, packet parsing.  Pushes
  parsed packets into `pendingPacketCallbacks_` (locked by `callbackMutex_`).
  The main thread drains this queue via `dispatchQueuedPackets()`.
* **Terrain workers** — background ADT/WMO/M2 file I/O, mesh decoding, texture
  decompression.  Workers push completed `PendingTile` objects into `readyQueue`
  (locked by `queueMutex`).  The main thread finalizes (GPU upload) via
  `processReadyTiles()`.
* **Watchdog** — periodic frame-stall detection.  Reads `watchdogHeartbeatMs`
  (atomic) and optionally requests a Vulkan device reset via
  `watchdogRequestRelease` (atomic).
* **Fire-and-forget** — short-lived tasks.  Each captures only the data it
  needs or uses a dedicated result channel (e.g. `std::future`,
  `completedNormalMaps_` with `normalMapResultsMutex_`).

---

## Shared State Map

### Legend

| Annotation              | Meaning |
|-------------------------|---------|
| `THREAD-SAFE: <mutex>`  | Protected by the named mutex/atomic. |
| `MAIN-THREAD-ONLY`      | Accessed exclusively by the main thread. No lock needed. |

### Asset Manager (`include/pipeline/asset_manager.hpp`)

| Variable                | Guard            | Notes |
|-------------------------|------------------|-------|
| `fileCache`             | `cacheMutex` (shared_mutex) | `shared_lock` for reads, `lock_guard` for writes/eviction |
| `dbcCache`              | `cacheMutex`     | Same mutex as fileCache |
| `fileCacheTotalBytes`   | `cacheMutex`     | Written under exclusive lock only |
| `fileCacheAccessCounter`| `cacheMutex`     | Written under exclusive lock only |
| `fileCacheHits`         | `std::atomic`    | Incremented after releasing cacheMutex |
| `fileCacheMisses`       | `std::atomic`    | Incremented after releasing cacheMutex |

### Audio Engine (`src/audio/audio_engine.cpp`)

| Variable               | Guard                     | Notes |
|------------------------|---------------------------|-------|
| `gDecodedWavCache`     | `gDecodedWavCacheMutex` (shared_mutex) | `shared_lock` for cache hits, `lock_guard` for miss+eviction. Double-check after decoding. |

### World Socket (`include/network/world_socket.hpp`)

| Variable                  | Guard            | Notes |
|---------------------------|------------------|-------|
| `sockfd`, `connected`, `encryptionEnabled`, `receiveBuffer`, `receiveReadOffset_`, `headerBytesDecrypted`, cipher state, `recentPacketHistory_` | `ioMutex_` | Consistent `lock_guard` in `send()` and `pumpNetworkIO()` |
| `pendingPacketCallbacks_` | `callbackMutex_` | Pump thread produces, main thread consumes in `dispatchQueuedPackets()` |
| `asyncPumpStop_`, `asyncPumpRunning_` | `std::atomic<bool>` | Memory-order acquire/release |
| `packetCallback`          | *implicit*       | Set once before `connect()` starts the pump thread |

### Terrain Manager (`include/rendering/terrain_manager.hpp`)

| Variable              | Guard                    | Notes |
|-----------------------|--------------------------|-------|
| `loadQueue`, `readyQueue`, `pendingTiles` | `queueMutex` + `queueCV` | Workers wait; main signals on enqueue/finalize |
| `tileCache_`, `tileCacheLru_`, `tileCacheBytes_` | `tileCacheMutex_` | Read/write by both main and workers |
| `uploadedM2Ids_`      | `uploadedM2IdsMutex_`    | Workers check, main inserts on finalize |
| `preparedWmoUniqueIds_`| `preparedWmoUniqueIdsMutex_` | Workers only |
| `missingAdtWarnings_` | `missingAdtWarningsMutex_` | Workers only |
| `workerRunning`       | `std::atomic<bool>`      | — |
| `placedDoodadIds`, `placedWmoIds`, `loadedTiles`, `failedTiles` | MAIN-THREAD-ONLY | Only touched in processReadyTiles / unloadDistantTiles |

### Entity Manager (`include/game/entity.hpp`)

| Variable   | Guard            | Notes |
|------------|------------------|-------|
| `entities` | MAIN-THREAD-ONLY | All mutations via `dispatchQueuedPackets()` on main thread |

### Character Renderer (`include/rendering/character_renderer.hpp`)

| Variable                | Guard                     | Notes |
|------------------------|---------------------------|-------|
| `completedNormalMaps_` | `normalMapResultsMutex_`  | Detached threads push, main thread drains |
| `pendingNormalMapCount_`| `std::atomic<int>`       | acq_rel ordering |

### Logger (`include/core/logger.hpp`)

| Variable    | Guard           | Notes |
|-------------|-----------------|-------|
| `minLevel_` | `std::atomic<int>` | Fast path check in `shouldLog()` |
| `fileStream`, `lastMessage_`, suppression state | `mutex` | Locked in `log()` |

### Application (`src/core/application.cpp`)

| Variable                | Guard              | Notes |
|-------------------------|--------------------|-------|
| `watchdogHeartbeatMs`   | `std::atomic<int64_t>` | Main stores, watchdog loads |
| `watchdogRequestRelease`| `std::atomic<bool>` | Watchdog stores, main exchanges |
| `watchdogRunning`       | `std::atomic<bool>` | — |

---

## Conventions for New Code

1. **Prefer `std::shared_mutex`** for read-heavy caches.  Use `std::shared_lock`
   for lookups and `std::lock_guard<std::shared_mutex>` for mutations.

2. **Annotate shared state** at the declaration site with either
   `// THREAD-SAFE: protected by <mutex_name>` or `// MAIN-THREAD-ONLY`.

3. **Keep lock scope minimal.**  Copy data under the lock, then process outside.

4. **Avoid detaching threads** when possible.  Prefer `std::async` with a
   `std::future` stored on the owning object so shutdown can wait for completion.

5. **Use `std::atomic` for counters and flags** that are read/written without
   other invariants (e.g. cache hit stats, boolean run flags).

6. **No lock-order inversions.**  Current order (most-outer first):
   `ioMutex_` → `callbackMutex_` → `queueMutex` → `cacheMutex`.

7. **ThreadSanitizer** — run periodically with `-fsanitize=thread` to catch
   regressions:
   ```bash
   cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" .. && make -j$(nproc)
   ```

---

## Known Limitations

* `EntityManager::entities` relies on the convention that all entity mutations
  happen on the main thread through `dispatchQueuedPackets()`.  There is no
  compile-time enforcement.  If a future change introduces direct entity
  modification from the network pump thread, a mutex must be added.

* `packetCallback` in `WorldSocket` is set once before `connect()` and
  never modified afterwards.  This is safe in practice but not formally
  synchronized — do not change the callback after `connect()`.

* `fileCacheMisses` is declared as `std::atomic<size_t>` for consistency but is
  currently never incremented; the actual miss count must be inferred from
  `fileCacheAccessCounter - fileCacheHits`.
