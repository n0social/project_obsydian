// Tests for the extracted world map zone metadata module
#include <catch_amalgamated.hpp>
#include "rendering/world_map/zone_metadata.hpp"
#include "rendering/world_map/world_map_types.hpp"

#include <string>

using namespace wowee::rendering::world_map;

TEST_CASE("ZoneMetadata: find returns nullptr for unknown zone", "[world_map][zone_metadata]") {
    ZoneMetadata zm;
    zm.initialize();
    REQUIRE(zm.find("NonexistentZoneXYZ") == nullptr);
}

TEST_CASE("ZoneMetadata: find returns valid data for known zones", "[world_map][zone_metadata]") {
    ZoneMetadata zm;
    zm.initialize();

    const ZoneMeta* elwynn = zm.find("Elwynn");
    REQUIRE(elwynn != nullptr);
    REQUIRE(elwynn->minLevel > 0);
    REQUIRE(elwynn->maxLevel >= elwynn->minLevel);
    REQUIRE(elwynn->faction == ZoneFaction::Alliance);
}

TEST_CASE("ZoneMetadata: Contested zones", "[world_map][zone_metadata]") {
    ZoneMetadata zm;
    zm.initialize();

    const ZoneMeta* sTV = zm.find("StranglethornVale");
    REQUIRE(sTV != nullptr);
    REQUIRE(sTV->faction == ZoneFaction::Contested);
}

TEST_CASE("ZoneMetadata: Horde zones", "[world_map][zone_metadata]") {
    ZoneMetadata zm;
    zm.initialize();

    const ZoneMeta* durotar = zm.find("Durotar");
    REQUIRE(durotar != nullptr);
    REQUIRE(durotar->faction == ZoneFaction::Horde);
}

TEST_CASE("ZoneMetadata: formatLabel with no metadata", "[world_map][zone_metadata]") {
    std::string label = ZoneMetadata::formatLabel("UnknownZone", nullptr);
    REQUIRE(label == "UnknownZone");
}

TEST_CASE("ZoneMetadata: formatLabel with metadata", "[world_map][zone_metadata]") {
    ZoneMeta meta;
    meta.minLevel = 10;
    meta.maxLevel = 20;
    meta.faction = ZoneFaction::Alliance;

    std::string label = ZoneMetadata::formatLabel("Elwynn", &meta);
    // Should contain the zone name
    REQUIRE(label.find("Elwynn") != std::string::npos);
}

TEST_CASE("ZoneMetadata: formatHoverLabel with metadata", "[world_map][zone_metadata]") {
    ZoneMeta meta;
    meta.minLevel = 30;
    meta.maxLevel = 40;
    meta.faction = ZoneFaction::Contested;

    std::string label = ZoneMetadata::formatHoverLabel("StranglethornVale", &meta);
    // Should contain both zone name and level range
    REQUIRE(label.find("StranglethornVale") != std::string::npos);
    REQUIRE(label.find("30") != std::string::npos);
    REQUIRE(label.find("40") != std::string::npos);
}

TEST_CASE("ZoneMetadata: formatHoverLabel with no metadata just returns name", "[world_map][zone_metadata]") {
    std::string label = ZoneMetadata::formatHoverLabel("UnknownZone", nullptr);
    REQUIRE(label == "UnknownZone");
}

TEST_CASE("ZoneMetadata: double initialization is safe", "[world_map][zone_metadata]") {
    ZoneMetadata zm;
    zm.initialize();
    zm.initialize();  // should not crash or change data

    const ZoneMeta* elwynn = zm.find("Elwynn");
    REQUIRE(elwynn != nullptr);
}
