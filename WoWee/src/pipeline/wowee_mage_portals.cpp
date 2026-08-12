#include "pipeline/wowee_mage_portals.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'R', 'T'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wprt") {
        base += ".wprt";
    }
    return base;
}

} // namespace

const WoweeMagePortals::Entry*
WoweeMagePortals::findById(uint32_t portalId) const {
    for (const auto& e : entries)
        if (e.portalId == portalId) return &e;
    return nullptr;
}

const WoweeMagePortals::Entry*
WoweeMagePortals::findBySpellId(uint32_t spellId) const {
    for (const auto& e : entries)
        if (e.spellId == spellId) return &e;
    return nullptr;
}

std::vector<const WoweeMagePortals::Entry*>
WoweeMagePortals::findByFaction(uint8_t faction) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.factionAccess == Both ||
            e.factionAccess == Neutral ||
            (faction != Both && e.factionAccess == faction)) {
            out.push_back(&e);
        }
    }
    return out;
}

bool WoweeMagePortalsLoader::save(const WoweeMagePortals& cat,
                                    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.portalId);
        writePOD(os, e.spellId);
        writeStr(os, e.destinationName);
        writePOD(os, e.destX);
        writePOD(os, e.destY);
        writePOD(os, e.destZ);
        writePOD(os, e.destOrientation);
        writePOD(os, e.destMapId);
        writePOD(os, e.factionAccess);
        writePOD(os, e.portalKind);
        writePOD(os, e.levelRequirement);
        writePOD(os, e.reagentCount);
        writePOD(os, e.reagentItemId);
    }
    return os.good();
}

WoweeMagePortals WoweeMagePortalsLoader::load(
    const std::string& basePath) {
    WoweeMagePortals out;
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
        if (!readPOD(is, e.portalId) ||
            !readPOD(is, e.spellId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.destinationName)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.destX) ||
            !readPOD(is, e.destY) ||
            !readPOD(is, e.destZ) ||
            !readPOD(is, e.destOrientation) ||
            !readPOD(is, e.destMapId) ||
            !readPOD(is, e.factionAccess) ||
            !readPOD(is, e.portalKind) ||
            !readPOD(is, e.levelRequirement) ||
            !readPOD(is, e.reagentCount) ||
            !readPOD(is, e.reagentItemId)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeMagePortalsLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeMagePortals WoweeMagePortalsLoader::makeAllianceCities(
    const std::string& catalogName) {
    using P = WoweeMagePortals;
    WoweeMagePortals c;
    c.name = catalogName;
    auto add = [&](uint32_t pid, uint32_t spellId,
                    const char* destName,
                    float x, float y, float z, float o,
                    uint32_t mapId, uint8_t levelReq) {
        P::Entry e;
        e.portalId = pid; e.spellId = spellId;
        e.destinationName = destName;
        e.destX = x; e.destY = y; e.destZ = z;
        e.destOrientation = o;
        e.destMapId = mapId;
        e.factionAccess = P::Alliance;
        e.portalKind = P::Portal;
        e.levelRequirement = levelReq;
        e.reagentCount = 1;
        e.reagentItemId = 17032;       // Rune of
                                         //  Portals
        c.entries.push_back(e);
    };
    // Vanilla mage portal coords (capital city
    // portal-room or central plaza). Coords sourced
    // from the canonical Stormwind, Ironforge,
    // Darnassus and Theramore portal landing zones.
    add(1, 10059, "Stormwind",
        -9009.f, 873.f, 148.f, 0.f, 0, 40);
    add(2, 11416, "Ironforge",
        -4623.f, -915.f, 502.f, 0.f, 0, 40);
    add(3, 11419, "Darnassus",
        9982.f, 2300.f, 1330.f, 0.f, 1, 40);
    add(4, 49361, "Theramore",
        -3753.f, -4527.f, 9.f, 0.f, 1, 40);
    return c;
}

WoweeMagePortals WoweeMagePortalsLoader::makeHordeCities(
    const std::string& catalogName) {
    using P = WoweeMagePortals;
    WoweeMagePortals c;
    c.name = catalogName;
    auto add = [&](uint32_t pid, uint32_t spellId,
                    const char* destName,
                    float x, float y, float z, float o,
                    uint32_t mapId) {
        P::Entry e;
        e.portalId = pid; e.spellId = spellId;
        e.destinationName = destName;
        e.destX = x; e.destY = y; e.destZ = z;
        e.destOrientation = o;
        e.destMapId = mapId;
        e.factionAccess = P::Horde;
        e.portalKind = P::Portal;
        e.levelRequirement = 40;
        e.reagentCount = 1;
        e.reagentItemId = 17032;       // Rune of
                                         //  Portals
        c.entries.push_back(e);
    };
    add(10, 11417, "Orgrimmar",
        1576.f, -4453.f, 16.f, 0.f, 1);
    add(11, 11418, "Undercity",
        1830.f, 239.f, 60.f, 0.f, 0);
    add(12, 11420, "Thunder Bluff",
        -1277.f, 122.f, 132.f, 0.f, 1);
    return c;
}

WoweeMagePortals WoweeMagePortalsLoader::makeTeleports(
    const std::string& catalogName) {
    using P = WoweeMagePortals;
    WoweeMagePortals c;
    c.name = catalogName;
    auto addT = [&](uint32_t pid, uint32_t spellId,
                     const char* destName,
                     float x, float y, float z, float o,
                     uint32_t mapId, uint8_t faction,
                     uint8_t levelReq) {
        P::Entry e;
        e.portalId = pid; e.spellId = spellId;
        e.destinationName = destName;
        e.destX = x; e.destY = y; e.destZ = z;
        e.destOrientation = o;
        e.destMapId = mapId;
        e.factionAccess = faction;
        e.portalKind = P::Teleport;    // self-only,
                                         //  costs Rune of
                                         //  Teleportation
        e.levelRequirement = levelReq;
        e.reagentCount = 1;
        e.reagentItemId = 17031;       // Rune of
                                         //  Teleportation
                                         //  (NOT Rune of
                                         //  Portals)
        c.entries.push_back(e);
    };
    addT(20, 3561, "Stormwind",
         -9009.f, 873.f, 148.f, 0.f, 0,
         P::Alliance, 20);
    addT(21, 3562, "Ironforge",
         -4623.f, -915.f, 502.f, 0.f, 0,
         P::Alliance, 20);
    addT(22, 3567, "Orgrimmar",
         1576.f, -4453.f, 16.f, 0.f, 1,
         P::Horde, 20);
    return c;
}

} // namespace pipeline
} // namespace wowee
