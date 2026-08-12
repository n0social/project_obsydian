#include "pipeline/wowee_loot_modes.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'M', 'A'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wlma") {
        base += ".wlma";
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

const WoweeLootModes::Entry*
WoweeLootModes::findById(uint32_t modeId) const {
    for (const auto& e : entries)
        if (e.modeId == modeId) return &e;
    return nullptr;
}

std::vector<const WoweeLootModes::Entry*>
WoweeLootModes::findByKind(uint8_t modeKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.modeKind == modeKind) out.push_back(&e);
    return out;
}

bool WoweeLootModesLoader::save(const WoweeLootModes& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.modeId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.modeKind);
        writePOD(os, e.thresholdQuality);
        writePOD(os, e.masterLooterRequired);
        writePOD(os, e.idleSkipSec);
        writePOD(os, e.timeoutFallbackKind);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeLootModes WoweeLootModesLoader::load(
    const std::string& basePath) {
    WoweeLootModes out;
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
        if (!readPOD(is, e.modeId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.modeKind) ||
            !readPOD(is, e.thresholdQuality) ||
            !readPOD(is, e.masterLooterRequired) ||
            !readPOD(is, e.idleSkipSec) ||
            !readPOD(is, e.timeoutFallbackKind) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeLootModesLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeLootModes WoweeLootModesLoader::makeStandard(
    const std::string& catalogName) {
    using L = WoweeLootModes;
    WoweeLootModes c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t threshold, uint8_t mlReq,
                    uint8_t idleSkip, const char* desc) {
        L::Entry e;
        e.modeId = id; e.name = name; e.description = desc;
        e.modeKind = kind;
        e.thresholdQuality = threshold;
        e.masterLooterRequired = mlReq;
        e.idleSkipSec = idleSkip;
        e.timeoutFallbackKind = L::FreeForAll;
        e.iconColorRGBA = packRgba(180, 220, 100);   // standard green
        c.entries.push_back(e);
    };
    add(1, "FFAFarming", L::FreeForAll, 0, 0, 0,
        "Free-For-All — first-click wins. Common in "
        "solo/duo farming runs where all loot is "
        "destined for the AH.");
    add(2, "RoundRobinTrash", L::RoundRobin, 0, 0, 0,
        "Round-Robin — rotates through party slots, "
        "assigns loot to the next player in sequence. "
        "Standard 5-man trash policy.");
    add(3, "NeedBeforeGreedUncommon",
        L::NeedBeforeGreed, 2, 0, 0,
        "Need-Before-Greed at Uncommon threshold — "
        "anything Uncommon (green) or above triggers "
        "the Need/Greed/Pass roll dialog. Standard "
        "5-man heroic policy.");
    add(4, "MasterLootRare", L::MasterLoot, 3, 1, 0,
        "Master Loot at Rare threshold — anything "
        "Rare (blue) or above goes to the master "
        "looter for hand distribution. Master looter "
        "REQUIRED.");
    return c;
}

WoweeLootModes WoweeLootModesLoader::makeRaidPolicies(
    const std::string& catalogName) {
    using L = WoweeLootModes;
    WoweeLootModes c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t threshold, uint8_t mlReq,
                    uint8_t fallback, const char* desc) {
        L::Entry e;
        e.modeId = id; e.name = name; e.description = desc;
        e.modeKind = kind;
        e.thresholdQuality = threshold;
        e.masterLooterRequired = mlReq;
        e.idleSkipSec = 0;
        e.timeoutFallbackKind = fallback;
        e.iconColorRGBA = packRgba(220, 80, 100);   // raid red
        c.entries.push_back(e);
    };
    add(100, "MasterLootEpicRaid", L::MasterLoot, 4, 1,
        L::NeedBeforeGreed,
        "Master Loot at Epic threshold — standard 25-"
        "man raid policy. Master looter REQUIRED. "
        "Fallback to Need-Before-Greed if master looter "
        "disconnects mid-distribution.");
    add(101, "PersonalLootEpic", L::Personal, 4, 0,
        L::Personal,
        "Personal Loot at Epic threshold — each player "
        "gets their own roll, no master-looter needed. "
        "Anti-drama policy for pug raids.");
    add(102, "NBGRareDefault", L::NeedBeforeGreed, 3, 0,
        L::FreeForAll,
        "Need-Before-Greed at Rare (Blue) threshold — "
        "less restrictive than Epic gating. Common "
        "10-man raid default.");
    return c;
}

WoweeLootModes WoweeLootModesLoader::makeAFKPrevention(
    const std::string& catalogName) {
    using L = WoweeLootModes;
    WoweeLootModes c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t threshold, uint8_t mlReq,
                    uint8_t idleSkip, uint8_t fallback,
                    const char* desc) {
        L::Entry e;
        e.modeId = id; e.name = name; e.description = desc;
        e.modeKind = kind;
        e.thresholdQuality = threshold;
        e.masterLooterRequired = mlReq;
        e.idleSkipSec = idleSkip;
        e.timeoutFallbackKind = fallback;
        e.iconColorRGBA = packRgba(220, 200, 80);   // AFK warning yellow
        c.entries.push_back(e);
    };
    add(200, "RoundRobinIdleSkip30",
        L::RoundRobin, 0, 0, 30, L::FreeForAll,
        "Round-Robin with 30-second idle-skip — "
        "advances rotation if current pick is AFK > "
        "30s. Prevents stuck loot rotations in casual "
        "groups.");
    add(201, "MasterLootTimeout",
        L::MasterLoot, 4, 1, 0, L::NeedBeforeGreed,
        "Master Loot with timeout fallback — if master "
        "looter is unresponsive for 60s, the loot mode "
        "auto-promotes to Need-Before-Greed. Server "
        "enforces the 60s window externally.");
    add(202, "PersonalIdleSkip45",
        L::Personal, 3, 0, 45, L::Personal,
        "Personal Loot with 45-second idle-skip — "
        "each player has 45s to pick up their personal "
        "drop, then it bag-skips. Anti-AFK-leech "
        "policy.");
    return c;
}

} // namespace pipeline
} // namespace wowee
