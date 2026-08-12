#include "pipeline/wowee_spells.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'P', 'L'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wspl") {
        base += ".wspl";
    }
    return base;
}

} // namespace

const WoweeSpell::Entry* WoweeSpell::findById(uint32_t spellId) const {
    for (const auto& e : entries) {
        if (e.spellId == spellId) return &e;
    }
    return nullptr;
}

const char* WoweeSpell::schoolName(uint8_t s) {
    switch (s) {
        case SchoolPhysical: return "physical";
        case SchoolHoly:     return "holy";
        case SchoolFire:     return "fire";
        case SchoolNature:   return "nature";
        case SchoolFrost:    return "frost";
        case SchoolShadow:   return "shadow";
        case SchoolArcane:   return "arcane";
        default:             return "unknown";
    }
}

const char* WoweeSpell::targetTypeName(uint8_t t) {
    switch (t) {
        case TargetSelf:        return "self";
        case TargetSingle:      return "single";
        case TargetCone:        return "cone";
        case TargetAoeFromSelf: return "aoe-self";
        case TargetLine:        return "line";
        case TargetGround:      return "ground";
        default:                return "unknown";
    }
}

const char* WoweeSpell::effectKindName(uint8_t e) {
    switch (e) {
        case EffectDamage:   return "damage";
        case EffectHeal:     return "heal";
        case EffectBuff:     return "buff";
        case EffectDebuff:   return "debuff";
        case EffectTeleport: return "teleport";
        case EffectSummon:   return "summon";
        case EffectDispel:   return "dispel";
        default:             return "unknown";
    }
}

bool WoweeSpellLoader::save(const WoweeSpell& cat,
                            const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.spellId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.school);
        writePOD(os, e.targetType);
        writePOD(os, e.effectKind);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, e.castTimeMs);
        writePOD(os, e.cooldownMs);
        writePOD(os, e.gcdMs);
        writePOD(os, e.manaCost);
        writePOD(os, e.rangeMin);
        writePOD(os, e.rangeMax);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxStacks);
        writePOD(os, e.durationMs);
        writePOD(os, e.effectValueMin);
        writePOD(os, e.effectValueMax);
        writePOD(os, e.effectMisc);
        writePOD(os, e.flags);
    }
    return os.good();
}

WoweeSpell WoweeSpellLoader::load(const std::string& basePath) {
    WoweeSpell out;
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
        if (!readPOD(is, e.spellId)) { out.entries.clear(); return out; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.school) ||
            !readPOD(is, e.targetType) ||
            !readPOD(is, e.effectKind)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.castTimeMs) ||
            !readPOD(is, e.cooldownMs) ||
            !readPOD(is, e.gcdMs) ||
            !readPOD(is, e.manaCost) ||
            !readPOD(is, e.rangeMin) ||
            !readPOD(is, e.rangeMax) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxStacks) ||
            !readPOD(is, e.durationMs) ||
            !readPOD(is, e.effectValueMin) ||
            !readPOD(is, e.effectValueMax) ||
            !readPOD(is, e.effectMisc) ||
            !readPOD(is, e.flags)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSpellLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeSpell WoweeSpellLoader::makeStarter(const std::string& catalogName) {
    WoweeSpell c;
    c.name = catalogName;
    {
        WoweeSpell::Entry e;
        e.spellId = 1; e.name = "Strike"; e.description = "Basic melee attack.";
        e.school = WoweeSpell::SchoolPhysical;
        e.targetType = WoweeSpell::TargetSingle;
        e.effectKind = WoweeSpell::EffectDamage;
        e.castTimeMs = 0; e.cooldownMs = 0; e.manaCost = 5;
        e.rangeMax = 5.0f;
        e.effectValueMin = 8; e.effectValueMax = 12;
        c.entries.push_back(e);
    }
    {
        WoweeSpell::Entry e;
        e.spellId = 2; e.name = "Lesser Heal";
        e.description = "Restores a small amount of health.";
        e.school = WoweeSpell::SchoolHoly;
        e.targetType = WoweeSpell::TargetSingle;
        e.effectKind = WoweeSpell::EffectHeal;
        e.castTimeMs = 1500; e.manaCost = 25;
        e.rangeMax = 30.0f;
        e.flags = WoweeSpell::FriendlyOnly;
        e.effectValueMin = 30; e.effectValueMax = 50;
        c.entries.push_back(e);
    }
    {
        WoweeSpell::Entry e;
        e.spellId = 3; e.name = "Power Word: Fortitude";
        e.description = "Increases stamina for 30 minutes.";
        e.school = WoweeSpell::SchoolHoly;
        e.targetType = WoweeSpell::TargetSingle;
        e.effectKind = WoweeSpell::EffectBuff;
        e.castTimeMs = 0; e.manaCost = 50;
        e.rangeMax = 30.0f;
        e.durationMs = 1800000;       // 30 min
        // Buffs use a single fixed value: min == max so the
        // validator's range check stays happy.
        e.effectValueMin = 5;
        e.effectValueMax = 5;
        e.flags = WoweeSpell::FriendlyOnly;
        c.entries.push_back(e);
    }
    {
        WoweeSpell::Entry e;
        e.spellId = 4; e.name = "Hearthstone";
        e.description = "Returns you to your home inn.";
        e.school = WoweeSpell::SchoolArcane;
        e.targetType = WoweeSpell::TargetSelf;
        e.effectKind = WoweeSpell::EffectTeleport;
        e.castTimeMs = 10000; e.cooldownMs = 3600000;   // 60 min CD
        e.flags = WoweeSpell::Channeled;
        c.entries.push_back(e);
    }
    return c;
}

WoweeSpell WoweeSpellLoader::makeMage(const std::string& catalogName) {
    WoweeSpell c;
    c.name = catalogName;
    {
        WoweeSpell::Entry e;
        e.spellId = 116; e.name = "Frostbolt";
        e.description = "A bolt of frost that slows the target.";
        e.school = WoweeSpell::SchoolFrost;
        e.targetType = WoweeSpell::TargetSingle;
        e.effectKind = WoweeSpell::EffectDamage;
        e.castTimeMs = 2500; e.manaCost = 25;
        e.rangeMax = 30.0f;
        e.minLevel = 4;
        e.effectValueMin = 27; e.effectValueMax = 31;
        e.effectMisc = -50;          // -50% movement speed (debuff hint)
        e.flags = WoweeSpell::HostileOnly;
        c.entries.push_back(e);
    }
    {
        WoweeSpell::Entry e;
        e.spellId = 133; e.name = "Fireball";
        e.description = "Hurls a fiery ball that explodes on impact.";
        e.school = WoweeSpell::SchoolFire;
        e.targetType = WoweeSpell::TargetSingle;
        e.effectKind = WoweeSpell::EffectDamage;
        e.castTimeMs = 3500; e.manaCost = 30;
        e.rangeMax = 35.0f;
        e.minLevel = 1;
        e.effectValueMin = 14; e.effectValueMax = 22;
        e.flags = WoweeSpell::HostileOnly;
        c.entries.push_back(e);
    }
    {
        WoweeSpell::Entry e;
        e.spellId = 1459; e.name = "Arcane Intellect";
        e.description = "Increases the target's intellect for 30 minutes.";
        e.school = WoweeSpell::SchoolArcane;
        e.targetType = WoweeSpell::TargetSingle;
        e.effectKind = WoweeSpell::EffectBuff;
        e.castTimeMs = 0; e.manaCost = 80;
        e.rangeMax = 30.0f;
        e.durationMs = 1800000;
        e.effectValueMin = 3;
        e.effectValueMax = 3;
        e.flags = WoweeSpell::FriendlyOnly;
        c.entries.push_back(e);
    }
    {
        WoweeSpell::Entry e;
        e.spellId = 1953; e.name = "Blink";
        e.description = "Teleports 20 yards forward.";
        e.school = WoweeSpell::SchoolArcane;
        e.targetType = WoweeSpell::TargetSelf;
        e.effectKind = WoweeSpell::EffectTeleport;
        e.castTimeMs = 0; e.cooldownMs = 15000; e.manaCost = 60;
        e.minLevel = 20;
        c.entries.push_back(e);
    }
    return c;
}

WoweeSpell WoweeSpellLoader::makeWarrior(const std::string& catalogName) {
    WoweeSpell c;
    c.name = catalogName;
    {
        WoweeSpell::Entry e;
        e.spellId = 78; e.name = "Heroic Strike";
        e.description = "A strong melee attack that consumes rage.";
        e.school = WoweeSpell::SchoolPhysical;
        e.targetType = WoweeSpell::TargetSingle;
        e.effectKind = WoweeSpell::EffectDamage;
        e.castTimeMs = 0; e.manaCost = 15;       // rage cost reuses field
        e.rangeMax = 5.0f;
        e.gcdMs = 0;                              // off-GCD
        e.flags = WoweeSpell::HostileOnly;
        e.effectValueMin = 11; e.effectValueMax = 11;
        c.entries.push_back(e);
    }
    {
        WoweeSpell::Entry e;
        e.spellId = 6343; e.name = "Thunder Clap";
        e.description = "Damages nearby enemies and slows attack speed.";
        e.school = WoweeSpell::SchoolPhysical;
        e.targetType = WoweeSpell::TargetAoeFromSelf;
        e.effectKind = WoweeSpell::EffectDamage;
        e.castTimeMs = 0; e.cooldownMs = 4000; e.manaCost = 20;
        e.rangeMax = 8.0f;
        e.minLevel = 6;
        e.effectValueMin = 18; e.effectValueMax = 20;
        e.flags = WoweeSpell::HostileOnly | WoweeSpell::AreaOfEffect;
        c.entries.push_back(e);
    }
    {
        WoweeSpell::Entry e;
        e.spellId = 6673; e.name = "Battle Shout";
        e.description = "Increases melee attack power for 2 minutes.";
        e.school = WoweeSpell::SchoolPhysical;
        e.targetType = WoweeSpell::TargetAoeFromSelf;
        e.effectKind = WoweeSpell::EffectBuff;
        e.castTimeMs = 0; e.manaCost = 10;
        e.rangeMax = 20.0f;
        e.durationMs = 120000;
        e.effectValueMin = 25;
        e.effectValueMax = 25;
        e.flags = WoweeSpell::FriendlyOnly | WoweeSpell::AreaOfEffect;
        c.entries.push_back(e);
    }
    {
        WoweeSpell::Entry e;
        e.spellId = 12294; e.name = "Mortal Strike";
        e.description = "Hard-hitting strike that reduces healing taken.";
        e.school = WoweeSpell::SchoolPhysical;
        e.targetType = WoweeSpell::TargetSingle;
        e.effectKind = WoweeSpell::EffectDamage;
        e.castTimeMs = 0; e.cooldownMs = 6000; e.manaCost = 30;
        e.rangeMax = 5.0f;
        e.minLevel = 40;
        e.effectValueMin = 75; e.effectValueMax = 100;
        e.effectMisc = -50;          // -50% healing applied (debuff hint)
        e.flags = WoweeSpell::HostileOnly;
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
