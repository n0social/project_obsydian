#include "pipeline/wowee_quests.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'Q', 'T', 'M'};
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
    if (base.size() < 4 || base.substr(base.size() - 4) != ".wqt") {
        base += ".wqt";
    }
    return base;
}

} // namespace

const WoweeQuest::Entry* WoweeQuest::findById(uint32_t questId) const {
    for (const auto& e : entries) {
        if (e.questId == questId) return &e;
    }
    return nullptr;
}

const char* WoweeQuest::objectiveKindName(uint8_t k) {
    switch (k) {
        case KillCreature:   return "kill";
        case CollectItem:    return "collect";
        case InteractObject: return "interact";
        case VisitArea:      return "visit";
        case EscortNpc:      return "escort";
        case SpellCast:      return "cast";
        default:             return "unknown";
    }
}

bool WoweeQuestLoader::save(const WoweeQuest& cat,
                            const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.questId);
        writeStr(os, e.title);
        writeStr(os, e.objective);
        writeStr(os, e.description);
        writePOD(os, e.minLevel);
        writePOD(os, e.questLevel);
        writePOD(os, e.maxLevel);
        uint16_t pad16 = 0;
        writePOD(os, pad16);
        writePOD(os, e.requiredClassMask);
        writePOD(os, e.requiredRaceMask);
        writePOD(os, e.prevQuestId);
        writePOD(os, e.nextQuestId);
        writePOD(os, e.giverCreatureId);
        writePOD(os, e.turninCreatureId);
        uint8_t objCount = static_cast<uint8_t>(
            e.objectives.size() > 255 ? 255 : e.objectives.size());
        writePOD(os, objCount);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        for (uint8_t k = 0; k < objCount; ++k) {
            const auto& o = e.objectives[k];
            writePOD(os, o.kind);
            os.write(reinterpret_cast<const char*>(pad3), 3);
            writePOD(os, o.targetId);
            writePOD(os, o.quantity);
            writePOD(os, pad16);
        }
        writePOD(os, e.xpReward);
        writePOD(os, e.moneyCopperReward);
        uint8_t rewCount = static_cast<uint8_t>(
            e.rewardItems.size() > 255 ? 255 : e.rewardItems.size());
        writePOD(os, rewCount);
        os.write(reinterpret_cast<const char*>(pad3), 3);
        for (uint8_t k = 0; k < rewCount; ++k) {
            const auto& r = e.rewardItems[k];
            writePOD(os, r.itemId);
            writePOD(os, r.qty);
            writePOD(os, r.pickFlags);
            writePOD(os, pad16);
        }
        writePOD(os, e.flags);
    }
    return os.good();
}

WoweeQuest WoweeQuestLoader::load(const std::string& basePath) {
    WoweeQuest out;
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
        if (!readPOD(is, e.questId)) { out.entries.clear(); return out; }
        if (!readStr(is, e.title) || !readStr(is, e.objective) ||
            !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.questLevel) ||
            !readPOD(is, e.maxLevel)) {
            out.entries.clear(); return out;
        }
        uint16_t pad16 = 0;
        if (!readPOD(is, pad16)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.requiredClassMask) ||
            !readPOD(is, e.requiredRaceMask) ||
            !readPOD(is, e.prevQuestId) ||
            !readPOD(is, e.nextQuestId) ||
            !readPOD(is, e.giverCreatureId) ||
            !readPOD(is, e.turninCreatureId)) {
            out.entries.clear(); return out;
        }
        uint8_t objCount = 0;
        if (!readPOD(is, objCount)) { out.entries.clear(); return out; }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        e.objectives.resize(objCount);
        for (uint8_t k = 0; k < objCount; ++k) {
            auto& o = e.objectives[k];
            if (!readPOD(is, o.kind)) {
                out.entries.clear(); return out;
            }
            is.read(reinterpret_cast<char*>(pad3), 3);
            if (is.gcount() != 3) { out.entries.clear(); return out; }
            if (!readPOD(is, o.targetId) || !readPOD(is, o.quantity)) {
                out.entries.clear(); return out;
            }
            uint16_t opad = 0;
            if (!readPOD(is, opad)) {
                out.entries.clear(); return out;
            }
        }
        if (!readPOD(is, e.xpReward) ||
            !readPOD(is, e.moneyCopperReward)) {
            out.entries.clear(); return out;
        }
        uint8_t rewCount = 0;
        if (!readPOD(is, rewCount)) { out.entries.clear(); return out; }
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        e.rewardItems.resize(rewCount);
        for (uint8_t k = 0; k < rewCount; ++k) {
            auto& r = e.rewardItems[k];
            if (!readPOD(is, r.itemId) ||
                !readPOD(is, r.qty) ||
                !readPOD(is, r.pickFlags)) {
                out.entries.clear(); return out;
            }
            uint16_t rpad = 0;
            if (!readPOD(is, rpad)) {
                out.entries.clear(); return out;
            }
        }
        if (!readPOD(is, e.flags)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeQuestLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeQuest WoweeQuestLoader::makeStarter(const std::string& catalogName) {
    WoweeQuest c;
    c.name = catalogName;
    {
        WoweeQuest::Entry e;
        e.questId = 1;
        e.title = "Bandit Trouble";
        e.objective = "Kill 10 Defias Bandits.";
        e.description =
            "The Defias have been raiding our farms. Make them pay.";
        e.minLevel = 5;
        e.questLevel = 6;
        e.giverCreatureId = 4001;          // village innkeeper from WCRT
        e.turninCreatureId = 4001;
        e.objectives.push_back({WoweeQuest::KillCreature, 1000, 10});
        e.xpReward = 500;
        e.moneyCopperReward = 250;          // 2s 50c
        e.rewardItems.push_back({3, 2, WoweeQuest::AutoGiven});  // 2 healing potions
        c.entries.push_back(e);
    }
    return c;
}

WoweeQuest WoweeQuestLoader::makeChain(const std::string& catalogName) {
    WoweeQuest c;
    c.name = catalogName;
    {
        WoweeQuest::Entry e;
        e.questId = 100;
        e.title = "Investigate the Camp";
        e.objective = "Visit the bandit camp clearing.";
        e.description =
            "Scout reports place a bandit camp east of here. "
            "See for yourself.";
        e.minLevel = 5; e.questLevel = 5;
        e.giverCreatureId = 4001;
        e.turninCreatureId = 4001;
        e.nextQuestId = 101;
        e.objectives.push_back({WoweeQuest::VisitArea, 9001, 1});
        e.xpReward = 200;
        e.moneyCopperReward = 100;
        c.entries.push_back(e);
    }
    {
        WoweeQuest::Entry e;
        e.questId = 101;
        e.title = "Recover the Stolen Goods";
        e.objective = "Recover 5 Stolen Letters from Defias Bandits.";
        e.description =
            "The bandits stole letters from the inn. "
            "Recover them.";
        e.minLevel = 6; e.questLevel = 7;
        e.giverCreatureId = 4001;
        e.turninCreatureId = 4001;
        e.prevQuestId = 100;
        e.nextQuestId = 102;
        e.objectives.push_back({WoweeQuest::CollectItem, 4, 5});
        e.xpReward = 600;
        e.moneyCopperReward = 350;
        e.rewardItems.push_back({2, 1, WoweeQuest::AutoGiven});  // linen vest
        c.entries.push_back(e);
    }
    {
        WoweeQuest::Entry e;
        e.questId = 102;
        e.title = "Report Back";
        e.objective = "Return to the Innkeeper with the recovered letters.";
        e.description =
            "Bring the recovered letters to the Innkeeper.";
        e.minLevel = 6; e.questLevel = 7;
        e.giverCreatureId = 4001;
        e.turninCreatureId = 4001;
        e.prevQuestId = 101;
        // No nextQuestId — chain end.
        // No objectives — quest completes on dialogue alone.
        e.flags = WoweeQuest::AutoComplete;
        e.xpReward = 200;
        e.moneyCopperReward = 500;          // 5s
        // Player choice: 1 of 2 weapons.
        e.rewardItems.push_back({1001, 1, WoweeQuest::PlayerChoice}); // sword
        e.rewardItems.push_back({1002, 1, WoweeQuest::PlayerChoice}); // blade
        c.entries.push_back(e);
    }
    return c;
}

WoweeQuest WoweeQuestLoader::makeDaily(const std::string& catalogName) {
    WoweeQuest c;
    c.name = catalogName;
    {
        WoweeQuest::Entry e;
        e.questId = 200;
        e.title = "Daily: Wolf Cull";
        e.objective = "Kill 8 wolves outside the village gate.";
        e.description = "The wolves are getting bolder. Thin the pack.";
        e.minLevel = 5; e.questLevel = 5;
        e.giverCreatureId = 4002;        // smith from WCRT village
        e.turninCreatureId = 4002;
        e.objectives.push_back({WoweeQuest::KillCreature, 2001, 8});
        e.xpReward = 250;
        e.moneyCopperReward = 1000;       // 10s = good daily payout
        e.flags = WoweeQuest::Daily |
                   WoweeQuest::Repeatable |
                   WoweeQuest::AutoAccept;
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
