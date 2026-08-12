// Tests for the extracted world map exploration state module
#include <catch_amalgamated.hpp>
#include "rendering/world_map/exploration_state.hpp"
#include "rendering/world_map/world_map_types.hpp"

#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>

using namespace wowee::rendering::world_map;

static Zone makeZone(uint32_t wmaID, uint32_t areaID,
                     float locLeft, float locRight,
                     float locTop, float locBottom,
                     uint32_t parentWmaID = 0) {
    Zone z;
    z.wmaID = wmaID;
    z.areaID = areaID;
    z.bounds.locLeft = locLeft;
    z.bounds.locRight = locRight;
    z.bounds.locTop = locTop;
    z.bounds.locBottom = locBottom;
    z.parentWorldMapID = parentWmaID;
    return z;
}

TEST_CASE("ExplorationState: initially has no server mask", "[world_map][exploration_state]") {
    ExplorationState es;
    REQUIRE(es.hasServerMask() == false);
    REQUIRE(es.exploredZones().empty());
    REQUIRE(es.exploredOverlays().empty());
}

TEST_CASE("ExplorationState: setServerMask toggles hasServerMask", "[world_map][exploration_state]") {
    ExplorationState es;
    std::vector<uint32_t> mask = {0xFF, 0x00, 0x01};
    es.setServerMask(mask, true);
    REQUIRE(es.hasServerMask() == true);

    es.setServerMask({}, false);
    REQUIRE(es.hasServerMask() == false);
}

TEST_CASE("ExplorationState: overlaysChanged tracks changes", "[world_map][exploration_state]") {
    ExplorationState es;
    REQUIRE(es.overlaysChanged() == false);
}

TEST_CASE("ExplorationState: clearLocal resets local data", "[world_map][exploration_state]") {
    ExplorationState es;
    es.clearLocal();
    REQUIRE(es.exploredZones().empty());
}

TEST_CASE("ExplorationState: update with empty zones is safe", "[world_map][exploration_state]") {
    ExplorationState es;
    std::vector<Zone> zones;
    std::unordered_map<uint32_t, uint32_t> exploreFlagByAreaId;
    glm::vec3 pos(0.0f);

    es.update(zones, pos, -1, exploreFlagByAreaId);
    REQUIRE(es.exploredZones().empty());
}

TEST_CASE("ExplorationState: update with valid zone and server mask", "[world_map][exploration_state]") {
    ExplorationState es;

    std::vector<Zone> zones;
    auto z = makeZone(1, 100, 1000.0f, -1000.0f, 1000.0f, -1000.0f, 0);
    z.exploreBits.push_back(0);  // bit 0

    OverlayEntry ov;
    ov.areaIDs[0] = 100;
    z.overlays.push_back(ov);
    zones.push_back(z);

    // Set server mask with bit 0 set
    es.setServerMask({0x01}, true);

    std::unordered_map<uint32_t, uint32_t> exploreFlagByAreaId;
    exploreFlagByAreaId[100] = 0;  // AreaID 100 → explore bit 0

    glm::vec3 playerPos(0.0f, 0.0f, 0.0f);
    es.update(zones, playerPos, 0, exploreFlagByAreaId);

    // Zone should be explored since bit 0 is set in the mask
    REQUIRE(es.exploredZones().count(0) == 1);
}
