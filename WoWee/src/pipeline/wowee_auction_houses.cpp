#include "pipeline/wowee_auction_houses.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'U', 'H'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wauh") {
        base += ".wauh";
    }
    return base;
}

} // namespace

const WoweeAuctionHouses::Entry*
WoweeAuctionHouses::findById(uint32_t ahId) const {
    for (const auto& e : entries)
        if (e.ahId == ahId) return &e;
    return nullptr;
}

const WoweeAuctionHouses::Entry*
WoweeAuctionHouses::findByNpc(uint32_t npcId) const {
    for (const auto& e : entries)
        if (e.npcAuctioneerId == npcId) return &e;
    return nullptr;
}

std::vector<const WoweeAuctionHouses::Entry*>
WoweeAuctionHouses::findByFaction(uint8_t faction) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.factionAccess == Both ||
            e.factionAccess == Neutral ||
            (faction != Both && e.factionAccess == faction)) {
            out.push_back(&e);
        }
    }
    return out;
}

bool WoweeAuctionHousesLoader::save(
    const WoweeAuctionHouses& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.ahId);
        writeStr(os, e.name);
        writePOD(os, e.factionAccess);
        writePOD(os, e.pad0);
        writePOD(os, e.depositRatePct);
        writePOD(os, e.cutPct);
        writePOD(os, e.minListingDurationHours);
        writePOD(os, e.maxListingDurationHours);
        writePOD(os, e.pad1);
        writePOD(os, e.feePerSlot);
        writePOD(os, e.npcAuctioneerId);
    }
    return os.good();
}

WoweeAuctionHouses WoweeAuctionHousesLoader::load(
    const std::string& basePath) {
    WoweeAuctionHouses out;
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
        if (!readPOD(is, e.ahId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.factionAccess) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.depositRatePct) ||
            !readPOD(is, e.cutPct) ||
            !readPOD(is, e.minListingDurationHours) ||
            !readPOD(is, e.maxListingDurationHours) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.feePerSlot) ||
            !readPOD(is, e.npcAuctioneerId)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeAuctionHousesLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

WoweeAuctionHouses::Entry makeAH(
    uint32_t ahId, const char* name,
    uint8_t factionAccess,
    uint16_t depositRatePct,
    uint16_t cutPct,
    uint16_t minHours, uint16_t maxHours,
    uint32_t feePerSlot,
    uint32_t npcAuctioneerId) {
    WoweeAuctionHouses::Entry e;
    e.ahId = ahId; e.name = name;
    e.factionAccess = factionAccess;
    e.depositRatePct = depositRatePct;
    e.cutPct = cutPct;
    e.minListingDurationHours = minHours;
    e.maxListingDurationHours = maxHours;
    e.feePerSlot = feePerSlot;
    e.npcAuctioneerId = npcAuctioneerId;
    return e;
}

} // namespace

WoweeAuctionHouses WoweeAuctionHousesLoader::makeStormwindAH(
    const std::string& catalogName) {
    using A = WoweeAuctionHouses;
    WoweeAuctionHouses c;
    c.name = catalogName;
    // Stormwind Trade District AH. Vanilla rates:
    // 5% deposit, 5% cut, 12/24/48 hr tiers, no
    // per-slot fee. NPC: Auctioneer Tricket
    // (creatureId 8666).
    c.entries.push_back(makeAH(
        1, "Stormwind Trade District AH",
        A::Alliance,
        500 /* 5% deposit */,
        500 /* 5% cut */,
        12, 48,
        0,
        8666 /* Auctioneer Tricket */));
    return c;
}

WoweeAuctionHouses WoweeAuctionHousesLoader::makeOrgrimmarAH(
    const std::string& catalogName) {
    using A = WoweeAuctionHouses;
    WoweeAuctionHouses c;
    c.name = catalogName;
    // Orgrimmar Valley of Strength AH. Same rates as
    // Stormwind. NPC: Auctioneer Tahesh
    // (creatureId 9856).
    c.entries.push_back(makeAH(
        2, "Orgrimmar Valley of Strength AH",
        A::Horde,
        500 /* 5% deposit */,
        500 /* 5% cut */,
        12, 48,
        0,
        9856 /* Auctioneer Tahesh */));
    return c;
}

WoweeAuctionHouses WoweeAuctionHousesLoader::makeBootyBayAH(
    const std::string& catalogName) {
    using A = WoweeAuctionHouses;
    WoweeAuctionHouses c;
    c.name = catalogName;
    // Booty Bay neutral AH. The famous 15% deposit
    // + 15% cut rates that made cross-faction
    // trading expensive. NPC: Auctioneer Beardo
    // (creatureId 9858).
    c.entries.push_back(makeAH(
        3, "Booty Bay Neutral AH",
        A::Neutral,
        1500 /* 15% deposit */,
        1500 /* 15% cut */,
        12, 48,
        0,
        9858 /* Auctioneer Beardo */));
    return c;
}

} // namespace pipeline
} // namespace wowee
