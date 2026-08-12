#include "pipeline/wowee_battlegrounds.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'G', 'D'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wbgd") {
        base += ".wbgd";
    }
    return base;
}

} // namespace

const WoweeBattleground::Entry*
WoweeBattleground::findById(uint32_t bgId) const {
    for (const auto& e : entries) if (e.battlegroundId == bgId) return &e;
    return nullptr;
}

const char* WoweeBattleground::objectiveKindName(uint8_t k) {
    switch (k) {
        case Annihilation: return "annihilation";
        case CaptureFlag:  return "ctf";
        case ControlNodes: return "nodes";
        case KingOfHill:   return "koh";
        case ResourceRace: return "resource-race";
        case CarryObject:  return "carry-object";
        default:           return "unknown";
    }
}

bool WoweeBattlegroundLoader::save(const WoweeBattleground& cat,
                                   const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.battlegroundId);
        writePOD(os, e.mapId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.objectiveKind);
        writePOD(os, e.minPlayersPerSide);
        writePOD(os, e.maxPlayersPerSide);
        uint8_t pad1 = 0;
        writePOD(os, pad1);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.scoreToWin);
        writePOD(os, e.timeLimitSeconds);
        writePOD(os, e.bracketSize);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, e.allianceStart.x);
        writePOD(os, e.allianceStart.y);
        writePOD(os, e.allianceStart.z);
        writePOD(os, e.allianceFacing);
        writePOD(os, e.hordeStart.x);
        writePOD(os, e.hordeStart.y);
        writePOD(os, e.hordeStart.z);
        writePOD(os, e.hordeFacing);
        writePOD(os, e.respawnTimeSeconds);
        os.write(reinterpret_cast<const char*>(pad3), 2);
        writePOD(os, e.markTokenId);
    }
    return os.good();
}

WoweeBattleground WoweeBattlegroundLoader::load(const std::string& basePath) {
    WoweeBattleground out;
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
        if (!readPOD(is, e.battlegroundId) ||
            !readPOD(is, e.mapId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.objectiveKind) ||
            !readPOD(is, e.minPlayersPerSide) ||
            !readPOD(is, e.maxPlayersPerSide)) {
            out.entries.clear(); return out;
        }
        uint8_t pad1 = 0;
        if (!readPOD(is, pad1)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.scoreToWin) ||
            !readPOD(is, e.timeLimitSeconds) ||
            !readPOD(is, e.bracketSize)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.allianceStart.x) ||
            !readPOD(is, e.allianceStart.y) ||
            !readPOD(is, e.allianceStart.z) ||
            !readPOD(is, e.allianceFacing) ||
            !readPOD(is, e.hordeStart.x) ||
            !readPOD(is, e.hordeStart.y) ||
            !readPOD(is, e.hordeStart.z) ||
            !readPOD(is, e.hordeFacing) ||
            !readPOD(is, e.respawnTimeSeconds)) {
            out.entries.clear(); return out;
        }
        is.read(reinterpret_cast<char*>(pad3), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.markTokenId)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeBattlegroundLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeBattleground WoweeBattlegroundLoader::makeStarter(const std::string& catalogName) {
    WoweeBattleground c;
    c.name = catalogName;
    {
        WoweeBattleground::Entry e;
        e.battlegroundId = 1; e.mapId = 0;
        e.name = "Mountain Crown";
        e.description = "Hold the central peak. Three captures to win.";
        e.objectiveKind = WoweeBattleground::KingOfHill;
        e.minPlayersPerSide = 5; e.maxPlayersPerSide = 10;
        e.scoreToWin = 3; e.timeLimitSeconds = 1800;
        e.allianceStart = {-100.0f, 50.0f, 0.0f}; e.allianceFacing = 0.0f;
        e.hordeStart = {100.0f, 50.0f, 0.0f}; e.hordeFacing = 3.14159265f;
        c.entries.push_back(e);
    }
    return c;
}

WoweeBattleground WoweeBattlegroundLoader::makeClassic(const std::string& catalogName) {
    WoweeBattleground c;
    c.name = catalogName;
    {
        WoweeBattleground::Entry e;
        e.battlegroundId = 489; e.mapId = 489;
        e.name = "Warsong Gulch";
        e.description = "Capture the enemy flag and return it 3 times.";
        e.objectiveKind = WoweeBattleground::CaptureFlag;
        e.minPlayersPerSide = 8; e.maxPlayersPerSide = 10;
        e.minLevel = 10; e.maxLevel = 80;
        e.scoreToWin = 3; e.timeLimitSeconds = 1800;
        // markTokenId 102 matches WTKN.makePvp's
        // "Mark of Honor: Warsong Gulch".
        e.markTokenId = 102;
        c.entries.push_back(e);
    }
    {
        WoweeBattleground::Entry e;
        e.battlegroundId = 529; e.mapId = 529;
        e.name = "Arathi Basin";
        e.description = "Control 5 nodes to harvest 1600 resources.";
        e.objectiveKind = WoweeBattleground::ControlNodes;
        e.minPlayersPerSide = 12; e.maxPlayersPerSide = 15;
        e.minLevel = 20; e.maxLevel = 80;
        e.scoreToWin = 1600; e.timeLimitSeconds = 1500;
        e.markTokenId = 103;
        c.entries.push_back(e);
    }
    {
        WoweeBattleground::Entry e;
        e.battlegroundId = 30; e.mapId = 30;
        e.name = "Alterac Valley";
        e.description = "Eliminate the opposing General. Reinforcements: 600.";
        e.objectiveKind = WoweeBattleground::ResourceRace;
        e.minPlayersPerSide = 30; e.maxPlayersPerSide = 40;
        e.minLevel = 51; e.maxLevel = 80;
        e.scoreToWin = 600; e.timeLimitSeconds = 0;   // no time limit
        e.markTokenId = 104;
        c.entries.push_back(e);
    }
    return c;
}

WoweeBattleground WoweeBattlegroundLoader::makeArena(const std::string& catalogName) {
    WoweeBattleground c;
    c.name = catalogName;
    auto add = [&](uint32_t bgId, uint32_t mapId, const char* name,
                    uint8_t minPlayers, uint8_t maxPlayers) {
        WoweeBattleground::Entry e;
        e.battlegroundId = bgId; e.mapId = mapId;
        e.name = name;
        e.description = "Annihilation arena.";
        e.objectiveKind = WoweeBattleground::Annihilation;
        e.minPlayersPerSide = minPlayers; e.maxPlayersPerSide = maxPlayers;
        e.minLevel = 80; e.maxLevel = 80;
        e.scoreToWin = 1; e.timeLimitSeconds = 1500;   // 25 min cap
        e.bracketSize = 1;
        e.respawnTimeSeconds = 0;                       // no respawn
        c.entries.push_back(e);
    };
    add(559, 559, "Nagrand Arena (2v2)", 2, 2);
    add(562, 562, "Blade's Edge Arena (3v3)", 3, 3);
    add(572, 572, "Ruins of Lordaeron (5v5)", 5, 5);
    return c;
}

} // namespace pipeline
} // namespace wowee
