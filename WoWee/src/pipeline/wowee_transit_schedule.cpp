#include "pipeline/wowee_transit_schedule.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'S', 'C'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wtsc") {
        base += ".wtsc";
    }
    return base;
}

} // namespace

const WoweeTransitSchedule::Entry*
WoweeTransitSchedule::findById(uint32_t routeId) const {
    for (const auto& e : entries)
        if (e.routeId == routeId) return &e;
    return nullptr;
}

std::vector<const WoweeTransitSchedule::Entry*>
WoweeTransitSchedule::findAccessibleByFaction(uint8_t faction) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.factionAccess == Both) {
            out.push_back(&e);
        } else if (faction != Both && e.factionAccess == faction) {
            out.push_back(&e);
        } else if (e.factionAccess == Neutral) {
            // Neutral routes are accessible to ALL
            // factions including Both — Booty Bay /
            // Ratchet style.
            out.push_back(&e);
        }
    }
    return out;
}

std::vector<const WoweeTransitSchedule::Entry*>
WoweeTransitSchedule::findDeparturesFromMap(uint32_t mapId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.originMapId == mapId) out.push_back(&e);
    }
    return out;
}

bool WoweeTransitScheduleLoader::save(
    const WoweeTransitSchedule& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.routeId);
        writeStr(os, e.name);
        writePOD(os, e.vehicleType);
        writePOD(os, e.factionAccess);
        writePOD(os, e.pad0);
        writeStr(os, e.originName);
        writePOD(os, e.originX);
        writePOD(os, e.originY);
        writePOD(os, e.originMapId);
        writeStr(os, e.destinationName);
        writePOD(os, e.destinationX);
        writePOD(os, e.destinationY);
        writePOD(os, e.destinationMapId);
        writePOD(os, e.departureIntervalSec);
        writePOD(os, e.travelDurationSec);
        writePOD(os, e.capacity);
        writePOD(os, e.pad1);
    }
    return os.good();
}

WoweeTransitSchedule WoweeTransitScheduleLoader::load(
    const std::string& basePath) {
    WoweeTransitSchedule out;
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
        if (!readPOD(is, e.routeId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.vehicleType) ||
            !readPOD(is, e.factionAccess) ||
            !readPOD(is, e.pad0)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.originName)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.originX) ||
            !readPOD(is, e.originY) ||
            !readPOD(is, e.originMapId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.destinationName)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.destinationX) ||
            !readPOD(is, e.destinationY) ||
            !readPOD(is, e.destinationMapId)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.departureIntervalSec) ||
            !readPOD(is, e.travelDurationSec) ||
            !readPOD(is, e.capacity) ||
            !readPOD(is, e.pad1)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeTransitScheduleLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeTransitSchedule WoweeTransitScheduleLoader::makeZeppelins(
    const std::string& catalogName) {
    using T = WoweeTransitSchedule;
    WoweeTransitSchedule c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* origin, float ox, float oy,
                    uint32_t omap,
                    const char* dest, float dx, float dy,
                    uint32_t dmap,
                    uint32_t intervalSec,
                    uint32_t travelSec,
                    uint16_t capacity) {
        T::Entry e;
        e.routeId = id; e.name = name;
        e.vehicleType = T::Zeppelin;
        e.factionAccess = T::Horde;
        e.originName = origin;
        e.originX = ox; e.originY = oy;
        e.originMapId = omap;
        e.destinationName = dest;
        e.destinationX = dx; e.destinationY = dy;
        e.destinationMapId = dmap;
        e.departureIntervalSec = intervalSec;
        e.travelDurationSec = travelSec;
        e.capacity = capacity;
        c.entries.push_back(e);
    };
    // Vanilla zeppelin tower coordinates (Orgrimmar
    // map=1, Eastern Kingdoms via UC map=0,
    // Stranglethorn via Grom'Gol map=0). Capacity 40
    // approximates the rim platform headcount.
    add(1, "Orgrimmar to Undercity",
        "Orgrimmar Zeppelin Tower",   1843.f, -4416.f, 1,
        "Tirisfal Zeppelin Tower",    2055.f,   273.f, 0,
        240, 60, 40);
    add(2, "Orgrimmar to Grom'Gol",
        "Orgrimmar Zeppelin Tower",   1843.f, -4416.f, 1,
        "Grom'Gol Zeppelin Tower",   -12422.f, 110.f,  0,
        240, 90, 40);
    add(3, "Undercity to Grom'Gol",
        "Tirisfal Zeppelin Tower",    2055.f,   273.f, 0,
        "Grom'Gol Zeppelin Tower",   -12422.f, 110.f,  0,
        240, 90, 40);
    return c;
}

WoweeTransitSchedule WoweeTransitScheduleLoader::makeBoats(
    const std::string& catalogName) {
    using T = WoweeTransitSchedule;
    WoweeTransitSchedule c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t faction,
                    const char* origin, float ox, float oy,
                    uint32_t omap,
                    const char* dest, float dx, float dy,
                    uint32_t dmap,
                    uint32_t intervalSec,
                    uint32_t travelSec,
                    uint16_t capacity) {
        T::Entry e;
        e.routeId = id; e.name = name;
        e.vehicleType = T::Boat;
        e.factionAccess = faction;
        e.originName = origin;
        e.originX = ox; e.originY = oy;
        e.originMapId = omap;
        e.destinationName = dest;
        e.destinationX = dx; e.destinationY = dy;
        e.destinationMapId = dmap;
        e.departureIntervalSec = intervalSec;
        e.travelDurationSec = travelSec;
        e.capacity = capacity;
        c.entries.push_back(e);
    };
    add(10, "Auberdine to Stormwind Harbor",
        T::Alliance,
        "Auberdine Dock",       6577.f, 769.f,  1,
        "Stormwind Harbor",   -8713.f, 1281.f, 0,
        300, 90, 30);
    add(11, "Menethil Harbor to Theramore",
        T::Alliance,
        "Menethil Harbor",     -3814.f, -616.f,  0,
        "Theramore Isle",       -3870.f, -4533.f, 1,
        300, 90, 30);
    add(12, "Booty Bay to Ratchet",
        T::Neutral,                  // both factions
                                       //  may board
        "Booty Bay Dock",     -14305.f,  570.f, 0,
        "Ratchet Dock",         -984.f, -3835.f, 1,
        180, 75, 35);
    return c;
}

WoweeTransitSchedule WoweeTransitScheduleLoader::makeTaxis(
    const std::string& catalogName) {
    using T = WoweeTransitSchedule;
    WoweeTransitSchedule c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t faction,
                    const char* origin, float ox, float oy,
                    uint32_t omap,
                    const char* dest, float dx, float dy,
                    uint32_t dmap,
                    uint32_t intervalSec,
                    uint32_t travelSec,
                    uint16_t capacity) {
        T::Entry e;
        e.routeId = id; e.name = name;
        e.vehicleType = T::Taxi;
        e.factionAccess = faction;
        e.originName = origin;
        e.originX = ox; e.originY = oy;
        e.originMapId = omap;
        e.destinationName = dest;
        e.destinationX = dx; e.destinationY = dy;
        e.destinationMapId = dmap;
        e.departureIntervalSec = intervalSec;
        e.travelDurationSec = travelSec;
        e.capacity = capacity;
        c.entries.push_back(e);
    };
    // Capacity=0 for taxis: each gryphon/wyvern is a
    // solo ride, no shared seating — interval matters
    // only for the visual gryphon respawn timer at the
    // taxi master.
    add(20, "Stormwind to Ironforge",
        T::Alliance,
        "Stormwind, Eastvale",  -8836.f,  490.f, 0,
        "Ironforge, Tinkertown", -4815.f, -1170.f, 0,
        30, 200, 0);
    add(21, "Crossroads to Razor Hill",
        T::Horde,
        "The Crossroads",        -445.f, -2598.f, 1,
        "Razor Hill",             314.f, -4748.f, 1,
        30, 130, 0);
    add(22, "Booty Bay to Stormwind",
        T::Neutral,
        "Booty Bay Gryphon Master", -14373.f, 555.f, 0,
        "Stormwind, Eastvale",      -8836.f, 490.f, 0,
        30, 320, 0);
    return c;
}

} // namespace pipeline
} // namespace wowee
