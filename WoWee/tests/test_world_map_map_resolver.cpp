// Tests for the map_resolver module — centralized map navigation resolution.
#include <catch_amalgamated.hpp>
#include "rendering/world_map/map_resolver.hpp"
#include "rendering/world_map/world_map_types.hpp"

using namespace wowee::rendering::world_map;

// ── Helper: build minimal zones for testing ──────────────────

static Zone makeZone(uint32_t wmaID, uint32_t areaID,
                     const std::string& name = "",
                     uint32_t displayMapID = 0,
                     uint32_t parentWorldMapID = 0) {
    Zone z;
    z.wmaID = wmaID;
    z.areaID = areaID;
    z.areaName = name;
    z.displayMapID = displayMapID;
    z.parentWorldMapID = parentWorldMapID;
    return z;
}

// Build zone list mimicking Azeroth (mapID=0) with root + leaf continents
static std::vector<Zone> buildAzerothZones() {
    std::vector<Zone> zones;
    // [0] Root continent (areaID=0, has children → isRootContinent)
    zones.push_back(makeZone(1, 0, "Azeroth", 0, 0));
    // [1] Leaf continent for EK (areaID=0, parentWorldMapID=1 → child of root)
    zones.push_back(makeZone(2, 0, "EasternKingdoms", 0, 1));
    // [2] Leaf continent for Kalimdor (shouldn't exist on mapID=0, but for testing)
    zones.push_back(makeZone(3, 0, "Kalimdor", 1, 1));
    // [3] Regular zone
    zones.push_back(makeZone(10, 40, "Westfall", 0, 2));
    // [4] Regular zone
    zones.push_back(makeZone(11, 44, "Redridge", 0, 2));
    return zones;
}

// Build zone list with only one continent (no leaf/root distinction)
static std::vector<Zone> buildSimpleZones() {
    std::vector<Zone> zones;
    // [0] Single continent entry
    zones.push_back(makeZone(1, 0, "Kalimdor", 1, 0));
    // [1] Zone
    zones.push_back(makeZone(10, 331, "Ashenvale", 1, 1));
    // [2] Zone
    zones.push_back(makeZone(11, 400, "ThousandNeedles", 1, 1));
    return zones;
}

// ── mapIdToFolder / folderToMapId / mapDisplayName ───────────

TEST_CASE("mapIdToFolder: known continent IDs",
          "[world_map][map_resolver]") {
    REQUIRE(std::string(mapIdToFolder(0))   == "Azeroth");
    REQUIRE(std::string(mapIdToFolder(1))   == "Kalimdor");
    REQUIRE(std::string(mapIdToFolder(530)) == "Expansion01");
    REQUIRE(std::string(mapIdToFolder(571)) == "Northrend");
}

TEST_CASE("mapIdToFolder: special views",
          "[world_map][map_resolver]") {
    REQUIRE(std::string(mapIdToFolder(UINT32_MAX))     == "World");
    REQUIRE(std::string(mapIdToFolder(UINT32_MAX - 1)) == "Cosmic");
}

TEST_CASE("mapIdToFolder: unknown returns empty",
          "[world_map][map_resolver]") {
    REQUIRE(std::string(mapIdToFolder(9999)) == "");
}

TEST_CASE("folderToMapId: case-insensitive lookup",
          "[world_map][map_resolver]") {
    REQUIRE(folderToMapId("Azeroth")   == 0);
    REQUIRE(folderToMapId("azeroth")   == 0);
    REQUIRE(folderToMapId("KALIMDOR")  == 1);
    REQUIRE(folderToMapId("Northrend") == 571);
    REQUIRE(folderToMapId("world")     == static_cast<int>(UINT32_MAX));
    REQUIRE(folderToMapId("unknown")   == -1);
}

TEST_CASE("mapDisplayName: returns UI labels",
          "[world_map][map_resolver]") {
    REQUIRE(std::string(mapDisplayName(0))   == "Eastern Kingdoms");
    REQUIRE(std::string(mapDisplayName(1))   == "Kalimdor");
    REQUIRE(std::string(mapDisplayName(571)) == "Northrend");
    REQUIRE(mapDisplayName(9999) == nullptr);
}

// ── findContinentForMapId ────────────────────────────────────

TEST_CASE("findContinentForMapId: prefers leaf continent with matching displayMapID",
          "[world_map][map_resolver]") {
    auto zones = buildAzerothZones();
    int idx = findContinentForMapId(zones, 0, -1);
    REQUIRE(idx == 1);
}

TEST_CASE("findContinentForMapId: finds leaf by displayMapID=1 for Kalimdor",
          "[world_map][map_resolver]") {
    auto zones = buildAzerothZones();
    int idx = findContinentForMapId(zones, 1, -1);
    REQUIRE(idx == 2);
}

TEST_CASE("findContinentForMapId: falls back to first non-root continent",
          "[world_map][map_resolver]") {
    auto zones = buildSimpleZones();
    int idx = findContinentForMapId(zones, 999, -1);
    REQUIRE(idx == 0);
}

TEST_CASE("findContinentForMapId: skips cosmic zone index",
          "[world_map][map_resolver]") {
    auto zones = buildAzerothZones();
    int idx = findContinentForMapId(zones, 0, 1);
    REQUIRE(idx == 2);
}

TEST_CASE("findContinentForMapId: returns -1 for empty zones",
          "[world_map][map_resolver]") {
    std::vector<Zone> empty;
    int idx = findContinentForMapId(empty, 0, -1);
    REQUIRE(idx == -1);
}

// ── resolveWorldRegionClick ──────────────────────────────────

TEST_CASE("resolveWorldRegionClick: same map returns NAVIGATE_CONTINENT",
          "[world_map][map_resolver]") {
    auto zones = buildAzerothZones();
    auto result = resolveWorldRegionClick(0, zones, 0, -1);
    REQUIRE(result.action == MapResolveAction::NAVIGATE_CONTINENT);
    REQUIRE(result.targetZoneIdx == 1);
}

TEST_CASE("resolveWorldRegionClick: different map returns LOAD_MAP",
          "[world_map][map_resolver]") {
    auto zones = buildAzerothZones();
    auto result = resolveWorldRegionClick(1, zones, 0, -1);
    REQUIRE(result.action == MapResolveAction::LOAD_MAP);
    REQUIRE(result.targetMapName == "Kalimdor");
}

TEST_CASE("resolveWorldRegionClick: Northrend from Azeroth returns LOAD_MAP",
          "[world_map][map_resolver]") {
    auto zones = buildAzerothZones();
    auto result = resolveWorldRegionClick(571, zones, 0, -1);
    REQUIRE(result.action == MapResolveAction::LOAD_MAP);
    REQUIRE(result.targetMapName == "Northrend");
}

TEST_CASE("resolveWorldRegionClick: unknown mapId returns NONE",
          "[world_map][map_resolver]") {
    auto zones = buildAzerothZones();
    auto result = resolveWorldRegionClick(9999, zones, 0, -1);
    REQUIRE(result.action == MapResolveAction::NONE);
}

// ── resolveZoneClick ─────────────────────────────────────────

TEST_CASE("resolveZoneClick: normal zone returns ENTER_ZONE",
          "[world_map][map_resolver]") {
    auto zones = buildAzerothZones();
    auto result = resolveZoneClick(3, zones, 0);
    REQUIRE(result.action == MapResolveAction::ENTER_ZONE);
    REQUIRE(result.targetZoneIdx == 3);
}

TEST_CASE("resolveZoneClick: zone with different displayMapID returns LOAD_MAP",
          "[world_map][map_resolver]") {
    std::vector<Zone> zones;
    zones.push_back(makeZone(1, 0, "Azeroth", 0, 0));
    zones.push_back(makeZone(50, 100, "DarkPortal", 530, 1));

    auto result = resolveZoneClick(1, zones, 0);
    REQUIRE(result.action == MapResolveAction::LOAD_MAP);
    REQUIRE(result.targetMapName == "Expansion01");
}

TEST_CASE("resolveZoneClick: zone with displayMapID matching current returns ENTER_ZONE",
          "[world_map][map_resolver]") {
    auto zones = buildSimpleZones();
    auto result = resolveZoneClick(1, zones, 1);
    REQUIRE(result.action == MapResolveAction::ENTER_ZONE);
    REQUIRE(result.targetZoneIdx == 1);
}

TEST_CASE("resolveZoneClick: out of range returns NONE",
          "[world_map][map_resolver]") {
    auto zones = buildAzerothZones();
    auto result = resolveZoneClick(-1, zones, 0);
    REQUIRE(result.action == MapResolveAction::NONE);

    result = resolveZoneClick(99, zones, 0);
    REQUIRE(result.action == MapResolveAction::NONE);
}

// ── resolveCosmicClick ───────────────────────────────────────

TEST_CASE("resolveCosmicClick: returns LOAD_MAP for known mapId",
          "[world_map][map_resolver]") {
    auto result = resolveCosmicClick(530);
    REQUIRE(result.action == MapResolveAction::LOAD_MAP);
    REQUIRE(result.targetMapName == "Expansion01");
}

TEST_CASE("resolveCosmicClick: returns NONE for unknown mapId",
          "[world_map][map_resolver]") {
    auto result = resolveCosmicClick(9999);
    REQUIRE(result.action == MapResolveAction::NONE);
}
