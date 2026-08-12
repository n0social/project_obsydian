#include "pipeline/wowee_unit_movement.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'U', 'M', 'V'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wumv") {
        base += ".wumv";
    }
    return base;
}

} // namespace

const WoweeUnitMovement::Entry*
WoweeUnitMovement::findById(uint32_t moveTypeId) const {
    for (const auto& e : entries)
        if (e.moveTypeId == moveTypeId) return &e;
    return nullptr;
}

const char* WoweeUnitMovement::movementCategoryName(uint8_t c) {
    switch (c) {
        case Walk:       return "walk";
        case Run:        return "run";
        case Backward:   return "backward";
        case Swim:       return "swim";
        case SwimBack:   return "swim-back";
        case Turn:       return "turn";
        case Flight:     return "flight";
        case FlightBack: return "flight-back";
        case Pitch:      return "pitch";
        case Fly:        return "fly";
        case FlyBack:    return "fly-back";
        case TempBuff:   return "temp-buff";
        default:         return "unknown";
    }
}

bool WoweeUnitMovementLoader::save(const WoweeUnitMovement& cat,
                                    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.moveTypeId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.movementCategory);
        writePOD(os, e.requiresFlight);
        writePOD(os, e.canStackBuffs);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, e.baseSpeed);
        writePOD(os, e.baseMultiplier);
        writePOD(os, e.maxMultiplier);
        writePOD(os, e.defaultDurationMs);
        writePOD(os, e.stackingPriority);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
    }
    return os.good();
}

WoweeUnitMovement WoweeUnitMovementLoader::load(
    const std::string& basePath) {
    WoweeUnitMovement out;
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
        if (!readPOD(is, e.moveTypeId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.movementCategory) ||
            !readPOD(is, e.requiresFlight) ||
            !readPOD(is, e.canStackBuffs)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.baseSpeed) ||
            !readPOD(is, e.baseMultiplier) ||
            !readPOD(is, e.maxMultiplier) ||
            !readPOD(is, e.defaultDurationMs) ||
            !readPOD(is, e.stackingPriority)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
    }
    return out;
}

bool WoweeUnitMovementLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeUnitMovement WoweeUnitMovementLoader::makeStarter(
    const std::string& catalogName) {
    WoweeUnitMovement c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t category,
                    float baseSpeed, const char* desc) {
        WoweeUnitMovement::Entry e;
        e.moveTypeId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Ability_") +
                      name + ".blp";
        e.movementCategory = category;
        e.baseSpeed = baseSpeed;
        e.canStackBuffs = 0;     // baseline speeds don't stack
        c.entries.push_back(e);
    };
    add(1, "WalkSpeed", WoweeUnitMovement::Walk, 2.5f,
        "Canonical walk speed — 2.5 yards / second.");
    add(2, "RunSpeed",  WoweeUnitMovement::Run,  7.0f,
        "Canonical run speed — 7.0 yards / second.");
    add(3, "SwimSpeed", WoweeUnitMovement::Swim, 4.7f,
        "Canonical swim speed — 4.7 yards / second underwater.");
    add(4, "TurnRate",  WoweeUnitMovement::Turn, 3.14f,
        "Canonical turn rate — π radians / second (180°/s).");
    return c;
}

WoweeUnitMovement WoweeUnitMovementLoader::makeFlight(
    const std::string& catalogName) {
    WoweeUnitMovement c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t category,
                    float baseSpeed, uint8_t requiresFlight,
                    const char* desc) {
        WoweeUnitMovement::Entry e;
        e.moveTypeId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Ability_Mount_") +
                      name + ".blp";
        e.movementCategory = category;
        e.baseSpeed = baseSpeed;
        e.requiresFlight = requiresFlight;
        e.canStackBuffs = 1;    // flight buffs stack
        c.entries.push_back(e);
    };
    add(100, "Flight",         WoweeUnitMovement::Flight,
        7.0f,  1,
        "Ground-rail flight speed — 7.0y/s, used by gryphon "
        "taxi rides.");
    add(101, "Fly",            WoweeUnitMovement::Fly,
        14.0f, 1,
        "Free-flight cruise speed — 14.0y/s base on a flying "
        "mount.");
    add(102, "FlyBack",        WoweeUnitMovement::FlyBack,
        4.5f,  1,
        "Backward flight — slower (no mount can reverse fast).");
    add(103, "FlightBack",     WoweeUnitMovement::FlightBack,
        4.5f,  1,
        "Backward ground-rail flight (taxi node reverse).");
    add(104, "Pitch",          WoweeUnitMovement::Pitch,
        1.5f,  1,
        "Pitch rate while flying — 1.5 radians/second (≈86°/s).");
    return c;
}

WoweeUnitMovement WoweeUnitMovementLoader::makeBuffs(
    const std::string& catalogName) {
    WoweeUnitMovement c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float multiplier,
                    float maxMultiplier, uint32_t durationMs,
                    uint8_t priority, const char* desc) {
        WoweeUnitMovement::Entry e;
        e.moveTypeId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Ability_") +
                      name + ".blp";
        e.movementCategory = WoweeUnitMovement::TempBuff;
        e.baseSpeed = 0.0f;     // multiplier-only entry
        e.baseMultiplier = multiplier;
        e.maxMultiplier = maxMultiplier;
        e.defaultDurationMs = durationMs;
        e.canStackBuffs = 1;
        e.stackingPriority = priority;
        c.entries.push_back(e);
    };
    add(200, "Sprint",            1.40f, 1.40f, 15000, 100,
        "Rogue sprint — 40% movement speed for 15 seconds.");
    add(201, "AspectCheetah",     1.30f, 1.30f, 0,    50,
        "Hunter aspect — 30% movement speed permanently. "
        "Breaks on damage.");
    add(202, "TravelForm",        1.40f, 1.40f, 0,    50,
        "Druid travel form — 40% speed, persists until "
        "shifted out.");
    add(203, "CrusaderAura",      1.20f, 1.20f, 0,    30,
        "Paladin aura — 20% mounted speed for the party.");
    add(204, "WindWalk",          1.50f, 1.50f, 12000, 80,
        "Shaman ghost-wolf style buff — 50% speed for "
        "12 seconds.");
    return c;
}

} // namespace pipeline
} // namespace wowee
