#include "pipeline/wowee_pvp.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'V', 'P'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wpvp") {
        base += ".wpvp";
    }
    return base;
}

} // namespace

const WoweePVPRank::Entry*
WoweePVPRank::findById(uint32_t rankId) const {
    for (const auto& e : entries) if (e.rankId == rankId) return &e;
    return nullptr;
}

const char* WoweePVPRank::rankKindName(uint8_t k) {
    switch (k) {
        case VanillaHonor:      return "vanilla-honor";
        case ArenaRating:       return "arena";
        case BattlegroundRated: return "rated-bg";
        case WorldPvP:          return "world-pvp";
        case ConquestPoint:     return "conquest";
        default:                return "unknown";
    }
}

bool WoweePVPRankLoader::save(const WoweePVPRank& cat,
                               const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.rankId);
        writeStr(os, e.name);
        writeStr(os, e.factionAllianceName);
        writeStr(os, e.factionHordeName);
        writeStr(os, e.description);
        writePOD(os, e.rankKind);
        writePOD(os, e.minBracketLevel);
        writePOD(os, e.maxBracketLevel);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, e.minHonorOrRating);
        writePOD(os, e.rewardEmblems);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.titleId);
        writePOD(os, e.chestItemId);
        writePOD(os, e.glovesItemId);
        writePOD(os, e.shouldersItemId);
        writePOD(os, e.bracketBgId);
    }
    return os.good();
}

WoweePVPRank WoweePVPRankLoader::load(const std::string& basePath) {
    WoweePVPRank out;
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
        if (!readPOD(is, e.rankId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) ||
            !readStr(is, e.factionAllianceName) ||
            !readStr(is, e.factionHordeName) ||
            !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.rankKind) ||
            !readPOD(is, e.minBracketLevel) ||
            !readPOD(is, e.maxBracketLevel)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.minHonorOrRating) ||
            !readPOD(is, e.rewardEmblems)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.titleId) ||
            !readPOD(is, e.chestItemId) ||
            !readPOD(is, e.glovesItemId) ||
            !readPOD(is, e.shouldersItemId) ||
            !readPOD(is, e.bracketBgId)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweePVPRankLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweePVPRank WoweePVPRankLoader::makeStarter(
    const std::string& catalogName) {
    WoweePVPRank c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* ally,
                    const char* horde, uint32_t honor,
                    const char* desc) {
        WoweePVPRank::Entry e;
        e.rankId = id; e.name = name;
        e.factionAllianceName = ally;
        e.factionHordeName = horde;
        e.description = desc;
        e.rankKind = WoweePVPRank::VanillaHonor;
        e.minHonorOrRating = honor;
        c.entries.push_back(e);
    };
    add(1, "Rank2", "Private",         "Scout",
        2000, "Vanilla rank 2 — first PvP title.");
    add(2, "Rank3", "Corporal",        "Grunt",
        5000, "Vanilla rank 3.");
    add(3, "Rank4", "Sergeant",        "Sergeant",
        10000,
        "Vanilla rank 4 — same name on both factions.");
    return c;
}

WoweePVPRank WoweePVPRankLoader::makeAllianceFull(
    const std::string& catalogName) {
    WoweePVPRank c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* ally, const char* horde,
                    uint32_t honor, uint32_t titleId,
                    uint32_t chest, uint32_t gloves, uint32_t shoulders,
                    const char* desc) {
        WoweePVPRank::Entry e;
        e.rankId = id;
        e.name = std::string("Rank") + std::to_string(id);
        e.factionAllianceName = ally;
        e.factionHordeName = horde;
        e.description = desc;
        e.rankKind = WoweePVPRank::VanillaHonor;
        e.minHonorOrRating = honor;
        e.titleId = titleId;
        e.chestItemId = chest;
        e.glovesItemId = gloves;
        e.shouldersItemId = shoulders;
        c.entries.push_back(e);
    };
    // Vanilla ranks 6-14 with WTTL + WIT cross-refs. Honor
    // values ramp exponentially toward the Grand Marshal cap.
    add(6,  "Knight",            "Stone Guard",
         50000,  3, 16462, 16472, 16482,
         "Rank 6 — first epic gear unlock.");
    add(7,  "Knight-Lieutenant", "Blood Guard",
         70000,  4, 16463, 16473, 16483,
         "Rank 7.");
    add(8,  "Knight-Captain",    "Legionnaire",
         90000,  5, 16464, 16474, 16484,
         "Rank 8.");
    add(9,  "Knight-Champion",   "Centurion",
        110000,  6, 16465, 16475, 16485,
        "Rank 9.");
    add(10, "Lieutenant Commander", "Champion",
        130000,  7, 16466, 16476, 16486,
        "Rank 10.");
    add(11, "Commander",         "Lieutenant General",
        160000,  8, 16467, 16477, 16487,
        "Rank 11.");
    add(12, "Marshal",           "General",
        190000,  9, 16468, 16478, 16488,
        "Rank 12.");
    add(13, "Field Marshal",     "Warlord",
        220000, 10, 16469, 16479, 16489,
        "Rank 13.");
    add(14, "Grand Marshal",     "High Warlord",
        260000, 11, 16470, 16480, 16490,
        "Rank 14 — pinnacle. 'Grand Marshal' / 'High Warlord' "
        "title + full epic PvP set.");
    return c;
}

WoweePVPRank WoweePVPRankLoader::makeArenaTiers(
    const std::string& catalogName) {
    WoweePVPRank c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t rating,
                    uint16_t emblems, uint32_t titleId,
                    const char* desc) {
        WoweePVPRank::Entry e;
        e.rankId = id; e.name = name;
        e.factionAllianceName = name;    // arena names match
        e.factionHordeName = name;
        e.description = desc;
        e.rankKind = WoweePVPRank::ArenaRating;
        e.minHonorOrRating = rating;
        e.rewardEmblems = emblems;
        e.titleId = titleId;
        e.minBracketLevel = 80; e.maxBracketLevel = 80;
        c.entries.push_back(e);
    };
    add(100, "Combatant",  1500,  10,  0,
        "Arena bracket — minimum entry rating.");
    add(101, "Challenger", 1750,  20,  44,
        "Arena bracket — 'Challenger' title earned.");
    add(102, "Rival",      2000,  40,  45,
        "Arena bracket — 'Rival' title earned.");
    add(103, "Duelist",    2200,  80,  46,
        "Arena bracket — 'Duelist' title earned.");
    add(104, "Gladiator",  2400, 160,  47,
        "Arena bracket — 'Gladiator' title + season mount.");
    return c;
}

} // namespace pipeline
} // namespace wowee
