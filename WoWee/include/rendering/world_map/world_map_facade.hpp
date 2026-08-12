// world_map_facade.hpp — Public API for the world map system.
// Drop-in replacement for the monolithic WorldMap class (Phase 10 of refactoring plan).
// Facade pattern — hides internal complexity behind the same public interface.
#pragma once

#include "rendering/world_map/world_map_types.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>

namespace wowee {
namespace rendering {
class VkContext;
}
namespace pipeline { class AssetManager; }
namespace rendering {
namespace world_map {

class WorldMapFacade {
public:
    /// Backward-compatible alias for old WorldMap::QuestPoi usage.
    using QuestPoi = QuestPOI;

    WorldMapFacade();
    ~WorldMapFacade();

    bool initialize(VkContext* ctx, pipeline::AssetManager* am);
    void shutdown();

    /// Off-screen composite pass — call BEFORE the main render pass begins.
    void compositePass(VkCommandBuffer cmd);

    /// ImGui overlay — call INSIDE the main render pass (during ImGui frame).
    void render(const glm::vec3& playerRenderPos,
                int screenWidth, int screenHeight,
                float playerYawDeg = 0.0f);

    void setMapName(const std::string& name);
    void setServerExplorationMask(const std::vector<uint32_t>& masks, bool hasData);
    /// The zone the server says the player is in. Used in preference to working
    /// it out from map geometry, which can only guess between overlapping zones.
    void setPlayerZoneId(uint32_t zoneId);
    void setPartyDots(std::vector<PartyDot> dots);
    void setTaxiNodes(std::vector<TaxiNode> nodes);
    void setQuestPois(std::vector<QuestPOI> pois);
    void setCorpsePos(bool hasCorpse, glm::vec3 renderPos);
    /// Nearby rare/rare-elite creatures currently spawned near the player.
    void setRares(std::vector<RareMark> rares);
    /// Nearby chest-type game objects currently spawned near the player.
    void setChests(std::vector<ChestMark> chests);

    bool isOpen() const;
    void close();

    /// Flight-map (taxi selection) mode — opens the map locked to the player's
    /// continent with interactive flight nodes (see TaxiNodeLayer flight-map
    /// rendering). routeProvider maps a destination node id to the hop chain
    /// used to draw the route; onSelect fires with the chosen destination;
    /// onClose fires when the player dismisses the map (Escape / X).
    void openTaxiMap(std::function<std::vector<uint32_t>(uint32_t)> routeProvider,
                     std::function<void(uint32_t)> onSelect,
                     std::function<void()> onClose);
    /// Silent close (no onClose callback) — used when the game state already
    /// closed the flight master window.
    void closeTaxiMap();
    bool isTaxiMapOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace world_map
} // namespace rendering
} // namespace wowee

// Backward-compatible alias for gradual migration
namespace wowee {
namespace rendering {
using WorldMap = world_map::WorldMapFacade;
} // namespace rendering
} // namespace wowee
