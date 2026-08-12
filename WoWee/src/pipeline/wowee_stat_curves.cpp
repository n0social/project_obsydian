#include "pipeline/wowee_stat_curves.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'T', 'M'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wstm") {
        base += ".wstm";
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

const WoweeStatCurve::Entry*
WoweeStatCurve::findById(uint32_t curveId) const {
    for (const auto& e : entries)
        if (e.curveId == curveId) return &e;
    return nullptr;
}

float WoweeStatCurve::resolveAtLevel(uint32_t curveId,
                                      uint8_t level) const {
    const Entry* e = findById(curveId);
    if (!e) return 0.0f;
    if (level < e->minLevel) return 0.0f;
    uint8_t clampedLevel = level;
    if (clampedLevel > e->maxLevel) clampedLevel = e->maxLevel;
    float v = e->baseValue +
              e->perLevelDelta * static_cast<float>(clampedLevel - 1);
    return v * e->multiplier;
}

const char* WoweeStatCurve::curveKindName(uint8_t k) {
    switch (k) {
        case Crit:       return "crit";
        case Hit:        return "hit";
        case Power:      return "power";
        case Regen:      return "regen";
        case Resist:     return "resist";
        case Mitigation: return "mitigation";
        case Misc:       return "misc";
        default:         return "unknown";
    }
}

bool WoweeStatCurveLoader::save(const WoweeStatCurve& cat,
                                 const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.curveId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.curveKind);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.pad0);
        writePOD(os, e.baseValue);
        writePOD(os, e.perLevelDelta);
        writePOD(os, e.multiplier);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeStatCurve WoweeStatCurveLoader::load(const std::string& basePath) {
    WoweeStatCurve out;
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
        if (!readPOD(is, e.curveId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.curveKind) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.baseValue) ||
            !readPOD(is, e.perLevelDelta) ||
            !readPOD(is, e.multiplier) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeStatCurveLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeStatCurve WoweeStatCurveLoader::makeCrit(
    const std::string& catalogName) {
    using S = WoweeStatCurve;
    WoweeStatCurve c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float base,
                    float perLvl, const char* desc) {
        S::Entry e;
        e.curveId = id; e.name = name; e.description = desc;
        e.curveKind = S::Crit;
        e.baseValue = base;
        e.perLevelDelta = perLvl;
        e.iconColorRGBA = packRgba(240, 100, 100);   // crit red
        c.entries.push_back(e);
    };
    // Canonical 3.3.5a crit-chance scaling: base 5%
    // for melee/ranged at lvl 1, +0.05% per level.
    // Spell crit base 1%, +0.04% per level (mages
    // get class bonus on top).
    add(1, "MeleeCritChance",   5.0f,  0.05f,
        "Melee crit chance — base 5%% at lvl 1, +0.05%% per level.");
    add(2, "RangedCritChance",  5.0f,  0.05f,
        "Ranged crit chance — same scaling as melee.");
    add(3, "SpellCritChance",   1.0f,  0.04f,
        "Spell crit chance — base 1%%, +0.04%% per level. "
        "Class talents add fixed bonuses.");
    add(4, "ParryChance",       5.0f,  0.0f,
        "Parry chance — flat 5%% from level 1, scales via "
        "Strength/Parry rating (see WCRR).");
    add(5, "DodgeChance",       5.0f,  0.04f,
        "Base dodge — 5%% + 0.04%%/level + Agility scaling.");
    return c;
}

WoweeStatCurve WoweeStatCurveLoader::makeRegen(
    const std::string& catalogName) {
    using S = WoweeStatCurve;
    WoweeStatCurve c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float base,
                    float perLvl, float mult, const char* desc) {
        S::Entry e;
        e.curveId = id; e.name = name; e.description = desc;
        e.curveKind = S::Regen;
        e.baseValue = base;
        e.perLevelDelta = perLvl;
        e.multiplier = mult;
        e.iconColorRGBA = packRgba(100, 200, 240);   // regen blue
        c.entries.push_back(e);
    };
    add(100, "ManaPerSpirit",    0.0f, 0.0125f, 1.0f,
        "Mana regen per Spirit out-of-combat — 0.0125 mp5/spirit "
        "scaling per level.");
    add(101, "HpPerSpirit",      0.0f, 0.05f,   1.0f,
        "Health regen per Spirit out-of-combat — 0.05 hp/sec "
        "per spirit scaling per level.");
    add(102, "EnergyPerSec",    20.0f, 0.0f,    1.0f,
        "Energy regen — flat 20 per 2s baseline (Rogue / Cat "
        "Druid). Haste reduces tick interval.");
    add(103, "RageDecayPerSec",  3.0f, 0.0f,    1.0f,
        "Rage decay out-of-combat — 3 rage per second uniformly. "
        "In-combat rage doesn't decay.");
    return c;
}

WoweeStatCurve WoweeStatCurveLoader::makeArmor(
    const std::string& catalogName) {
    using S = WoweeStatCurve;
    WoweeStatCurve c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    float base, float perLvl, const char* desc) {
        S::Entry e;
        e.curveId = id; e.name = name; e.description = desc;
        e.curveKind = kind;
        e.baseValue = base;
        e.perLevelDelta = perLvl;
        e.iconColorRGBA = packRgba(180, 180, 200);   // armor grey
        c.entries.push_back(e);
    };
    add(200, "BaseArmorPerLevel", S::Mitigation,    0.0f,  10.0f,
        "Base armor scaling — 10 armor per character level "
        "for cloth/leather wearers without items.");
    add(201, "ArmorMitigationPct", S::Mitigation,   0.0f,   0.4f,
        "Armor → damage reduction conversion — ~0.4%% per level "
        "of effectiveness against same-level attackers.");
    add(202, "ResistancePerLevel", S::Resist,       0.0f,   1.0f,
        "Magic resistance scaling — 1 resist per level for "
        "Holy / Fire / Frost / etc; capped at level*5.");
    return c;
}

} // namespace pipeline
} // namespace wowee
