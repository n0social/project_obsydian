// tests/test_spline.cpp
// Unit tests for wowee::math::CatmullRomSpline
#include <catch_amalgamated.hpp>
#include "math/spline.hpp"
#include "game/spline_packet.hpp"
#include "network/packet.hpp"
#include <cmath>

using namespace wowee::math;

namespace {

void writeWotlkCreateSplineHeader(wowee::network::Packet& packet, uint32_t nodeCount) {
    packet.writeUInt32(0);       // spline flags
    packet.writeUInt32(25);      // time passed
    packet.writeUInt32(1000);    // duration
    packet.writeUInt32(7);       // spline id
    packet.writeFloat(1.0f);     // duration mod
    packet.writeFloat(1.0f);     // duration mod next
    packet.writeFloat(0.0f);     // vertical acceleration
    packet.writeUInt32(0);       // effect start time
    packet.writeUInt32(nodeCount);
}

} // namespace

TEST_CASE("Pre-WotLK monster-move run mode has the protocol spline bit", "[spline][packet][movement]") {
    REQUIRE(wowee::game::SplineFlag::PRE_WOTLK_RUNMODE == 0x00000100u);
    REQUIRE(wowee::game::isPreWotlkSplineWalking(0u));
    REQUIRE_FALSE(wowee::game::isPreWotlkSplineWalking(
        wowee::game::SplineFlag::PRE_WOTLK_RUNMODE));
}

TEST_CASE("WotLK AzerothCore create spline consumes uncompressed nodes", "[spline][packet]") {
    wowee::network::Packet packet;
    writeWotlkCreateSplineHeader(packet, 2);
    packet.writeFloat(10.0f); packet.writeFloat(20.0f); packet.writeFloat(30.0f);
    packet.writeFloat(11.0f); packet.writeFloat(21.0f); packet.writeFloat(31.0f);
    packet.writeUInt8(1); // spline mode
    packet.writeFloat(12.0f); packet.writeFloat(22.0f); packet.writeFloat(32.0f);
    packet.writeUInt32(0xAABBCCDD); // next block sentinel

    wowee::game::SplineBlockData data;
    REQUIRE(wowee::game::parseWotlkMoveUpdateSpline(packet, data));
    REQUIRE(packet.getRemainingSize() == 4);
    REQUIRE(packet.readUInt32() == 0xAABBCCDD);
    REQUIRE(data.endPoint.x == Catch::Approx(12.0f));
}

TEST_CASE("WotLK AzerothCore zero-node spline still consumes trailer", "[spline][packet]") {
    wowee::network::Packet packet;
    writeWotlkCreateSplineHeader(packet, 0);
    packet.writeUInt8(0); // spline mode
    packet.writeFloat(40.0f); packet.writeFloat(50.0f); packet.writeFloat(60.0f);
    packet.writeUInt32(0x11223344); // next block sentinel

    wowee::game::SplineBlockData data;
    REQUIRE(wowee::game::parseWotlkMoveUpdateSpline(packet, data));
    REQUIRE(packet.getRemainingSize() == 4);
    REQUIRE(packet.readUInt32() == 0x11223344);
}

TEST_CASE("WotLK compressed create spline remains supported as fallback", "[spline][packet]") {
    wowee::network::Packet packet;
    writeWotlkCreateSplineHeader(packet, 2);
    packet.writeFloat(10.0f); packet.writeFloat(20.0f); packet.writeFloat(30.0f);
    packet.writeUInt32(0); // one packed intermediate delta
    packet.writeUInt8(1);  // spline mode
    packet.writeFloat(12.0f); packet.writeFloat(22.0f); packet.writeFloat(32.0f);
    packet.writeUInt32(0x55667788); // next block sentinel

    wowee::game::SplineBlockData data;
    REQUIRE(wowee::game::parseWotlkMoveUpdateSpline(packet, data));
    REQUIRE(packet.getRemainingSize() == 4);
    REQUIRE(packet.readUInt32() == 0x55667788);
}

TEST_CASE("WotLK taxi spline accepts vertical acceleration without effect start", "[spline][packet][taxi]") {
    wowee::network::Packet packet;
    packet.writeUInt32(wowee::game::SplineFlag::FINAL_POINT);
    packet.writeFloat(-9215.0f); packet.writeFloat(43.0f); packet.writeFloat(85.0f);
    packet.writeUInt32(0);       // time passed
    packet.writeUInt32(12000);   // duration
    packet.writeUInt32(9);       // spline id
    packet.writeFloat(1.0f);     // duration mod
    packet.writeFloat(0.0f);     // duration mod next
    packet.writeFloat(0.0f);     // vertical acceleration
    packet.writeUInt32(4);       // point count; this layout has no effect start time
    for (int i = 0; i < 4; ++i) {
        packet.writeFloat(-9200.0f + static_cast<float>(i) * 10.0f);
        packet.writeFloat(50.0f + static_cast<float>(i));
        packet.writeFloat(90.0f + static_cast<float>(i));
    }
    packet.writeUInt8(1);        // spline mode
    packet.writeFloat(-9160.0f); packet.writeFloat(54.0f); packet.writeFloat(94.0f);
    packet.writeUInt32(0xCAFEBABE);

    wowee::game::SplineBlockData data;
    REQUIRE(wowee::game::parseWotlkMoveUpdateSpline(packet, data));
    REQUIRE(packet.getRemainingSize() == 4);
    REQUIRE(packet.readUInt32() == 0xCAFEBABE);
    REQUIRE(data.hasEndPoint);
    REQUIRE(data.endPoint.x == Catch::Approx(-9160.0f));
}

// ── Helper: build a simple 4-point linear path ─────────────────────
static std::vector<SplineKey> linearKeys() {
    // Straight line along X axis: (0,0,0) → (10,0,0) → (20,0,0) → (30,0,0)
    return {
        {0,    glm::vec3(0.0f,  0.0f, 0.0f)},
        {1000, glm::vec3(10.0f, 0.0f, 0.0f)},
        {2000, glm::vec3(20.0f, 0.0f, 0.0f)},
        {3000, glm::vec3(30.0f, 0.0f, 0.0f)},
    };
}

// ── Helper: build a square looping path ─────────────────────────────
static std::vector<SplineKey> squareKeys() {
    // Square path: (0,0,0) → (10,0,0) → (10,10,0) → (0,10,0) → (0,0,0)
    return {
        {0,    glm::vec3(0.0f,  0.0f,  0.0f)},
        {1000, glm::vec3(10.0f, 0.0f,  0.0f)},
        {2000, glm::vec3(10.0f, 10.0f, 0.0f)},
        {3000, glm::vec3(0.0f,  10.0f, 0.0f)},
        {4000, glm::vec3(0.0f,  0.0f,  0.0f)},  // Wrap back to start
    };
}

// ── Construction ────────────────────────────────────────────────────

TEST_CASE("CatmullRomSpline empty construction", "[spline]") {
    CatmullRomSpline spline({});
    REQUIRE(spline.keyCount() == 0);
    REQUIRE(spline.durationMs() == 0);
    REQUIRE(spline.evaluatePosition(0) == glm::vec3(0.0f));
}

TEST_CASE("CatmullRomSpline single key", "[spline]") {
    CatmullRomSpline spline({{500, glm::vec3(1.0f, 2.0f, 3.0f)}});
    REQUIRE(spline.keyCount() == 1);
    REQUIRE(spline.durationMs() == 0);

    auto pos = spline.evaluatePosition(0);
    REQUIRE(pos.x == Catch::Approx(1.0f));
    REQUIRE(pos.y == Catch::Approx(2.0f));
    REQUIRE(pos.z == Catch::Approx(3.0f));

    // Any time returns the same position
    pos = spline.evaluatePosition(9999);
    REQUIRE(pos.x == Catch::Approx(1.0f));
}

TEST_CASE("CatmullRomSpline duration calculation", "[spline]") {
    CatmullRomSpline spline(linearKeys());
    REQUIRE(spline.durationMs() == 3000);
    REQUIRE(spline.keyCount() == 4);
    REQUIRE_FALSE(spline.isTimeClosed());
}

// ── Position evaluation ─────────────────────────────────────────────

TEST_CASE("CatmullRomSpline evaluates at key positions", "[spline]") {
    auto keys = linearKeys();
    CatmullRomSpline spline(keys);

    // At exact key times, Catmull-Rom passes through the control point
    auto pos0 = spline.evaluatePosition(0);
    REQUIRE(pos0.x == Catch::Approx(0.0f).margin(0.01f));

    auto pos1 = spline.evaluatePosition(1000);
    REQUIRE(pos1.x == Catch::Approx(10.0f).margin(0.01f));

    auto pos2 = spline.evaluatePosition(2000);
    REQUIRE(pos2.x == Catch::Approx(20.0f).margin(0.01f));

    auto pos3 = spline.evaluatePosition(3000);
    REQUIRE(pos3.x == Catch::Approx(30.0f).margin(0.01f));
}

TEST_CASE("CatmullRomSpline midpoint evaluation", "[spline]") {
    CatmullRomSpline spline(linearKeys());

    // For a straight line, midpoint should be approximately halfway.
    // Catmull-Rom with clamped endpoints at segment boundaries
    // has some overshoot, so use a wider tolerance.
    auto mid = spline.evaluatePosition(500);
    REQUIRE(mid.x == Catch::Approx(5.0f).margin(1.0f));
    REQUIRE(mid.y == Catch::Approx(0.0f).margin(0.1f));
    REQUIRE(mid.z == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("CatmullRomSpline repeated-position segment is an exact dwell", "[spline][transport]") {
    CatmullRomSpline spline({
        {0,    glm::vec3(-10.0f, 0.0f, 0.0f)},
        {1000, glm::vec3(0.0f, 0.0f, 0.0f)},
        {61000, glm::vec3(0.0f, 0.0f, 0.0f)},
        {62000, glm::vec3(10.0f, 0.0f, 0.0f)},
    });

    const auto stopped = spline.evaluate(30000);
    REQUIRE(stopped.position == glm::vec3(0.0f));
    REQUIRE(stopped.tangent == glm::vec3(0.0f));
}

TEST_CASE("CatmullRomSpline clamping at boundaries", "[spline]") {
    CatmullRomSpline spline(linearKeys());

    // Before start should clamp to first segment start
    auto before = spline.evaluatePosition(0);
    REQUIRE(before.x == Catch::Approx(0.0f).margin(0.01f));

    // After end should clamp to last segment end
    auto after = spline.evaluatePosition(5000);
    REQUIRE(after.x == Catch::Approx(30.0f).margin(0.01f));
}

// ── Time-closed (looping) path ──────────────────────────────────────

TEST_CASE("CatmullRomSpline time-closed path", "[spline]") {
    CatmullRomSpline spline(squareKeys(), true);
    REQUIRE(spline.durationMs() == 4000);
    REQUIRE(spline.isTimeClosed());

    // Start position
    auto pos0 = spline.evaluatePosition(0);
    REQUIRE(pos0.x == Catch::Approx(0.0f).margin(0.1f));
    REQUIRE(pos0.y == Catch::Approx(0.0f).margin(0.1f));

    // Quarter way — should be near (10, 0, 0)
    auto pos1 = spline.evaluatePosition(1000);
    REQUIRE(pos1.x == Catch::Approx(10.0f).margin(0.1f));
    REQUIRE(pos1.y == Catch::Approx(0.0f).margin(0.1f));
}

// ── Tangent / evaluate() ────────────────────────────────────────────

TEST_CASE("CatmullRomSpline evaluate returns tangent", "[spline]") {
    CatmullRomSpline spline(linearKeys());

    auto result = spline.evaluate(1500);
    // For a straight line along X, tangent should be predominantly in X
    REQUIRE(std::abs(result.tangent.x) > std::abs(result.tangent.y));
    REQUIRE(std::abs(result.tangent.x) > std::abs(result.tangent.z));
}

// ── orientationFromTangent ──────────────────────────────────────────

TEST_CASE("orientationFromTangent identity for zero tangent", "[spline]") {
    auto q = CatmullRomSpline::orientationFromTangent(glm::vec3(0.0f));
    // Should return identity quaternion
    REQUIRE(q.w == Catch::Approx(1.0f).margin(0.01f));
}

TEST_CASE("orientationFromTangent for forward direction", "[spline]") {
    auto q = CatmullRomSpline::orientationFromTangent(glm::vec3(1.0f, 0.0f, 0.0f));
    // Should return a valid quaternion (unit length)
    float length = glm::length(q);
    REQUIRE(length == Catch::Approx(1.0f).margin(0.01f));
}

TEST_CASE("orientationFromTangent for vertical tangent", "[spline]") {
    // Nearly vertical tangent — tests the fallback up vector
    auto q = CatmullRomSpline::orientationFromTangent(glm::vec3(0.0f, 0.0f, 1.0f));
    float length = glm::length(q);
    REQUIRE(length == Catch::Approx(1.0f).margin(0.01f));
}

// ── hasXYMovement ───────────────────────────────────────────────────

TEST_CASE("hasXYMovement detects horizontal movement", "[spline]") {
    CatmullRomSpline spline(linearKeys());
    REQUIRE(spline.hasXYMovement(1.0f));
}

TEST_CASE("hasXYMovement detects Z-only (elevator)", "[spline]") {
    std::vector<SplineKey> elevator = {
        {0,    glm::vec3(5.0f, 5.0f, 0.0f)},
        {1000, glm::vec3(5.0f, 5.0f, 10.0f)},
        {2000, glm::vec3(5.0f, 5.0f, 20.0f)},
    };
    CatmullRomSpline spline(elevator);
    REQUIRE_FALSE(spline.hasXYMovement(1.0f));
}

// ── findNearestKey ──────────────────────────────────────────────────

TEST_CASE("findNearestKey returns closest key", "[spline]") {
    CatmullRomSpline spline(linearKeys());

    // Closest to (9, 0, 0) should be key at (10, 0, 0) = index 1
    size_t idx = spline.findNearestKey(glm::vec3(9.0f, 0.0f, 0.0f));
    REQUIRE(idx == 1);

    // Closest to (0, 0, 0) should be key 0
    idx = spline.findNearestKey(glm::vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(idx == 0);

    // Closest to (25, 0, 0) should be key at (20, 0, 0) = index 2  or (30,0,0) = index 3
    idx = spline.findNearestKey(glm::vec3(25.0f, 0.0f, 0.0f));
    REQUIRE((idx == 2 || idx == 3));
}

// ── Binary search segment lookup ────────────────────────────────────

TEST_CASE("CatmullRomSpline segment lookup is correct", "[spline]") {
    // Build a path with uneven timing
    std::vector<SplineKey> keys = {
        {0,    glm::vec3(0.0f)},
        {100,  glm::vec3(1.0f, 0.0f, 0.0f)},
        {500,  glm::vec3(2.0f, 0.0f, 0.0f)},
        {2000, glm::vec3(3.0f, 0.0f, 0.0f)},
        {5000, glm::vec3(4.0f, 0.0f, 0.0f)},
    };
    CatmullRomSpline spline(keys);

    // At t=50, should be in first segment → position near key 0
    auto pos50 = spline.evaluatePosition(50);
    REQUIRE(pos50.x == Catch::Approx(0.5f).margin(0.5f)); // Somewhere between 0 and 1

    // At t=300, should be in second segment → between key 1 and key 2
    auto pos300 = spline.evaluatePosition(300);
    REQUIRE(pos300.x > 1.0f);
    REQUIRE(pos300.x < 2.5f);

    // At t=3000, should be in fourth segment → between key 3 and key 4
    auto pos3000 = spline.evaluatePosition(3000);
    REQUIRE(pos3000.x > 2.5f);
    REQUIRE(pos3000.x < 4.5f);
}

// ── Two-point spline (minimum viable path) ──────────────────────────

TEST_CASE("CatmullRomSpline with two points", "[spline]") {
    std::vector<SplineKey> keys = {
        {0,    glm::vec3(0.0f, 0.0f, 0.0f)},
        {1000, glm::vec3(10.0f, 0.0f, 0.0f)},
    };
    CatmullRomSpline spline(keys);
    REQUIRE(spline.durationMs() == 1000);

    auto start = spline.evaluatePosition(0);
    REQUIRE(start.x == Catch::Approx(0.0f).margin(0.01f));

    auto end = spline.evaluatePosition(1000);
    REQUIRE(end.x == Catch::Approx(10.0f).margin(0.01f));

    // Midpoint should be near 5
    auto mid = spline.evaluatePosition(500);
    REQUIRE(mid.x == Catch::Approx(5.0f).margin(1.0f));
}
