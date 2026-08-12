// LocomotionFSM unit tests
#include <catch_amalgamated.hpp>
#include "rendering/animation/locomotion_fsm.hpp"
#include "rendering/animation/animation_ids.hpp"

using namespace wowee::rendering;
namespace anim = wowee::rendering::anim;

// Helper: create a capability set with basic locomotion resolved
static AnimCapabilitySet makeLocoCaps() {
    AnimCapabilitySet caps;
    caps.resolvedStand = anim::STAND;
    caps.resolvedWalk = anim::WALK;
    caps.resolvedRun = anim::RUN;
    caps.resolvedSprint = anim::SPRINT;
    caps.resolvedWalkBackwards = anim::WALK_BACKWARDS;
    caps.resolvedJumpStart = anim::JUMP_START;
    caps.resolvedJump = anim::JUMP;
    caps.resolvedJumpEnd = anim::JUMP_END;
    caps.resolvedSwimIdle = anim::SWIM_IDLE;
    caps.resolvedSwim = anim::SWIM;
    caps.hasStand = true;
    caps.hasWalk = true;
    caps.hasRun = true;
    caps.hasSprint = true;
    caps.hasWalkBackwards = true;
    caps.hasJump = true;
    caps.hasSwim = true;
    return caps;
}

static LocomotionFSM::Input idle() {
    LocomotionFSM::Input in;
    in.deltaTime = 0.016f;
    return in;
}

TEST_CASE("LocomotionFSM: IDLE → WALK on move start (non-sprinting)", "[locomotion]") {
    LocomotionFSM fsm;
    auto caps = makeLocoCaps();

    auto in = idle();
    in.moving = true;
    auto out = fsm.resolve(in, caps);

    REQUIRE(out.valid);
    REQUIRE(out.animId == anim::WALK);
    REQUIRE(out.loop == true);
    CHECK(fsm.getState() == LocomotionFSM::State::WALK);
}

TEST_CASE("LocomotionFSM: IDLE → RUN on move start (sprinting)", "[locomotion]") {
    LocomotionFSM fsm;
    auto caps = makeLocoCaps();

    auto in = idle();
    in.moving = true;
    in.sprinting = true;
    auto out = fsm.resolve(in, caps);

    REQUIRE(out.valid);
    REQUIRE(out.animId == anim::RUN);
    CHECK(fsm.getState() == LocomotionFSM::State::RUN);
}

TEST_CASE("LocomotionFSM: WALK → IDLE on move stop (after grace)", "[locomotion]") {
    LocomotionFSM fsm;
    auto caps = makeLocoCaps();

    // Start walking
    auto in = idle();
    in.moving = true;
    fsm.resolve(in, caps);
    CHECK(fsm.getState() == LocomotionFSM::State::WALK);

    // Stop moving — grace timer keeps walk for a bit
    in.moving = false;
    in.deltaTime = 0.2f; // > grace period (0.12s)
    auto out = fsm.resolve(in, caps);

    CHECK(fsm.getState() == LocomotionFSM::State::IDLE);
}

TEST_CASE("LocomotionFSM: WALK → JUMP_START on jump", "[locomotion]") {
    LocomotionFSM fsm;
    auto caps = makeLocoCaps();

    // Start walking
    auto in = idle();
    in.moving = true;
    fsm.resolve(in, caps);

    // Jump
    in.jumping = true;
    in.grounded = false;
    auto out = fsm.resolve(in, caps);

    REQUIRE(out.valid);
    REQUIRE(out.animId == anim::JUMP_START);
    CHECK(fsm.getState() == LocomotionFSM::State::JUMP_START);
}

TEST_CASE("LocomotionFSM: SWIM_IDLE → SWIM on move start while swimming", "[locomotion]") {
    LocomotionFSM fsm;
    auto caps = makeLocoCaps();

    // Enter swim idle
    auto in = idle();
    in.swimming = true;
    fsm.resolve(in, caps);
    CHECK(fsm.getState() == LocomotionFSM::State::SWIM_IDLE);

    // Start swimming
    in.moving = true;
    auto out = fsm.resolve(in, caps);

    REQUIRE(out.valid);
    REQUIRE(out.animId == anim::SWIM);
    CHECK(fsm.getState() == LocomotionFSM::State::SWIM);
}

TEST_CASE("LocomotionFSM: backward walking resolves WALK_BACKWARDS", "[locomotion]") {
    LocomotionFSM fsm;
    auto caps = makeLocoCaps();

    auto in = idle();
    in.moving = true;
    in.movingBackward = true;
    auto out = fsm.resolve(in, caps);

    REQUIRE(out.valid);
    // Should use WALK_BACKWARDS when available
    CHECK(out.animId == anim::WALK_BACKWARDS);
}

TEST_CASE("LocomotionFSM: STAY when WALK_BACKWARDS missing from caps", "[locomotion]") {
    LocomotionFSM fsm;
    AnimCapabilitySet caps;
    caps.resolvedStand = anim::STAND;
    caps.resolvedWalk = anim::WALK;
    caps.hasStand = true;
    caps.hasWalk = true;
    // No WALK_BACKWARDS in caps

    auto in = idle();
    in.moving = true;
    in.movingBackward = true;
    auto out = fsm.resolve(in, caps);

    // Should still resolve something — falls back to walk
    REQUIRE(out.valid);
}

TEST_CASE("LocomotionFSM: reset restores IDLE", "[locomotion]") {
    LocomotionFSM fsm;
    auto caps = makeLocoCaps();

    auto in = idle();
    in.moving = true;
    fsm.resolve(in, caps);
    CHECK(fsm.getState() == LocomotionFSM::State::WALK);

    fsm.reset();
    CHECK(fsm.getState() == LocomotionFSM::State::IDLE);
}
