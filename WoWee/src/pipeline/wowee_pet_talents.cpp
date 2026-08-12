#include "pipeline/wowee_pet_talents.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'T', 'T'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wptt") {
        base += ".wptt";
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

const WoweePetTalents::Entry*
WoweePetTalents::findById(uint32_t talentId) const {
    for (const auto& e : entries)
        if (e.talentId == talentId) return &e;
    return nullptr;
}

std::vector<const WoweePetTalents::Entry*>
WoweePetTalents::findByTree(uint8_t treeKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.treeKind == treeKind) out.push_back(&e);
    return out;
}

const WoweePetTalents::Entry*
WoweePetTalents::findAtCell(uint8_t treeKind, uint8_t tier,
                              uint8_t column) const {
    for (const auto& e : entries) {
        if (e.treeKind == treeKind && e.tier == tier &&
            e.column == column) return &e;
    }
    return nullptr;
}

bool WoweePetTalentsLoader::save(const WoweePetTalents& cat,
                                   const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.talentId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.treeKind);
        writePOD(os, e.tier);
        writePOD(os, e.column);
        writePOD(os, e.maxRank);
        writePOD(os, e.prerequisiteTalentId);
        writePOD(os, e.requiredLoyalty);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.iconColorRGBA);
        uint32_t spellCount = static_cast<uint32_t>(
            e.spellIdsByRank.size());
        writePOD(os, spellCount);
        for (uint32_t s : e.spellIdsByRank) writePOD(os, s);
    }
    return os.good();
}

WoweePetTalents WoweePetTalentsLoader::load(
    const std::string& basePath) {
    WoweePetTalents out;
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
        if (!readPOD(is, e.talentId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.treeKind) ||
            !readPOD(is, e.tier) ||
            !readPOD(is, e.column) ||
            !readPOD(is, e.maxRank) ||
            !readPOD(is, e.prerequisiteTalentId) ||
            !readPOD(is, e.requiredLoyalty) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
        uint32_t spellCount = 0;
        if (!readPOD(is, spellCount)) {
            out.entries.clear(); return out;
        }
        if (spellCount > 16) {
            out.entries.clear(); return out;
        }
        e.spellIdsByRank.resize(spellCount);
        for (uint32_t k = 0; k < spellCount; ++k) {
            if (!readPOD(is, e.spellIdsByRank[k])) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweePetTalentsLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweePetTalents WoweePetTalentsLoader::makeFerocity(
    const std::string& catalogName) {
    using P = WoweePetTalents;
    WoweePetTalents c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t tier, uint8_t column,
                    uint8_t maxRank,
                    std::vector<uint32_t> spells,
                    uint32_t prereq, const char* desc) {
        P::Entry e;
        e.talentId = id; e.name = name; e.description = desc;
        e.treeKind = P::Ferocity;
        e.tier = tier; e.column = column;
        e.maxRank = maxRank;
        e.spellIdsByRank = std::move(spells);
        e.prerequisiteTalentId = prereq;
        e.iconColorRGBA = packRgba(220, 60, 60);   // ferocity red
        c.entries.push_back(e);
    };
    add(1, "CobraReflexes", 0, 0, 2,
        { 61682, 61683 }, 0,
        "Tier 0 col 0 — Increases pet attack speed by "
        "15%/30%, reduces damage by 15%. Two ranks. No "
        "prerequisite (root of Ferocity tree).");
    add(2, "SerpentSwiftness", 0, 1, 5,
        { 16093, 16094, 16095, 16096, 16097 }, 0,
        "Tier 0 col 1 — Increases pet attack speed by "
        "1%/2%/3%/4%/5%. Five ranks. Root talent.");
    add(3, "SpikedCollar", 1, 0, 3,
        { 19582, 19583, 19584 }, 1,
        "Tier 1 col 0 — Increases pet damage by 1%/2%/3%. "
        "Requires CobraReflexes (talentId=1) as prereq.");
    add(4, "BoarsSpeed", 2, 1, 1,
        { 19596 }, 2,
        "Tier 2 col 1 — Increases pet movement speed by "
        "30%. Requires SerpentSwiftness rank 2+ (modeled "
        "by talentId=2 prereq).");
    add(5, "SpidersBite", 2, 2, 3,
        { 19589, 19591, 19592 }, 3,
        "Tier 2 col 2 — Increases pet melee crit chance "
        "by 3%/6%/9%. Requires SpikedCollar (talentId=3) "
        "as prereq.");
    add(6, "Rabid", 3, 1, 1,
        { 53401 }, 4,
        "Tier 3 col 1 — Active ability. Pet enrages, "
        "increasing damage by 5% per stack (max 5). Top-"
        "tier Ferocity talent. Requires BoarsSpeed "
        "(talentId=4).");
    return c;
}

WoweePetTalents WoweePetTalentsLoader::makeCunning(
    const std::string& catalogName) {
    using P = WoweePetTalents;
    WoweePetTalents c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t tier, uint8_t column,
                    uint8_t maxRank,
                    std::vector<uint32_t> spells,
                    uint32_t prereq, const char* desc) {
        P::Entry e;
        e.talentId = id; e.name = name; e.description = desc;
        e.treeKind = P::Cunning;
        e.tier = tier; e.column = column;
        e.maxRank = maxRank;
        e.spellIdsByRank = std::move(spells);
        e.prerequisiteTalentId = prereq;
        e.iconColorRGBA = packRgba(140, 200, 255);   // cunning blue
        c.entries.push_back(e);
    };
    add(100, "Dash", 0, 0, 3,
        { 61684, 61685, 61686 }, 0,
        "Tier 0 col 0 — Pet sprint. Increases run speed "
        "by 30%/40%/50% for 16 sec. 32-sec cooldown. Root "
        "talent.");
    add(101, "OwlsFocus", 1, 1, 5,
        { 53513, 53514, 53515, 53516, 53517 }, 0,
        "Tier 1 col 1 — Pet special-attack damage chance "
        "increased by 4%/8%/12%/16%/20%. Root talent.");
    add(102, "RoarOfRecovery", 2, 0, 1,
        { 53517 }, 100,
        "Tier 2 col 0 — Active. Restores 30% of hunter's "
        "maximum mana over 9 sec. 3-min cooldown. "
        "Requires Dash (talentId=100).");
    add(103, "Cornered", 2, 2, 2,
        { 53497, 53499 }, 101,
        "Tier 2 col 2 — Pet damage increased by 5%/10% "
        "and crit by 5%/10% when below 35% health. "
        "Requires OwlsFocus (talentId=101).");
    add(104, "HeartOfThePhoenix", 3, 1, 1,
        { 55709 }, 102,
        "Tier 3 col 1 — Active. Pet self-resurrects if "
        "killed in combat. 10-min cooldown. Top-tier "
        "Cunning talent. Requires RoarOfRecovery "
        "(talentId=102).");
    return c;
}

WoweePetTalents WoweePetTalentsLoader::makeTenacity(
    const std::string& catalogName) {
    using P = WoweePetTalents;
    WoweePetTalents c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t tier, uint8_t column,
                    uint8_t maxRank,
                    std::vector<uint32_t> spells,
                    uint32_t prereq, const char* desc) {
        P::Entry e;
        e.talentId = id; e.name = name; e.description = desc;
        e.treeKind = P::Tenacity;
        e.tier = tier; e.column = column;
        e.maxRank = maxRank;
        e.spellIdsByRank = std::move(spells);
        e.prerequisiteTalentId = prereq;
        e.iconColorRGBA = packRgba(160, 220, 80);   // tenacity green
        c.entries.push_back(e);
    };
    add(200, "Charge", 0, 0, 1,
        { 61685 }, 0,
        "Tier 0 col 0 — Active. Pet charges target, "
        "stunning for 1 sec and increasing next attack "
        "by 25%. 25-yd range. Root talent.");
    add(201, "GreatStamina", 0, 1, 3,
        { 61686, 61687, 61688 }, 0,
        "Tier 0 col 1 — Increases pet stamina by "
        "4%/8%/12%. Root talent — most Tenacity builds "
        "max this first.");
    add(202, "Thunderstomp", 1, 1, 1,
        { 63900 }, 200,
        "Tier 1 col 1 — Active AoE. Pet stomps the "
        "ground, dealing nature damage to nearby enemies "
        "and threat. Requires Charge (talentId=200).");
    add(203, "Taunt", 2, 0, 1,
        { 53477 }, 202,
        "Tier 2 col 0 — Active. Forces target to attack "
        "the pet for 3 sec. Requires Thunderstomp "
        "(talentId=202).");
    add(204, "LastStand", 3, 1, 1,
        { 53478 }, 203,
        "Tier 3 col 1 — Active. Pet temporarily gains "
        "30% maximum health for 20 sec. 10-min cooldown. "
        "Top-tier Tenacity talent. Requires Taunt "
        "(talentId=203).");
    return c;
}

} // namespace pipeline
} // namespace wowee
