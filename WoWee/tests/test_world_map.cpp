// Tests for WorldMap data structures and coordinate math
// Updated to use new modular types from world_map_types.hpp
#include <catch_amalgamated.hpp>
#include "rendering/world_map/world_map_types.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>
#include <string>

using wowee::rendering::world_map::Zone;
using wowee::rendering::world_map::OverlayEntry;
using wowee::rendering::world_map::POI;

// ── MapPOI struct ────────────────────────────────────────────

TEST_CASE("POI default-constructed is zeroed", "[world_map]") {
    POI poi{};
    REQUIRE(poi.id == 0);
    REQUIRE(poi.importance == 0);
    REQUIRE(poi.iconType == 0);
    REQUIRE(poi.factionId == 0);
    REQUIRE(poi.wowX == 0.0f);
    REQUIRE(poi.wowY == 0.0f);
    REQUIRE(poi.wowZ == 0.0f);
    REQUIRE(poi.mapId == 0);
    REQUIRE(poi.name.empty());
    REQUIRE(poi.description.empty());
}

TEST_CASE("POI sorts by importance ascending", "[world_map]") {
    std::vector<POI> pois;

    POI capital;
    capital.id = 1;
    capital.importance = 3;
    capital.name = "Stormwind";
    pois.push_back(capital);

    POI town;
    town.id = 2;
    town.importance = 1;
    town.name = "Goldshire";
    pois.push_back(town);

    POI minor;
    minor.id = 3;
    minor.importance = 0;
    minor.name = "Mirror Lake";
    pois.push_back(minor);

    std::sort(pois.begin(), pois.end(), [](const POI& a, const POI& b) {
        return a.importance < b.importance;
    });

    REQUIRE(pois[0].name == "Mirror Lake");
    REQUIRE(pois[1].name == "Goldshire");
    REQUIRE(pois[2].name == "Stormwind");
}

// ── WorldMapZone struct ──────────────────────────────────────

TEST_CASE("Zone default-constructed is valid", "[world_map]") {
    Zone z{};
    REQUIRE(z.wmaID == 0);
    REQUIRE(z.areaID == 0);
    REQUIRE(z.areaName.empty());
    REQUIRE(z.bounds.locLeft == 0.0f);
    REQUIRE(z.bounds.locRight == 0.0f);
    REQUIRE(z.bounds.locTop == 0.0f);
    REQUIRE(z.bounds.locBottom == 0.0f);
    REQUIRE(z.displayMapID == 0);
    REQUIRE(z.parentWorldMapID == 0);
    REQUIRE(z.exploreBits.empty());
}

TEST_CASE("Zone areaID==0 identifies continent", "[world_map]") {
    Zone continent{};
    continent.areaID = 0;
    continent.wmaID = 10;
    continent.areaName = "Kalimdor";

    Zone zone{};
    zone.areaID = 440;
    zone.wmaID = 100;
    zone.areaName = "Tanaris";

    REQUIRE(continent.areaID == 0);
    REQUIRE(zone.areaID != 0);
}

// ── Coordinate projection logic ──────────────────────────────
// Replicate the UV projection formula from renderPosToMapUV for standalone testing.

static glm::vec2 computeMapUV(float wowX, float wowY,
                                float locLeft, float locRight,
                                float locTop, float locBottom,
                                bool isContinent) {
    float denom_h = locLeft - locRight;
    float denom_v = locTop - locBottom;
    if (std::abs(denom_h) < 0.001f || std::abs(denom_v) < 0.001f)
        return glm::vec2(0.5f, 0.5f);

    float u = (locLeft - wowX) / denom_h;
    float v = (locTop - wowY) / denom_v;

    if (isContinent) {
        constexpr float kVOffset = -0.15f;
        v = (v - 0.5f) + 0.5f + kVOffset;
    }
    return glm::vec2(u, v);
}

TEST_CASE("UV projection: center of zone maps to (0.5, 0.5)", "[world_map]") {
    // Zone bounds: left=1000, right=0, top=1000, bottom=0
    float centerX = 500.0f, centerY = 500.0f;
    glm::vec2 uv = computeMapUV(centerX, centerY, 1000.0f, 0.0f, 1000.0f, 0.0f, false);
    REQUIRE(uv.x == Catch::Approx(0.5f).margin(0.001f));
    REQUIRE(uv.y == Catch::Approx(0.5f).margin(0.001f));
}

TEST_CASE("UV projection: top-left corner maps to (0, 0)", "[world_map]") {
    glm::vec2 uv = computeMapUV(1000.0f, 1000.0f, 1000.0f, 0.0f, 1000.0f, 0.0f, false);
    REQUIRE(uv.x == Catch::Approx(0.0f).margin(0.001f));
    REQUIRE(uv.y == Catch::Approx(0.0f).margin(0.001f));
}

TEST_CASE("UV projection: bottom-right corner maps to (1, 1)", "[world_map]") {
    glm::vec2 uv = computeMapUV(0.0f, 0.0f, 1000.0f, 0.0f, 1000.0f, 0.0f, false);
    REQUIRE(uv.x == Catch::Approx(1.0f).margin(0.001f));
    REQUIRE(uv.y == Catch::Approx(1.0f).margin(0.001f));
}

TEST_CASE("UV projection: degenerate bounds returns center", "[world_map]") {
    // left == right → degenerate
    glm::vec2 uv = computeMapUV(500.0f, 500.0f, 500.0f, 500.0f, 1000.0f, 0.0f, false);
    REQUIRE(uv.x == Catch::Approx(0.5f));
    REQUIRE(uv.y == Catch::Approx(0.5f));
}

TEST_CASE("UV projection: continent mode applies vertical offset", "[world_map]") {
    // Same center point, but continent mode shifts V by kVOffset=-0.15
    glm::vec2 uvZone = computeMapUV(500.0f, 500.0f, 1000.0f, 0.0f, 1000.0f, 0.0f, false);
    glm::vec2 uvCont = computeMapUV(500.0f, 500.0f, 1000.0f, 0.0f, 1000.0f, 0.0f, true);

    REQUIRE(uvZone.x == Catch::Approx(uvCont.x).margin(0.001f));
    // Continent V should be shifted by -0.15
    REQUIRE(uvCont.y == Catch::Approx(uvZone.y - 0.15f).margin(0.001f));
}

// ── Expansion level derivation ───────────────────────────────
// Replicate the expansion detection logic from getExpansionLevel.

static int deriveExpansionLevel(int maxLevel) {
    if (maxLevel <= 60) return 0;   // vanilla
    if (maxLevel <= 70) return 1;   // TBC
    return 2;                        // WotLK
}

TEST_CASE("Expansion level from maxLevel", "[world_map]") {
    REQUIRE(deriveExpansionLevel(60) == 0);   // vanilla
    REQUIRE(deriveExpansionLevel(58) == 0);   // below vanilla cap
    REQUIRE(deriveExpansionLevel(70) == 1);   // TBC
    REQUIRE(deriveExpansionLevel(65) == 1);   // mid TBC range
    REQUIRE(deriveExpansionLevel(80) == 2);   // WotLK
    REQUIRE(deriveExpansionLevel(75) == 2);   // mid WotLK range
}

// ── Expansion continent filtering ────────────────────────────

static std::vector<uint32_t> filterContinentsByExpansion(
    const std::vector<uint32_t>& mapIds, int expansionLevel) {
    std::vector<uint32_t> result;
    for (uint32_t id : mapIds) {
        if (id == 530 && expansionLevel < 1) continue;
        if (id == 571 && expansionLevel < 2) continue;
        result.push_back(id);
    }
    return result;
}

TEST_CASE("Vanilla hides TBC and WotLK continents", "[world_map]") {
    std::vector<uint32_t> all = {0, 1, 530, 571};
    auto filtered = filterContinentsByExpansion(all, 0);
    REQUIRE(filtered.size() == 2);
    REQUIRE(filtered[0] == 0);
    REQUIRE(filtered[1] == 1);
}

TEST_CASE("TBC shows Outland but hides Northrend", "[world_map]") {
    std::vector<uint32_t> all = {0, 1, 530, 571};
    auto filtered = filterContinentsByExpansion(all, 1);
    REQUIRE(filtered.size() == 3);
    REQUIRE(filtered[2] == 530);
}

TEST_CASE("WotLK shows all continents", "[world_map]") {
    std::vector<uint32_t> all = {0, 1, 530, 571};
    auto filtered = filterContinentsByExpansion(all, 2);
    REQUIRE(filtered.size() == 4);
}

// ── POI faction coloring logic ───────────────────────────────

enum class Faction { Alliance, Horde, Neutral };

static Faction classifyFaction(uint32_t factionId) {
    if (factionId == 469) return Faction::Alliance;
    if (factionId == 67) return Faction::Horde;
    return Faction::Neutral;
}

TEST_CASE("POI faction classification", "[world_map]") {
    REQUIRE(classifyFaction(469) == Faction::Alliance);
    REQUIRE(classifyFaction(67) == Faction::Horde);
    REQUIRE(classifyFaction(0) == Faction::Neutral);
    REQUIRE(classifyFaction(35) == Faction::Neutral);
}

// ── Overlay entry defaults ───────────────────────────────────

TEST_CASE("OverlayEntry defaults", "[world_map]") {
    OverlayEntry ov{};
    for (int i = 0; i < 4; i++) {
        REQUIRE(ov.areaIDs[i] == 0);
    }
    REQUIRE(ov.textureName.empty());
    REQUIRE(ov.texWidth == 0);
    REQUIRE(ov.texHeight == 0);
    REQUIRE(ov.offsetX == 0);
    REQUIRE(ov.offsetY == 0);
    REQUIRE(ov.hitRectLeft == 0);
    REQUIRE(ov.hitRectRight == 0);
    REQUIRE(ov.hitRectTop == 0);
    REQUIRE(ov.hitRectBottom == 0);
    REQUIRE(ov.tileCols == 0);
    REQUIRE(ov.tileRows == 0);
    REQUIRE(ov.tilesLoaded == false);
}

// ── ZMP pixel-map zone lookup ────────────────────────────────

TEST_CASE("ZMP grid lookup resolves mouse UV to zone", "[world_map]") {
    // Simulate a 128x128 ZMP grid with a zone at a known cell
    std::array<uint32_t, 128 * 128> grid{};
    uint32_t testAreaId = 42;
    // Place area ID at grid cell (64, 64) — center of map
    grid[64 * 128 + 64] = testAreaId;

    // Mouse at UV (0.5, 0.5) → col=64, row=64
    float mu = 0.5f, mv = 0.5f;
    constexpr int ZMP_SIZE = 128;
    int col = std::clamp(static_cast<int>(mu * ZMP_SIZE), 0, ZMP_SIZE - 1);
    int row = std::clamp(static_cast<int>(mv * ZMP_SIZE), 0, ZMP_SIZE - 1);
    uint32_t areaId = grid[row * ZMP_SIZE + col];
    REQUIRE(areaId == testAreaId);
}

TEST_CASE("ZMP grid returns 0 for empty cells", "[world_map]") {
    std::array<uint32_t, 128 * 128> grid{};
    // Empty grid — all cells zero (ocean/no zone)
    constexpr int ZMP_SIZE = 128;
    int col = 10, row = 10;
    REQUIRE(grid[row * ZMP_SIZE + col] == 0);
}

TEST_CASE("ZMP grid clamps out-of-range UV", "[world_map]") {
    std::array<uint32_t, 128 * 128> grid{};
    grid[0] = 100;           // (0,0) cell
    grid[127 * 128 + 127] = 200;  // (127,127) cell

    constexpr int ZMP_SIZE = 128;
    // UV at (-0.1, -0.1) should clamp to (0, 0)
    float mu = -0.1f, mv = -0.1f;
    int col = std::clamp(static_cast<int>(mu * ZMP_SIZE), 0, ZMP_SIZE - 1);
    int row = std::clamp(static_cast<int>(mv * ZMP_SIZE), 0, ZMP_SIZE - 1);
    REQUIRE(grid[row * ZMP_SIZE + col] == 100);

    // UV at (1.5, 1.5) should clamp to (127, 127)
    mu = 1.5f; mv = 1.5f;
    col = std::clamp(static_cast<int>(mu * ZMP_SIZE), 0, ZMP_SIZE - 1);
    row = std::clamp(static_cast<int>(mv * ZMP_SIZE), 0, ZMP_SIZE - 1);
    REQUIRE(grid[row * ZMP_SIZE + col] == 200);
}

// ── HitRect overlay AABB pre-filter ──────────────────────────

TEST_CASE("HitRect filters overlays correctly", "[world_map]") {
    OverlayEntry ov{};
    ov.hitRectLeft = 100;
    ov.hitRectRight = 300;
    ov.hitRectTop = 50;
    ov.hitRectBottom = 200;
    ov.texWidth = 200;
    ov.texHeight = 150;
    ov.textureName = "Goldshire";

    bool hasHitRect = (ov.hitRectRight > ov.hitRectLeft &&
                       ov.hitRectBottom > ov.hitRectTop);
    REQUIRE(hasHitRect);

    // Point inside HitRect
    float px = 150.0f, py = 100.0f;
    bool inside = (px >= ov.hitRectLeft && px <= ov.hitRectRight &&
                   py >= ov.hitRectTop && py <= ov.hitRectBottom);
    REQUIRE(inside);

    // Point outside HitRect
    px = 50.0f; py = 25.0f;
    inside = (px >= ov.hitRectLeft && px <= ov.hitRectRight &&
              py >= ov.hitRectTop && py <= ov.hitRectBottom);
    REQUIRE_FALSE(inside);
}

TEST_CASE("HitRect with zero values falls back to offset AABB", "[world_map]") {
    OverlayEntry ov{};
    // HitRect fields all zero → hasHitRect should be false
    bool hasHitRect = (ov.hitRectRight > ov.hitRectLeft &&
                       ov.hitRectBottom > ov.hitRectTop);
    REQUIRE_FALSE(hasHitRect);
}

TEST_CASE("Subzone hover with HitRect picks smallest overlay", "[world_map]") {
    // Simulate two overlays — one large with HitRect, one small with HitRect
    struct TestHitOverlay {
        float hitLeft, hitRight, hitTop, hitBottom;
        float texW, texH;
        std::string name;
    };

    TestHitOverlay large{0.0f, 500.0f, 0.0f, 400.0f, 500.0f, 400.0f, "BigArea"};
    TestHitOverlay small{100.0f, 250.0f, 80.0f, 180.0f, 150.0f, 100.0f, "SmallArea"};
    std::vector<TestHitOverlay> overlays = {large, small};

    float px = 150.0f, py = 120.0f;  // Inside both HitRects
    std::string best;
    float bestArea = std::numeric_limits<float>::max();
    for (const auto& ov : overlays) {
        bool inside = (px >= ov.hitLeft && px <= ov.hitRight &&
                       py >= ov.hitTop && py <= ov.hitBottom);
        if (inside) {
            float area = ov.texW * ov.texH;
            if (area < bestArea) {
                bestArea = area;
                best = ov.name;
            }
        }
    }
    REQUIRE(best == "SmallArea");
}

// ── Cosmic view expansion logic ──────────────────────────────

struct CosmicMapEntry {
    int mapId = 0;
    std::string label;
};

static std::vector<CosmicMapEntry> buildCosmicMaps(int expLevel) {
    std::vector<CosmicMapEntry> maps;
    if (expLevel == 0) return maps;  // Vanilla: no cosmic
    maps.push_back({0, "Azeroth"});
    if (expLevel >= 1) maps.push_back({530, "Outland"});
    if (expLevel >= 2) maps.push_back({571, "Northrend"});
    return maps;
}

TEST_CASE("Vanilla has no cosmic view entries", "[world_map]") {
    auto maps = buildCosmicMaps(0);
    REQUIRE(maps.empty());
}

TEST_CASE("TBC cosmic view has Azeroth + Outland", "[world_map]") {
    auto maps = buildCosmicMaps(1);
    REQUIRE(maps.size() == 2);
    REQUIRE(maps[0].mapId == 0);
    REQUIRE(maps[0].label == "Azeroth");
    REQUIRE(maps[1].mapId == 530);
    REQUIRE(maps[1].label == "Outland");
}

TEST_CASE("WotLK cosmic view has all three worlds", "[world_map]") {
    auto maps = buildCosmicMaps(2);
    REQUIRE(maps.size() == 3);
    REQUIRE(maps[2].mapId == 571);
    REQUIRE(maps[2].label == "Northrend");
}

// ── Subzone hover priority (smallest overlay wins) ───────────

struct TestOverlay {
    float offsetX, offsetY;
    float width, height;
    std::string name;
};

static std::string findSmallestOverlay(const std::vector<TestOverlay>& overlays,
                                        float mu, float mv, float fboW, float fboH) {
    std::string best;
    float bestArea = std::numeric_limits<float>::max();
    for (const auto& ov : overlays) {
        float ovLeft   = ov.offsetX / fboW;
        float ovTop    = ov.offsetY / fboH;
        float ovRight  = (ov.offsetX + ov.width) / fboW;
        float ovBottom = (ov.offsetY + ov.height) / fboH;
        if (mu >= ovLeft && mu <= ovRight && mv >= ovTop && mv <= ovBottom) {
            float area = ov.width * ov.height;
            if (area < bestArea) {
                bestArea = area;
                best = ov.name;
            }
        }
    }
    return best;
}

TEST_CASE("Subzone hover returns smallest overlapping overlay", "[world_map]") {
    // Large overlay covers 0-512, 0-512
    TestOverlay large{0.0f, 0.0f, 512.0f, 512.0f, "BigZone"};
    // Small overlay covers 100-200, 100-200
    TestOverlay small{100.0f, 100.0f, 100.0f, 100.0f, "SmallSubzone"};

    std::vector<TestOverlay> overlays = {large, small};

    // Mouse at UV (0.15, 0.15) → pixel (153.6, 115.2) for 1024x768 FBO
    // Both overlays overlap; small should win
    std::string result = findSmallestOverlay(overlays, 0.15f, 0.15f, 1024.0f, 768.0f);
    REQUIRE(result == "SmallSubzone");
}

TEST_CASE("Subzone hover returns only overlay when one matches", "[world_map]") {
    TestOverlay large{0.0f, 0.0f, 512.0f, 512.0f, "BigZone"};
    TestOverlay small{100.0f, 100.0f, 100.0f, 100.0f, "SmallSubzone"};
    std::vector<TestOverlay> overlays = {large, small};

    // Mouse at UV (0.01, 0.01) → only large matches
    std::string result = findSmallestOverlay(overlays, 0.01f, 0.01f, 1024.0f, 768.0f);
    REQUIRE(result == "BigZone");
}

TEST_CASE("Subzone hover returns empty when nothing matches", "[world_map]") {
    TestOverlay ov{100.0f, 100.0f, 50.0f, 50.0f, "Tiny"};
    std::vector<TestOverlay> overlays = {ov};

    std::string result = findSmallestOverlay(overlays, 0.01f, 0.01f, 1024.0f, 768.0f);
    REQUIRE(result.empty());
}

// ── Zone metadata (level range + faction) ────────────────────

enum class TestFaction { Neutral, Alliance, Horde, Contested };

struct TestZoneMeta {
    uint8_t minLevel = 0, maxLevel = 0;
    TestFaction faction = TestFaction::Neutral;
};

static std::string formatZoneLabel(const std::string& name, const TestZoneMeta* meta) {
    std::string label = name;
    if (meta) {
        if (meta->minLevel > 0 && meta->maxLevel > 0) {
            label += " (" + std::to_string(meta->minLevel) + "-" +
                     std::to_string(meta->maxLevel) + ")";
        }
        switch (meta->faction) {
            case TestFaction::Alliance:  label += " [Alliance]"; break;
            case TestFaction::Horde:     label += " [Horde]"; break;
            case TestFaction::Contested: label += " [Contested]"; break;
            default: break;
        }
    }
    return label;
}

TEST_CASE("Zone label includes level range and faction", "[world_map]") {
    TestZoneMeta meta{1, 10, TestFaction::Alliance};
    std::string label = formatZoneLabel("Elwynn", &meta);
    REQUIRE(label == "Elwynn (1-10) [Alliance]");
}

TEST_CASE("Zone label shows Contested faction", "[world_map]") {
    TestZoneMeta meta{30, 45, TestFaction::Contested};
    std::string label = formatZoneLabel("Stranglethorn", &meta);
    REQUIRE(label == "Stranglethorn (30-45) [Contested]");
}

TEST_CASE("Zone label without metadata is just the name", "[world_map]") {
    std::string label = formatZoneLabel("UnknownZone", nullptr);
    REQUIRE(label == "UnknownZone");
}

TEST_CASE("Zone label with Neutral faction omits tag", "[world_map]") {
    TestZoneMeta meta{55, 60, TestFaction::Neutral};
    std::string label = formatZoneLabel("Moonglade", &meta);
    REQUIRE(label == "Moonglade (55-60)");
}
