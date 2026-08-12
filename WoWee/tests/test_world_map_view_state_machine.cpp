// Tests for the extracted world map view state machine module
#include <catch_amalgamated.hpp>
#include "rendering/world_map/view_state_machine.hpp"

using namespace wowee::rendering::world_map;

TEST_CASE("ViewStateMachine: initial state is CONTINENT", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    REQUIRE(sm.currentLevel() == ViewLevel::CONTINENT);
    REQUIRE(sm.continentIdx() == -1);
    REQUIRE(sm.currentZoneIdx() == -1);
    REQUIRE(sm.transition().active == false);
}

TEST_CASE("ViewStateMachine: zoomIn from CONTINENT to ZONE", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::CONTINENT);
    sm.setContinentIdx(0);

    auto result = sm.zoomIn(5, 5);
    REQUIRE(result.changed == true);
    REQUIRE(result.newLevel == ViewLevel::ZONE);
    REQUIRE(result.targetIdx == 5);
    REQUIRE(sm.currentLevel() == ViewLevel::ZONE);
    REQUIRE(sm.currentZoneIdx() == 5);
}

TEST_CASE("ViewStateMachine: zoomIn from CONTINENT with no zone does nothing", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::CONTINENT);
    sm.setContinentIdx(0);

    auto result = sm.zoomIn(-1, -1);
    REQUIRE(result.changed == false);
    REQUIRE(sm.currentLevel() == ViewLevel::CONTINENT);
}

TEST_CASE("ViewStateMachine: zoomOut from ZONE to CONTINENT", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::ZONE);
    sm.setContinentIdx(0);
    sm.setCurrentZoneIdx(5);

    auto result = sm.zoomOut();
    REQUIRE(result.changed == true);
    REQUIRE(result.newLevel == ViewLevel::CONTINENT);
    REQUIRE(result.targetIdx == 0);
    REQUIRE(sm.currentLevel() == ViewLevel::CONTINENT);
}

TEST_CASE("ViewStateMachine: zoomOut from ZONE without continent does nothing", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::ZONE);
    sm.setContinentIdx(-1);

    auto result = sm.zoomOut();
    REQUIRE(result.changed == false);
    REQUIRE(sm.currentLevel() == ViewLevel::ZONE);
}

TEST_CASE("ViewStateMachine: zoomOut from CONTINENT to WORLD", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::CONTINENT);

    auto result = sm.zoomOut();
    REQUIRE(result.changed == true);
    REQUIRE(result.newLevel == ViewLevel::WORLD);
    REQUIRE(sm.currentLevel() == ViewLevel::WORLD);
}

TEST_CASE("ViewStateMachine: zoomOut from WORLD to COSMIC when enabled", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::WORLD);
    sm.setCosmicEnabled(true);

    auto result = sm.zoomOut();
    REQUIRE(result.changed == true);
    REQUIRE(result.newLevel == ViewLevel::COSMIC);
}

TEST_CASE("ViewStateMachine: zoomOut from WORLD stays when cosmic disabled", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::WORLD);
    sm.setCosmicEnabled(false);

    auto result = sm.zoomOut();
    REQUIRE(result.changed == false);
    REQUIRE(sm.currentLevel() == ViewLevel::WORLD);
}

TEST_CASE("ViewStateMachine: zoomIn from COSMIC goes to WORLD", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::COSMIC);
    sm.setCosmicEnabled(true);

    auto result = sm.zoomIn(-1, -1);
    REQUIRE(result.changed == true);
    REQUIRE(result.newLevel == ViewLevel::WORLD);
}

TEST_CASE("ViewStateMachine: zoomIn from WORLD to CONTINENT with continent set", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::WORLD);
    sm.setContinentIdx(3);

    auto result = sm.zoomIn(-1, -1);
    REQUIRE(result.changed == true);
    REQUIRE(result.newLevel == ViewLevel::CONTINENT);
    REQUIRE(result.targetIdx == 3);
}

TEST_CASE("ViewStateMachine: enterWorldView sets WORLD level", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::ZONE);

    auto result = sm.enterWorldView();
    REQUIRE(result.changed == true);
    REQUIRE(result.newLevel == ViewLevel::WORLD);
    REQUIRE(sm.currentLevel() == ViewLevel::WORLD);
}

TEST_CASE("ViewStateMachine: enterCosmicView when disabled falls back to WORLD", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setCosmicEnabled(false);

    auto result = sm.enterCosmicView();
    REQUIRE(result.newLevel == ViewLevel::WORLD);
}

TEST_CASE("ViewStateMachine: enterZone goes to ZONE level", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::CONTINENT);

    auto result = sm.enterZone(7);
    REQUIRE(result.changed == true);
    REQUIRE(result.newLevel == ViewLevel::ZONE);
    REQUIRE(result.targetIdx == 7);
    REQUIRE(sm.currentZoneIdx() == 7);
}

// ── Transition animation ─────────────────────────────────────

TEST_CASE("ViewStateMachine: transition starts on zoom", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::CONTINENT);
    sm.setContinentIdx(0);
    sm.zoomIn(5, 5);

    REQUIRE(sm.transition().active == true);
    REQUIRE(sm.transition().progress == Catch::Approx(0.0f));
}

TEST_CASE("ViewStateMachine: updateTransition advances progress", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::CONTINENT);
    sm.setContinentIdx(0);
    sm.zoomIn(5, 5);

    float halfDuration = sm.transition().duration / 2.0f;
    bool stillActive = sm.updateTransition(halfDuration);
    REQUIRE(stillActive == true);
    REQUIRE(sm.transition().progress == Catch::Approx(0.5f).margin(0.01f));
}

TEST_CASE("ViewStateMachine: transition completes after full duration", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::CONTINENT);
    sm.setContinentIdx(0);
    sm.zoomIn(5, 5);

    float dur = sm.transition().duration;
    sm.updateTransition(dur + 0.1f);  // overshoot
    REQUIRE(sm.transition().active == false);
    REQUIRE(sm.transition().progress == Catch::Approx(1.0f));
}

TEST_CASE("ViewStateMachine: updateTransition when no transition returns false", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    REQUIRE(sm.updateTransition(0.1f) == false);
}

TEST_CASE("ViewStateMachine: zoomIn prefers hovered zone over player zone", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::CONTINENT);
    sm.setContinentIdx(0);

    auto result = sm.zoomIn(/*hovered=*/3, /*player=*/7);
    REQUIRE(result.targetIdx == 3);
}

TEST_CASE("ViewStateMachine: zoomIn falls back to player zone when no hover", "[world_map][view_state_machine]") {
    ViewStateMachine sm;
    sm.setLevel(ViewLevel::CONTINENT);
    sm.setContinentIdx(0);

    auto result = sm.zoomIn(/*hovered=*/-1, /*player=*/7);
    REQUIRE(result.targetIdx == 7);
}
