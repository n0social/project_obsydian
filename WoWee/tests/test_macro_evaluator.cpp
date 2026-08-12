// Tests for MacroEvaluator — WoW macro conditional parsing and evaluation.
// Phase 4.5 of chat_panel_ref.md.

#include <catch_amalgamated.hpp>
#include "ui/chat/macro_evaluator.hpp"
#include "ui/chat/i_game_state.hpp"
#include "ui/chat/i_modifier_state.hpp"
#include <unordered_map>
#include <unordered_set>

using namespace wowee::ui;

// ── Mock IGameState ─────────────────────────────────────────

class MockGameState : public IGameState {
public:
    // GUIDs
    uint64_t playerGuid = 1;
    uint64_t targetGuid = 2;
    uint64_t focusGuid  = 0;
    uint64_t petGuid    = 0;
    uint64_t mouseoverGuid = 0;

    // State flags
    bool inCombat   = false;
    bool mounted    = false;
    bool swimming   = false;
    bool flying     = false;
    bool casting    = false;
    bool channeling = false;
    bool stealthed  = false;
    bool pet        = false;
    bool inGroup    = false;
    bool inRaid     = false;
    bool indoors    = false;

    // Numeric
    uint8_t  talentSpec    = 0;
    uint32_t vehicleId     = 0;
    uint32_t castSpellId   = 0;
    std::string castSpellName;

    // Entity states (guid → exists, dead, hostile)
    struct EntityInfo { bool exists = true; bool dead = false; bool hostile = false; };
    std::unordered_map<uint64_t, EntityInfo> entities;

    // Form / aura
    bool formAura = false;
    // Aura checks: map of "spellname_debuff" → true
    std::unordered_set<std::string> auras;

    // IGameState implementation
    uint64_t getPlayerGuid() const override { return playerGuid; }
    uint64_t getTargetGuid() const override { return targetGuid; }
    uint64_t getFocusGuid() const override  { return focusGuid; }
    uint64_t getPetGuid() const override    { return petGuid; }
    uint64_t getMouseoverGuid() const override { return mouseoverGuid; }

    bool isInCombat() const override  { return inCombat; }
    bool isMounted() const override   { return mounted; }
    bool isSwimming() const override  { return swimming; }
    bool isFlying() const override    { return flying; }
    bool isCasting() const override   { return casting; }
    bool isChanneling() const override { return channeling; }
    bool isStealthed() const override { return stealthed; }
    bool hasPet() const override      { return pet; }
    bool isInGroup() const override   { return inGroup; }
    bool isInRaid() const override    { return inRaid; }
    bool isIndoors() const override   { return indoors; }

    uint8_t getActiveTalentSpec() const override { return talentSpec; }
    uint32_t getVehicleId() const override { return vehicleId; }
    uint32_t getCurrentCastSpellId() const override { return castSpellId; }

    std::string getSpellName(uint32_t /*spellId*/) const override { return castSpellName; }

    bool hasAuraByName(uint64_t /*targetGuid*/, const std::string& spellName,
                       bool wantDebuff) const override {
        std::string key = spellName + (wantDebuff ? "_debuff" : "_buff");
        // Lowercase for comparison
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return auras.count(key) > 0;
    }

    bool hasFormAura() const override { return formAura; }

    bool entityExists(uint64_t guid) const override {
        if (guid == 0 || guid == static_cast<uint64_t>(-1)) return false;
        auto it = entities.find(guid);
        return it != entities.end() && it->second.exists;
    }
    bool entityIsDead(uint64_t guid) const override {
        auto it = entities.find(guid);
        return it != entities.end() && it->second.dead;
    }
    bool entityIsHostile(uint64_t guid) const override {
        auto it = entities.find(guid);
        return it != entities.end() && it->second.hostile;
    }

private:
    // Need these for unordered_set/map
    struct StringHash { size_t operator()(const std::string& s) const { return std::hash<std::string>{}(s); } };
};

// ── Mock IModifierState ─────────────────────────────────────

class MockModState : public IModifierState {
public:
    bool shift = false;
    bool ctrl  = false;
    bool alt   = false;

    bool isShiftHeld() const override { return shift; }
    bool isCtrlHeld() const override  { return ctrl; }
    bool isAltHeld() const override   { return alt; }
};

// ── Helper ──────────────────────────────────────────────────

struct TestFixture {
    MockGameState gs;
    MockModState  ms;
    MacroEvaluator eval{gs, ms};

    std::string run(const std::string& input) {
        uint64_t tgt;
        return eval.evaluate(input, tgt);
    }

    std::pair<std::string, uint64_t> runWithTarget(const std::string& input) {
        uint64_t tgt;
        std::string result = eval.evaluate(input, tgt);
        return {result, tgt};
    }
};

// ── Basic parsing tests ─────────────────────────────────────

TEST_CASE("No conditionals returns input as-is", "[macro_eval]") {
    TestFixture f;
    CHECK(f.run("Fireball") == "Fireball");
}

TEST_CASE("Empty string returns empty", "[macro_eval]") {
    TestFixture f;
    CHECK(f.run("") == "");
}

TEST_CASE("Whitespace-only returns empty", "[macro_eval]") {
    TestFixture f;
    CHECK(f.run("   ") == "");
}

TEST_CASE("Semicolon-separated alternatives: first matches", "[macro_eval]") {
    TestFixture f;
    CHECK(f.run("Fireball; Frostbolt") == "Fireball");
}

TEST_CASE("Default fallback after conditions", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = false;
    CHECK(f.run("[combat] Attack; Heal") == "Heal");
}

// ── Combat conditions ───────────────────────────────────────

TEST_CASE("[combat] true when in combat", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = true;
    CHECK(f.run("[combat] Attack; Heal") == "Attack");
}

TEST_CASE("[combat] false when not in combat", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = false;
    CHECK(f.run("[combat] Attack; Heal") == "Heal");
}

TEST_CASE("[nocombat] true when not in combat", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = false;
    CHECK(f.run("[nocombat] Heal; Attack") == "Heal");
}

TEST_CASE("[nocombat] false when in combat", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = true;
    CHECK(f.run("[nocombat] Heal; Attack") == "Attack");
}

// ── Modifier conditions ─────────────────────────────────────

TEST_CASE("[mod:shift] true when shift held", "[macro_eval]") {
    TestFixture f;
    f.ms.shift = true;
    CHECK(f.run("[mod:shift] Polymorph; Fireball") == "Polymorph");
}

TEST_CASE("[mod:shift] false when shift not held", "[macro_eval]") {
    TestFixture f;
    f.ms.shift = false;
    CHECK(f.run("[mod:shift] Polymorph; Fireball") == "Fireball");
}

TEST_CASE("[mod:ctrl] true when ctrl held", "[macro_eval]") {
    TestFixture f;
    f.ms.ctrl = true;
    CHECK(f.run("[mod:ctrl] Decurse; Fireball") == "Decurse");
}

TEST_CASE("[mod:alt] true when alt held", "[macro_eval]") {
    TestFixture f;
    f.ms.alt = true;
    CHECK(f.run("[mod:alt] Special; Normal") == "Special");
}

TEST_CASE("[nomod] true when no modifier held", "[macro_eval]") {
    TestFixture f;
    CHECK(f.run("[nomod] Normal; Modified") == "Normal");
}

TEST_CASE("[nomod] false when shift held", "[macro_eval]") {
    TestFixture f;
    f.ms.shift = true;
    CHECK(f.run("[nomod] Normal; Modified") == "Modified");
}

// ── Target specifiers ───────────────────────────────────────

TEST_CASE("[@player] sets target override to player guid", "[macro_eval]") {
    TestFixture f;
    f.gs.playerGuid = 42;
    auto [result, tgt] = f.runWithTarget("[@player] Heal");
    CHECK(result == "Heal");
    CHECK(tgt == 42);
}

TEST_CASE("[@focus] sets target override to focus guid", "[macro_eval]") {
    TestFixture f;
    f.gs.focusGuid = 99;
    auto [result, tgt] = f.runWithTarget("[@focus] Heal");
    CHECK(result == "Heal");
    CHECK(tgt == 99);
}

TEST_CASE("[@pet] fails when no pet", "[macro_eval]") {
    TestFixture f;
    f.gs.petGuid = 0;
    CHECK(f.run("[@pet] Mend Pet; Steady Shot") == "Steady Shot");
}

TEST_CASE("[@pet] succeeds when pet exists", "[macro_eval]") {
    TestFixture f;
    f.gs.petGuid = 50;
    auto [result, tgt] = f.runWithTarget("[@pet] Mend Pet; Steady Shot");
    CHECK(result == "Mend Pet");
    CHECK(tgt == 50);
}

TEST_CASE("[@mouseover] fails when no mouseover", "[macro_eval]") {
    TestFixture f;
    f.gs.mouseoverGuid = 0;
    CHECK(f.run("[@mouseover] Heal; Flash Heal") == "Flash Heal");
}

TEST_CASE("[@mouseover] succeeds when mouseover exists", "[macro_eval]") {
    TestFixture f;
    f.gs.mouseoverGuid = 77;
    auto [result, tgt] = f.runWithTarget("[@mouseover] Heal; Flash Heal");
    CHECK(result == "Heal");
    CHECK(tgt == 77);
}

TEST_CASE("[target=focus] sets target override", "[macro_eval]") {
    TestFixture f;
    f.gs.focusGuid = 33;
    auto [result, tgt] = f.runWithTarget("[target=focus] Polymorph");
    CHECK(result == "Polymorph");
    CHECK(tgt == 33);
}

// ── Entity conditions (exists/dead/help/harm) ───────────────

TEST_CASE("[exists] true when target entity exists", "[macro_eval]") {
    TestFixture f;
    f.gs.targetGuid = 10;
    f.gs.entities[10] = {true, false, false};
    CHECK(f.run("[exists] Attack; Buff") == "Attack");
}

TEST_CASE("[exists] false when no target", "[macro_eval]") {
    TestFixture f;
    f.gs.targetGuid = 0;
    CHECK(f.run("[exists] Attack; Buff") == "Buff");
}

TEST_CASE("[dead] true when target is dead", "[macro_eval]") {
    TestFixture f;
    f.gs.targetGuid = 10;
    f.gs.entities[10] = {true, true, false};
    CHECK(f.run("[dead] Resurrect; Heal") == "Resurrect");
}

TEST_CASE("[nodead] true when target is alive", "[macro_eval]") {
    TestFixture f;
    f.gs.targetGuid = 10;
    f.gs.entities[10] = {true, false, false};
    CHECK(f.run("[nodead] Heal; Resurrect") == "Heal");
}

TEST_CASE("[harm] true when target is hostile", "[macro_eval]") {
    TestFixture f;
    f.gs.targetGuid = 10;
    f.gs.entities[10] = {true, false, true};
    CHECK(f.run("[harm] Attack; Heal") == "Attack");
}

TEST_CASE("[help] true when target is friendly", "[macro_eval]") {
    TestFixture f;
    f.gs.targetGuid = 10;
    f.gs.entities[10] = {true, false, false};
    CHECK(f.run("[help] Heal; Attack") == "Heal");
}

// ── Chained conditions ──────────────────────────────────────

TEST_CASE("Chained conditions all must pass", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = true;
    f.ms.shift = true;
    CHECK(f.run("[combat,mod:shift] Special; Normal") == "Special");
}

TEST_CASE("Chained conditions: one fails → whole group fails", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = true;
    f.ms.shift = false;
    CHECK(f.run("[combat,mod:shift] Special; Normal") == "Normal");
}

// ── State conditions ────────────────────────────────────────

TEST_CASE("[mounted] true when mounted", "[macro_eval]") {
    TestFixture f;
    f.gs.mounted = true;
    CHECK(f.run("[mounted] Dismount; Mount") == "Dismount");
}

TEST_CASE("[swimming] true when swimming", "[macro_eval]") {
    TestFixture f;
    f.gs.swimming = true;
    CHECK(f.run("[swimming] Swim; Walk") == "Swim");
}

TEST_CASE("[flying] true when flying", "[macro_eval]") {
    TestFixture f;
    f.gs.flying = true;
    CHECK(f.run("[flying] Land; Fly") == "Land");
}

TEST_CASE("[stealthed] true when stealthed", "[macro_eval]") {
    TestFixture f;
    f.gs.stealthed = true;
    CHECK(f.run("[stealthed] Ambush; Sinister Strike") == "Ambush");
}

TEST_CASE("[indoors] true when indoors", "[macro_eval]") {
    TestFixture f;
    f.gs.indoors = true;
    CHECK(f.run("[indoors] Walk; Mount") == "Walk");
}

TEST_CASE("[outdoors] true when not indoors", "[macro_eval]") {
    TestFixture f;
    f.gs.indoors = false;
    CHECK(f.run("[outdoors] Mount; Walk") == "Mount");
}

TEST_CASE("[group] true when in group", "[macro_eval]") {
    TestFixture f;
    f.gs.inGroup = true;
    CHECK(f.run("[group] Party Spell; Solo Spell") == "Party Spell");
}

TEST_CASE("[nogroup] true when not in group", "[macro_eval]") {
    TestFixture f;
    f.gs.inGroup = false;
    CHECK(f.run("[nogroup] Solo; Party") == "Solo");
}

TEST_CASE("[raid] true when in raid", "[macro_eval]") {
    TestFixture f;
    f.gs.inRaid = true;
    CHECK(f.run("[raid] Raid Spell; Normal") == "Raid Spell");
}

TEST_CASE("[pet] true when has pet", "[macro_eval]") {
    TestFixture f;
    f.gs.pet = true;
    CHECK(f.run("[pet] Mend Pet; Steady Shot") == "Mend Pet");
}

TEST_CASE("[nopet] true when no pet", "[macro_eval]") {
    TestFixture f;
    f.gs.pet = false;
    CHECK(f.run("[nopet] Steady Shot; Mend Pet") == "Steady Shot");
}

// ── Spec conditions ─────────────────────────────────────────

TEST_CASE("[spec:1] matches primary spec (0-based index 0)", "[macro_eval]") {
    TestFixture f;
    f.gs.talentSpec = 0;
    CHECK(f.run("[spec:1] Heal; DPS") == "Heal");
}

TEST_CASE("[spec:2] matches secondary spec (0-based index 1)", "[macro_eval]") {
    TestFixture f;
    f.gs.talentSpec = 1;
    CHECK(f.run("[spec:2] DPS; Heal") == "DPS");
}

TEST_CASE("[spec:1] fails when spec is secondary", "[macro_eval]") {
    TestFixture f;
    f.gs.talentSpec = 1;
    CHECK(f.run("[spec:1] Heal; DPS") == "DPS");
}

// ── Form / stance ───────────────────────────────────────────

TEST_CASE("[noform] true when no form aura", "[macro_eval]") {
    TestFixture f;
    f.gs.formAura = false;
    CHECK(f.run("[noform] Cast; Shift") == "Cast");
}

TEST_CASE("[noform] false when in form", "[macro_eval]") {
    TestFixture f;
    f.gs.formAura = true;
    CHECK(f.run("[noform] Cast; Shift") == "Shift");
}

// ── Vehicle ─────────────────────────────────────────────────

TEST_CASE("[vehicle] true when in vehicle", "[macro_eval]") {
    TestFixture f;
    f.gs.vehicleId = 100;
    CHECK(f.run("[vehicle] Vehicle Spell; Normal") == "Vehicle Spell");
}

TEST_CASE("[novehicle] true when not in vehicle", "[macro_eval]") {
    TestFixture f;
    f.gs.vehicleId = 0;
    CHECK(f.run("[novehicle] Normal; Vehicle Spell") == "Normal");
}

// ── Casting / channeling ────────────────────────────────────

TEST_CASE("[casting] true when casting", "[macro_eval]") {
    TestFixture f;
    f.gs.casting = true;
    CHECK(f.run("[casting] Interrupt; Cast") == "Interrupt");
}

TEST_CASE("[channeling] requires both casting and channeling", "[macro_eval]") {
    TestFixture f;
    f.gs.casting = true;
    f.gs.channeling = true;
    CHECK(f.run("[channeling] Stop; Continue") == "Stop");
}

TEST_CASE("[channeling] false when not channeling", "[macro_eval]") {
    TestFixture f;
    f.gs.casting = true;
    f.gs.channeling = false;
    CHECK(f.run("[channeling] Stop; Continue") == "Continue");
}

// ── Multiple alternatives with conditions ───────────────────

TEST_CASE("Three alternatives: first condition fails, second matches", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = false;
    f.gs.mounted = true;
    CHECK(f.run("[combat] Attack; [mounted] Dismount; Idle") == "Dismount");
}

TEST_CASE("Three alternatives: all conditions fail, default used", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = false;
    f.gs.mounted = false;
    CHECK(f.run("[combat] Attack; [mounted] Dismount; Idle") == "Idle");
}

TEST_CASE("No matching conditions and no default returns empty", "[macro_eval]") {
    TestFixture f;
    f.gs.inCombat = false;
    CHECK(f.run("[combat] Attack") == "");
}

// ── Unknown conditions are permissive ───────────────────────

TEST_CASE("Unknown condition passes (permissive)", "[macro_eval]") {
    TestFixture f;
    CHECK(f.run("[unknowncond] Spell") == "Spell");
}

// ── targetOverride is -1 when no target specifier ───────────

TEST_CASE("No target specifier leaves override as -1", "[macro_eval]") {
    TestFixture f;
    auto [result, tgt] = f.runWithTarget("[combat] Spell");
    CHECK(tgt == static_cast<uint64_t>(-1));
}

// ── Malformed input ──────────────────────────────────────────

TEST_CASE("Missing closing bracket skips alternative", "[macro_eval]") {
    TestFixture f;
    // [combat without ] → skip, then "Fallback" matches
    CHECK(f.run("[combat Spell; Fallback") == "Fallback");
}

TEST_CASE("Empty brackets match (empty condition = true)", "[macro_eval]") {
    TestFixture f;
    CHECK(f.run("[] Spell") == "Spell");
}
