#include <catch_amalgamated.hpp>

#include "pipeline/dbc_layout.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// The spell code reads DBC fields by name, resolving them through the per-expansion
// layout files. If a layout loses a field the code depends on, the lookup returns
// "missing" and the behaviour silently degrades rather than failing: a Spell layout
// with no RangeIndex makes every spell's range unknown, which turns off melee range
// checks and self-cast detection across the board. Nothing else would catch that, so
// pin the fields the spell logic actually depends on.

using wowee::pipeline::DBCLayout;

namespace {

constexpr uint32_t kMissing = 0xFFFFFFFF;

const std::vector<std::string>& expansions() {
    static const std::vector<std::string> kExpansions{"classic", "tbc", "wotlk", "turtle"};
    return kExpansions;
}

std::string layoutPath(const std::string& expansion) {
    return (std::filesystem::path(WOWEE_SOURCE_DIR) /
            "Data" / "expansions" / expansion / "dbc_layouts.json").string();
}

} // namespace

TEST_CASE("Every expansion ships a loadable DBC layout", "[dbc][layout]") {
    for (const auto& expansion : expansions()) {
        INFO("expansion: " << expansion);
        DBCLayout layout;
        REQUIRE(layout.loadFromJson(layoutPath(expansion)));
    }
}

TEST_CASE("Spell layout exposes the fields the spell logic reads", "[dbc][layout]") {
    for (const auto& expansion : expansions()) {
        INFO("expansion: " << expansion);
        DBCLayout layout;
        REQUIRE(layout.loadFromJson(layoutPath(expansion)));

        const auto* spell = layout.getLayout("Spell");
        REQUIRE(spell != nullptr);

        // Identity and display.
        REQUIRE(spell->field("ID") != kMissing);
        REQUIRE(spell->field("Name") != kMissing);
        // Rank drives superseded-rank resolution ("Rank 3" -> 3).
        REQUIRE(spell->field("Rank") != kMissing);

        // RangeIndex resolves against SpellRange.dbc and decides whether a spell is
        // self-cast (0 yards) or melee (Combat Range, 5 yards). Lose it and Battle
        // Shout reads as a melee ability again.
        REQUIRE(spell->field("RangeIndex") != kMissing);

        // Duration drives aura timers.
        REQUIRE(spell->field("DurationIndex") != kMissing);

        // School is per-expansion: a bitmask on TBC/WotLK, a 0-6 enum on Classic/Turtle.
        const bool hasSchool = spell->field("SchoolMask") != kMissing ||
                               spell->field("SchoolEnum") != kMissing;
        REQUIRE(hasSchool);
    }
}

TEST_CASE("SpellRange layout exposes MaxRange", "[dbc][layout]") {
    for (const auto& expansion : expansions()) {
        INFO("expansion: " << expansion);
        DBCLayout layout;
        REQUIRE(layout.loadFromJson(layoutPath(expansion)));

        const auto* range = layout.getLayout("SpellRange");
        REQUIRE(range != nullptr);

        // Without MaxRange the RangeIndex cannot be resolved into yards, and every
        // spell falls back to "unknown range".
        REQUIRE(range->field("MaxRange") != kMissing);
    }
}

TEST_CASE("Spell field indices are distinct", "[dbc][layout]") {
    // A copy-paste slip that points two fields at the same column reads the wrong
    // data rather than failing, so check the ones that sit next to each other.
    for (const auto& expansion : expansions()) {
        INFO("expansion: " << expansion);
        DBCLayout layout;
        REQUIRE(layout.loadFromJson(layoutPath(expansion)));
        const auto* spell = layout.getLayout("Spell");
        REQUIRE(spell != nullptr);

        const uint32_t id = spell->field("ID");
        const uint32_t name = spell->field("Name");
        const uint32_t rank = spell->field("Rank");
        const uint32_t rangeIndex = spell->field("RangeIndex");
        const uint32_t durationIndex = spell->field("DurationIndex");

        REQUIRE(id != name);
        REQUIRE(name != rank);
        REQUIRE(rangeIndex != durationIndex);
        REQUIRE(rangeIndex != rank);
    }
}

// CharacterFacialHairStyles drives the three facial-feature geoset channels: a
// beard, and for races like the Draenei the face tendrils. The geoset columns
// are not where the obvious reading of the WotLK definition puts them — in every
// copy of the DBC shipped here, columns 3-5 hold a constant per race and the
// variant numbers are at 6-8. Reading 3-5 yields values like 2010429269, which
// match no geoset in any model, so every character silently lost their facial
// features. Pin the columns against the real files.
TEST_CASE("CharacterFacialHairStyles geoset columns hold plausible variants",
          "[dbc][layout]") {
    for (const auto& expansion : expansions()) {
        DBCLayout layout;
        REQUIRE(layout.loadFromJson(layoutPath(expansion)));
        const auto* fm = layout.getLayout("CharacterFacialHairStyles");
        INFO("expansion: " << expansion);
        REQUIRE(fm != nullptr);

        // Whatever the columns are, they must not be the ones holding the
        // per-race constant, and the three channels must be distinct.
        const uint32_t g100 = (*fm)["Geoset100"];
        const uint32_t g200 = (*fm)["Geoset200"];
        const uint32_t g300 = (*fm)["Geoset300"];
        CHECK(g100 != kMissing);
        CHECK(g200 != kMissing);
        CHECK(g300 != kMissing);
        CHECK(g100 != g200);
        CHECK(g100 != g300);
        CHECK(g200 != g300);
        CHECK(g100 >= 6);
        CHECK(g200 >= 6);
        CHECK(g300 >= 6);

        // And they must not collide with the identity columns.
        CHECK((*fm)["RaceID"] == 0);
        CHECK((*fm)["SexID"] == 1);
        CHECK((*fm)["Variation"] == 2);
    }
}

// ── Update-field indices ────────────────────────────────────────────────────

// These are read straight out of the wire, so a wrong index does not fail — it
// quietly reports whatever field happens to sit there. UNIT_FIELD_CRITTER is the
// only signal that a non-combat companion is out (it has no aura), and the
// dismiss toggle keys off it, so pin it against neighbours whose values are
// independently known.
TEST_CASE("UNIT_FIELD_CRITTER sits where the stock WotLK layout puts it",
          "[update_fields]") {
    const std::string path = (std::filesystem::path(WOWEE_SOURCE_DIR) /
        "Data" / "expansions" / "wotlk" / "update_fields.json").string();
    std::ifstream in(path);
    REQUIRE(in.good());
    nlohmann::json j;
    in >> j;

    // Anchors. In 3.3.5a the unit block starts at OBJECT_END = 6, which fixes
    // BYTES_0 at 23, HEALTH at 24 and MAXHEALTH at 32. If these still hold, the
    // layout is the stock one and CRITTER is at 10.
    REQUIRE(j["UNIT_FIELD_BYTES_0"].get<uint32_t>() == 23u);
    REQUIRE(j["UNIT_FIELD_HEALTH"].get<uint32_t>() == 24u);
    REQUIRE(j["UNIT_FIELD_MAXHEALTH"].get<uint32_t>() == 32u);

    REQUIRE(j.contains("UNIT_FIELD_CRITTER"));
    REQUIRE(j["UNIT_FIELD_CRITTER"].get<uint32_t>() == 10u);
}

TEST_CASE("Pre-WotLK expansions publish no companion field", "[update_fields]") {
    // The field did not exist before WotLK, and neither did CMSG_DISMISS_CRITTER.
    // Claiming an index here would read some unrelated field and hand the dismiss
    // toggle a guid that is not a companion.
    for (const char* expansion : {"classic", "tbc", "turtle"}) {
        const std::string path = (std::filesystem::path(WOWEE_SOURCE_DIR) /
            "Data" / "expansions" / expansion / "update_fields.json").string();
        std::ifstream in(path);
        if (!in.good()) continue;
        nlohmann::json j;
        in >> j;
        INFO(expansion);
        REQUIRE_FALSE(j.contains("UNIT_FIELD_CRITTER"));
    }
}
