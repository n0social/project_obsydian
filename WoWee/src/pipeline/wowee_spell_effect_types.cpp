#include "pipeline/wowee_spell_effect_types.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'E', 'F'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wsef") {
        base += ".wsef";
    }
    return base;
}

uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
            static_cast<uint32_t>(r);
}

} // namespace

const WoweeSpellEffectType::Entry*
WoweeSpellEffectType::findById(uint32_t effectId) const {
    for (const auto& e : entries)
        if (e.effectId == effectId) return &e;
    return nullptr;
}

const char* WoweeSpellEffectType::effectKindName(uint8_t k) {
    switch (k) {
        case Damage:   return "damage";
        case Heal:     return "heal";
        case Aura:     return "aura";
        case Energize: return "energize";
        case Trigger:  return "trigger";
        case Movement: return "movement";
        case Summon:   return "summon";
        case Dispel:   return "dispel";
        case Dummy:    return "dummy";
        case Misc:     return "misc";
        default:       return "unknown";
    }
}

bool WoweeSpellEffectTypeLoader::save(const WoweeSpellEffectType& cat,
                                       const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.effectId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.effectKind);
        writePOD(os, e.behaviorFlags);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.baseAmount);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeSpellEffectType WoweeSpellEffectTypeLoader::load(
    const std::string& basePath) {
    WoweeSpellEffectType out;
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
        if (!readPOD(is, e.effectId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.effectKind) ||
            !readPOD(is, e.behaviorFlags) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.baseAmount) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSpellEffectTypeLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeSpellEffectType WoweeSpellEffectTypeLoader::makeDamage(
    const std::string& catalogName) {
    using S = WoweeSpellEffectType;
    WoweeSpellEffectType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t flags, int32_t base, const char* desc) {
        S::Entry e;
        e.effectId = id; e.name = name; e.description = desc;
        e.effectKind = kind;
        e.behaviorFlags = flags;
        e.baseAmount = base;
        e.iconColorRGBA = packRgba(220, 80, 80);   // damage red
        c.entries.push_back(e);
    };
    // Standard WotLK damage effect IDs from Spell.dbc.
    add(2,   "SchoolDamage",         S::Damage,
        S::RequiresTarget | S::RequiresLineOfSight |
        S::IsHostileEffect | S::TriggersGCD,
        0, "School-typed magical damage (Fire / Frost / etc).");
    add(13,  "EnvironmentalDamage",  S::Damage,
        S::IsHostileEffect, 0,
        "Environmental damage (lava / falling / drowning).");
    add(58,  "WeaponDamageNoSchool", S::Damage,
        S::RequiresTarget | S::IsHostileEffect | S::TriggersGCD,
        0, "Weapon damage with no spell school override.");
    add(121, "NormalizedWeaponDmg",  S::Damage,
        S::RequiresTarget | S::IsHostileEffect | S::TriggersGCD,
        0, "Weapon damage normalized to weapon speed.");
    add(67,  "PowerBurn",            S::Damage,
        S::RequiresTarget | S::IsHostileEffect | S::TriggersGCD,
        0, "Burns power resource and deals damage equal to "
        "the burned amount (Mana Burn).");
    return c;
}

WoweeSpellEffectType WoweeSpellEffectTypeLoader::makeHealing(
    const std::string& catalogName) {
    using S = WoweeSpellEffectType;
    WoweeSpellEffectType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t flags,
                    int32_t base, const char* desc) {
        S::Entry e;
        e.effectId = id; e.name = name; e.description = desc;
        e.effectKind = S::Heal;
        e.behaviorFlags = flags;
        e.baseAmount = base;
        e.iconColorRGBA = packRgba(80, 240, 80);   // healing green
        c.entries.push_back(e);
    };
    add(10,  "Heal",          S::RequiresTarget | S::IsBeneficialEffect |
                              S::TriggersGCD, 0,
        "Restore health to target by baseAmount.");
    add(46,  "HealMaxHealth", S::RequiresTarget | S::IsBeneficialEffect,
        0, "Restore target to full health (Lay on Hands).");
    add(116, "HealPct",       S::RequiresTarget | S::IsBeneficialEffect |
                              S::TriggersGCD, 25,
        "Restore health as a percentage of target's max HP.");
    add(118, "ScriptedHeal",  S::RequiresTarget | S::IsBeneficialEffect,
        0, "Custom heal effect, server-script implements the "
        "actual restoration formula.");
    return c;
}

WoweeSpellEffectType WoweeSpellEffectTypeLoader::makeAura(
    const std::string& catalogName) {
    using S = WoweeSpellEffectType;
    WoweeSpellEffectType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t flags,
                    const char* desc) {
        S::Entry e;
        e.effectId = id; e.name = name; e.description = desc;
        e.effectKind = S::Aura;
        e.behaviorFlags = flags;
        e.iconColorRGBA = packRgba(180, 100, 240);   // aura purple
        c.entries.push_back(e);
    };
    add(6,   "ApplyAura",          S::RequiresTarget | S::TriggersGCD,
        "Apply a buff/debuff to target — auraType field "
        "selects the aura behavior (see WAUR catalog).");
    add(35,  "ApplyAuraOnPet",     S::RequiresTarget,
        "Apply aura to caster's pet (Hunter Mend Pet, "
        "Warlock Demonic Empowerment).");
    add(65,  "AreaAuraParty",      S::IsBeneficialEffect,
        "Area aura that affects all party members within "
        "range (Power Word: Fortitude raid buffs).");
    add(82,  "AreaAuraOwner",      S::IsBeneficialEffect,
        "Area aura tied to a totem/object that affects its "
        "owner (Mana Spring totem).");
    add(27,  "PersistentAreaAura", 0,
        "Ground-targeted persistent area aura (Consecration, "
        "Blizzard, Death and Decay).");
    return c;
}

} // namespace pipeline
} // namespace wowee
