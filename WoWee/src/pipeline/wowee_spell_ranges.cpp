#include "pipeline/wowee_spell_ranges.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'R', 'G'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wsrg") {
        base += ".wsrg";
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

const WoweeSpellRange::Entry*
WoweeSpellRange::findById(uint32_t rangeId) const {
    for (const auto& e : entries)
        if (e.rangeId == rangeId) return &e;
    return nullptr;
}

const char* WoweeSpellRange::rangeKindName(uint8_t k) {
    switch (k) {
        case Self:        return "self";
        case Melee:       return "melee";
        case ShortRanged: return "short";
        case Ranged:      return "ranged";
        case LongRanged:  return "long";
        case VeryLong:    return "very-long";
        case Unlimited:   return "unlimited";
        default:          return "unknown";
    }
}

bool WoweeSpellRangeLoader::save(const WoweeSpellRange& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.rangeId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.rangeKind);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, e.minRange);
        writePOD(os, e.maxRange);
        writePOD(os, e.minRangeFriendly);
        writePOD(os, e.maxRangeFriendly);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeSpellRange WoweeSpellRangeLoader::load(const std::string& basePath) {
    WoweeSpellRange out;
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
        if (!readPOD(is, e.rangeId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.rangeKind)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.minRange) ||
            !readPOD(is, e.maxRange) ||
            !readPOD(is, e.minRangeFriendly) ||
            !readPOD(is, e.maxRangeFriendly) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSpellRangeLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeSpellRange WoweeSpellRangeLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellRange c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    float minR, float maxR, uint8_t r, uint8_t g,
                    uint8_t b, const char* desc) {
        WoweeSpellRange::Entry e;
        e.rangeId = id; e.name = name; e.description = desc;
        e.rangeKind = kind;
        e.minRange = minR; e.maxRange = maxR;
        // Default friendly == hostile.
        e.minRangeFriendly = minR; e.maxRangeFriendly = maxR;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "SelfRange",     WoweeSpellRange::Self,
        0.0f, 0.0f,  240, 240, 240,
        "Self-only — caster is the only valid target.");
    add(2, "MeleeRange",    WoweeSpellRange::Melee,
        0.0f, 5.0f,  220, 80, 80,
        "Melee — within white-attack range (5y).");
    add(3, "SpellRange",    WoweeSpellRange::Ranged,
        0.0f, 30.0f, 100, 180, 240,
        "Standard spell — 30 yards, common caster range.");
    return c;
}

WoweeSpellRange WoweeSpellRangeLoader::makeRanged(
    const std::string& catalogName) {
    WoweeSpellRange c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    float maxR, uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeSpellRange::Entry e;
        e.rangeId = id; e.name = name; e.description = desc;
        e.rangeKind = kind;
        e.maxRange = maxR;
        e.maxRangeFriendly = maxR;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(100, "ShortCast",  WoweeSpellRange::ShortRanged,  20.0f,
        100, 200, 240, "Short-range spell — 20y. Close-up casts.");
    add(101, "MediumCast", WoweeSpellRange::Ranged,       30.0f,
        100, 180, 240, "Medium-range spell — 30y. Default caster range.");
    add(102, "LongCast",   WoweeSpellRange::LongRanged,   40.0f,
        100, 160, 240, "Long-range spell — 40y. Hunter / sniper range.");
    add(103, "VeryLong",   WoweeSpellRange::VeryLong,     100.0f,
        100, 140, 240, "Very-long range — 100y. Vision / aura range.");
    add(104, "Unlimited",  WoweeSpellRange::Unlimited,    99999.0f,
        140, 100, 240, "Unlimited range — global server-tracked spell.");
    return c;
}

WoweeSpellRange WoweeSpellRangeLoader::makeFriendly(
    const std::string& catalogName) {
    WoweeSpellRange c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, float maxFriendly,
                    float maxHostile, const char* desc) {
        WoweeSpellRange::Entry e;
        e.rangeId = id; e.name = name; e.description = desc;
        e.rangeKind = WoweeSpellRange::Ranged;
        // Friendly range is the larger of the two.
        e.maxRange = maxHostile;
        e.maxRangeFriendly = maxFriendly;
        e.iconColorRGBA = packRgba(80, 240, 100);   // green for healing
        c.entries.push_back(e);
    };
    add(200, "HealRange",     40.0f,  0.0f,
        "Heal target — 40y friendly, 0y hostile (heals don't "
        "reach enemies).");
    add(201, "CleanseRange",  30.0f,  0.0f,
        "Cleanse / dispel — 30y friendly only.");
    add(202, "BuffRange",     30.0f,  0.0f,
        "Beneficial buff — 30y friendly only (Power Word: "
        "Fortitude, Mark of the Wild).");
    return c;
}

} // namespace pipeline
} // namespace wowee
