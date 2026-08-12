#include "pipeline/wowee_auction.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'U', 'C'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wauc") {
        base += ".wauc";
    }
    return base;
}

} // namespace

const WoweeAuction::Entry* WoweeAuction::findById(uint32_t houseId) const {
    for (const auto& e : entries) if (e.houseId == houseId) return &e;
    return nullptr;
}

const char* WoweeAuction::factionAccessName(uint8_t f) {
    switch (f) {
        case Alliance: return "alliance";
        case Horde:    return "horde";
        case Neutral:  return "neutral";
        case Both:     return "both";
        default:       return "unknown";
    }
}

bool WoweeAuctionLoader::save(const WoweeAuction& cat,
                              const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.houseId);
        writePOD(os, e.auctioneerNpcId);
        writeStr(os, e.name);
        writePOD(os, e.factionAccess);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, e.baseDepositRateBp);
        writePOD(os, e.houseCutRateBp);
        writePOD(os, e.maxBidCopper);
        writePOD(os, e.shortHours);
        writePOD(os, e.mediumHours);
        writePOD(os, e.longHours);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.shortMultBp);
        writePOD(os, e.mediumMultBp);
        writePOD(os, e.longMultBp);
        writePOD(os, e.disallowedClassMask);
    }
    return os.good();
}

WoweeAuction WoweeAuctionLoader::load(const std::string& basePath) {
    WoweeAuction out;
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
        if (!readPOD(is, e.houseId) ||
            !readPOD(is, e.auctioneerNpcId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.factionAccess)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.baseDepositRateBp) ||
            !readPOD(is, e.houseCutRateBp) ||
            !readPOD(is, e.maxBidCopper) ||
            !readPOD(is, e.shortHours) ||
            !readPOD(is, e.mediumHours) ||
            !readPOD(is, e.longHours)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.shortMultBp) ||
            !readPOD(is, e.mediumMultBp) ||
            !readPOD(is, e.longMultBp) ||
            !readPOD(is, e.disallowedClassMask)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeAuctionLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeAuction WoweeAuctionLoader::makeStarter(const std::string& catalogName) {
    WoweeAuction c;
    c.name = catalogName;
    {
        WoweeAuction::Entry e;
        e.houseId = 1; e.name = "Starter House";
        e.factionAccess = WoweeAuction::Neutral;
        c.entries.push_back(e);
    }
    return c;
}

WoweeAuction WoweeAuctionLoader::makeFactionPair(const std::string& catalogName) {
    WoweeAuction c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t fac,
                    uint32_t cutBp, uint32_t auctioneerNpc) {
        WoweeAuction::Entry e;
        e.houseId = id; e.name = name;
        e.factionAccess = fac;
        e.houseCutRateBp = cutBp;
        e.auctioneerNpcId = auctioneerNpc;
        c.entries.push_back(e);
    };
    // Faction houses charge 5%; neutral charges the canonical
    // 15% premium for cross-faction trade.
    add(1, "Stormwind",   WoweeAuction::Alliance, 500,  8719);
    add(2, "Orgrimmar",   WoweeAuction::Horde,    500,  8718);
    add(3, "Booty Bay",   WoweeAuction::Neutral,  1500, 2622);
    return c;
}

WoweeAuction WoweeAuctionLoader::makeRestricted(const std::string& catalogName) {
    WoweeAuction c;
    c.name = catalogName;
    {
        WoweeAuction::Entry e;
        e.houseId = 100; e.name = "Restricted House";
        e.factionAccess = WoweeAuction::Both;
        // Disallow Quest items (class 12) and Containers (class 1)
        // and Keys (class 13) — bitmask combination.
        e.disallowedClassMask = (1u << 1) | (1u << 12) | (1u << 13);
        // Tighter durations + lower max bid for testing.
        e.shortHours = 2;
        e.mediumHours = 6;
        e.longHours = 12;
        e.maxBidCopper = 10000000;     // 1000g cap
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
