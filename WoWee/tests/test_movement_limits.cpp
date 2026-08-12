#include <catch_amalgamated.hpp>
#include "rendering/movement_limits.hpp"
#include "core/coordinates.hpp"

#include <cmath>

TEST_CASE("stock hill climbing limits are shared by all surfaces") {
    using namespace wowee::rendering::movement;
    REQUIRE(kMaxWalkableSlopeDegrees == 50.0f);
    REQUIRE(isWalkableNormal(kMinWalkableNormalZ));
    REQUIRE_FALSE(isWalkableNormal(kMinWalkableNormalZ - 0.001f));
    REQUIRE(isReachableStep(kMaxStepUp));
    REQUIRE_FALSE(isReachableStep(kMaxStepUp + 0.001f));
}

// A walkable slope can rise faster than the step-up budget allows for, which is
// why grounding cannot rely on the budget alone. At the steepest walkable angle
// a mounted player crosses more ground per frame than kMaxStepUp covers as soon
// as the frame runs long — and the floor selection rejects any surface above
// feet + budget as unreachable, so the terrain under a climbing player stops
// counting as ground and they sink into the hill.
TEST_CASE("a walkable slope out-climbs the step-up budget in a long frame") {
    using namespace wowee::rendering::movement;

    // tan(50 degrees), the rise per unit travelled along the steepest slope a
    // player may walk up.
    constexpr float kSteepestRisePerYard = 1.19175f;

    auto riseOverFrame = [](float speedYardsPerSec, float frameSeconds) {
        return speedYardsPerSec * frameSeconds * kSteepestRisePerYard;
    };

    // A smooth frame stays well inside the budget at every travel speed.
    CHECK(riseOverFrame(7.0f, 1.0f / 60.0f) < kMaxStepUp);   // running
    CHECK(riseOverFrame(14.0f, 1.0f / 60.0f) < kMaxStepUp);  // epic mount

    // A slow frame does not. This is the case that put the player inside the
    // hill, so grounding has to recover from penetration rather than assume it
    // cannot happen.
    CHECK(riseOverFrame(14.0f, 1.0f / 20.0f) > kMaxStepUp);
}

// Facing crosses two representations: the renderer holds the character's yaw in
// degrees, the game side holds canonical yaw in radians, and the frame loop
// converts render → game every frame. Both directions of that conversion were
// hand-written at four call sites, two of them inverting the other two from
// memory. If they ever disagree, facing a target writes one value and the next
// frame reads back another — which is how a cast could be accepted and then
// fail the server's arc check a second and a half later.
TEST_CASE("character yaw and canonical yaw convert back to each other") {
    using namespace wowee::core::coords;

    for (float deg = -720.0f; deg <= 720.0f; deg += 7.5f) {
        const float canonical = characterYawDegToCanonical(deg);
        const float back = canonicalToCharacterYawDeg(canonical);
        // Round-trips to the same heading, allowing for full turns.
        float delta = std::fmod(std::fabs(back - deg), 360.0f);
        if (delta > 180.0f) delta = 360.0f - delta;
        INFO("degrees: " << deg << " canonical: " << canonical << " back: " << back);
        CHECK(delta < 0.01f);
        CHECK(canonical >= -PI - 0.001f);
        CHECK(canonical <= PI + 0.001f);
    }

    // Render yaw is canonical + 90: canonicalToRender swaps x and y, and
    // canonical yaw is atan2(-dy, dx), so a heading of canonical 0 (north) has
    // render components (0, 1) and a render yaw of 90.
    CHECK(canonicalToCharacterYawDeg(0.0f) == Catch::Approx(90.0f).margin(1e-4));
    CHECK(characterYawDegToCanonical(90.0f) == Catch::Approx(0.0f).margin(1e-5));

    // And it must agree with taking the angle of the render direction directly,
    // which is how the combat auto-turn and the camera both produce it.
    for (float canon = -3.0f; canon <= 3.0f; canon += 0.25f) {
        const glm::vec3 dirCanonical(std::cos(canon), -std::sin(canon), 0.0f);
        const glm::vec3 dirRender = canonicalToRender(dirCanonical);
        const float renderYawDeg =
            std::atan2(dirRender.y, dirRender.x) * (180.0f / PI);
        float diff = std::fmod(std::fabs(renderYawDeg - canonicalToCharacterYawDeg(canon)), 360.0f);
        if (diff > 180.0f) diff = 360.0f - diff;
        INFO("canonical: " << canon);
        CHECK(diff < 0.01f);
    }
}

// Standing on the hillside over a cave used to drop the player onto the cave's
// ceiling. Being "inside" a WMO is decided by bounding-box containment, and an
// underground WMO's interior box reaches up through the ground above it — so
// the terrain veto meant for Undercity's halls fired out in the open, leaving
// the WMO as the only floor candidate and its ceiling as the nearest surface
// below.
TEST_CASE("the terrain veto only refuses ground overhead") {
    using namespace wowee::rendering::movement;
    constexpr float kStepUp = kMaxStepUp;

    SECTION("Undercity: the surface is ~113m over the halls, and refused") {
        REQUIRE(terrainIsOverheadRoof(true, 61.66f, -51.5f, kStepUp));
    }

    SECTION("the hillside over a cave is at the feet, and kept") {
        // Same containment answer, entirely different situation.
        REQUIRE_FALSE(terrainIsOverheadRoof(true, 120.0f, 120.0f, kStepUp));
        REQUIRE_FALSE(terrainIsOverheadRoof(true, 120.4f, 120.0f, kStepUp));
    }

    SECTION("ground below the feet is never a roof") {
        REQUIRE_FALSE(terrainIsOverheadRoof(true, 100.0f, 120.0f, kStepUp));
    }

    SECTION("outdoors nothing is vetoed, however far above") {
        REQUIRE_FALSE(terrainIsOverheadRoof(false, 500.0f, 0.0f, kStepUp));
    }

    SECTION("the boundary is what the player could step onto") {
        const float feet = 10.0f;
        const float edge = feet + kStepUp + 0.5f;
        REQUIRE_FALSE(terrainIsOverheadRoof(true, edge, feet, kStepUp));
        REQUIRE(terrainIsOverheadRoof(true, edge + 0.01f, feet, kStepUp));
    }
}
