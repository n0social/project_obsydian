// tests/test_transport_components.cpp
// Unit tests for TransportClockSync and TransportAnimator (Phase 3 extractions).
#include <catch_amalgamated.hpp>
#include "game/transport_clock_sync.hpp"
#include "game/transport_animator.hpp"
#include "game/transport_manager.hpp"
#include "game/transport_path_repository.hpp"
#include "math/spline.hpp"
#include "core/coordinates.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

using namespace wowee::game;
using namespace wowee::math;

// ── Helper: build a simple circular path ──────────────────────────
static PathEntry makeCirclePath() {
    // Circle-ish path with 4 points, 4000ms duration
    std::vector<SplineKey> keys = {
        {0,    glm::vec3(0.0f,  0.0f, 0.0f)},
        {1000, glm::vec3(10.0f, 0.0f, 0.0f)},
        {2000, glm::vec3(10.0f, 10.0f, 0.0f)},
        {3000, glm::vec3(0.0f,  10.0f, 0.0f)},
        {4000, glm::vec3(0.0f,  0.0f, 0.0f)},
    };
    CatmullRomSpline spline(std::move(keys), /*timeClosed=*/true);
    return PathEntry(std::move(spline), /*pathId=*/100, /*zOnly=*/false, /*fromDBC=*/true, /*worldCoords=*/false);
}

// ── Helper: create a fresh ActiveTransport ────────────────────────
static ActiveTransport makeTransport(uint64_t guid = 1, uint32_t pathId = 100) {
    ActiveTransport t{};
    t.guid = guid;
    t.pathId = pathId;
    t.basePosition = glm::vec3(100.0f, 200.0f, 0.0f);
    t.position = glm::vec3(100.0f, 200.0f, 0.0f);
    t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    t.playerOnBoard = false;
    t.playerLocalOffset = glm::vec3(0);
    t.hasDeckBounds = false;
    t.localClockMs = 0;
    t.hasServerClock = false;
    t.serverClockOffsetMs = 0;
    t.useClientAnimation = true;
    t.clientAnimationReverse = false;
    t.serverYaw = 0.0f;
    t.hasServerYaw = false;
    t.dockYaw = 0.0f;
    t.hasDockYaw = false;
    t.serverYawFlipped180 = false;
    t.serverYawAlignmentScore = 0;
    t.lastServerUpdate = 0.0;
    t.serverUpdateCount = 0;
    t.serverLinearVelocity = glm::vec3(0);
    t.serverAngularVelocity = 0.0f;
    t.hasServerVelocity = false;
    t.allowBootstrapVelocity = true;
    t.isM2 = false;
    return t;
}

// ══════════════════════════════════════════════════════════════════
// TransportClockSync tests
// ══════════════════════════════════════════════════════════════════

TEST_CASE("ClockSync: client animation advances localClockMs", "[transport_clock_sync]") {
    TransportClockSync sync;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.useClientAnimation = true;
    t.hasServerClock = false;

    uint32_t pathTimeMs = 0;
    bool result = sync.computePathTime(t, path.spline, 1.0, 0.016f, pathTimeMs);
    REQUIRE(result);
    REQUIRE(t.localClockMs > 0);  // Should have advanced
    REQUIRE(pathTimeMs == t.localClockMs % path.spline.durationMs());
}

TEST_CASE("ClockSync: server clock mode wraps correctly", "[transport_clock_sync]") {
    TransportClockSync sync;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.hasServerClock = true;
    t.serverClockOffsetMs = 500;  // Server is 500ms ahead

    uint32_t pathTimeMs = 0;
    double elapsedTime = 3.7;  // 3700ms local → 4200ms server → 200ms wrapped (dur=4000)
    bool result = sync.computePathTime(t, path.spline, elapsedTime, 0.016f, pathTimeMs);
    REQUIRE(result);
    REQUIRE(pathTimeMs == 200);
}

TEST_CASE("ClockSync: strict server mode returns false", "[transport_clock_sync]") {
    TransportClockSync sync;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.useClientAnimation = false;
    t.hasServerClock = false;

    uint32_t pathTimeMs = 0;
    bool result = sync.computePathTime(t, path.spline, 1.0, 0.016f, pathTimeMs);
    REQUIRE_FALSE(result);
}

TEST_CASE("ClockSync: reverse client animation decrements", "[transport_clock_sync]") {
    TransportClockSync sync;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.useClientAnimation = true;
    t.clientAnimationReverse = true;
    t.localClockMs = 2000;

    uint32_t pathTimeMs = 0;
    bool result = sync.computePathTime(t, path.spline, 1.0, 0.5f, pathTimeMs);
    REQUIRE(result);
    // localClockMs should have decreased by ~500ms
    REQUIRE(t.localClockMs < 2000);
}

TEST_CASE("ClockSync: processServerUpdate sets yaw and rotation", "[transport_clock_sync]") {
    TransportClockSync sync;
    auto path = makeCirclePath();
    auto t = makeTransport();

    glm::vec3 pos(105.0f, 205.0f, 1.0f);
    float yaw = 1.5f;
    sync.processServerUpdate(t, &path, pos, yaw, 10.0);

    REQUIRE(t.serverUpdateCount == 1);
    REQUIRE(t.hasServerYaw);
    REQUIRE(t.serverYaw == Catch::Approx(1.5f));
    REQUIRE(t.position == pos);
}

// Server yaw s points along canonical (sin s, cos s) — see core/coordinates.hpp.
// So a transport travelling along canonical +X is facing its direction of travel
// at s = +pi/2, and facing exactly backwards at s = -pi/2.
static constexpr float kServerYawAlongCanonicalX = glm::half_pi<float>();

// Every transport hull is authored with its bow at model-space -X, so a hull
// travelling along canonical +X renders at that heading plus PI. Measured from
// the art, not chosen — see TransportManager::transportModelBowOffset.
static constexpr float kHullYawAlongCanonicalX =
    kServerYawAlongCanonicalX + glm::pi<float>();

TEST_CASE("ClockSync: yaw flip detection after repeated misaligned updates", "[transport_clock_sync]") {
    TransportClockSync sync;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.useClientAnimation = false;

    // Travelling along canonical +X while reporting the yaw for -X.
    const float reversedYaw = kServerYawAlongCanonicalX - glm::pi<float>();
    glm::vec3 pos(100.0f, 200.0f, 0.0f);
    sync.processServerUpdate(t, &path, pos, reversedYaw, 1.0);

    for (int i = 1; i <= 8; i++) {
        pos.x += 5.0f;
        sync.processServerUpdate(t, &path, pos, reversedYaw, 1.0 + i * 0.5);
    }

    REQUIRE(t.serverYawFlipped180);
}

TEST_CASE("ClockSync: a transport facing its direction of travel is never flipped",
          "[transport_clock_sync]") {
    TransportClockSync sync;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.useClientAnimation = false;

    // The check used to compare the velocity against (cos s, sin s), the two
    // components of the heading swapped. That is a reflection, so a transport
    // facing exactly along its travel scored sin(2s) instead of 1 — negative for
    // half of all headings, which flipped correct transports through 180 degrees
    // purely on which way their route ran. Sweep the headings to pin that down.
    for (int step = 0; step < 16; ++step) {
        const float canonicalHeading = static_cast<float>(step) * glm::pi<float>() / 8.0f;
        const glm::vec3 dir(std::cos(canonicalHeading), -std::sin(canonicalHeading), 0.0f);
        const float serverYaw = canonicalHeading + glm::half_pi<float>();

        auto moving = makeTransport();
        moving.useClientAnimation = false;
        glm::vec3 p(100.0f, 200.0f, 0.0f);
        sync.processServerUpdate(moving, &path, p, serverYaw, 1.0);
        for (int i = 1; i <= 10; i++) {
            p += dir * 5.0f;
            sync.processServerUpdate(moving, &path, p, serverYaw, 1.0 + i * 0.5);
        }

        INFO("canonical heading step " << step);
        REQUIRE_FALSE(moving.serverYawFlipped180);
        REQUIRE(moving.serverYawAlignmentScore > 0);
    }
}

// ══════════════════════════════════════════════════════════════════
// TransportAnimator tests
// ══════════════════════════════════════════════════════════════════

TEST_CASE("Animator: evaluateAndApply updates position from spline", "[transport_animator]") {
    TransportAnimator animator;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.hasServerYaw = false;

    animator.evaluateAndApply(t, path, 0);
    // At t=0, path offset is (0,0,0), so pos = base + (0,0,0) = (100,200,0)
    REQUIRE(t.position.x == Catch::Approx(100.0f));
    REQUIRE(t.position.y == Catch::Approx(200.0f));

    animator.evaluateAndApply(t, path, 1000);
    // At t=1000, path offset is (10,0,0), so pos = base + (10,0,0) = (110,200,0)
    REQUIRE(t.position.x == Catch::Approx(110.0f));
}

TEST_CASE("Animator: uses server yaw when the server drives position", "[transport_animator]") {
    TransportAnimator animator;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.useClientAnimation = false;   // server-driven: its yaw is current
    t.hasServerYaw = true;
    t.serverYaw = 1.0f;
    t.serverYawFlipped180 = false;

    animator.evaluateAndApply(t, path, 500);
    // Rotation should be based on serverYaw=1.0, not spline tangent
    float expectedYaw = 1.0f;
    glm::quat expected = glm::angleAxis(expectedYaw, glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(t.rotation.w == Catch::Approx(expected.w).margin(0.01f));
    REQUIRE(t.rotation.z == Catch::Approx(expected.z).margin(0.01f));
}

TEST_CASE("Animator: a client-animated transport follows its route, not a stale server yaw",
          "[transport_animator][transport]") {
    // hasServerYaw is set by every server update, including those for a ship the
    // client animates itself. Taking it unconditionally pinned the ship's facing
    // to its berth heading for the whole voyage while the position ran along the
    // route underneath — sailing sideways or stern-first, and lying across the
    // pier on arrival.
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,    glm::vec3(0.0f, 0.0f, 0.0f)},
        {1000, glm::vec3(10.0f, 0.0f, 0.0f)},
        {2000, glm::vec3(20.0f, 0.0f, 0.0f)},
    });
    PathEntry path(std::move(spline), 302, false, true, true);

    auto t = makeTransport();
    t.basePosition = glm::vec3(0.0f);
    t.isM2 = false;
    t.useClientAnimation = true;
    t.hasServerYaw = true;
    t.serverYaw = 2.5f;             // a berth heading unrelated to the route

    animator.evaluateAndApply(t, path, 500);

    const glm::quat berth = glm::angleAxis(2.5f, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::quat route = glm::angleAxis(kHullYawAlongCanonicalX, glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(t.rotation.z != Catch::Approx(berth.z).margin(0.01f));
    REQUIRE(t.rotation.w == Catch::Approx(route.w).margin(0.01f));
    REQUIRE(t.rotation.z == Catch::Approx(route.z).margin(0.01f));
}

TEST_CASE("Animator: world-coordinate WMO faces along the server-space route", "[transport_animator][transport]") {
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,    glm::vec3(0.0f, 0.0f, 0.0f)},
        {1000, glm::vec3(10.0f, 0.0f, 0.0f)},
        {2000, glm::vec3(20.0f, 0.0f, 0.0f)},
    });
    PathEntry path(std::move(spline), 300, false, true, true);
    auto t = makeTransport();
    t.basePosition = glm::vec3(0.0f);
    t.isM2 = false;

    animator.evaluateAndApply(t, path, 500);

    // canonical +X is server +Y, and the hull's bow is model-space -X, so the
    // rendered heading is the route heading plus the hull's PI bow offset.
    const glm::quat expected = glm::angleAxis(
        kHullYawAlongCanonicalX, glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(t.rotation.w == Catch::Approx(expected.w).margin(0.01f));
    REQUIRE(t.rotation.z == Catch::Approx(expected.z).margin(0.01f));
}

TEST_CASE("Animator: a docked ship holds its arrival heading, not its spawn yaw",
          "[transport_animator][transport]") {
    // A dock wait is encoded as repeated positions, so the tangent vanishes and
    // there is no heading to derive. The ship keeps the rotation it arrived with.
    //
    // It used to restore the GO's authored spawn orientation here. That value is
    // a snapshot from whenever the GO query happened to answer rather than a
    // heading for the berth, and restoring it swung the ship round for the stop
    // and back again on departure.
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,    glm::vec3(-10.0f, 0.0f, 0.0f)},
        {1000, glm::vec3(0.0f, 0.0f, 0.0f)},
        {61000, glm::vec3(0.0f, 0.0f, 0.0f)},
        {62000, glm::vec3(10.0f, 0.0f, 0.0f)},
    });
    PathEntry path(std::move(spline), 301, false, true, true);
    auto t = makeTransport();
    t.basePosition = glm::vec3(0.0f);
    t.isM2 = false;
    t.rotation = glm::angleAxis(0.75f, glm::vec3(0.0f, 0.0f, 1.0f));
    t.dockYaw = -0.4f;
    t.hasDockYaw = true;

    animator.evaluateAndApply(t, path, 30000);

    // Pinned to the authored dwell node, not wherever the spline overshot to.
    REQUIRE(t.position == glm::vec3(0.0f));

    // The route approaches along canonical +X, so the heading through the stop
    // is that approach plus the hull's bow offset — derived, not inherited from
    // whatever rotation happened to be left over, and never the spawn yaw.
    const glm::quat approach = glm::angleAxis(
        kHullYawAlongCanonicalX, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::quat spawn = glm::angleAxis(t.dockYaw, glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(t.rotation.w == Catch::Approx(approach.w).margin(0.01f));
    REQUIRE(t.rotation.z == Catch::Approx(approach.z).margin(0.01f));
    REQUIRE(t.rotation.z != Catch::Approx(spawn.z).margin(0.01f));
}

TEST_CASE("Animator: a ship holds its dock instead of overshooting through the wait",
          "[transport_animator][transport]") {
    // A Catmull-Rom spline is not constrained to the hull of its control points,
    // so evaluating through a repeated-position dwell key overshoots and recovers
    // — the ship sails past its dock and comes back, for the whole wait. The hold
    // used to apply only to the three entries with broadside berths; every ship
    // needs it. This entry is in none of those lists.
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,     glm::vec3(0.0f, 0.0f, 0.0f)},
        {10000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {70000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {80000, glm::vec3(200.0f, 0.0f, 0.0f)},
    });
    PathEntry path(std::move(spline), 190536u, false, true, true);

    for (uint32_t atMs : {12000u, 25000u, 40000u, 55000u, 68000u}) {
        auto t = makeTransport(1, 190536u);
        t.entry = 190536u;          // Kraken — not a berthRunsParallel entry
        t.displayId = 7446u;
        t.basePosition = glm::vec3(0.0f);
        t.isM2 = false;

        animator.evaluateAndApply(t, path, atMs);

        INFO("dwell time " << atMs);
        REQUIRE(t.position.x == Catch::Approx(100.0f));
        REQUIRE(t.position.y == Catch::Approx(0.0f));
        REQUIRE(t.atDockDwell);
    }
}

TEST_CASE("Animator: the dwell flag is only set while the ship is actually stopped",
          "[transport_animator][transport]") {
    // The flag drives the hull's machinery: ShipMoving under way, ShipStop at
    // the pier. It has to go false again on departure, or the paddlewheel that
    // used to spin through the stop simply stays still for the rest of the trip.
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,     glm::vec3(0.0f, 0.0f, 0.0f)},
        {10000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {70000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {80000, glm::vec3(200.0f, 0.0f, 0.0f)},
    });
    PathEntry path(std::move(spline), 190536u, false, true, true);

    auto t = makeTransport(1, 190536u);
    t.entry = 190536u;
    t.displayId = 7446u;
    t.basePosition = glm::vec3(0.0f);
    t.isM2 = false;

    animator.evaluateAndApply(t, path, 5000);    // under way, approaching
    REQUIRE_FALSE(t.atDockDwell);

    animator.evaluateAndApply(t, path, 40000);   // holding at the dock
    REQUIRE(t.atDockDwell);

    animator.evaluateAndApply(t, path, 75000);   // under way again
    REQUIRE_FALSE(t.atDockDwell);
}

TEST_CASE("Animator: Bravery holds side-on at its dock dwell", "[transport_animator][transport]") {
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,     glm::vec3(0.0f, 0.0f, 0.0f)},
        {10000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {70000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {80000, glm::vec3(200.0f, 0.0f, 0.0f)},
    });
    PathEntry path(std::move(spline), 176310u, false, true, true);
    auto t = makeTransport(1, 176310u);
    t.entry = 176310u;
    t.displayId = 3015u;     // Bravery-class hull (bow at model -X, like every hull)
    t.basePosition = glm::vec3(0.0f);
    t.isM2 = false;
    // Deliberately unrelated live server yaw. Docking must not depend on the
    // orientation snapshot received when this transport happened to load.
    t.dockYaw = glm::pi<float>();
    t.hasDockYaw = true;

    animator.evaluateAndApply(t, path, 30000);

    // Keep the authored pier node so the gangway reaches the dock.
    REQUIRE(t.position.x == Catch::Approx(100.0f));
    REQUIRE(t.position.y == Catch::Approx(0.0f));
    // Bravery's TaxiPath runs parallel to the pier, so retaining the corrected
    // route heading is what leaves it broadside rather than nosed into the dock.
    const glm::quat expectedDock = glm::angleAxis(
        kHullYawAlongCanonicalX,
        glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(t.rotation.w == Catch::Approx(expectedDock.w).margin(0.001f));
    REQUIRE(t.rotation.z == Catch::Approx(expectedDock.z).margin(0.001f));

    t.dockYaw = -0.35f;
    t.hasDockYaw = false;
    animator.evaluateAndApply(t, path, 30000);
    REQUIRE(t.rotation.w == Catch::Approx(expectedDock.w).margin(0.001f));
    REQUIRE(t.rotation.z == Catch::Approx(expectedDock.z).margin(0.001f));
}

TEST_CASE("Animator: every ship hull faces its direction of travel",
          "[transport_animator][transport]") {
    // Every transport hull in the data is authored with its bow at model -X, so
    // they all take the same PI correction and none of them is a special case.
    //
    // This used to assert the opposite: that 7087 and 7446 were bow-reversed
    // while 3015 was not. Measuring the art says otherwise — the hulls taper to
    // a point at -X (transportship 1.0 vs 10.1 half-width at the two ends,
    // icebreaker 4.1 vs 14.8), and the icebreaker's paddlewheel doodad, which
    // belongs at the stern of a paddle steamer, sits at x=+36.3 on a hull
    // spanning -60.7..+50.1. The old table had it exactly inverted because it
    // was fitted against a facing that came from a frozen server yaw rather than
    // from the route, so what it was correcting was never the hull.
    struct ShipCase { uint32_t entry; uint32_t displayId; };
    for (const ShipCase& sc : {
             ShipCase{176310u, 3015u},  // The Bravery
             ShipCase{176244u, 7087u},  // The Moonspray
             ShipCase{181646u, 7087u},  // Elune's Blessing
             ShipCase{190536u, 7446u},  // Kraken-class icebreaker
         }) {
        TransportAnimator animator;
        CatmullRomSpline spline({
            {0,    glm::vec3(0.0f, 0.0f, 0.0f)},
            {1000, glm::vec3(10.0f, 0.0f, 0.0f)},
            {2000, glm::vec3(20.0f, 0.0f, 0.0f)},
        });
        PathEntry path(std::move(spline), sc.entry, false, false, true);
        auto t = makeTransport(1, sc.entry);
        t.entry = sc.entry;
        t.displayId = sc.displayId;
        t.basePosition = glm::vec3(0.0f);
        t.isM2 = false;

        animator.evaluateAndApply(t, path, 500);

        const glm::quat expected = glm::angleAxis(
            kHullYawAlongCanonicalX, glm::vec3(0.0f, 0.0f, 1.0f));
        INFO("entry=" << sc.entry << " displayId=" << sc.displayId);
        REQUIRE(t.rotation.w == Catch::Approx(expected.w).margin(0.001f));
        REQUIRE(t.rotation.z == Catch::Approx(expected.z).margin(0.001f));
    }
}

TEST_CASE("Animator: Kraken retains arrival heading throughout dock dwell",
          "[transport_animator][transport]") {
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,     glm::vec3(0.0f, 0.0f, 0.0f)},
        {10000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {70000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {80000, glm::vec3(200.0f, 0.0f, 0.0f)},
    });
    PathEntry path(std::move(spline), 190536u, false, true, true);
    auto t = makeTransport(1, 190536u);
    t.entry = 190536u;
    t.displayId = 7446u;     // Icebreaker hull: excluded from spawn-yaw restore at dwell
    t.basePosition = glm::vec3(0.0f);
    t.isM2 = false;
    t.dockYaw = glm::half_pi<float>();
    t.hasDockYaw = true;

    animator.evaluateAndApply(t, path, 5000);
    const glm::quat arrivalRotation = t.rotation;
    animator.evaluateAndApply(t, path, 30000);

    REQUIRE(t.rotation.w == Catch::Approx(arrivalRotation.w).margin(0.001f));
    REQUIRE(t.rotation.z == Catch::Approx(arrivalRotation.z).margin(0.001f));
}

TEST_CASE("Animator: Moonspray holds side-on at its dock dwell", "[transport_animator][transport]") {
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,     glm::vec3(0.0f, 0.0f, 0.0f)},
        {10000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {70000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {80000, glm::vec3(200.0f, 0.0f, 0.0f)},
    });
    PathEntry path(std::move(spline), 176244u, false, true, true);
    auto t = makeTransport(1, 176244u);
    t.entry = 176244u;
    t.displayId = 7087u;
    t.basePosition = glm::vec3(0.0f);
    t.isM2 = false;
    // Deliberately wrong bow-first server yaw: Moonspray keeps the berth-parallel route yaw.
    t.dockYaw = glm::pi<float>() - 0.1f;
    t.hasDockYaw = true;

    animator.evaluateAndApply(t, path, 30000);

    REQUIRE(t.position.x == Catch::Approx(100.0f));
    REQUIRE(t.position.y == Catch::Approx(0.0f));
    const glm::quat expectedDock = glm::angleAxis(
        glm::half_pi<float>() + glm::pi<float>(),
        glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(t.rotation.w == Catch::Approx(expectedDock.w).margin(0.001f));
    REQUIRE(t.rotation.z == Catch::Approx(expectedDock.z).margin(0.001f));
}

TEST_CASE("Animator: Elune's Blessing holds side-on at its dock dwell", "[transport_animator][transport]") {
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,     glm::vec3(0.0f, 0.0f, 0.0f)},
        {10000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {70000, glm::vec3(100.0f, 0.0f, 0.0f)},
        {80000, glm::vec3(200.0f, 0.0f, 0.0f)},
    });
    PathEntry path(std::move(spline), 181646u, false, true, true);
    auto t = makeTransport(1, 181646u);
    t.entry = 181646u;       // Elune's Blessing
    t.displayId = 7087u;     // Same model family as Moonspray
    t.basePosition = glm::vec3(0.0f);
    t.isM2 = false;
    t.dockYaw = glm::pi<float>();
    t.hasDockYaw = true;

    animator.evaluateAndApply(t, path, 30000);

    const glm::quat expectedDock = glm::angleAxis(
        glm::half_pi<float>() + glm::pi<float>(),
        glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(t.rotation.w == Catch::Approx(expectedDock.w).margin(0.001f));
    REQUIRE(t.rotation.z == Catch::Approx(expectedDock.z).margin(0.001f));
}

TEST_CASE("Animator: Z clamping on non-world-coord client anim", "[transport_animator]") {
    TransportAnimator animator;

    // Build a path with a deep negative Z offset
    std::vector<SplineKey> keys = {
        {0,    glm::vec3(0.0f, 0.0f, 0.0f)},
        {1000, glm::vec3(5.0f, 0.0f, -50.0f)},  // Deep negative Z
        {2000, glm::vec3(10.0f, 0.0f, 0.0f)},
    };
    CatmullRomSpline spline(std::move(keys), false);
    PathEntry path(std::move(spline), 200, false, true, false);

    auto t = makeTransport();
    t.useClientAnimation = true;
    t.serverUpdateCount = 0;  // <= 1, so Z clamping applies

    animator.evaluateAndApply(t, path, 1000);
    // Z should be clamped to >= -2.0 (kMinFallbackZOffset)
    REQUIRE(t.position.z >= (t.basePosition.z - 2.0f));
}

// ══════════════════════════════════════════════════════════════════
// Server route clock
// ══════════════════════════════════════════════════════════════════

TEST_CASE("ClockSync: the server's route phase drives the path time", "[transport_clock_sync]") {
    // The server publishes phase as a fraction of its own route period, so it maps
    // onto whatever timeline the client's spline has without the two periods
    // needing to agree. That is the point: the client's period was invented from
    // distance over speed, and when it came out short the ferry lapped its shore.
    TransportClockSync sync;
    auto path = makeCirclePath();          // 4000ms client-side
    const uint32_t clientMs = path.spline.durationMs();

    auto t = makeTransport();
    t.useClientAnimation = true;
    t.hasServerRouteClock = true;
    t.routePeriodMs = 200000;              // server route is far longer
    t.routePhaseAtTime = 100.0;

    uint32_t out = 0;

    t.routePhase = 0.0f;
    REQUIRE(sync.computePathTime(t, path.spline, 100.0, 0.0f, out));
    REQUIRE(out == 0u);

    t.routePhase = 0.5f;
    REQUIRE(sync.computePathTime(t, path.spline, 100.0, 0.0f, out));
    REQUIRE(out == clientMs / 2u);

    // A quarter of the server's period later, a quarter further along the spline.
    t.routePhase = 0.0f;
    REQUIRE(sync.computePathTime(t, path.spline, 100.0 + 50.0, 0.0f, out));
    REQUIRE(out == Catch::Approx(clientMs / 4u).margin(2));
}

TEST_CASE("ClockSync: the server route phase wraps rather than running off the end",
          "[transport_clock_sync]") {
    TransportClockSync sync;
    auto path = makeCirclePath();
    const uint32_t clientMs = path.spline.durationMs();

    auto t = makeTransport();
    t.useClientAnimation = true;
    t.hasServerRouteClock = true;
    t.routePeriodMs = 10000;
    t.routePhase = 0.9f;
    t.routePhaseAtTime = 0.0;

    uint32_t out = 0;
    // 2.5 server periods on from a 0.9 phase — must land inside the spline, not
    // beyond it, and not at zero by accident.
    REQUIRE(sync.computePathTime(t, path.spline, 25.0, 0.0f, out));
    REQUIRE(out < clientMs);
    REQUIRE(out == Catch::Approx(static_cast<uint32_t>(0.4f * clientMs)).margin(2));
}

TEST_CASE("ClockSync: without a server route clock the client keeps its own",
          "[transport_clock_sync]") {
    // Pre-WotLK publishes no transport phase, so those expansions must keep
    // animating locally rather than freezing at zero.
    TransportClockSync sync;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.useClientAnimation = true;
    t.hasServerRouteClock = false;
    t.localClockMs = 0;

    uint32_t out = 0;
    REQUIRE(sync.computePathTime(t, path.spline, 0.0, 1.0f, out));
    REQUIRE(out == 1000u);
}

TEST_CASE("ClockSync: a zero period is ignored rather than dividing by it",
          "[transport_clock_sync]") {
    TransportClockSync sync;
    auto path = makeCirclePath();
    auto t = makeTransport();
    t.useClientAnimation = true;
    t.hasServerRouteClock = true;
    t.routePeriodMs = 0;               // a GameObject with nothing to report
    t.routePhase = 0.5f;
    t.localClockMs = 0;

    uint32_t out = 0;
    REQUIRE(sync.computePathTime(t, path.spline, 0.0, 1.0f, out));
    REQUIRE(out == 1000u);             // fell through to the local clock
}

TEST_CASE("Animator: a docked hull lies on the chord through its berth, not the arrival leg",
          "[transport_animator][transport]") {
    // A route generally turns as it passes through its dock. Taking only the leg
    // the boat arrived on parks the hull half that turn out of true, which walks
    // the gangway off the plank — reported live on the Maiden's Fancy at
    // Menethil, whose route turns 26 degrees at the berth.
    //
    // Approach runs along canonical +X, departure along canonical +Y, so the
    // chord through the berth is the 45-degree diagonal between them.
    TransportAnimator animator;
    CatmullRomSpline spline({
        {0,     glm::vec3(-100.0f, 0.0f, 0.0f)},   // before the berth
        {10000, glm::vec3(0.0f, 0.0f, 0.0f)},      // berth
        {70000, glm::vec3(0.0f, 0.0f, 0.0f)},      // ...held
        {80000, glm::vec3(0.0f, 100.0f, 0.0f)},    // after the berth
    });
    PathEntry path(std::move(spline), 176231u, false, true, true);

    auto t = makeTransport(1, 176231u);
    t.entry = 176231u;
    t.displayId = 3015u;
    t.basePosition = glm::vec3(0.0f);
    t.isM2 = false;

    animator.evaluateAndApply(t, path, 40000);
    REQUIRE(t.atDockDwell);

    const float actual = glm::eulerAngles(t.rotation).z;
    auto yawFor = [](const glm::vec3& dir) {
        return glm::angleAxis(std::atan2(dir.x, dir.y) + glm::pi<float>(),
                              glm::vec3(0.0f, 0.0f, 1.0f));
    };
    const glm::quat chord   = yawFor(glm::vec3(1.0f, 1.0f, 0.0f));   // through the berth
    const glm::quat arrival = yawFor(glm::vec3(1.0f, 0.0f, 0.0f));   // the old answer

    const float chordYaw   = glm::eulerAngles(chord).z;
    const float arrivalYaw = glm::eulerAngles(arrival).z;
    INFO("actual=" << actual << " chord=" << chordYaw << " arrival=" << arrivalYaw);

    REQUIRE(std::abs(wowee::core::coords::normalizeAngleRad(actual - chordYaw)) < 0.02f);
    REQUIRE(std::abs(wowee::core::coords::normalizeAngleRad(actual - arrivalYaw)) > 0.2f);
}
