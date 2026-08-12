// AnimCapabilitySet unit tests
#include <catch_amalgamated.hpp>
#include "rendering/animation/anim_capability_set.hpp"

using namespace wowee::rendering;

TEST_CASE("AnimCapabilitySet: default-constructed has all zero IDs", "[capability]") {
    AnimCapabilitySet caps;

    CHECK(caps.resolvedStand == 0);
    CHECK(caps.resolvedWalk == 0);
    CHECK(caps.resolvedRun == 0);
    CHECK(caps.resolvedSprint == 0);
    CHECK(caps.resolvedJumpStart == 0);
    CHECK(caps.resolvedJump == 0);
    CHECK(caps.resolvedJumpEnd == 0);
    CHECK(caps.resolvedSwimIdle == 0);
    CHECK(caps.resolvedSwim == 0);
    CHECK(caps.resolvedCombatIdle == 0);
    CHECK(caps.resolvedMelee1H == 0);
    CHECK(caps.resolvedMelee2H == 0);
    CHECK(caps.resolvedStun == 0);
    CHECK(caps.resolvedMount == 0);
    CHECK(caps.resolvedStealthIdle == 0);
    CHECK(caps.resolvedLoot == 0);
}

TEST_CASE("AnimCapabilitySet: default-constructed has all flags false", "[capability]") {
    AnimCapabilitySet caps;

    CHECK_FALSE(caps.hasStand);
    CHECK_FALSE(caps.hasWalk);
    CHECK_FALSE(caps.hasRun);
    CHECK_FALSE(caps.hasSprint);
    CHECK_FALSE(caps.hasWalkBackwards);
    CHECK_FALSE(caps.hasJump);
    CHECK_FALSE(caps.hasSwim);
    CHECK_FALSE(caps.hasMelee);
    CHECK_FALSE(caps.hasStealth);
    CHECK_FALSE(caps.hasDeath);
    CHECK_FALSE(caps.hasMount);
}

TEST_CASE("AnimOutput::ok creates valid output", "[capability]") {
    auto out = AnimOutput::ok(42, true);
    CHECK(out.valid);
    CHECK(out.animId == 42);
    CHECK(out.loop == true);
}

TEST_CASE("AnimOutput::stay creates invalid output", "[capability]") {
    auto out = AnimOutput::stay();
    CHECK_FALSE(out.valid);
    CHECK(out.animId == 0);
    CHECK(out.loop == false);
}
