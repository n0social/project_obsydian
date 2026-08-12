// Tests for TransportPathRepository (Phase 3 of spline refactoring).
// Verifies PathEntry wrapping, path operations, and CatmullRomSpline integration.
#include <catch2/catch_amalgamated.hpp>
#include "game/transport_path_repository.hpp"
#include "pipeline/asset_manager.hpp"
#include "math/spline.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cmath>

// ── Minimal stubs for pipeline symbols referenced by DBC loading functions ──
// The test never calls loadTransportAnimationDBC / loadTaxiPathNodeDBC,
// but the linker still needs these symbols from the compiled translation unit.
namespace wowee::pipeline {
AssetManager::AssetManager() {}
AssetManager::~AssetManager() {}
std::vector<uint8_t> AssetManager::readFile(const std::string&) const { return {}; }
std::vector<uint8_t> AssetManager::readFileOptional(const std::string&) const { return {}; }
} // namespace wowee::pipeline

using namespace wowee;

// ── Helpers ────────────────────────────────────────────────────

static constexpr float kEps = 0.001f;

static void requireVec3Near(const glm::vec3& v, float x, float y, float z,
                             float eps = kEps) {
    REQUIRE(std::abs(v.x - x) < eps);
    REQUIRE(std::abs(v.y - y) < eps);
    REQUIRE(std::abs(v.z - z) < eps);
}

// ── PathEntry construction ─────────────────────────────────────

TEST_CASE("PathEntry wraps CatmullRomSpline with metadata", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {10.0f, 0.0f, 0.0f}},
        {2000, {20.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 42, false, true, false);

    REQUIRE(entry.pathId == 42);
    REQUIRE(entry.fromDBC == true);
    REQUIRE(entry.zOnly == false);
    REQUIRE(entry.worldCoords == false);
    REQUIRE(entry.spline.keyCount() == 3);
    REQUIRE(entry.spline.durationMs() == 2000);
}

TEST_CASE("PathEntry is move-constructible", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {500, {5.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 99, true, false, true);

    game::PathEntry moved(std::move(entry));
    REQUIRE(moved.pathId == 99);
    REQUIRE(moved.zOnly == true);
    REQUIRE(moved.worldCoords == true);
    REQUIRE(moved.spline.keyCount() == 2);
}

// ── Repository: loadPathFromNodes ──────────────────────────────

TEST_CASE("loadPathFromNodes stores a retrievable path", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    std::vector<glm::vec3> waypoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 0.0f},
        {200.0f, 0.0f, 0.0f},
    };

    repo.loadPathFromNodes(1001, waypoints, true, 10.0f);
    auto* entry = repo.findPath(1001);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->pathId == 1001);
    REQUIRE(entry->fromDBC == false);
    REQUIRE(entry->zOnly == false);
    // 3 waypoints + 1 wrap point = 4 keys
    REQUIRE(entry->spline.keyCount() == 4);
    REQUIRE(entry->spline.durationMs() > 0);
}

TEST_CASE("loadPathFromNodes single waypoint creates stationary path", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    repo.loadPathFromNodes(2001, {{5.0f, 6.0f, 7.0f}}, false, 10.0f);

    auto* entry = repo.findPath(2001);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->spline.keyCount() == 1);
    // Stationary: duration is the last key's time (0ms for single key)
    requireVec3Near(entry->spline.evaluatePosition(0), 5.0f, 6.0f, 7.0f);
}

TEST_CASE("loadPathFromNodes non-looping omits wrap point", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    std::vector<glm::vec3> waypoints = {
        {0.0f, 0.0f, 0.0f},
        {50.0f, 0.0f, 0.0f},
    };

    repo.loadPathFromNodes(3001, waypoints, false, 10.0f);
    auto* entry = repo.findPath(3001);
    REQUIRE(entry != nullptr);
    // Non-looping: 2 waypoints, no wrap point
    REQUIRE(entry->spline.keyCount() == 2);
}

TEST_CASE("loadPathFromNodes looping adds wrap point", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    std::vector<glm::vec3> waypoints = {
        {0.0f, 0.0f, 0.0f},
        {50.0f, 0.0f, 0.0f},
    };

    repo.loadPathFromNodes(3002, waypoints, true, 10.0f);
    auto* entry = repo.findPath(3002);
    REQUIRE(entry != nullptr);
    // Looping: 2 waypoints + 1 wrap point = 3 keys
    REQUIRE(entry->spline.keyCount() == 3);
    // Wrap point should match first waypoint
    const auto& keys = entry->spline.keys();
    requireVec3Near(keys.back().position, 0.0f, 0.0f, 0.0f);
}

// ── Repository: findPath / storePath / hasPathForEntry ──────────

TEST_CASE("findPath returns nullptr for missing paths", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    REQUIRE(repo.findPath(999) == nullptr);
}

TEST_CASE("storePath overwrites existing paths", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Store first path
    std::vector<math::SplineKey> keys1 = {{0, {0, 0, 0}}, {1000, {10, 0, 0}}};
    math::CatmullRomSpline spline1(std::move(keys1), false);
    repo.storePath(500, game::PathEntry(std::move(spline1), 500, false, true, false));

    auto* e1 = repo.findPath(500);
    REQUIRE(e1 != nullptr);
    REQUIRE(e1->spline.keyCount() == 2);

    // Overwrite with different path
    std::vector<math::SplineKey> keys2 = {{0, {0, 0, 0}}, {500, {5, 0, 0}}, {1000, {10, 5, 0}}};
    math::CatmullRomSpline spline2(std::move(keys2), false);
    repo.storePath(500, game::PathEntry(std::move(spline2), 500, true, false, true));

    auto* e2 = repo.findPath(500);
    REQUIRE(e2 != nullptr);
    REQUIRE(e2->spline.keyCount() == 3);
    REQUIRE(e2->zOnly == true);
    REQUIRE(e2->worldCoords == true);
}

TEST_CASE("hasPathForEntry checks fromDBC flag", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Non-DBC path
    repo.loadPathFromNodes(700, {{0, 0, 0}, {10, 0, 0}}, true, 10.0f);
    REQUIRE(repo.hasPathForEntry(700) == false);  // fromDBC=false

    // DBC path (via storePath)
    std::vector<math::SplineKey> keys = {{0, {0, 0, 0}}, {1000, {10, 0, 0}}};
    math::CatmullRomSpline spline(std::move(keys), false);
    repo.storePath(701, game::PathEntry(std::move(spline), 701, false, true, false));
    REQUIRE(repo.hasPathForEntry(701) == true);
}

TEST_CASE("loadPathFromNodes replaces stale runtime paths but preserves DBC paths",
          "[transport_path_repo][expansion]") {
    game::TransportPathRepository repo;

    repo.loadPathFromNodes(710, {{0, 0, 0}, {10, 0, 0}}, false, 10.0f);
    repo.loadPathFromNodes(710, {{0, 0, 0}, {20, 0, 0}, {40, 0, 0}}, false, 10.0f);
    REQUIRE(repo.findPath(710)->spline.keyCount() == 3);

    std::vector<math::SplineKey> dbcKeys = {
        {0, {0, 0, 0}}, {1000, {50, 0, 0}}
    };
    math::CatmullRomSpline dbcSpline(std::move(dbcKeys), false);
    repo.storePath(711, game::PathEntry(std::move(dbcSpline), 711, false, true, false));

    repo.loadPathFromNodes(711, {{1, 2, 3}}, false, 10.0f);
    const auto* preserved = repo.findPath(711);
    REQUIRE(preserved != nullptr);
    REQUIRE(preserved->fromDBC);
    REQUIRE(preserved->spline.keyCount() == 2);
}

// ── Repository: hasUsableMovingPathForEntry ─────────────────────

TEST_CASE("hasUsableMovingPathForEntry rejects stationary/z-only", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Single-point path (stationary)
    std::vector<math::SplineKey> keys1 = {{0, {0, 0, 0}}};
    math::CatmullRomSpline sp1(std::move(keys1), false);
    repo.storePath(800, game::PathEntry(std::move(sp1), 800, false, true, false));
    REQUIRE(repo.hasUsableMovingPathForEntry(800) == false);

    // Z-only path (flagged)
    std::vector<math::SplineKey> keys2 = {{0, {0, 0, 0}}, {1000, {0, 0, 5}}};
    math::CatmullRomSpline sp2(std::move(keys2), false);
    repo.storePath(801, game::PathEntry(std::move(sp2), 801, true, true, false));
    REQUIRE(repo.hasUsableMovingPathForEntry(801) == false);

    // Moving XY path
    std::vector<math::SplineKey> keys3 = {{0, {0, 0, 0}}, {1000, {100, 0, 0}}};
    math::CatmullRomSpline sp3(std::move(keys3), false);
    repo.storePath(802, game::PathEntry(std::move(sp3), 802, false, true, false));
    REQUIRE(repo.hasUsableMovingPathForEntry(802) == true);
}

// ── Repository: inferDbcPathForSpawn ────────────────────────────

TEST_CASE("inferDbcPathForSpawn finds nearest DBC path", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Path A at (100, 0, 0)
    std::vector<math::SplineKey> keysA = {{0, {100, 0, 0}}, {1000, {200, 0, 0}}};
    math::CatmullRomSpline spA(std::move(keysA), false);
    repo.storePath(10, game::PathEntry(std::move(spA), 10, false, true, false));

    // Path B at (500, 0, 0)
    std::vector<math::SplineKey> keysB = {{0, {500, 0, 0}}, {1000, {600, 0, 0}}};
    math::CatmullRomSpline spB(std::move(keysB), false);
    repo.storePath(20, game::PathEntry(std::move(spB), 20, false, true, false));

    // Spawn near Path A
    uint32_t result = repo.inferDbcPathForSpawn({105.0f, 0.0f, 0.0f}, 200.0f, true);
    REQUIRE(result == 10);

    // Spawn near Path B
    result = repo.inferDbcPathForSpawn({510.0f, 0.0f, 0.0f}, 200.0f, true);
    REQUIRE(result == 20);

    // Spawn too far from both
    result = repo.inferDbcPathForSpawn({9999.0f, 0.0f, 0.0f}, 200.0f, true);
    REQUIRE(result == 0);
}

TEST_CASE("inferMovingPathForSpawn skips z-only paths", "[transport_path_repo]") {
    game::TransportPathRepository repo;

    // Z-only path near spawn
    std::vector<math::SplineKey> keys = {{0, {10, 0, 0}}, {1000, {10, 0, 5}}};
    math::CatmullRomSpline sp(std::move(keys), false);
    repo.storePath(30, game::PathEntry(std::move(sp), 30, true, true, false));

    // inferMovingPathForSpawn passes allowZOnly=false
    uint32_t result = repo.inferMovingPathForSpawn({10.0f, 0.0f, 0.0f}, 200.0f);
    REQUIRE(result == 0);
}

TEST_CASE("night-elf ships never borrow unrelated TransportAnimation paths",
          "[transport_path_repo][transport][expansion]") {
    game::TransportPathRepository repo;
    std::vector<math::SplineKey> keys = {
        {0, {0, 0, 0}}, {1000, {100, 0, 0}}
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    repo.storePath(194675, game::PathEntry(std::move(spline), 194675, false, true, false));

    REQUIRE(repo.pickFallbackMovingPath(176244, 7087) == 0); // Moonspray
    REQUIRE(repo.pickFallbackMovingPath(181646, 7087) == 0); // Elune's Blessing
    REQUIRE(repo.pickFallbackMovingPath(177233, 7087) == 0); // shared model, other route
}

// ── Repository: taxi paths ─────────────────────────────────────

TEST_CASE("hasTaxiPath and findTaxiPath", "[transport_path_repo]") {
    game::TransportPathRepository repo;
    REQUIRE(repo.hasTaxiPath(100) == false);
    REQUIRE(repo.findTaxiPath(100, 0) == nullptr);

    // loadTaxiPathNodeDBC would populate this, but we can't test it without AssetManager.
    // This just verifies the API works with empty data.
}

// ── Spline evaluation through PathEntry (Phase 3 integration) ──

TEST_CASE("PathEntry spline evaluates position at midpoint", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {100.0f, 0.0f, 0.0f}},
        {2000, {200.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 1, false, true, false);

    // At t=1000ms, should be near (100, 0, 0) — exactly at key 1
    glm::vec3 pos = entry.spline.evaluatePosition(1000);
    requireVec3Near(pos, 100.0f, 0.0f, 0.0f);
}

TEST_CASE("PathEntry spline evaluates position at interpolated time", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {100.0f, 0.0f, 0.0f}},
        {2000, {200.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 1, false, true, false);

    // At t=500ms, should be approximately (50, 0, 0)
    glm::vec3 pos = entry.spline.evaluatePosition(500);
    REQUIRE(pos.x > 40.0f);
    REQUIRE(pos.x < 60.0f);
    REQUIRE(std::abs(pos.y) < 1.0f);
}

TEST_CASE("PathEntry spline evaluate returns tangent for orientation", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {100.0f, 0.0f, 0.0f}},
        {2000, {200.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 1, false, true, false);

    // Tangent at midpoint should point roughly in +X direction
    auto result = entry.spline.evaluate(1000);
    REQUIRE(result.tangent.x > 0.0f);
    REQUIRE(std::abs(result.tangent.y) < 1.0f);
}

TEST_CASE("PathEntry findNearestKey finds closest waypoint", "[transport_path_repo]") {
    std::vector<math::SplineKey> keys = {
        {0, {0.0f, 0.0f, 0.0f}},
        {1000, {100.0f, 0.0f, 0.0f}},
        {2000, {200.0f, 0.0f, 0.0f}},
    };
    math::CatmullRomSpline spline(std::move(keys), false);
    game::PathEntry entry(std::move(spline), 1, false, true, false);

    // Point near key 1 (100, 0, 0)
    size_t nearest = entry.spline.findNearestKey({105.0f, 0.0f, 0.0f});
    REQUIRE(nearest == 1);

    // Point near key 2 (200, 0, 0)
    nearest = entry.spline.findNearestKey({195.0f, 0.0f, 0.0f});
    REQUIRE(nearest == 2);
}

// ── buildTaxiSegmentSpline: wrap topology per route shape ──────

// Sample a spline densely and return the longest interval (ms) over which its position
// does not move — used to prove a ferry never parks dead (no stationary offshore hold).
static uint32_t longestStationaryMs(const math::CatmullRomSpline& s) {
    const uint32_t dur = s.durationMs();
    const uint32_t step = std::max<uint32_t>(1u, dur / 400u);
    uint32_t worst = 0, run = 0;
    glm::vec3 prev = s.evaluatePosition(0);
    for (uint32_t t = step; t <= dur; t += step) {
        const glm::vec3 cur = s.evaluatePosition(t);
        if (glm::distance(cur, prev) < 1.0f) { run += step; worst = std::max(worst, run); }
        else run = 0;
        prev = cur;
    }
    return worst;
}

TEST_CASE("buildTaxiSegmentSpline single-map shuttle sails back (position-closed)",
          "[transport_path_repo][taxi]") {
    // Dock at origin, open water 1000 units out. The boat must return under sail, so the
    // cycle ends where it began.
    std::vector<glm::vec3> pts = {
        {0.0f, 0.0f, 0.0f}, {400.0f, 0.0f, 0.0f}, {1000.0f, 0.0f, 0.0f},
    };
    std::vector<uint32_t> delays(pts.size(), 0u);
    auto spline = game::TransportPathRepository::buildTaxiSegmentSpline(pts, delays);

    // Position-closed: no teleport-snap at the modulo wrap.
    const glm::vec3 atStart = spline.evaluatePosition(0);
    const glm::vec3 atEnd = spline.evaluatePosition(spline.durationMs());
    requireVec3Near(atEnd, atStart.x, atStart.y, atStart.z, 0.5f);
    requireVec3Near(atStart, 0.0f, 0.0f, 0.0f, 0.5f);

    // Mid-cycle the hull is out at the far turnaround, i.e. it actually leaves the dock.
    const glm::vec3 mid = spline.evaluatePosition(spline.durationMs() / 2);
    REQUIRE(mid.x > 800.0f);
}

TEST_CASE("buildTaxiSegmentSpline continent slice ferries without parking offshore",
          "[transport_path_repo][taxi]") {
    // One map's slice of a cross-continent route: offshore-in -> dock -> offshore-out.
    // It must oscillate (there-and-back), reaching the far offshore node so the server's
    // handoff can fire there, and NEVER hold stationary at sea (the reported bug).
    std::vector<glm::vec3> pts = {
        {0.0f, 0.0f, 0.0f}, {500.0f, 0.0f, 0.0f}, {1000.0f, 0.0f, 0.0f},  // dock at 500
    };
    std::vector<uint32_t> delays(pts.size(), 0u);
    auto spline = game::TransportPathRepository::buildTaxiSegmentSpline(pts, delays);

    // Position-closed and reaches the far offshore node mid-cycle.
    const glm::vec3 atStart = spline.evaluatePosition(0);
    const glm::vec3 atEnd = spline.evaluatePosition(spline.durationMs());
    requireVec3Near(atEnd, atStart.x, atStart.y, atStart.z, 0.5f);
    REQUIRE(spline.evaluatePosition(spline.durationMs() / 2).x > 900.0f);

    // No dead offshore hold: with no authored dwell, the only near-stationary moment is
    // the brief deceleration as the hull U-turns at a node (a few seconds), nowhere near
    // the tens of seconds the old offshore hold parked it for.
    REQUIRE(longestStationaryMs(spline) < 5000u);
}

TEST_CASE("buildTaxiSegmentSpline honors an authored dock dwell but nothing longer",
          "[transport_path_repo][taxi]") {
    // A 60s dwell at the mid dock is the only stationary stretch; there is no offshore
    // hold anywhere near the ~2s the boat needs to sail each 500u leg.
    std::vector<glm::vec3> pts = {
        {0.0f, 0.0f, 0.0f}, {500.0f, 0.0f, 0.0f}, {1000.0f, 0.0f, 0.0f},
    };
    std::vector<uint32_t> delays = {0u, 60000u, 0u};
    auto spline = game::TransportPathRepository::buildTaxiSegmentSpline(pts, delays);

    const uint32_t stationary = longestStationaryMs(spline);
    REQUIRE(stationary >= 55000u);   // the authored dock dwell is preserved
    REQUIRE(stationary <= 65000u);   // and nothing parks longer than it
}

TEST_CASE("buildTaxiSegmentSpline closed loop returns to first node",
          "[transport_path_repo][taxi]") {
    // Endpoints coincide (within 60u): a ring that closes back to the start.
    std::vector<glm::vec3> pts = {
        {0.0f, 0.0f, 0.0f}, {300.0f, 0.0f, 0.0f}, {300.0f, 300.0f, 0.0f}, {10.0f, 5.0f, 0.0f},
    };
    std::vector<uint32_t> delays(pts.size(), 0u);
    auto spline = game::TransportPathRepository::buildTaxiSegmentSpline(pts, delays);

    const glm::vec3 atStart = spline.evaluatePosition(0);
    const glm::vec3 atEnd = spline.evaluatePosition(spline.durationMs());
    requireVec3Near(atEnd, atStart.x, atStart.y, atStart.z, 0.5f);
}

TEST_CASE("buildTaxiSegmentSpline waits at the pier for the rest of the route",
          "[transport_path_repo]") {
    // A cross-continent route is split into a slice per map, each animated on its
    // own. Sized from its own nodes alone, a slice's cycle is far shorter than the
    // server's, so the boat laps its shore several times before the transfer comes
    // due — the Kraken doing circuits instead of leaving Borean Tundra.
    //
    // The surplus is spent at the pier: visibly stopped, still boardable, and
    // never adrift offshore.
    std::vector<glm::vec3> pts = {
        glm::vec3(0.0f, 0.0f, 0.0f),        // offshore in
        glm::vec3(500.0f, 0.0f, 0.0f),      // the pier
        glm::vec3(1000.0f, 0.0f, 0.0f),     // offshore out
    };
    std::vector<uint32_t> delays = {0u, 20000u, 0u};

    const auto bare = game::TransportPathRepository::buildTaxiSegmentSpline(pts, delays);
    const uint32_t bareMs = bare.durationMs();

    // Whole route costs half again as much as this slice.
    const uint32_t fullCycleMs = bareMs + 90000u;
    const auto stretched =
        game::TransportPathRepository::buildTaxiSegmentSpline(pts, delays, 28.0f, fullCycleMs);

    REQUIRE(stretched.durationMs() == bareMs + 90000u);

    // The extra time is spent held at the pier, not adrift: the hull sits at the
    // dock node across the whole surplus rather than moving through it.
    const auto& keys = stretched.keys();
    uint32_t longestHold = 0;
    glm::vec3 holdAt(0.0f);
    for (size_t i = 1; i < keys.size(); ++i) {
        if (glm::distance(keys[i].position, keys[i - 1].position) > 0.01f) continue;
        const uint32_t held = keys[i].timeMs - keys[i - 1].timeMs;
        if (held > longestHold) { longestHold = held; holdAt = keys[i].position; }
    }
    REQUIRE(longestHold >= 90000u);
    REQUIRE(holdAt.x == Catch::Approx(500.0f));

    // A single-map route has no other slice to wait for and is left alone.
    const auto unchanged =
        game::TransportPathRepository::buildTaxiSegmentSpline(pts, delays, 28.0f, bareMs);
    REQUIRE(unchanged.durationMs() == bareMs);
}
