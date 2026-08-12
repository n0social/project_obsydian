#include "pipeline/wowee_skill_costs.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'C', 'S'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wscs") {
        base += ".wscs";
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

const WoweeSkillCost::Entry*
WoweeSkillCost::findById(uint32_t costId) const {
    for (const auto& e : entries)
        if (e.costId == costId) return &e;
    return nullptr;
}

const WoweeSkillCost::Entry*
WoweeSkillCost::nextTrainable(uint16_t currentSkill,
                                uint8_t characterLevel) const {
    const Entry* best = nullptr;
    for (const auto& e : entries) {
        if (characterLevel < e.requiredLevel) continue;
        // Already maxed this tier; skip.
        if (currentSkill >= e.maxSkillUnlocked) continue;
        // Choose the lowest-rank tier the character is
        // qualified for — typically the one whose
        // minSkillToLearn matches their current skill.
        if (best == nullptr ||
            e.skillRankIndex < best->skillRankIndex) {
            best = &e;
        }
    }
    return best;
}

const char* WoweeSkillCost::costKindName(uint8_t k) {
    switch (k) {
        case Profession:  return "profession";
        case WeaponSkill: return "weapon";
        case RidingSkill: return "riding";
        case ClassSkill:  return "class-skill";
        case Misc:        return "misc";
        default:          return "unknown";
    }
}

bool WoweeSkillCostLoader::save(const WoweeSkillCost& cat,
                                 const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.costId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.skillRankIndex);
        writePOD(os, e.minSkillToLearn);
        writePOD(os, e.maxSkillUnlocked);
        writePOD(os, e.requiredLevel);
        writePOD(os, e.costKind);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.copperCost);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeSkillCost WoweeSkillCostLoader::load(
    const std::string& basePath) {
    WoweeSkillCost out;
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
        if (!readPOD(is, e.costId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.skillRankIndex) ||
            !readPOD(is, e.minSkillToLearn) ||
            !readPOD(is, e.maxSkillUnlocked) ||
            !readPOD(is, e.requiredLevel) ||
            !readPOD(is, e.costKind) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.copperCost) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSkillCostLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeSkillCost WoweeSkillCostLoader::makeProfession(
    const std::string& catalogName) {
    using S = WoweeSkillCost;
    WoweeSkillCost c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t rank,
                    uint16_t minSkill, uint16_t maxSkill,
                    uint8_t lvl, uint32_t cop, const char* desc) {
        S::Entry e;
        e.costId = id; e.name = name; e.description = desc;
        e.skillRankIndex = rank;
        e.minSkillToLearn = minSkill;
        e.maxSkillUnlocked = maxSkill;
        e.requiredLevel = lvl;
        e.costKind = S::Profession;
        e.copperCost = cop;
        e.iconColorRGBA = packRgba(180, 140, 80);   // crafting brown
        c.entries.push_back(e);
    };
    // The canonical 6-tier profession progression with
    // standard gold costs (1g = 10000c).
    add(1, "Apprentice",   0,   0,  75,  5,       100,
        "Apprentice — entry tier; 1 silver, lvl 5+.");
    add(2, "Journeyman",   1,  50, 150, 10,       500,
        "Journeyman — basic tier; 5 silver, lvl 10+.");
    add(3, "Expert",       2, 125, 225, 20,     10000,
        "Expert — pre-Outland tier; 1 gold, lvl 20+.");
    add(4, "Artisan",      3, 200, 300, 35,     50000,
        "Artisan — Vanilla cap tier; 5 gold, lvl 35+.");
    add(5, "Master",       4, 275, 375, 50,    100000,
        "Master — TBC cap tier; 10 gold, lvl 50+.");
    add(6, "GrandMaster",  5, 350, 450, 65,    250000,
        "Grand Master — WotLK cap tier; 25 gold, lvl 65+.");
    return c;
}

WoweeSkillCost WoweeSkillCostLoader::makeWeapon(
    const std::string& catalogName) {
    using S = WoweeSkillCost;
    WoweeSkillCost c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t rank,
                    uint16_t minSkill, uint16_t maxSkill,
                    uint8_t lvl, const char* desc) {
        S::Entry e;
        e.costId = id; e.name = name; e.description = desc;
        e.skillRankIndex = rank;
        e.minSkillToLearn = minSkill;
        e.maxSkillUnlocked = maxSkill;
        e.requiredLevel = lvl;
        e.costKind = S::WeaponSkill;
        e.copperCost = 0;     // weapon skills are free
        e.iconColorRGBA = packRgba(220, 180, 100);   // weapon yellow
        c.entries.push_back(e);
    };
    // Weapon skills cap at 5x char level. Free to train
    // but level-gated.
    add(100, "WeaponBeginner", 0,   0, 100,  5,
        "Beginner weapon skill — caps at 100 (lvl 5+).");
    add(101, "WeaponTrained",  1, 100, 200, 20,
        "Trained weapon skill — caps at 200 (lvl 20+).");
    add(102, "WeaponSkilled",  2, 200, 300, 40,
        "Skilled weapon skill — caps at 300 (lvl 40+).");
    add(103, "WeaponExpert",   3, 300, 400, 60,
        "Expert weapon skill — caps at 400 (lvl 60+).");
    add(104, "WeaponMaster",   4, 400, 500, 80,
        "Master weapon skill — caps at 500 (lvl 80+).");
    return c;
}

WoweeSkillCost WoweeSkillCostLoader::makeRiding(
    const std::string& catalogName) {
    using S = WoweeSkillCost;
    WoweeSkillCost c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t rank,
                    uint16_t minSkill, uint16_t maxSkill,
                    uint8_t lvl, uint32_t cop, const char* desc) {
        S::Entry e;
        e.costId = id; e.name = name; e.description = desc;
        e.skillRankIndex = rank;
        e.minSkillToLearn = minSkill;
        e.maxSkillUnlocked = maxSkill;
        e.requiredLevel = lvl;
        e.costKind = S::RidingSkill;
        e.copperCost = cop;
        e.iconColorRGBA = packRgba(180, 100, 220);   // riding purple
        c.entries.push_back(e);
    };
    // Canonical Vanilla / TBC / WotLK riding gold costs.
    add(200, "Apprentice60",   0,   0,  75, 20,    900000,
        "Apprentice Riding — 60% land mount; 90g, lvl 20+.");
    add(201, "Journeyman100",  1,  75, 150, 40,   5000000,
        "Journeyman Riding — 100% land mount; 500g, lvl 40+.");
    add(202, "Expert150",      2, 150, 225, 60,   8000000,
        "Expert Riding — 150% flying; 800g, lvl 60+ (TBC).");
    add(203, "Artisan280",     3, 225, 300, 70,  50000000,
        "Artisan Riding — 280% flying; 5000g, lvl 70+ (TBC epic).");
    add(204, "ColdWeather",    4, 300, 375, 77,   1000000,
        "Cold Weather Flying — required for Northrend; "
        "1000g, lvl 77+ (WotLK).");
    return c;
}

} // namespace pipeline
} // namespace wowee
