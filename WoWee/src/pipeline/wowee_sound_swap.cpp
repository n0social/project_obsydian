#include "pipeline/wowee_sound_swap.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'W', 'P'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wswp") {
        base += ".wswp";
    }
    return base;
}

} // namespace

const WoweeSoundSwap::Entry*
WoweeSoundSwap::findById(uint32_t ruleId) const {
    for (const auto& e : entries)
        if (e.ruleId == ruleId) return &e;
    return nullptr;
}

std::vector<const WoweeSoundSwap::Entry*>
WoweeSoundSwap::findByOriginalSound(uint32_t soundId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.originalSoundId == soundId)
            out.push_back(&e);
    return out;
}

bool WoweeSoundSwapLoader::save(const WoweeSoundSwap& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.ruleId);
        writeStr(os, e.name);
        writePOD(os, e.originalSoundId);
        writePOD(os, e.replacementSoundId);
        writePOD(os, e.conditionKind);
        writePOD(os, e.priorityIndex);
        writePOD(os, e.gainAdjustDb_x10);
        writePOD(os, e.conditionValue);
    }
    return os.good();
}

WoweeSoundSwap WoweeSoundSwapLoader::load(
    const std::string& basePath) {
    WoweeSoundSwap out;
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
        if (!readPOD(is, e.ruleId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.originalSoundId) ||
            !readPOD(is, e.replacementSoundId) ||
            !readPOD(is, e.conditionKind) ||
            !readPOD(is, e.priorityIndex) ||
            !readPOD(is, e.gainAdjustDb_x10) ||
            !readPOD(is, e.conditionValue)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSoundSwapLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

WoweeSoundSwap::Entry makeRule(
    uint32_t ruleId, const char* name,
    uint32_t origSound, uint32_t replSound,
    uint8_t conditionKind, uint8_t priority,
    int16_t gainAdjustDb_x10,
    uint32_t conditionValue) {
    WoweeSoundSwap::Entry e;
    e.ruleId = ruleId; e.name = name;
    e.originalSoundId = origSound;
    e.replacementSoundId = replSound;
    e.conditionKind = conditionKind;
    e.priorityIndex = priority;
    e.gainAdjustDb_x10 = gainAdjustDb_x10;
    e.conditionValue = conditionValue;
    return e;
}

} // namespace

WoweeSoundSwap WoweeSoundSwapLoader::makeBossOverrides(
    const std::string& catalogName) {
    using S = WoweeSoundSwap;
    WoweeSoundSwap c;
    c.name = catalogName;
    // Onyxia (mapId 249 — Onyxia's Lair). Replace
    // her stock dragon roar (FDID 1234) with a
    // beefier custom roar (FDID 5001). Priority 100
    // ensures it wins over global rules.
    c.entries.push_back(makeRule(
        1, "Onyxia Custom Roar",
        1234 /* stock dragon roar */,
        5001 /* custom Onyxia roar */,
        S::ZoneOnly, 100, 0,
        249 /* Onyxia's Lair mapId */));
    // Ragnaros emerge sound (FDID 1567) -> custom
    // (5002). Molten Core mapId 409. +20 (+2dB)
    // gain to make it dramatic.
    c.entries.push_back(makeRule(
        2, "Ragnaros Emerge Sound",
        1567 /* stock Ragnaros emerge */,
        5002 /* custom emerge */,
        S::ZoneOnly, 100, 20,
        409 /* Molten Core */));
    // Nefarian shout in BWL (mapId 469). FDID 1789
    // -> 5003. Priority 100, no gain adjust.
    c.entries.push_back(makeRule(
        3, "Nefarian Shout",
        1789, 5003,
        S::ZoneOnly, 100, 0,
        469 /* BWL */));
    return c;
}

WoweeSoundSwap WoweeSoundSwapLoader::makeRaceVoices(
    const std::string& catalogName) {
    using S = WoweeSoundSwap;
    WoweeSoundSwap c;
    c.name = catalogName;
    // BloodElf priest cast voice (sound 2001) ->
    // custom (5101). Race id 10 = BloodElf.
    // Priority 50 (less than boss overrides).
    c.entries.push_back(makeRule(
        10, "BloodElf Priest Cast Voice",
        2001 /* stock cast voice */,
        5101 /* custom BE voice */,
        S::RaceOnly, 50, 0,
        10 /* BloodElf raceId */));
    // Tauren shaman cast voice (sound 2002) ->
    // 5102. Race id 6 = Tauren.
    c.entries.push_back(makeRule(
        11, "Tauren Shaman Cast Voice",
        2002, 5102,
        S::RaceOnly, 50, 0,
        6 /* Tauren raceId */));
    // Undead warlock cast voice (sound 2003) ->
    // 5103. Race id 5 = Undead.
    c.entries.push_back(makeRule(
        12, "Undead Warlock Cast Voice",
        2003, 5103,
        S::RaceOnly, 50, 0,
        5 /* Undead raceId */));
    return c;
}

WoweeSoundSwap WoweeSoundSwapLoader::makeGlobalUI(
    const std::string& catalogName) {
    using S = WoweeSoundSwap;
    WoweeSoundSwap c;
    c.name = catalogName;
    // Level-up (sound 888) -> custom 5201, +3 dB
    // gain adjust to make custom version slightly
    // louder. Always condition. Lowest priority
    // (10) because boss/race overrides should win.
    c.entries.push_back(makeRule(
        20, "Custom Level-Up Fanfare",
        888 /* stock level-up */,
        5201, S::Always, 10, 30,
        0));
    // Quest-complete sound (sound 877) -> 5202.
    c.entries.push_back(makeRule(
        21, "Custom Quest-Complete",
        877, 5202, S::Always, 10, 30,
        0));
    // Mount-up (sound 866) -> 5203. +3 dB.
    c.entries.push_back(makeRule(
        22, "Custom Mount-Up",
        866, 5203, S::Always, 10, 30,
        0));
    return c;
}

} // namespace pipeline
} // namespace wowee
