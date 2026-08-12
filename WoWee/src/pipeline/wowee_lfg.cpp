#include "pipeline/wowee_lfg.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'F', 'G'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wlfg") {
        base += ".wlfg";
    }
    return base;
}

} // namespace

const WoweeLFGDungeon::Entry*
WoweeLFGDungeon::findById(uint32_t dungeonId) const {
    for (const auto& e : entries)
        if (e.dungeonId == dungeonId) return &e;
    return nullptr;
}

const char* WoweeLFGDungeon::difficultyName(uint8_t d) {
    switch (d) {
        case Normal:   return "normal";
        case Heroic:   return "heroic";
        case Mythic:   return "mythic";
        case Hardmode: return "hardmode";
        default:       return "unknown";
    }
}

const char* WoweeLFGDungeon::expansionRequiredName(uint8_t e) {
    switch (e) {
        case Classic:   return "classic";
        case TBC:       return "tbc";
        case WotLK:     return "wotlk";
        case TurtleWoW: return "turtle";
        default:        return "unknown";
    }
}

bool WoweeLFGDungeonLoader::save(const WoweeLFGDungeon& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.dungeonId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mapId);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.recommendedLevel);
        writePOD(os, e.minGearLevel);
        writePOD(os, e.difficulty);
        writePOD(os, e.groupSize);
        writePOD(os, e.requiredRolesMask);
        writePOD(os, e.expansionRequired);
        writePOD(os, e.queueRewardItemId);
        writePOD(os, e.queueRewardEmblemCount);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.firstClearAchievement);
    }
    return os.good();
}

WoweeLFGDungeon WoweeLFGDungeonLoader::load(
    const std::string& basePath) {
    WoweeLFGDungeon out;
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
        if (!readPOD(is, e.dungeonId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.recommendedLevel) ||
            !readPOD(is, e.minGearLevel) ||
            !readPOD(is, e.difficulty) ||
            !readPOD(is, e.groupSize) ||
            !readPOD(is, e.requiredRolesMask) ||
            !readPOD(is, e.expansionRequired) ||
            !readPOD(is, e.queueRewardItemId) ||
            !readPOD(is, e.queueRewardEmblemCount)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.firstClearAchievement)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeLFGDungeonLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeLFGDungeon WoweeLFGDungeonLoader::makeStarter(
    const std::string& catalogName) {
    WoweeLFGDungeon c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t mapId,
                    uint16_t minL, uint16_t maxL, uint16_t recL,
                    const char* desc) {
        WoweeLFGDungeon::Entry e;
        e.dungeonId = id; e.name = name; e.description = desc;
        e.mapId = mapId;
        e.minLevel = minL; e.maxLevel = maxL;
        e.recommendedLevel = recL;
        e.difficulty = WoweeLFGDungeon::Normal;
        e.groupSize = 5;
        e.expansionRequired = WoweeLFGDungeon::Classic;
        c.entries.push_back(e);
    };
    add(1, "Ragefire Chasm",  389, 13, 18, 15,
        "Volcanic 5-man dungeon under Orgrimmar.");
    add(2, "Wailing Caverns", 43,  17, 24, 20,
        "Druidic 5-man dungeon in the Barrens.");
    add(3, "The Deadmines",   36,  18, 23, 20,
        "Defias hideout 5-man dungeon in Westfall.");
    return c;
}

WoweeLFGDungeon WoweeLFGDungeonLoader::makeHeroic(
    const std::string& catalogName) {
    WoweeLFGDungeon c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t mapId,
                    uint16_t minIlvl, uint16_t emblemCount,
                    uint32_t ach, const char* desc) {
        WoweeLFGDungeon::Entry e;
        e.dungeonId = id; e.name = name; e.description = desc;
        e.mapId = mapId;
        e.minLevel = 80; e.maxLevel = 80;
        e.recommendedLevel = 80;
        e.minGearLevel = minIlvl;
        e.difficulty = WoweeLFGDungeon::Heroic;
        e.groupSize = 5;
        e.expansionRequired = WoweeLFGDungeon::WotLK;
        e.queueRewardEmblemCount = emblemCount;
        e.firstClearAchievement = ach;
        c.entries.push_back(e);
    };
    add(100, "Halls of Lightning Heroic",  602, 180, 2, 1862,
        "Storm titan-keeper 5-man — Loken finale.");
    add(101, "Halls of Stone Heroic",      599, 180, 2, 1865,
        "Iron dwarf 5-man — Tribunal of Ages event.");
    add(102, "Utgarde Pinnacle Heroic",    575, 180, 2, 1487,
        "Vrykul 5-man — King Ymiron finale.");
    add(103, "The Violet Hold Heroic",     608, 180, 2, 1816,
        "Dalaran prison breakout 5-man.");
    add(104, "Old Kingdom Heroic",         595, 180, 2, 1860,
        "Faceless ones 5-man — Herald Volazj.");
    return c;
}

WoweeLFGDungeon WoweeLFGDungeonLoader::makeRaid(
    const std::string& catalogName) {
    WoweeLFGDungeon c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t mapId,
                    uint8_t difficulty, uint8_t groupSize,
                    uint16_t minIlvl, uint16_t emblemCount,
                    uint32_t ach, const char* desc) {
        WoweeLFGDungeon::Entry e;
        e.dungeonId = id; e.name = name; e.description = desc;
        e.mapId = mapId;
        e.minLevel = 80; e.maxLevel = 80;
        e.recommendedLevel = 80;
        e.minGearLevel = minIlvl;
        e.difficulty = difficulty;
        e.groupSize = groupSize;
        e.expansionRequired = WoweeLFGDungeon::WotLK;
        e.queueRewardEmblemCount = emblemCount;
        e.firstClearAchievement = ach;
        c.entries.push_back(e);
    };
    add(200, "Naxxramas-25",          533,
        WoweeLFGDungeon::Normal,   25, 200,  5, 1996,
        "25-man recycled tier-3 raid in Northrend.");
    add(201, "Ulduar-25 Hardmode",    603,
        WoweeLFGDungeon::Hardmode, 25, 220,  5, 2200,
        "25-man with toggleable hardmode boss difficulty.");
    add(202, "Trial of the Crusader-25", 649,
        WoweeLFGDungeon::Mythic,   25, 232, 10, 4047,
        "25-man Argent Crusade raid, mythic difficulty.");
    return c;
}

} // namespace pipeline
} // namespace wowee
