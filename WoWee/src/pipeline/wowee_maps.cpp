#include "pipeline/wowee_maps.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'S', 'X'};
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
    if (base.size() < 4 || base.substr(base.size() - 4) != ".wms") {
        base += ".wms";
    }
    return base;
}

} // namespace

const WoweeMaps::Map* WoweeMaps::findMap(uint32_t mapId) const {
    for (const auto& m : maps) if (m.mapId == mapId) return &m;
    return nullptr;
}

const WoweeMaps::Area* WoweeMaps::findArea(uint32_t areaId) const {
    for (const auto& a : areas) if (a.areaId == areaId) return &a;
    return nullptr;
}

const char* WoweeMaps::mapTypeName(uint8_t t) {
    switch (t) {
        case Continent:    return "continent";
        case Instance:     return "instance";
        case Raid:         return "raid";
        case Battleground: return "battleground";
        case Arena:        return "arena";
        default:           return "unknown";
    }
}

const char* WoweeMaps::expansionName(uint8_t e) {
    switch (e) {
        case Classic: return "classic";
        case Tbc:     return "tbc";
        case Wotlk:   return "wotlk";
        case Cata:    return "cata";
        case Mop:     return "mop";
        default:      return "unknown";
    }
}

const char* WoweeMaps::factionGroupName(uint8_t f) {
    switch (f) {
        case FactionBoth:      return "both";
        case FactionAlliance:  return "alliance";
        case FactionHorde:     return "horde";
        case FactionContested: return "contested";
        default:               return "unknown";
    }
}

bool WoweeMapsLoader::save(const WoweeMaps& cat,
                           const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t mapCount = static_cast<uint32_t>(cat.maps.size());
    writePOD(os, mapCount);
    for (const auto& m : cat.maps) {
        writePOD(os, m.mapId);
        writeStr(os, m.name);
        writeStr(os, m.shortName);
        writePOD(os, m.mapType);
        writePOD(os, m.expansionId);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, m.maxPlayers);
        os.write(reinterpret_cast<const char*>(pad2), 2);
    }
    uint32_t areaCount = static_cast<uint32_t>(cat.areas.size());
    writePOD(os, areaCount);
    for (const auto& a : cat.areas) {
        writePOD(os, a.areaId);
        writePOD(os, a.mapId);
        writePOD(os, a.parentAreaId);
        writeStr(os, a.name);
        writePOD(os, a.minLevel);
        writePOD(os, a.maxLevel);
        writePOD(os, a.factionGroup);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, a.explorationXP);
        writePOD(os, a.ambienceSoundId);
    }
    return os.good();
}

WoweeMaps WoweeMapsLoader::load(const std::string& basePath) {
    WoweeMaps out;
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    if (!is) return out;
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return out;
    uint32_t version = 0;
    if (!readPOD(is, version) || version != kVersion) return out;
    if (!readStr(is, out.name)) return out;
    uint32_t mapCount = 0;
    if (!readPOD(is, mapCount)) return out;
    if (mapCount > (1u << 20)) return out;
    out.maps.resize(mapCount);
    for (auto& m : out.maps) {
        if (!readPOD(is, m.mapId)) { out.maps.clear(); return out; }
        if (!readStr(is, m.name) || !readStr(is, m.shortName)) {
            out.maps.clear(); return out;
        }
        if (!readPOD(is, m.mapType) || !readPOD(is, m.expansionId)) {
            out.maps.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.maps.clear(); return out; }
        if (!readPOD(is, m.maxPlayers)) {
            out.maps.clear(); return out;
        }
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.maps.clear(); return out; }
    }
    uint32_t areaCount = 0;
    if (!readPOD(is, areaCount)) {
        out.maps.clear(); return out;
    }
    if (areaCount > (1u << 20)) {
        out.maps.clear(); return out;
    }
    out.areas.resize(areaCount);
    for (auto& a : out.areas) {
        if (!readPOD(is, a.areaId) ||
            !readPOD(is, a.mapId) ||
            !readPOD(is, a.parentAreaId)) {
            out.maps.clear(); out.areas.clear(); return out;
        }
        if (!readStr(is, a.name)) {
            out.maps.clear(); out.areas.clear(); return out;
        }
        if (!readPOD(is, a.minLevel) ||
            !readPOD(is, a.maxLevel) ||
            !readPOD(is, a.factionGroup)) {
            out.maps.clear(); out.areas.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) {
            out.maps.clear(); out.areas.clear(); return out;
        }
        if (!readPOD(is, a.explorationXP) ||
            !readPOD(is, a.ambienceSoundId)) {
            out.maps.clear(); out.areas.clear(); return out;
        }
    }
    return out;
}

bool WoweeMapsLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeMaps WoweeMapsLoader::makeStarter(const std::string& catalogName) {
    WoweeMaps c;
    c.name = catalogName;
    {
        WoweeMaps::Map m;
        m.mapId = 0; m.name = "Eastern Kingdoms";
        m.shortName = "EK";
        m.mapType = WoweeMaps::Continent;
        c.maps.push_back(m);
    }
    {
        WoweeMaps::Area a;
        a.areaId = 1; a.mapId = 0;
        a.name = "Stormwind City";
        a.minLevel = 1; a.maxLevel = 70;
        a.factionGroup = WoweeMaps::FactionAlliance;
        a.explorationXP = 200;
        c.areas.push_back(a);
    }
    {
        WoweeMaps::Area a;
        a.areaId = 2; a.mapId = 0;
        a.name = "Elwynn Forest";
        a.minLevel = 1; a.maxLevel = 10;
        a.factionGroup = WoweeMaps::FactionAlliance;
        a.explorationXP = 100;
        a.ambienceSoundId = 100;        // WSND.makeAmbient bird-loop
        c.areas.push_back(a);
    }
    {
        WoweeMaps::Area a;
        a.areaId = 3; a.mapId = 0; a.parentAreaId = 2;
        a.name = "Goldshire";
        a.minLevel = 5; a.maxLevel = 10;
        a.factionGroup = WoweeMaps::FactionAlliance;
        a.explorationXP = 50;
        c.areas.push_back(a);
    }
    return c;
}

WoweeMaps WoweeMapsLoader::makeClassic(const std::string& catalogName) {
    WoweeMaps c;
    c.name = catalogName;
    auto addMap = [&](uint32_t id, const char* name,
                       const char* shortName, uint8_t type,
                       uint16_t maxPlayers) {
        WoweeMaps::Map m;
        m.mapId = id; m.name = name; m.shortName = shortName;
        m.mapType = type; m.expansionId = WoweeMaps::Classic;
        m.maxPlayers = maxPlayers;
        c.maps.push_back(m);
    };
    addMap(0, "Eastern Kingdoms", "EK",   WoweeMaps::Continent, 0);
    addMap(1, "Kalimdor",         "Kalim", WoweeMaps::Continent, 0);
    addMap(36, "Deadmines",       "DM",    WoweeMaps::Instance, 5);
    auto addArea = [&](uint32_t id, uint32_t mapId,
                        uint32_t parent, const char* name,
                        uint16_t minLvl, uint16_t maxLvl,
                        uint8_t faction, uint32_t xp,
                        uint32_t soundId = 0) {
        WoweeMaps::Area a;
        a.areaId = id; a.mapId = mapId; a.parentAreaId = parent;
        a.name = name; a.minLevel = minLvl; a.maxLevel = maxLvl;
        a.factionGroup = faction; a.explorationXP = xp;
        a.ambienceSoundId = soundId;
        c.areas.push_back(a);
    };
    // Top-level zones in EK + sub-zones (parent chain).
    addArea(12, 0, 0, "Elwynn Forest", 1, 10,
            WoweeMaps::FactionAlliance, 100, 100);
    addArea(87, 0, 12, "Goldshire", 5, 10,
            WoweeMaps::FactionAlliance, 50);
    addArea(40, 0, 0, "Westfall", 10, 20,
            WoweeMaps::FactionContested, 200);
    addArea(39, 0, 0, "Duskwood", 18, 30,
            WoweeMaps::FactionContested, 250);
    // Kalimdor.
    addArea(141, 1, 0, "Teldrassil", 1, 10,
            WoweeMaps::FactionAlliance, 100);
    // Instance areas.
    addArea(2017, 36, 0, "The Deadmines", 17, 22,
            WoweeMaps::FactionBoth, 0, 200);    // fire-crackle ambient
    return c;
}

WoweeMaps WoweeMapsLoader::makeBgArena(const std::string& catalogName) {
    WoweeMaps c;
    c.name = catalogName;
    {
        WoweeMaps::Map m;
        m.mapId = 30; m.name = "Alterac Valley";
        m.shortName = "AV";
        m.mapType = WoweeMaps::Battleground;
        m.expansionId = WoweeMaps::Classic;
        m.maxPlayers = 40;
        c.maps.push_back(m);
    }
    {
        WoweeMaps::Map m;
        m.mapId = 559; m.name = "Nagrand Arena";
        m.shortName = "Naga";
        m.mapType = WoweeMaps::Arena;
        m.expansionId = WoweeMaps::Tbc;
        m.maxPlayers = 10;          // 5v5 cap
        c.maps.push_back(m);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
