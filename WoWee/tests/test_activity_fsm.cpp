// ActivityFSM unit tests
#include <catch_amalgamated.hpp>
#include "rendering/animation/activity_fsm.hpp"
#include "rendering/animation/animation_ids.hpp"

using namespace wowee::rendering;
namespace anim = wowee::rendering::anim;

static AnimCapabilitySet makeActivityCaps() {
    AnimCapabilitySet caps;
    caps.resolvedStand = anim::STAND;
    caps.resolvedSitDown = anim::SIT_GROUND_DOWN;
    caps.resolvedSitLoop = anim::SITTING;
    caps.resolvedSitUp = anim::SIT_GROUND_UP;
    caps.resolvedKneel = anim::KNEEL_LOOP;
    caps.resolvedLoot = anim::LOOT;
    return caps;
}

static ActivityFSM::Input idleInput() {
    ActivityFSM::Input in;
    in.grounded = true;
    return in;
}

TEST_CASE("ActivityFSM: NONE by default", "[activity]") {
    ActivityFSM fsm;
    CHECK(fsm.getState() == ActivityFSM::State::NONE);
    CHECK_FALSE(fsm.isActive());
}

TEST_CASE("ActivityFSM: emote starts and returns valid output", "[activity]") {
    ActivityFSM fsm;
    auto caps = makeActivityCaps();

    fsm.startEmote(anim::EMOTE_WAVE, false);
    CHECK(fsm.getState() == ActivityFSM::State::EMOTE);
    CHECK(fsm.isEmoteActive());

    auto in = idleInput();
    auto out = fsm.resolve(in, caps);
    REQUIRE(out.valid);
    CHECK(out.animId == anim::EMOTE_WAVE);
    CHECK(out.loop == false);
}

TEST_CASE("ActivityFSM: emote cancelled by movement", "[activity]") {
    ActivityFSM fsm;
    auto caps = makeActivityCaps();

    fsm.startEmote(anim::EMOTE_WAVE, false);

    auto in = idleInput();
    in.moving = true;
    auto out = fsm.resolve(in, caps);

    // After movement, emote should be cancelled
    CHECK_FALSE(fsm.isEmoteActive());
    CHECK(fsm.getState() == ActivityFSM::State::NONE);
}

TEST_CASE("ActivityFSM: looting starts and stops", "[activity]") {
    ActivityFSM fsm;
    auto caps = makeActivityCaps();

    fsm.startLooting();
    CHECK(fsm.getState() == ActivityFSM::State::LOOTING);

    auto in = idleInput();
    auto out = fsm.resolve(in, caps);
    REQUIRE(out.valid);

    // stopLooting transitions through LOOT_END (KNEEL_END one-shot) before NONE
    fsm.stopLooting();
    CHECK(fsm.getState() == ActivityFSM::State::LOOT_END);
}

TEST_CASE("ActivityFSM: sit stand state triggers sit down", "[activity]") {
    ActivityFSM fsm;
    auto caps = makeActivityCaps();

    fsm.setStandState(ActivityFSM::STAND_STATE_SIT);
    CHECK(fsm.getState() == ActivityFSM::State::SIT_DOWN);

    auto in = idleInput();
    in.sitting = true;
    auto out = fsm.resolve(in, caps);
    REQUIRE(out.valid);
}

TEST_CASE("ActivityFSM: consumption loop preserves sit-down transition", "[activity]") {
    ActivityFSM fsm;
    auto caps = makeActivityCaps();
    fsm.setSeatedLoopAnimation(anim::EATING_LOOP);
    fsm.setStandState(ActivityFSM::STAND_STATE_SIT);

    auto in = idleInput();
    in.sitting = true;
    auto down = fsm.resolve(in, caps);
    REQUIRE(down.valid);
    CHECK(down.animId == anim::SIT_GROUND_DOWN);
    CHECK_FALSE(down.loop);

    fsm.setState(ActivityFSM::State::SITTING);
    auto eating = fsm.resolve(in, caps);
    REQUIRE(eating.valid);
    CHECK(eating.animId == anim::EATING_LOOP);
    CHECK(eating.loop);
}

TEST_CASE("ActivityFSM: cancel emote explicitly", "[activity]") {
    ActivityFSM fsm;
    auto caps = makeActivityCaps();

    fsm.startEmote(anim::EMOTE_DANCE, true);
    CHECK(fsm.isEmoteActive());
    CHECK(fsm.getEmoteAnimId() == anim::EMOTE_DANCE);

    fsm.cancelEmote();
    CHECK_FALSE(fsm.isEmoteActive());
    CHECK(fsm.getState() == ActivityFSM::State::NONE);
}

TEST_CASE("ActivityFSM: reset clears all state", "[activity]") {
    ActivityFSM fsm;
    fsm.startEmote(anim::EMOTE_WAVE, false);
    CHECK(fsm.isActive());

    fsm.reset();
    CHECK(fsm.getState() == ActivityFSM::State::NONE);
    CHECK_FALSE(fsm.isActive());
}
