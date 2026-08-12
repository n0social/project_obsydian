#include "pipeline/wowee_channels.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'H', 'N'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wchn") {
        base += ".wchn";
    }
    return base;
}

} // namespace

const WoweeChannel::Entry*
WoweeChannel::findById(uint32_t channelId) const {
    for (const auto& e : entries) if (e.channelId == channelId) return &e;
    return nullptr;
}

const char* WoweeChannel::channelTypeName(uint8_t t) {
    switch (t) {
        case AreaLocal:       return "local";
        case Zone:            return "zone";
        case Continent:       return "continent";
        case World:           return "world";
        case Trade:           return "trade";
        case LookingForGroup: return "lfg";
        case GuildRecruit:    return "guild-recruit";
        case LocalDefense:    return "local-defense";
        case Custom:          return "custom";
        case Pvp:             return "pvp";
        default:              return "unknown";
    }
}

const char* WoweeChannel::factionAccessName(uint8_t f) {
    switch (f) {
        case Alliance: return "alliance";
        case Horde:    return "horde";
        case Both:     return "both";
        default:       return "unknown";
    }
}

bool WoweeChannelLoader::save(const WoweeChannel& cat,
                              const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.channelId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.channelType);
        writePOD(os, e.factionAccess);
        writePOD(os, e.autoJoin);
        writePOD(os, e.announce);
        writePOD(os, e.moderated);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, e.minLevel);
        writePOD(os, e.areaIdGate);
        writePOD(os, e.mapIdGate);
    }
    return os.good();
}

WoweeChannel WoweeChannelLoader::load(const std::string& basePath) {
    WoweeChannel out;
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
        if (!readPOD(is, e.channelId)) { out.entries.clear(); return out; }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.channelType) ||
            !readPOD(is, e.factionAccess) ||
            !readPOD(is, e.autoJoin) ||
            !readPOD(is, e.announce) ||
            !readPOD(is, e.moderated)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.areaIdGate) ||
            !readPOD(is, e.mapIdGate)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeChannelLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeChannel WoweeChannelLoader::makeStarter(const std::string& catalogName) {
    WoweeChannel c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    uint8_t autoJ, uint16_t minLvl,
                    const char* desc) {
        WoweeChannel::Entry e;
        e.channelId = id; e.name = name; e.description = desc;
        e.channelType = type; e.autoJoin = autoJ;
        e.minLevel = minLvl;
        c.entries.push_back(e);
    };
    add(1, "General",       WoweeChannel::Zone,            1, 1,
        "Zone-wide chatter; auto-joined for current zone.");
    add(2, "Trade",         WoweeChannel::Trade,           0, 1,
        "Cross-zone trade chatter; opt-in.");
    add(3, "LookingForGroup", WoweeChannel::LookingForGroup, 1, 10,
        "Global LFG queue chat.");
    add(4, "GuildRecruitment", WoweeChannel::GuildRecruit,  0, 10,
        "Recruit-a-guild + hire-a-guild bulletin.");
    return c;
}

WoweeChannel WoweeChannelLoader::makeCity(const std::string& catalogName) {
    WoweeChannel c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    uint8_t fac, uint32_t mapId, uint32_t areaId,
                    const char* desc) {
        WoweeChannel::Entry e;
        e.channelId = id; e.name = name; e.description = desc;
        e.channelType = type; e.factionAccess = fac;
        e.autoJoin = 1;
        e.mapIdGate = mapId; e.areaIdGate = areaId;
        c.entries.push_back(e);
    };
    // mapId 0 + areaId 1 ~ Stormwind City (matches WMS preset).
    add(100, "General - Stormwind",  WoweeChannel::Zone,
        WoweeChannel::Alliance, 0, 1,
        "Stormwind general chat.");
    add(101, "Trade - Stormwind",    WoweeChannel::Trade,
        WoweeChannel::Alliance, 0, 1,
        "Stormwind trade district chat.");
    add(102, "LFG - Stormwind",      WoweeChannel::LookingForGroup,
        WoweeChannel::Alliance, 0, 1,
        "Stormwind LFG within-city queue.");
    add(110, "General - Orgrimmar",  WoweeChannel::Zone,
        WoweeChannel::Horde, 1, 1637,
        "Orgrimmar general chat.");
    add(111, "Trade - Orgrimmar",    WoweeChannel::Trade,
        WoweeChannel::Horde, 1, 1637,
        "Orgrimmar trade district chat.");
    return c;
}

WoweeChannel WoweeChannelLoader::makeModerated(const std::string& catalogName) {
    WoweeChannel c;
    c.name = catalogName;
    {
        WoweeChannel::Entry e;
        e.channelId = 200; e.name = "LocalDefense";
        e.description =
            "Alarm channel — broadcasts when zone is attacked. "
            "Level 10+ auto-joined.";
        e.channelType = WoweeChannel::LocalDefense;
        e.autoJoin = 1; e.minLevel = 10;
        c.entries.push_back(e);
    }
    {
        WoweeChannel::Entry e;
        e.channelId = 201; e.name = "WorldDefense";
        e.description =
            "Cross-zone defense alarm. World boss / invasion broadcast.";
        e.channelType = WoweeChannel::World;
        e.autoJoin = 1; e.minLevel = 10;
        e.moderated = 1;
        c.entries.push_back(e);
    }
    {
        WoweeChannel::Entry e;
        e.channelId = 202; e.name = "RaidCoordination";
        e.description =
            "Custom moderated channel for cross-guild raid runs.";
        e.channelType = WoweeChannel::Custom;
        e.autoJoin = 0; e.minLevel = 60;
        e.moderated = 1;
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
