#include "pipeline/wowee_boss_encounters.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'O', 'S'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wbos") {
        base += ".wbos";
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

const WoweeBossEncounter::Entry*
WoweeBossEncounter::findById(uint32_t encounterId) const {
    for (const auto& e : entries)
        if (e.encounterId == encounterId) return &e;
    return nullptr;
}

std::vector<const WoweeBossEncounter::Entry*>
WoweeBossEncounter::findByMap(uint32_t mapId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.mapId == mapId) out.push_back(&e);
    return out;
}

std::vector<const WoweeBossEncounter::Entry*>
WoweeBossEncounter::findByBossCreature(uint32_t bossCreatureId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.bossCreatureId == bossCreatureId) out.push_back(&e);
    return out;
}

bool WoweeBossEncounterLoader::save(const WoweeBossEncounter& cat,
                                     const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.encounterId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.bossCreatureId);
        writePOD(os, e.mapId);
        writePOD(os, e.difficultyId);
        writePOD(os, e.berserkSpellId);
        writePOD(os, e.enrageTimerMs);
        writePOD(os, e.phaseCount);
        writePOD(os, e.requiredPartySize);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.recommendedItemLevel);
        writePOD(os, e.pad2);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeBossEncounter WoweeBossEncounterLoader::load(
    const std::string& basePath) {
    WoweeBossEncounter out;
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
        if (!readPOD(is, e.encounterId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.bossCreatureId) ||
            !readPOD(is, e.mapId) ||
            !readPOD(is, e.difficultyId) ||
            !readPOD(is, e.berserkSpellId) ||
            !readPOD(is, e.enrageTimerMs) ||
            !readPOD(is, e.phaseCount) ||
            !readPOD(is, e.requiredPartySize) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.recommendedItemLevel) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeBossEncounterLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeBossEncounter WoweeBossEncounterLoader::makeFiveMan(
    const std::string& catalogName) {
    using B = WoweeBossEncounter;
    WoweeBossEncounter c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t boss,
                    uint32_t map, uint32_t diff, uint8_t phases,
                    uint16_t ilvl, const char* desc) {
        B::Entry e;
        e.encounterId = id; e.name = name; e.description = desc;
        e.bossCreatureId = boss;
        e.mapId = map;
        e.difficultyId = diff;
        e.phaseCount = phases;
        e.requiredPartySize = 5;
        e.recommendedItemLevel = ilvl;
        e.iconColorRGBA = packRgba(180, 220, 100);   // dungeon green
        c.entries.push_back(e);
    };
    // 5-man dungeon bosses with no soft-enrage. mapId 600
    // is illustrative for "Drak'Tharon Keep"-style WotLK
    // 5-man instance.
    add(1, "TrollChieftain",  31000, 600, 200, 2, 200,
        "Troll Chieftain — 5-man dungeon, 2-phase encounter, "
        "no enrage.");
    add(2, "ShamanWraith",    31010, 600, 201, 1, 200,
        "Shaman Wraith — 5-man mid boss, single phase.");
    add(3, "DrakTharonFinal", 31030, 600, 203, 3, 210,
        "Drak'Tharon final boss — 5-man, 3-phase mass-death "
        "encounter.");
    return c;
}

WoweeBossEncounter WoweeBossEncounterLoader::makeRaid10(
    const std::string& catalogName) {
    using B = WoweeBossEncounter;
    WoweeBossEncounter c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t boss,
                    uint32_t diff, uint8_t phases,
                    uint32_t enrageMs, uint32_t berserkSpell,
                    uint16_t ilvl, const char* desc) {
        B::Entry e;
        e.encounterId = id; e.name = name; e.description = desc;
        e.bossCreatureId = boss;
        // mapId 631 = Icecrown Citadel.
        e.mapId = 631;
        e.difficultyId = diff;
        e.phaseCount = phases;
        e.requiredPartySize = 10;
        e.enrageTimerMs = enrageMs;
        e.berserkSpellId = berserkSpell;
        e.recommendedItemLevel = ilvl;
        e.iconColorRGBA = packRgba(220, 80, 100);    // raid red
        c.entries.push_back(e);
    };
    // Spell 26662 = Berserk (canonical raid soft-enrage spell).
    add(100, "LordMarrowgar",     36612, 100, 2, 600000,  26662, 232,
        "Lord Marrowgar 10N — 2-phase: tank-and-spank then "
        "Bone Storm whirlwind. 10min soft enrage.");
    add(101, "LadyDeathwhisper",  36855, 101, 2, 600000,  26662, 232,
        "Lady Deathwhisper 10N — 2-phase: shield drop then "
        "mind-control adds. 10min soft enrage.");
    add(102, "DeathbringerSaurfang",37813, 102, 1, 480000, 26662, 232,
        "Deathbringer Saurfang 10N — single phase, blood "
        "beasts add wave. 8min soft enrage.");
    add(103, "TheLichKing",       36597, 103, 5, 900000,  72546, 251,
        "The Lich King 10N — 5-phase encounter (tank-spank, "
        "platform jumps, Frostmourne soul realm). 15min hard "
        "enrage via Fury of Frostmourne.");
    return c;
}

WoweeBossEncounter WoweeBossEncounterLoader::makeWorldBoss(
    const std::string& catalogName) {
    using B = WoweeBossEncounter;
    WoweeBossEncounter c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t boss,
                    uint32_t map, uint16_t ilvl, const char* desc) {
        B::Entry e;
        e.encounterId = id; e.name = name; e.description = desc;
        e.bossCreatureId = boss;
        e.mapId = map;
        e.phaseCount = 1;
        e.requiredPartySize = 25;
        // World bosses don't use the difficulty system —
        // single open-world spawn that scales to attackers.
        e.difficultyId = 0;
        // No soft-enrage — outdoor encounters can take
        // arbitrarily long.
        e.recommendedItemLevel = ilvl;
        e.iconColorRGBA = packRgba(240, 100, 240);   // world boss magenta
        c.entries.push_back(e);
    };
    // mapId 530 = Outland (Hellfire Peninsula for Kazzak).
    // mapId 530 also for Doomwalker (Shadowmoon Valley).
    add(200, "DoomLordKazzak", 18728, 530, 134,
        "Doom Lord Kazzak — Hellfire Peninsula world boss, "
        "single phase, no enrage. 25-player encounter.");
    add(201, "Doomwalker",     17711, 530, 132,
        "Doomwalker — Shadowmoon Valley patrol, single phase, "
        "rare 25-player tap encounter.");
    return c;
}

} // namespace pipeline
} // namespace wowee
