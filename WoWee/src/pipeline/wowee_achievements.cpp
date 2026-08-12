#include "pipeline/wowee_achievements.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'C', 'H'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wach") {
        base += ".wach";
    }
    return base;
}

} // namespace

const WoweeAchievement::Entry* WoweeAchievement::findById(uint32_t achievementId) const {
    for (const auto& e : entries) {
        if (e.achievementId == achievementId) return &e;
    }
    return nullptr;
}

const char* WoweeAchievement::criteriaKindName(uint8_t k) {
    switch (k) {
        case KillCreature:        return "kill";
        case CompleteQuest:       return "quest";
        case LootItem:            return "loot";
        case ReachLevel:          return "level";
        case EarnReputation:      return "rep";
        case CastSpell:           return "cast";
        case ReachSkillLevel:     return "skill";
        case VisitArea:           return "visit";
        case CompleteAchievement: return "meta";
        default:                  return "unknown";
    }
}

const char* WoweeAchievement::factionName(uint8_t f) {
    switch (f) {
        case FactionBoth:     return "both";
        case FactionAlliance: return "alliance";
        case FactionHorde:    return "horde";
        default:              return "unknown";
    }
}

bool WoweeAchievementLoader::save(const WoweeAchievement& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.achievementId);
        writePOD(os, e.categoryId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writeStr(os, e.titleReward);
        writePOD(os, e.points);
        writePOD(os, e.minLevel);
        writePOD(os, e.faction);
        uint8_t critCount = static_cast<uint8_t>(
            e.criteria.size() > 255 ? 255 : e.criteria.size());
        writePOD(os, critCount);
        writePOD(os, e.flags);
        for (uint8_t k = 0; k < critCount; ++k) {
            const auto& cr = e.criteria[k];
            writePOD(os, cr.criteriaId);
            writePOD(os, cr.kind);
            uint8_t pad[3] = {0, 0, 0};
            os.write(reinterpret_cast<const char*>(pad), 3);
            writePOD(os, cr.targetId);
            writePOD(os, cr.quantity);
            writeStr(os, cr.description);
        }
    }
    return os.good();
}

WoweeAchievement WoweeAchievementLoader::load(const std::string& basePath) {
    WoweeAchievement out;
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
        if (!readPOD(is, e.achievementId) ||
            !readPOD(is, e.categoryId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath) || !readStr(is, e.titleReward)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.points) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.faction)) {
            out.entries.clear(); return out;
        }
        uint8_t critCount = 0;
        if (!readPOD(is, critCount)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.flags)) { out.entries.clear(); return out; }
        e.criteria.resize(critCount);
        for (uint8_t k = 0; k < critCount; ++k) {
            auto& cr = e.criteria[k];
            if (!readPOD(is, cr.criteriaId) ||
                !readPOD(is, cr.kind)) {
                out.entries.clear(); return out;
            }
            uint8_t pad[3];
            is.read(reinterpret_cast<char*>(pad), 3);
            if (is.gcount() != 3) { out.entries.clear(); return out; }
            if (!readPOD(is, cr.targetId) ||
                !readPOD(is, cr.quantity) ||
                !readStr(is, cr.description)) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeAchievementLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeAchievement WoweeAchievementLoader::makeStarter(const std::string& catalogName) {
    WoweeAchievement c;
    c.name = catalogName;
    {
        WoweeAchievement::Entry e;
        e.achievementId = 1;
        e.name = "First Blood";
        e.description = "Kill your first hostile creature.";
        e.points = 5;
        e.criteria.push_back({1, WoweeAchievement::KillCreature,
                               1000, 1, "Kill any hostile creature"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 2;
        e.name = "Helping Hand";
        e.description = "Complete your first quest.";
        e.points = 5;
        e.criteria.push_back({2, WoweeAchievement::CompleteQuest,
                               1, 1, "Complete the Bandit Trouble quest"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 3;
        e.name = "Coming of Age";
        e.description = "Reach character level 10.";
        e.points = 10;
        e.criteria.push_back({3, WoweeAchievement::ReachLevel,
                               0, 10, "Reach level 10"});
        c.entries.push_back(e);
    }
    return c;
}

WoweeAchievement WoweeAchievementLoader::makeBandit(const std::string& catalogName) {
    WoweeAchievement c;
    c.name = catalogName;
    {
        WoweeAchievement::Entry e;
        e.achievementId = 100;
        e.name = "Bandit Hunter";
        e.description = "Slay 50 Defias Bandits.";
        e.points = 10;
        // creatureId 1000 matches WCRT.makeBandit + WSPN.makeCamp
        // + WLOT.makeBandit + WQT.makeStarter target.
        e.criteria.push_back({100, WoweeAchievement::KillCreature,
                               1000, 50, "Defias Bandits slain"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 101;
        e.name = "Strongbox Cracked";
        e.description = "Loot the Bandit Strongbox.";
        e.points = 5;
        // objectId 2000 matches WGOT.makeDungeon's bandit chest.
        e.criteria.push_back({101, WoweeAchievement::LootItem,
                               2000, 1, "Loot the Bandit Strongbox"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 102;
        e.name = "Brotherhood Down";
        e.description = "Complete the Bandit Trouble quest line.";
        e.points = 15;
        e.criteria.push_back({102, WoweeAchievement::CompleteQuest,
                               1, 1, "Quest 1: Bandit Trouble"});
        c.entries.push_back(e);
    }
    return c;
}

WoweeAchievement WoweeAchievementLoader::makeMeta(const std::string& catalogName) {
    WoweeAchievement c;
    c.name = catalogName;
    {
        WoweeAchievement::Entry e;
        e.achievementId = 200; e.name = "Mining Apprentice";
        e.description = "Reach 100 in Mining.";
        e.points = 10;
        // skillId 186 matches WSKL.makeProfessions + WGOT.makeGather.
        e.criteria.push_back({200, WoweeAchievement::ReachSkillLevel,
                               186, 100, "Mining at rank 100"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 201; e.name = "Lockbreaker";
        e.description = "Reach 100 in Lockpicking.";
        e.points = 10;
        // skillId 633 matches WSKL.makeStarter + WLCK.makeDungeon.
        e.criteria.push_back({201, WoweeAchievement::ReachSkillLevel,
                               633, 100, "Lockpicking at rank 100"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 202; e.name = "Frostbinder";
        e.description = "Cast Frostbolt 100 times.";
        e.points = 5;
        // spellId 116 matches WSPL.makeMage's Frostbolt.
        e.criteria.push_back({202, WoweeAchievement::CastSpell,
                               116, 100, "Frostbolt cast count"});
        c.entries.push_back(e);
    }
    {
        WoweeAchievement::Entry e;
        e.achievementId = 250; e.name = "Jack of All Trades";
        e.description = "Complete all 3 sub-achievements.";
        e.points = 25;
        e.titleReward = "the Versatile";
        e.flags = WoweeAchievement::HiddenUntilEarned;
        e.criteria.push_back({250, WoweeAchievement::CompleteAchievement,
                               200, 1, "Mining Apprentice"});
        e.criteria.push_back({251, WoweeAchievement::CompleteAchievement,
                               201, 1, "Lockbreaker"});
        e.criteria.push_back({252, WoweeAchievement::CompleteAchievement,
                               202, 1, "Frostbinder"});
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
