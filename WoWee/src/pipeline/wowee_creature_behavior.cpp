#include "pipeline/wowee_creature_behavior.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'H', 'V'};
constexpr uint32_t kVersion = 1;

template <typename T>
void writePOD(std::ofstream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool readPOD(std::ifstream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return is.gcount() == static_cast<std::streamsize>(sizeof(T));
}

void writeStr(std::ofstream& os, const std::string& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    writePOD(os, n);
    if (n > 0) os.write(s.data(), n);
}

bool readStr(std::ifstream& is, std::string& s) {
    uint32_t n = 0;
    if (!readPOD(is, n)) return false;
    if (n > (1u << 20)) return false;
    s.resize(n);
    if (n > 0) {
        is.read(s.data(), n);
        if (is.gcount() != static_cast<std::streamsize>(n)) {
            s.clear();
            return false;
        }
    }
    return true;
}

std::string normalizePath(std::string base) {
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wbhv") {
        base += ".wbhv";
    }
    return base;
}

} // namespace

const WoweeCreatureBehavior::Entry*
WoweeCreatureBehavior::findById(uint32_t behaviorId) const {
    for (const auto& e : entries)
        if (e.behaviorId == behaviorId) return &e;
    return nullptr;
}

std::vector<const WoweeCreatureBehavior::Entry*>
WoweeCreatureBehavior::findByKind(uint8_t creatureKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.creatureKind == creatureKind) out.push_back(&e);
    return out;
}

bool WoweeCreatureBehaviorLoader::save(
    const WoweeCreatureBehavior& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.behaviorId);
        writeStr(os, e.name);
        writePOD(os, e.creatureKind);
        writePOD(os, e.evadeBehavior);
        writePOD(os, e.pad0);
        writePOD(os, e.aggroRadius);
        writePOD(os, e.leashRadius);
        writePOD(os, e.corpseDurationSec);
        writePOD(os, e.mainAttackSpellId);
        uint32_t specCount =
            static_cast<uint32_t>(e.specialAbilities.size());
        writePOD(os, specCount);
        for (const auto& s : e.specialAbilities) {
            writePOD(os, s.spellId);
            writePOD(os, s.cooldownMs);
            writePOD(os, s.useChancePct);
            writePOD(os, s.pad1);
        }
    }
    return os.good();
}

WoweeCreatureBehavior WoweeCreatureBehaviorLoader::load(
    const std::string& basePath) {
    WoweeCreatureBehavior out;
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    if (!is) return out;
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return out;
    uint32_t version = 0;
    if (!readPOD(is, version) || version != kVersion) return out;
    if (!readStr(is, out.name)) return out;
    uint32_t entryCount = 0;
    if (!readPOD(is, entryCount)) return out;
    if (entryCount > (1u << 20)) return out;
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readPOD(is, e.behaviorId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.creatureKind) ||
            !readPOD(is, e.evadeBehavior) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.aggroRadius) ||
            !readPOD(is, e.leashRadius) ||
            !readPOD(is, e.corpseDurationSec) ||
            !readPOD(is, e.mainAttackSpellId)) {
            out.entries.clear(); return out;
        }
        uint32_t specCount = 0;
        if (!readPOD(is, specCount)) {
            out.entries.clear(); return out;
        }
        // Sanity cap — real bosses cap at ~6
        // abilities; format cap 32.
        if (specCount > 32) {
            out.entries.clear(); return out;
        }
        e.specialAbilities.resize(specCount);
        for (auto& s : e.specialAbilities) {
            if (!readPOD(is, s.spellId) ||
                !readPOD(is, s.cooldownMs) ||
                !readPOD(is, s.useChancePct) ||
                !readPOD(is, s.pad1)) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeCreatureBehaviorLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

WoweeCreatureBehavior::Entry makeBehavior(
    uint32_t behaviorId, const char* name,
    uint8_t creatureKind, uint8_t evadeBehavior,
    float aggroRadius, float leashRadius,
    uint32_t corpseDurationSec, uint32_t mainAttackSpellId,
    std::vector<WoweeCreatureBehavior::SpecialAbility>
        specials) {
    WoweeCreatureBehavior::Entry e;
    e.behaviorId = behaviorId; e.name = name;
    e.creatureKind = creatureKind;
    e.evadeBehavior = evadeBehavior;
    e.aggroRadius = aggroRadius;
    e.leashRadius = leashRadius;
    e.corpseDurationSec = corpseDurationSec;
    e.mainAttackSpellId = mainAttackSpellId;
    e.specialAbilities = std::move(specials);
    return e;
}

WoweeCreatureBehavior::SpecialAbility makeSpec(
    uint32_t spellId, uint32_t cooldownMs,
    uint16_t useChancePct) {
    WoweeCreatureBehavior::SpecialAbility s;
    s.spellId = spellId;
    s.cooldownMs = cooldownMs;
    s.useChancePct = useChancePct;
    return s;
}

} // namespace

WoweeCreatureBehavior WoweeCreatureBehaviorLoader::makeMeleeBehaviors(
    const std::string& catalogName) {
    using B = WoweeCreatureBehavior;
    WoweeCreatureBehavior c;
    c.name = catalogName;
    // Kobold: aggro 8yd, leash 30yd, melee swing
    // (spellId 0 = no override = use default melee).
    // 1 special: throw-rock at 5% chance.
    c.entries.push_back(makeBehavior(
        1, "Kobold Worker",
        B::Melee, B::ResetToSpawn,
        8.f, 30.f, 60, 0,
        {makeSpec(11876 /* throw rock */, 8000, 500)}));
    // Wolf: aggro 10yd (predator scent), pet-style
    // claw-bite rotation.
    c.entries.push_back(makeBehavior(
        2, "Timber Wolf",
        B::Beast, B::ResetToSpawn,
        10.f, 35.f, 60, 0,
        {makeSpec(3009 /* claw */, 5000, 1500)}));
    // Raptor: aggro 9yd, faster ResetToSpawn (these
    // are noted runners). 1 chase-leap special.
    c.entries.push_back(makeBehavior(
        3, "Stranglethorn Raptor",
        B::Beast, B::ResetToSpawn,
        9.f, 40.f, 60, 0,
        {makeSpec(7165 /* leap */, 12000, 2000)}));
    return c;
}

WoweeCreatureBehavior
WoweeCreatureBehaviorLoader::makeCasterBehaviors(
    const std::string& catalogName) {
    using B = WoweeCreatureBehavior;
    WoweeCreatureBehavior c;
    c.name = catalogName;
    // Defias Wizard: caster, fireball default rotation,
    // Polymorph + Frost Nova specials.
    c.entries.push_back(makeBehavior(
        10, "Defias Wizard",
        B::Caster, B::ResetToSpawn,
        20.f, 60.f, 60, 133 /* Fireball */,
        {makeSpec(118 /* Polymorph */, 30000, 3000),
         makeSpec(122 /* Frost Nova */, 25000, 2500)}));
    // Murloc Coastrunner: low-level caster with bolt-
    // type ability + heal-self special.
    c.entries.push_back(makeBehavior(
        11, "Murloc Coastrunner",
        B::Caster, B::ResetToSpawn,
        15.f, 35.f, 60, 11979 /* Frost Bolt */,
        {makeSpec(2050 /* Lesser Heal */, 20000, 1500)}));
    // Voidwalker (warlock pet pattern): tank-caster
    // hybrid with taunt + sacrifice.
    c.entries.push_back(makeBehavior(
        12, "Voidwalker Pet Pattern",
        B::Tank, B::ResetToSpawn,
        12.f, 40.f, 60, 0 /* default melee */,
        {makeSpec(7264 /* Taunt */, 10000, 5000),
         makeSpec(7812 /* Sacrifice */, 0, 0),  // 0%
                                                 //  use,
                                                 //  master-
                                                 //  triggered
                                                 //  only
         makeSpec(17767 /* Suffering */, 30000, 2000)}));
    return c;
}

WoweeCreatureBehavior WoweeCreatureBehaviorLoader::makeBossBehaviors(
    const std::string& catalogName) {
    using B = WoweeCreatureBehavior;
    WoweeCreatureBehavior c;
    c.name = catalogName;
    // Onyxia-pattern boss: NoEvade (raid bosses
    // permanent-leash to encounter zone), 600s
    // (10min) corpse duration so 40-man raid can
    // distribute loot. 4 abilities in rotation.
    c.entries.push_back(makeBehavior(
        100, "Onyxia-Pattern Dragon Boss",
        B::Tank, B::NoEvade,   // dragons render as
                                //  Tank kind for AI
                                //  threat dispatch
        50.f, 999.f, 600, 0 /* default melee + flame */,
        {makeSpec(18395 /* Wing Buffet */, 25000, 4000),
         makeSpec(18392 /* Flame Breath */, 12000, 5000),
         makeSpec(18435 /* Tail Sweep */, 20000, 3500),
         makeSpec(18650 /* Deep Breath */, 90000, 2500)}));
    return c;
}

} // namespace pipeline
} // namespace wowee
