#include "pipeline/wowee_battleground_rewards.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'R', 'D'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wbrd") {
        base += ".wbrd";
    }
    return base;
}

} // namespace

const WoweeBattlegroundRewards::Entry*
WoweeBattlegroundRewards::findById(uint32_t rewardId) const {
    for (const auto& e : entries)
        if (e.rewardId == rewardId) return &e;
    return nullptr;
}

const WoweeBattlegroundRewards::Entry*
WoweeBattlegroundRewards::find(uint16_t bgId,
                                  uint8_t bracketIndex) const {
    for (const auto& e : entries) {
        if (e.battlegroundId == bgId &&
            e.bracketIndex == bracketIndex)
            return &e;
    }
    return nullptr;
}

std::vector<const WoweeBattlegroundRewards::Entry*>
WoweeBattlegroundRewards::findByBg(uint16_t bgId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.battlegroundId == bgId) out.push_back(&e);
    return out;
}

bool WoweeBattlegroundRewardsLoader::save(
    const WoweeBattlegroundRewards& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.rewardId);
        writePOD(os, e.battlegroundId);
        writePOD(os, e.bracketIndex);
        writePOD(os, e.minPlayersToStart);
        writePOD(os, e.winHonor);
        writePOD(os, e.lossHonor);
        writePOD(os, e.markItemId);
        writePOD(os, e.winMarks);
        writePOD(os, e.lossMarks);
        writePOD(os, e.bonusItemId);
        writePOD(os, e.bonusItemCount);
        writePOD(os, e.pad0);
    }
    return os.good();
}

WoweeBattlegroundRewards WoweeBattlegroundRewardsLoader::load(
    const std::string& basePath) {
    WoweeBattlegroundRewards out;
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
        if (!readPOD(is, e.rewardId) ||
            !readPOD(is, e.battlegroundId) ||
            !readPOD(is, e.bracketIndex) ||
            !readPOD(is, e.minPlayersToStart) ||
            !readPOD(is, e.winHonor) ||
            !readPOD(is, e.lossHonor) ||
            !readPOD(is, e.markItemId) ||
            !readPOD(is, e.winMarks) ||
            !readPOD(is, e.lossMarks) ||
            !readPOD(is, e.bonusItemId) ||
            !readPOD(is, e.bonusItemCount) ||
            !readPOD(is, e.pad0)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeBattlegroundRewardsLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

WoweeBattlegroundRewards::Entry makeStage(
    uint32_t rewardId, uint16_t bgId,
    uint8_t bracketIndex, uint8_t minPlayers,
    uint32_t winHonor, uint32_t lossHonor,
    uint32_t markItemId,
    uint16_t winMarks, uint16_t lossMarks,
    uint32_t bonusItemId = 0,
    uint16_t bonusItemCount = 0) {
    WoweeBattlegroundRewards::Entry e;
    e.rewardId = rewardId;
    e.battlegroundId = bgId;
    e.bracketIndex = bracketIndex;
    e.minPlayersToStart = minPlayers;
    e.winHonor = winHonor;
    e.lossHonor = lossHonor;
    e.markItemId = markItemId;
    e.winMarks = winMarks;
    e.lossMarks = lossMarks;
    e.bonusItemId = bonusItemId;
    e.bonusItemCount = bonusItemCount;
    return e;
}

} // namespace

WoweeBattlegroundRewards WoweeBattlegroundRewardsLoader::makeAlteracValley(
    const std::string& catalogName) {
    WoweeBattlegroundRewards c;
    c.name = catalogName;
    // AV bgId=1, requires 20/side. Brackets 5
    // (51-60) and 6 (61-69 — endgame). Mark of AV =
    // itemId 17502. Win 3 marks / loss 1 mark
    // baseline.
    c.entries.push_back(makeStage(
        1, 1, 5, 20,
        750 /* winHonor */, 250 /* lossHonor */,
        17502 /* Mark of Honor: AV */,
        3, 1));
    c.entries.push_back(makeStage(
        2, 1, 6, 20,
        1000, 350,
        17502,
        3, 1));
    return c;
}

WoweeBattlegroundRewards WoweeBattlegroundRewardsLoader::makeWarsong(
    const std::string& catalogName) {
    WoweeBattlegroundRewards c;
    c.name = catalogName;
    // WSG bgId=2, requires 10/side. All 6 brackets.
    // Mark of WSG = itemId 20558.
    c.entries.push_back(makeStage(
        10, 2, 1, 10, 50, 25, 20558, 1, 0));
    c.entries.push_back(makeStage(
        11, 2, 2, 10, 100, 50, 20558, 1, 1));
    c.entries.push_back(makeStage(
        12, 2, 3, 10, 200, 100, 20558, 2, 1));
    c.entries.push_back(makeStage(
        13, 2, 4, 10, 350, 175, 20558, 2, 1));
    c.entries.push_back(makeStage(
        14, 2, 5, 10, 500, 250, 20558, 3, 1));
    c.entries.push_back(makeStage(
        15, 2, 6, 10, 750, 375, 20558, 3, 1));
    return c;
}

WoweeBattlegroundRewards WoweeBattlegroundRewardsLoader::makeArathiBasin(
    const std::string& catalogName) {
    WoweeBattlegroundRewards c;
    c.name = catalogName;
    // AB bgId=3, requires 15/side. Brackets 2..6.
    // Mark of AB = itemId 20559. Weekly bonus quest
    // turns in 3 marks for an extra Token of the
    // Triumvirate (placeholder itemId 20560).
    c.entries.push_back(makeStage(
        20, 3, 2, 15, 80, 40, 20559, 1, 0,
        20560 /* weekly bonus */, 1));
    c.entries.push_back(makeStage(
        21, 3, 3, 15, 175, 90, 20559, 2, 1,
        20560, 1));
    c.entries.push_back(makeStage(
        22, 3, 4, 15, 300, 150, 20559, 2, 1,
        20560, 1));
    c.entries.push_back(makeStage(
        23, 3, 5, 15, 450, 225, 20559, 3, 1,
        20560, 1));
    c.entries.push_back(makeStage(
        24, 3, 6, 15, 700, 350, 20559, 3, 1,
        20560, 1));
    return c;
}

} // namespace pipeline
} // namespace wowee
