#include "pipeline/wowee_spell_schools.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'C', 'H'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wsch") {
        base += ".wsch";
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

const WoweeSpellSchool::Entry*
WoweeSpellSchool::findById(uint32_t schoolId) const {
    for (const auto& e : entries)
        if (e.schoolId == schoolId) return &e;
    return nullptr;
}

bool WoweeSpellSchoolLoader::save(const WoweeSpellSchool& cat,
                                   const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.schoolId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.canBeImmune);
        writePOD(os, e.canBeAbsorbed);
        writePOD(os, e.canBeReflected);
        writePOD(os, e.canCrit);
        writePOD(os, e.colorRGBA);
        writePOD(os, e.baseResistanceCap);
        writePOD(os, e.castSoundId);
        writePOD(os, e.impactSoundId);
        writePOD(os, e.combinedSchoolMask);
    }
    return os.good();
}

WoweeSpellSchool WoweeSpellSchoolLoader::load(
    const std::string& basePath) {
    WoweeSpellSchool out;
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
        if (!readPOD(is, e.schoolId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.canBeImmune) ||
            !readPOD(is, e.canBeAbsorbed) ||
            !readPOD(is, e.canBeReflected) ||
            !readPOD(is, e.canCrit) ||
            !readPOD(is, e.colorRGBA) ||
            !readPOD(is, e.baseResistanceCap) ||
            !readPOD(is, e.castSoundId) ||
            !readPOD(is, e.impactSoundId) ||
            !readPOD(is, e.combinedSchoolMask)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSpellSchoolLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeSpellSchool WoweeSpellSchoolLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellSchool c;
    c.name = catalogName;
    {
        WoweeSpellSchool::Entry e;
        e.schoolId = WoweeSpellSchool::kSchoolPhysical;
        e.name = "Physical";
        e.description = "Melee + ranged weapon damage. Mitigated by "
                         "armor instead of resistance.";
        e.iconPath = "Interface/Icons/INV_Sword_04.blp";
        e.canBeAbsorbed = 1;
        e.canBeReflected = 0;     // physical hits aren't reflected
        e.canCrit = 1;
        e.colorRGBA = packRgba(220, 220, 220);  // light gray
        e.baseResistanceCap = 0;  // armor instead
        c.entries.push_back(e);
    }
    {
        WoweeSpellSchool::Entry e;
        e.schoolId = WoweeSpellSchool::kSchoolFire;
        e.name = "Fire";
        e.description = "Pyromancy / dragon breath / molten attacks.";
        e.iconPath = "Interface/Icons/Spell_Fire_FlameBolt.blp";
        e.canBeAbsorbed = 1;
        e.canBeReflected = 1;
        e.canCrit = 1;
        e.colorRGBA = packRgba(220, 70, 0);  // orange-red
        e.baseResistanceCap = 365;
        c.entries.push_back(e);
    }
    {
        WoweeSpellSchool::Entry e;
        e.schoolId = WoweeSpellSchool::kSchoolHoly;
        e.name = "Holy";
        e.description = "Light-aspected damage and healing.";
        e.iconPath = "Interface/Icons/Spell_Holy_HolyBolt.blp";
        e.canBeImmune = 0;        // holy can't be resisted
        e.canBeAbsorbed = 1;
        e.canBeReflected = 0;
        e.canCrit = 1;
        e.colorRGBA = packRgba(255, 230, 130);  // pale gold
        e.baseResistanceCap = 0;
        c.entries.push_back(e);
    }
    return c;
}

WoweeSpellSchool WoweeSpellSchoolLoader::makeMagical(
    const std::string& catalogName) {
    WoweeSpellSchool c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t r,
                    uint8_t g, uint8_t b, uint32_t resCap,
                    const char* desc) {
        WoweeSpellSchool::Entry e;
        e.schoolId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Spell_") +
                      name + ".blp";
        e.colorRGBA = packRgba(r, g, b);
        e.baseResistanceCap = resCap;
        e.canBeAbsorbed = 1;
        e.canBeReflected = 1;
        e.canCrit = 1;
        c.entries.push_back(e);
    };
    add(WoweeSpellSchool::kSchoolHoly,   "Holy",
        255, 230, 130,   0,  // no holy resist gear
        "Light-aspected damage / heal.");
    add(WoweeSpellSchool::kSchoolFire,   "Fire",
        220, 70, 0,      365,
        "Pyromancy / dragon breath.");
    add(WoweeSpellSchool::kSchoolNature, "Nature",
        50, 200, 50,     365,
        "Lightning / poison / wild growth.");
    add(WoweeSpellSchool::kSchoolFrost,  "Frost",
        150, 200, 255,   365,
        "Ice / chill / glacial.");
    add(WoweeSpellSchool::kSchoolShadow, "Shadow",
        90, 30, 130,     365,
        "Necromancy / void / corruption.");
    add(WoweeSpellSchool::kSchoolArcane, "Arcane",
        180, 100, 220,   365,
        "Pure mana / arcane missiles / time.");
    return c;
}

WoweeSpellSchool WoweeSpellSchoolLoader::makeCombined(
    const std::string& catalogName) {
    WoweeSpellSchool c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t maskBits,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeSpellSchool::Entry e;
        e.schoolId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Spell_") +
                      name + ".blp";
        e.combinedSchoolMask = maskBits;
        e.colorRGBA = packRgba(r, g, b);
        e.canBeAbsorbed = 1;
        e.baseResistanceCap = 365;
        c.entries.push_back(e);
    };
    // Hybrid schools — combinedSchoolMask is the bitmask of
    // canonical schools they qualify as. Spell engine uses the
    // LOWER resistance of the combined set, so hybrids bypass
    // single-school resist gear.
    add(0x80000001, "Spellfire",
        WoweeSpellSchool::kSchoolFire | WoweeSpellSchool::kSchoolArcane,
        230, 100, 200,
        "Combined Fire+Arcane — bypasses single-school resist.");
    add(0x80000002, "Spellshadow",
        WoweeSpellSchool::kSchoolShadow | WoweeSpellSchool::kSchoolArcane,
        140, 50, 200,
        "Combined Shadow+Arcane — Shadow priest specialty.");
    add(0x80000003, "Spellfrost",
        WoweeSpellSchool::kSchoolFrost | WoweeSpellSchool::kSchoolArcane,
        130, 180, 240,
        "Combined Frost+Arcane — Frostfire bolt class.");
    return c;
}

} // namespace pipeline
} // namespace wowee
