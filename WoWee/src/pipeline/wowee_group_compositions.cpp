#include "pipeline/wowee_group_compositions.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'R', 'P'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wgrp") {
        base += ".wgrp";
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

const WoweeGroupComposition::Entry*
WoweeGroupComposition::findById(uint32_t compId) const {
    for (const auto& e : entries)
        if (e.compId == compId) return &e;
    return nullptr;
}

std::vector<const WoweeGroupComposition::Entry*>
WoweeGroupComposition::findByMap(uint32_t mapId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.mapId == mapId) out.push_back(&e);
    return out;
}

bool WoweeGroupComposition::partyMeetsComp(uint32_t compId,
                                            uint8_t haveTanks,
                                            uint8_t haveHealers,
                                            uint8_t haveDps) const {
    const Entry* e = findById(compId);
    if (!e) return false;
    if (haveTanks < e->requiredTanks) return false;
    if (haveHealers < e->requiredHealers) return false;
    if (haveDps < e->requiredDamageDealers) return false;
    uint8_t total = haveTanks + haveHealers + haveDps;
    if (total < e->minPartySize) return false;
    if (total > e->maxPartySize) return false;
    return true;
}

bool WoweeGroupCompositionLoader::save(const WoweeGroupComposition& cat,
                                        const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.compId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mapId);
        writePOD(os, e.difficultyId);
        writePOD(os, e.requiredTanks);
        writePOD(os, e.requiredHealers);
        writePOD(os, e.requiredDamageDealers);
        writePOD(os, e.minPartySize);
        writePOD(os, e.maxPartySize);
        writePOD(os, e.requireSpec);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeGroupComposition WoweeGroupCompositionLoader::load(
    const std::string& basePath) {
    WoweeGroupComposition out;
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
        if (!readPOD(is, e.compId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.difficultyId) ||
            !readPOD(is, e.requiredTanks) ||
            !readPOD(is, e.requiredHealers) ||
            !readPOD(is, e.requiredDamageDealers) ||
            !readPOD(is, e.minPartySize) ||
            !readPOD(is, e.maxPartySize) ||
            !readPOD(is, e.requireSpec) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeGroupCompositionLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeGroupComposition WoweeGroupCompositionLoader::makeFiveMan(
    const std::string& catalogName) {
    using G = WoweeGroupComposition;
    WoweeGroupComposition c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint8_t tanks, uint8_t healers, uint8_t dps,
                    uint8_t requireSpec, const char* desc) {
        G::Entry e;
        e.compId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.difficultyId = 1;     // 5-man heroic
        e.requiredTanks = tanks;
        e.requiredHealers = healers;
        e.requiredDamageDealers = dps;
        e.minPartySize = 5;
        e.maxPartySize = 5;
        e.requireSpec = requireSpec;
        e.iconColorRGBA = packRgba(180, 220, 100);   // dungeon green
        c.entries.push_back(e);
    };
    add(1, "Classic5ManTanksHealsDPS", 600, 1, 1, 3, 1,
        "Classic 5-man comp — 1 tank / 1 healer / 3 dps, "
        "spec roles enforced.");
    add(2, "Heavy5ManTrashHeal",       600, 1, 2, 2, 1,
        "Heavy-heal 5-man trash run — 1T/2H/2D for "
        "healing-intensive content.");
    add(3, "RolelessSpeedRun",         600, 0, 0, 5, 0,
        "Roleless 5-man speed run — 5 dps, no spec gate. "
        "Used by speed-run guilds for sub-15min clears.");
    return c;
}

WoweeGroupComposition WoweeGroupCompositionLoader::makeRaid10(
    const std::string& catalogName) {
    using G = WoweeGroupComposition;
    WoweeGroupComposition c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint8_t tanks, uint8_t healers, uint8_t dps,
                    const char* desc) {
        G::Entry e;
        e.compId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.difficultyId = 100;
        e.requiredTanks = tanks;
        e.requiredHealers = healers;
        e.requiredDamageDealers = dps;
        e.minPartySize = 10;
        e.maxPartySize = 10;
        e.requireSpec = 1;
        e.iconColorRGBA = packRgba(220, 80, 100);    // raid red
        c.entries.push_back(e);
    };
    add(100, "Standard10Man", 631, 2, 3, 5,
        "Standard 10-man raid — 2T/3H/5D matches most ICC "
        "10N progression.");
    add(101, "HealingHeavy10Man", 631, 2, 4, 4,
        "Healing-heavy 10-man — 2T/4H/4D for healing-"
        "intensive ICC 10H bosses (Putricide, Sindragosa).");
    add(102, "MeleeStack10Man", 631, 1, 2, 7,
        "Melee-stack 10-man — 1T/2H/7D for melee-cleave "
        "fights with no DPS race (Saurfang heroic exec, "
        "Festergut). Brings extra melee to nuke a single "
        "target; one-tank because no swap mechanic.");
    return c;
}

WoweeGroupComposition WoweeGroupCompositionLoader::makeRaid25(
    const std::string& catalogName) {
    using G = WoweeGroupComposition;
    WoweeGroupComposition c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint8_t tanks, uint8_t healers, uint8_t dps,
                    const char* desc) {
        G::Entry e;
        e.compId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.difficultyId = 101;
        e.requiredTanks = tanks;
        e.requiredHealers = healers;
        e.requiredDamageDealers = dps;
        e.minPartySize = 25;
        e.maxPartySize = 25;
        e.requireSpec = 1;
        e.iconColorRGBA = packRgba(180, 100, 240);    // 25-man purple
        c.entries.push_back(e);
    };
    add(200, "Standard25Man", 631, 2, 6, 17,
        "Standard 25-man raid — 2T/6H/17D matches most ICC "
        "25N progression.");
    add(201, "HealingHeavy25Man", 631, 1, 8, 16,
        "Healing-heavy 25-man — 1T/8H/16D for healing-"
        "intensive ICC 25H Putricide / LK heroic.");
    add(202, "ZergDPS25Man", 631, 0, 4, 21,
        "Zerg DPS 25-man — 0T/4H/21D for tank-immune fights "
        "(Loatheb-style trash piles).");
    return c;
}

} // namespace pipeline
} // namespace wowee
