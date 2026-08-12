#include "pipeline/wowee_quest_graph.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'Q', 'G', 'R'};
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

void writeU32Vec(std::ofstream& os,
                  const std::vector<uint32_t>& v) {
    uint32_t n = static_cast<uint32_t>(v.size());
    writePOD(os, n);
    if (n > 0) {
        os.write(reinterpret_cast<const char*>(v.data()),
                 static_cast<std::streamsize>(n * sizeof(uint32_t)));
    }
}

bool readU32Vec(std::ifstream& is, std::vector<uint32_t>& v) {
    uint32_t n = 0;
    if (!readPOD(is, n)) return false;
    if (n > 4096) return false;
    v.resize(n);
    if (n > 0) {
        is.read(reinterpret_cast<char*>(v.data()),
                static_cast<std::streamsize>(n * sizeof(uint32_t)));
        if (is.gcount() !=
            static_cast<std::streamsize>(n * sizeof(uint32_t))) {
            v.clear();
            return false;
        }
    }
    return true;
}

std::string normalizePath(std::string base) {
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wqgr") {
        base += ".wqgr";
    }
    return base;
}

} // namespace

const WoweeQuestGraph::Entry*
WoweeQuestGraph::findById(uint32_t questId) const {
    for (const auto& e : entries)
        if (e.questId == questId) return &e;
    return nullptr;
}

std::vector<const WoweeQuestGraph::Entry*>
WoweeQuestGraph::findUnlocksFrom(uint32_t questId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        for (uint32_t p : e.prevQuestIds) {
            if (p == questId) { out.push_back(&e); break; }
        }
    }
    return out;
}

std::vector<const WoweeQuestGraph::Entry*>
WoweeQuestGraph::findByZone(uint32_t zoneId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.zoneId == zoneId) out.push_back(&e);
    return out;
}

bool WoweeQuestGraphLoader::save(const WoweeQuestGraph& cat,
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
        writeStr(os, e.name);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.questType);
        writePOD(os, e.factionAccess);
        writePOD(os, e.classRestriction);
        writePOD(os, e.raceRestriction);
        writePOD(os, e.zoneId);
        writePOD(os, e.chainHeadHint);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writeU32Vec(os, e.prevQuestIds);
        writeU32Vec(os, e.followupQuestIds);
    }
    return os.good();
}

WoweeQuestGraph WoweeQuestGraphLoader::load(
    const std::string& basePath) {
    WoweeQuestGraph out;
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
        if (!readPOD(is, e.questId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.questType) ||
            !readPOD(is, e.factionAccess) ||
            !readPOD(is, e.classRestriction) ||
            !readPOD(is, e.raceRestriction) ||
            !readPOD(is, e.zoneId) ||
            !readPOD(is, e.chainHeadHint) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1)) {
            out.entries.clear(); return out;
        }
        if (!readU32Vec(is, e.prevQuestIds) ||
            !readU32Vec(is, e.followupQuestIds)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeQuestGraphLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

WoweeQuestGraph::Entry makeQuest(uint32_t qid, const char* name,
                                    uint8_t minL, uint8_t maxL,
                                    uint8_t qtype, uint8_t faction,
                                    uint32_t zoneId,
                                    uint8_t chainHead,
                                    std::vector<uint32_t> prev,
                                    std::vector<uint32_t> followups) {
    WoweeQuestGraph::Entry e;
    e.questId = qid; e.name = name;
    e.minLevel = minL; e.maxLevel = maxL;
    e.questType = qtype;
    e.factionAccess = faction;
    e.zoneId = zoneId;
    e.chainHeadHint = chainHead;
    e.prevQuestIds = std::move(prev);
    e.followupQuestIds = std::move(followups);
    return e;
}

} // namespace

WoweeQuestGraph WoweeQuestGraphLoader::makeStarterChain(
    const std::string& catalogName) {
    using G = WoweeQuestGraph;
    WoweeQuestGraph c;
    c.name = catalogName;
    // Northshire Abbey starter chain (zoneId=12):
    // Q1 = chain head (no prereqs, hints next).
    // Q2..Q5 form linear progression.
    c.entries.push_back(makeQuest(
        100, "A Threat Within", 1, 5,
        G::Normal, G::Alliance, 12, 1,
        {}, {101}));
    c.entries.push_back(makeQuest(
        101, "Wolves Across the Border", 2, 5,
        G::Normal, G::Alliance, 12, 0,
        {100}, {102}));
    c.entries.push_back(makeQuest(
        102, "Kobold Camp Cleanup", 3, 6,
        G::Normal, G::Alliance, 12, 0,
        {101}, {103}));
    c.entries.push_back(makeQuest(
        103, "Investigate Echo Ridge", 4, 7,
        G::Normal, G::Alliance, 12, 0,
        {102}, {104}));
    c.entries.push_back(makeQuest(
        104, "Report to Goldshire", 5, 8,
        G::Normal, G::Alliance, 12, 0,
        {103}, {}));   // last in chain, no
                        //  followups
    return c;
}

WoweeQuestGraph WoweeQuestGraphLoader::makeBranchedChain(
    const std::string& catalogName) {
    using G = WoweeQuestGraph;
    WoweeQuestGraph c;
    c.name = catalogName;
    // Demonstrates DAG semantics — Q1 unlocks both
    // Q2a and Q2b; both prereq Q3:
    //   Q1 -> Q2a -> Q3
    //   Q1 -> Q2b -> Q3
    // Q3 has TWO prereqs (must complete BOTH branches).
    c.entries.push_back(makeQuest(
        200, "Discover the Crystal", 10, 14,
        G::Group, G::Both, 47, 1,
        {}, {201, 202}));
    c.entries.push_back(makeQuest(
        201, "The Frost Branch", 11, 15,
        G::Normal, G::Both, 47, 0,
        {200}, {203}));
    c.entries.push_back(makeQuest(
        202, "The Fire Branch", 11, 15,
        G::Normal, G::Both, 47, 0,
        {200}, {203}));
    c.entries.push_back(makeQuest(
        203, "Forge the Amulet", 12, 16,
        G::Group, G::Both, 47, 0,
        {201, 202}, {}));   // requires BOTH 201
                              //  AND 202
    return c;
}

WoweeQuestGraph WoweeQuestGraphLoader::makeDailies(
    const std::string& catalogName) {
    using G = WoweeQuestGraph;
    WoweeQuestGraph c;
    c.name = catalogName;
    // Standalone daily quests — no prereqs, no
    // followups. chainHeadHint=1 since each is its
    // own root.
    c.entries.push_back(makeQuest(
        300, "Daily: Mana Cell Disposal", 50, 0,
        G::Daily, G::Both, 100, 1,
        {}, {}));
    c.entries.push_back(makeQuest(
        301, "Daily: Felblood Sample", 50, 0,
        G::Daily, G::Both, 100, 1,
        {}, {}));
    c.entries.push_back(makeQuest(
        302, "Daily: Crystal Restoration", 50, 0,
        G::Daily, G::Both, 100, 1,
        {}, {}));
    return c;
}

} // namespace pipeline
} // namespace wowee
