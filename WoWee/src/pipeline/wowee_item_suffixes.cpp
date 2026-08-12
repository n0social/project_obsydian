#include "pipeline/wowee_item_suffixes.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'U', 'F'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wsuf") {
        base += ".wsuf";
    }
    return base;
}

} // namespace

const WoweeItemSuffix::Entry*
WoweeItemSuffix::findById(uint32_t suffixId) const {
    for (const auto& e : entries)
        if (e.suffixId == suffixId) return &e;
    return nullptr;
}

const char* WoweeItemSuffix::suffixCategoryName(uint8_t c) {
    switch (c) {
        case Generic:   return "generic";
        case Elemental: return "elemental";
        case Defensive: return "defensive";
        case PvPSuffix: return "pvp";
        case Crafted:   return "crafted";
        default:        return "unknown";
    }
}

bool WoweeItemSuffixLoader::save(const WoweeItemSuffix& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.suffixId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.itemQualityFloor);
        writePOD(os, e.itemQualityCeiling);
        writePOD(os, e.suffixCategory);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, e.restrictedSlotMask);
        for (size_t k = 0; k < WoweeItemSuffix::kMaxStats; ++k) {
            writePOD(os, e.statKind[k]);
        }
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        for (size_t k = 0; k < WoweeItemSuffix::kMaxStats; ++k) {
            writePOD(os, e.statValuePoints[k]);
        }
    }
    return os.good();
}

WoweeItemSuffix WoweeItemSuffixLoader::load(const std::string& basePath) {
    WoweeItemSuffix out;
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
        if (!readPOD(is, e.suffixId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.itemQualityFloor) ||
            !readPOD(is, e.itemQualityCeiling) ||
            !readPOD(is, e.suffixCategory)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.restrictedSlotMask)) {
            out.entries.clear(); return out;
        }
        for (size_t k = 0; k < WoweeItemSuffix::kMaxStats; ++k) {
            if (!readPOD(is, e.statKind[k])) {
                out.entries.clear(); return out;
            }
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        for (size_t k = 0; k < WoweeItemSuffix::kMaxStats; ++k) {
            if (!readPOD(is, e.statValuePoints[k])) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeItemSuffixLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeItemSuffix WoweeItemSuffixLoader::makeStarter(
    const std::string& catalogName) {
    WoweeItemSuffix c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t s1,
                    uint16_t v1, uint8_t s2, uint16_t v2,
                    const char* desc) {
        WoweeItemSuffix::Entry e;
        e.suffixId = id; e.name = name; e.description = desc;
        e.itemQualityFloor = 2;     // green minimum
        e.itemQualityCeiling = 3;   // blue maximum
        e.suffixCategory = WoweeItemSuffix::Generic;
        e.statKind[0] = s1; e.statValuePoints[0] = v1;
        e.statKind[1] = s2; e.statValuePoints[1] = v2;
        c.entries.push_back(e);
    };
    // statKind values match WIT.statType:
    //  4 = STR, 3 = AGI, 5 = INT, 6 = SPI, 7 = STA.
    add(1, "of the Bear",  4, 100, 7,  80,
        "Strength + Stamina — favored by tanks.");
    add(2, "of the Eagle", 5, 100, 6,  60,
        "Intellect + Spirit — favored by casters.");
    add(3, "of the Tiger", 4, 100, 3,  80,
        "Strength + Agility — favored by physical DPS.");
    return c;
}

WoweeItemSuffix WoweeItemSuffixLoader::makeMagical(
    const std::string& catalogName) {
    WoweeItemSuffix c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t schoolStat,
                    uint16_t value, const char* desc) {
        WoweeItemSuffix::Entry e;
        e.suffixId = id; e.name = name; e.description = desc;
        e.itemQualityFloor = 2;
        e.itemQualityCeiling = 4;
        e.suffixCategory = WoweeItemSuffix::Elemental;
        // schoolStat values 30..36 represent per-school spell
        // power — Fire=30, Frost=31, Shadow=32, Arcane=33,
        // Holy=34, Nature=35, Healing=36 (engine-internal
        // mapping outside the WIT canonical set).
        e.statKind[0] = schoolStat;
        e.statValuePoints[0] = value;
        e.restrictedSlotMask =
            WoweeItemSuffix::kSlotHead | WoweeItemSuffix::kSlotChest |
            WoweeItemSuffix::kSlotShoulder | WoweeItemSuffix::kSlotLegs |
            WoweeItemSuffix::kSlotHands | WoweeItemSuffix::kSlotWeapon;
        c.entries.push_back(e);
    };
    add(100, "of Fire",   30, 150, "+ Fire spell power.");
    add(101, "of Frost",  31, 150, "+ Frost spell power.");
    add(102, "of Shadow", 32, 150, "+ Shadow spell power.");
    add(103, "of Arcane", 33, 150, "+ Arcane spell power.");
    return c;
}

WoweeItemSuffix WoweeItemSuffixLoader::makePvP(
    const std::string& catalogName) {
    WoweeItemSuffix c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t s1,
                    uint16_t v1, uint8_t s2, uint16_t v2,
                    uint8_t s3, uint16_t v3, const char* desc) {
        WoweeItemSuffix::Entry e;
        e.suffixId = id; e.name = name; e.description = desc;
        e.itemQualityFloor = 3;     // PvP suffixes only on blues+
        e.itemQualityCeiling = 4;
        e.suffixCategory = WoweeItemSuffix::PvPSuffix;
        // statKind 50 = Resilience (engine-internal, outside
        // the WIT canonical set).
        e.statKind[0] = s1; e.statValuePoints[0] = v1;
        e.statKind[1] = s2; e.statValuePoints[1] = v2;
        e.statKind[2] = s3; e.statValuePoints[2] = v3;
        c.entries.push_back(e);
    };
    add(200, "of the Champion",  50, 80, 4,  60, 7,  60,
        "Resilience + Strength + Stamina — melee PvP.");
    add(201, "of the Gladiator", 50, 80, 5,  60, 6,  40,
        "Resilience + Intellect + Spirit — caster PvP.");
    add(202, "of Resilience",    50, 120, 7, 80,  0,   0,
        "Pure resilience + extra Stamina — peak PvP defense.");
    return c;
}

} // namespace pipeline
} // namespace wowee
