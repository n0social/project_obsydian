#include "pipeline/wowee_animations.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'N', 'I'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wani") {
        base += ".wani";
    }
    return base;
}

} // namespace

const WoweeAnimation::Entry*
WoweeAnimation::findById(uint32_t animationId) const {
    for (const auto& e : entries)
        if (e.animationId == animationId) return &e;
    return nullptr;
}

const char* WoweeAnimation::behaviorTierName(uint8_t t) {
    switch (t) {
        case Default:  return "default";
        case Mounted:  return "mounted";
        case Sitting:  return "sitting";
        case Aerial:   return "aerial";
        case Swimming: return "swimming";
        default:       return "unknown";
    }
}

bool WoweeAnimationLoader::save(const WoweeAnimation& cat,
                                const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.animationId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.fallbackId);
        writePOD(os, e.behaviorId);
        writePOD(os, e.behaviorTier);
        uint8_t pad[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad), 3);
        writePOD(os, e.flags);
        writePOD(os, e.weaponFlags);
        writePOD(os, e.loopDurationMs);
    }
    return os.good();
}

WoweeAnimation WoweeAnimationLoader::load(const std::string& basePath) {
    WoweeAnimation out;
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
        if (!readPOD(is, e.animationId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.fallbackId) ||
            !readPOD(is, e.behaviorId) ||
            !readPOD(is, e.behaviorTier)) {
            out.entries.clear(); return out;
        }
        uint8_t pad[3];
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.flags) ||
            !readPOD(is, e.weaponFlags) ||
            !readPOD(is, e.loopDurationMs)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeAnimationLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeAnimation WoweeAnimationLoader::makeStarter(const std::string& catalogName) {
    WoweeAnimation c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t fallback,
                    uint32_t flags, uint32_t durMs, const char* desc) {
        WoweeAnimation::Entry e;
        e.animationId = id; e.name = name; e.description = desc;
        e.fallbackId = fallback;
        e.flags = flags;
        e.loopDurationMs = durMs;
        e.behaviorTier = WoweeAnimation::Default;
        c.entries.push_back(e);
    };
    // Animation IDs match the canonical WoW table.
    add(0,  "Stand",          0,
        WoweeAnimation::kFlagLooped |
        WoweeAnimation::kFlagBlendableCycle, 2000,
        "Idle stance — looping default.");
    add(4,  "Walk",           0,    // fall back to Stand
        WoweeAnimation::kFlagLooped |
        WoweeAnimation::kFlagMovementSync, 1000,
        "Slow walk cycle — synced to movement speed.");
    add(5,  "Run",            4,    // fall back to Walk
        WoweeAnimation::kFlagLooped |
        WoweeAnimation::kFlagMovementSync, 800,
        "Run cycle — synced to movement speed.");
    add(1,  "Death",          0,
        WoweeAnimation::kFlagOneShot |
        WoweeAnimation::kFlagPreserveAtEnd, 2500,
        "Death animation — pose preserved at end.");
    add(17, "AttackUnarmed",  0,
        WoweeAnimation::kFlagOneShot |
        WoweeAnimation::kFlagInterruptable, 1500,
        "Bare-handed melee swing.");
    return c;
}

WoweeAnimation WoweeAnimationLoader::makeCombat(const std::string& catalogName) {
    WoweeAnimation c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t fallback,
                    uint32_t weaponFlags, uint32_t durMs,
                    const char* desc) {
        WoweeAnimation::Entry e;
        e.animationId = id; e.name = name; e.description = desc;
        e.fallbackId = fallback;
        e.flags = WoweeAnimation::kFlagOneShot |
                   WoweeAnimation::kFlagInterruptable;
        e.weaponFlags = weaponFlags;
        e.loopDurationMs = durMs;
        c.entries.push_back(e);
    };
    add(17, "Attack1H",       0,
        WoweeAnimation::kWeapon1HMelee, 1500,
        "1H melee swing.");
    add(18, "Attack2H",       17,    // fall back to Attack1H
        WoweeAnimation::kWeapon2HMelee, 2000,
        "2H melee swing — slower wind-up.");
    add(19, "AttackDualWield", 17,
        WoweeAnimation::kWeaponDualWield, 1200,
        "Dual-wield alternating swings.");
    add(40, "AttackBow",      0,
        WoweeAnimation::kWeaponBow, 1800,
        "Bow draw + release.");
    add(41, "AttackRifle",    40,
        WoweeAnimation::kWeaponRifle, 1500,
        "Rifle shoulder + fire.");
    add(46, "AttackThrown",   0,
        WoweeAnimation::kWeaponAny, 1000,
        "Thrown weapon over-arm release.");
    add(53, "Parry",          0,
        WoweeAnimation::kWeapon1HMelee |
        WoweeAnimation::kWeapon2HMelee, 600,
        "Defensive weapon parry.");
    add(54, "ChannelCast",    0,
        WoweeAnimation::kWeaponAny, 3000,
        "Channeled spell cast — looping arms-out.");
    return c;
}

WoweeAnimation WoweeAnimationLoader::makeMovement(const std::string& catalogName) {
    WoweeAnimation c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t fallback,
                    uint8_t tier, uint32_t flags, uint32_t durMs,
                    const char* desc) {
        WoweeAnimation::Entry e;
        e.animationId = id; e.name = name; e.description = desc;
        e.fallbackId = fallback;
        e.behaviorTier = tier;
        e.flags = flags;
        e.loopDurationMs = durMs;
        c.entries.push_back(e);
    };
    add(4,   "Walk",         0,
        WoweeAnimation::Default,
        WoweeAnimation::kFlagLooped |
        WoweeAnimation::kFlagMovementSync, 1000,
        "Walk cycle (default tier).");
    add(5,   "Run",          4,
        WoweeAnimation::Default,
        WoweeAnimation::kFlagLooped |
        WoweeAnimation::kFlagMovementSync, 800,
        "Run cycle.");
    add(7,   "Sprint",       5,
        WoweeAnimation::Default,
        WoweeAnimation::kFlagLooped, 600,
        "Sprint — boosted run.");
    add(8,   "Swim",         4,
        WoweeAnimation::Swimming,
        WoweeAnimation::kFlagLooped |
        WoweeAnimation::kFlagMovementSync, 1200,
        "Underwater swim cycle.");
    add(91,  "MountIdle",    0,
        WoweeAnimation::Mounted,
        WoweeAnimation::kFlagLooped, 2500,
        "Sitting on a mount, holding the reins.");
    add(132, "Fly",          5,
        WoweeAnimation::Aerial,
        WoweeAnimation::kFlagLooped |
        WoweeAnimation::kFlagMovementSync, 1000,
        "Flying with wings extended.");
    return c;
}

} // namespace pipeline
} // namespace wowee
