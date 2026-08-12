#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include <atomic>
#include <thread>
#include <cstdint>

namespace wowee {

namespace rendering { class Renderer; }
namespace pipeline { class AssetManager; class DBCLayout; }
namespace game { class GameHandler; class World; }
namespace addons { class AddonManager; }

namespace core {

class Application;
class EntitySpawner;
class AppearanceComposer;
class Window;

/// Handles terrain streaming, map transitions, world preloading,
/// and coordinate-aware tile management for online world entry.
class WorldLoader {
public:
    WorldLoader(Application& app,
                rendering::Renderer* renderer,
                pipeline::AssetManager* assetManager,
                game::GameHandler* gameHandler,
                EntitySpawner* entitySpawner,
                AppearanceComposer* appearanceComposer,
                Window* window,
                game::World* world,
                addons::AddonManager* addonManager);
    ~WorldLoader();

    // Main terrain loading — drives loading screen, WMO/ADT detection, player spawn
    void loadOnlineWorldTerrain(uint32_t mapId, float x, float y, float z);

    // Process deferred world entry (called from Application::update each frame)
    void processPendingEntry();

    // Map name utilities
    static const char* mapIdToName(uint32_t mapId);
    static int mapNameToId(const std::string& name);
    static const char* mapDisplayName(uint32_t mapId);

    // Background preloading — warms AssetManager file cache
    void startWorldPreload(uint32_t mapId, const std::string& mapName,
                           float serverX, float serverY);
    void cancelWorldPreload();

    // Persistent world info for session-to-session preloading
    void saveLastWorldInfo(uint32_t mapId, const std::string& mapName,
                           float serverX, float serverY);
    struct LastWorldInfo {
        uint32_t mapId = 0;
        std::string mapName;
        float x = 0, y = 0;
        bool valid = false;
    };
    LastWorldInfo loadLastWorldInfo() const;

    // State accessors
    uint32_t getLoadedMapId() const { return loadedMapId_; }
    bool isLoadingWorld() const { return loadingWorld_; }
    bool hasPendingEntry() const { return pendingWorldEntry_.has_value(); }

    // Get cached map name by ID (returns empty string if not found)
    std::string getMapNameById(uint32_t mapId) const {
        auto it = mapNameById_.find(mapId);
        return (it != mapNameById_.end()) ? it->second : std::string{};
    }

    // Set pending world entry for deferred processing via processPendingEntry()
    void setPendingEntry(uint32_t mapId, float x, float y, float z) {
        pendingWorldEntry_ = PendingWorldEntry{mapId, x, y, z};
    }

    // Reset methods (for logout / character switch)
    void resetLoadedMap() { loadedMapId_ = 0xFFFFFFFF; }
    void resetMapNameCache() { mapNameCacheLoaded_ = false; mapNameById_.clear(); }

private:
    Application& app_;
    rendering::Renderer* renderer_;
    pipeline::AssetManager* assetManager_;
    game::GameHandler* gameHandler_;
    EntitySpawner* entitySpawner_;
    AppearanceComposer* appearanceComposer_;
    Window* window_;
    game::World* world_;
    addons::AddonManager* addonManager_;

    uint32_t loadedMapId_ = 0xFFFFFFFF;  // Map ID of currently loaded terrain (0xFFFFFFFF = none)
    uint32_t worldLoadGeneration_ = 0;   // Incremented on each world entry to detect re-entrant loads
    bool loadingWorld_ = false;          // True while loadOnlineWorldTerrain is running

    struct PendingWorldEntry {
        uint32_t mapId; float x, y, z;
    };
    std::optional<PendingWorldEntry> pendingWorldEntry_;

    // Map.dbc name cache (loaded once per session)
    bool mapNameCacheLoaded_ = false;
    std::unordered_map<uint32_t, std::string> mapNameById_;

    // Background world preloader — warms AssetManager file cache for the
    // expected world before the user clicks Enter World.
    struct WorldPreload {
        uint32_t mapId = 0;
        std::string mapName;
        int centerTileX = 0;
        int centerTileY = 0;
        std::atomic<bool> cancel{false};
        std::vector<std::thread> workers;
    };
    std::unique_ptr<WorldPreload> worldPreload_;
};

} // namespace core
} // namespace wowee
