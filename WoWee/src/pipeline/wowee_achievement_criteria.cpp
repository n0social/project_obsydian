#include "pipeline/wowee_achievement_criteria.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'C', 'R'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wacr") {
        base += ".wacr";
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

const WoweeAchievementCriteria::Entry*
WoweeAchievementCriteria::findById(uint32_t criteriaId) const {
    for (const auto& e : entries)
        if (e.criteriaId == criteriaId) return &e;
    return nullptr;
}

std::vector<const WoweeAchievementCriteria::Entry*>
WoweeAchievementCriteria::findByAchievement(
    uint32_t achievementId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.achievementId == achievementId) out.push_back(&e);
    }
    std::sort(out.begin(), out.end(),
              [](const Entry* a, const Entry* b) {
                  return a->progressOrder < b->progressOrder;
              });
    return out;
}

const char* WoweeAchievementCriteria::criteriaTypeName(uint8_t k) {
    switch (k) {
        case KillCreature:    return "kill-creature";
        case ReachLevel:      return "reach-level";
        case CompleteQuest:   return "complete-quest";
        case EarnGold:        return "earn-gold";
        case GainHonor:       return "gain-honor";
        case EarnReputation:  return "earn-reputation";
        case ExploreZone:     return "explore-zone";
        case LootItem:        return "loot-item";
        case UseItem:         return "use-item";
        case CastSpell:       return "cast-spell";
        case PvPKill:         return "pvp-kill";
        case DungeonRun:      return "dungeon-run";
        case Misc:            return "misc";
        default:              return "unknown";
    }
}

bool WoweeAchievementCriteriaLoader::save(
    const WoweeAchievementCriteria& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.criteriaId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.achievementId);
        writePOD(os, e.targetId);
        writePOD(os, e.requiredCount);
        writePOD(os, e.timeLimitMs);
        writePOD(os, e.criteriaType);
        writePOD(os, e.progressOrder);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeAchievementCriteria WoweeAchievementCriteriaLoader::load(
    const std::string& basePath) {
    WoweeAchievementCriteria out;
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
        if (!readPOD(is, e.criteriaId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.achievementId) ||
            !readPOD(is, e.targetId) ||
            !readPOD(is, e.requiredCount) ||
            !readPOD(is, e.timeLimitMs) ||
            !readPOD(is, e.criteriaType) ||
            !readPOD(is, e.progressOrder) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeAchievementCriteriaLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeAchievementCriteria WoweeAchievementCriteriaLoader::makeKill(
    const std::string& catalogName) {
    using A = WoweeAchievementCriteria;
    WoweeAchievementCriteria c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t ach,
                    uint32_t creatureId, uint32_t count,
                    uint8_t order, const char* desc) {
        A::Entry e;
        e.criteriaId = id; e.name = name; e.description = desc;
        e.achievementId = ach;
        e.targetId = creatureId;
        e.requiredCount = count;
        e.criteriaType = A::KillCreature;
        e.progressOrder = order;
        e.iconColorRGBA = packRgba(220, 80, 80);   // kill red
        c.entries.push_back(e);
    };
    // Five kill criteria all under one composite
    // achievement (achievementId 5000) — slay diverse
    // enemies for "Kill 'Em All".
    add(1, "DefiasKills",     5000,  448, 50, 0,
        "Slay 50 Defias bandits in Westfall.");
    add(2, "MurlocKills",     5000,  346, 25, 1,
        "Slay 25 murlocs anywhere.");
    add(3, "NagaKills",       5000, 4356, 100, 2,
        "Slay 100 naga in Azshara or Maraudon.");
    add(4, "DragonKills",     5000, 6109,  1, 3,
        "Slay 1 dragon (Onyxia / Nefarian / Ysondre / etc).");
    add(5, "RareEliteKills",  5000, 7846,  1, 4,
        "Slay 1 rare elite mob (silver dragon nameplate).");
    return c;
}

WoweeAchievementCriteria WoweeAchievementCriteriaLoader::makeQuest(
    const std::string& catalogName) {
    using A = WoweeAchievementCriteria;
    WoweeAchievementCriteria c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t ach,
                    uint32_t questId, uint8_t order,
                    const char* desc) {
        A::Entry e;
        e.criteriaId = id; e.name = name; e.description = desc;
        e.achievementId = ach;
        e.targetId = questId;
        e.requiredCount = 1;
        e.criteriaType = A::CompleteQuest;
        e.progressOrder = order;
        e.iconColorRGBA = packRgba(220, 200, 100);   // quest gold
        c.entries.push_back(e);
    };
    // 4-step quest progression under achievement 5100.
    add(100, "FinishTutorial",    5100,    1, 0,
        "Complete the starting-area tutorial chain.");
    add(101, "FinishStartingZone", 5100,   24, 1,
        "Complete every quest in the level-1 starting zone.");
    add(102, "FinishDaily",        5100, 12013, 2,
        "Complete a daily quest.");
    add(103, "FinishEscort",       5100,   123, 3,
        "Complete an escort quest of any kind.");
    return c;
}

WoweeAchievementCriteria WoweeAchievementCriteriaLoader::makeMixed(
    const std::string& catalogName) {
    using A = WoweeAchievementCriteria;
    WoweeAchievementCriteria c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t ach,
                    uint8_t kind, uint32_t target, uint32_t count,
                    uint8_t order,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        A::Entry e;
        e.criteriaId = id; e.name = name; e.description = desc;
        e.achievementId = ach;
        e.targetId = target;
        e.requiredCount = count;
        e.criteriaType = kind;
        e.progressOrder = order;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    // Five different criteria types under achievement 5200
    // — demonstrate the full CriteriaType variety.
    add(200, "ReachLevel80",   5200, A::ReachLevel,    0,    80, 0,
        100, 240, 100, "Reach level 80.");
    add(201, "EarnGold10k",    5200, A::EarnGold,      0, 100000000, 1,
        220, 200, 100, "Accumulate 10000 gold (100M copper).");
    add(202, "GainHonor5k",    5200, A::GainHonor,     0,   5000, 2,
        220,  80,  80, "Earn 5000 honor points.");
    add(203, "PvPKill100",     5200, A::PvPKill,       0,    100, 3,
        180, 100, 240, "Kill 100 enemy players in PvP.");
    add(204, "ExploreStormwind",5200,A::ExploreZone, 1519,     1, 4,
        100, 140, 240, "Discover every subzone in Stormwind.");
    return c;
}

} // namespace pipeline
} // namespace wowee
