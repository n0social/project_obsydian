#include "pipeline/wowee_spell_cast_times.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'C', 'T'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wsct") {
        base += ".wsct";
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

const WoweeSpellCastTime::Entry*
WoweeSpellCastTime::findById(uint32_t castTimeId) const {
    for (const auto& e : entries)
        if (e.castTimeId == castTimeId) return &e;
    return nullptr;
}

int32_t WoweeSpellCastTime::resolveAtLevel(uint32_t castTimeId,
                                            uint32_t characterLevel) const {
    const Entry* e = findById(castTimeId);
    if (!e) return 0;
    int64_t ms = static_cast<int64_t>(e->baseCastMs) +
                 static_cast<int64_t>(e->perLevelMs) *
                 static_cast<int64_t>(characterLevel);
    if (e->minCastMs != 0 || e->maxCastMs != 0) {
        // Clamp only when bounds are non-trivial — minCastMs=
        // maxCastMs=0 means "no clamp configured" rather than
        // "must be exactly zero".
        if (ms < e->minCastMs) ms = e->minCastMs;
        if (e->maxCastMs > 0 && ms > e->maxCastMs) ms = e->maxCastMs;
    }
    if (ms < 0) ms = 0;
    return static_cast<int32_t>(ms);
}

const char* WoweeSpellCastTime::castKindName(uint8_t k) {
    switch (k) {
        case Instant:     return "instant";
        case Cast:        return "cast";
        case Channel:     return "channel";
        case DelayedCast: return "delayed";
        case ChargeCast:  return "charge";
        default:          return "unknown";
    }
}

bool WoweeSpellCastTimeLoader::save(const WoweeSpellCastTime& cat,
                                     const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.castTimeId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.castKind);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, e.baseCastMs);
        writePOD(os, e.perLevelMs);
        writePOD(os, e.minCastMs);
        writePOD(os, e.maxCastMs);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeSpellCastTime WoweeSpellCastTimeLoader::load(
    const std::string& basePath) {
    WoweeSpellCastTime out;
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
        if (!readPOD(is, e.castTimeId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.castKind)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.baseCastMs) ||
            !readPOD(is, e.perLevelMs) ||
            !readPOD(is, e.minCastMs) ||
            !readPOD(is, e.maxCastMs) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSpellCastTimeLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeSpellCastTime WoweeSpellCastTimeLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellCastTime c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    int32_t baseMs, uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeSpellCastTime::Entry e;
        e.castTimeId = id; e.name = name; e.description = desc;
        e.castKind = kind;
        e.baseCastMs = baseMs;
        // Starter buckets do not scale with level and don't
        // clamp — leave perLevel=0, min=0, max=0.
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "Instant",    WoweeSpellCastTime::Instant, 0,
        100, 240, 100, "Instant — fires on cast (0ms).");
    add(2, "FastCast",   WoweeSpellCastTime::Cast, 1000,
        180, 240, 100, "Fast cast — 1.0s base.");
    add(3, "MediumCast", WoweeSpellCastTime::Cast, 1500,
        240, 240, 100, "Medium cast — 1.5s base (Frostbolt rank 1).");
    add(4, "LongCast",   WoweeSpellCastTime::Cast, 3000,
        240, 180, 100, "Long cast — 3.0s base (Pyroblast).");
    return c;
}

WoweeSpellCastTime WoweeSpellCastTimeLoader::makeChannel(
    const std::string& catalogName) {
    WoweeSpellCastTime c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, int32_t baseMs,
                    const char* desc) {
        WoweeSpellCastTime::Entry e;
        e.castTimeId = id; e.name = name; e.description = desc;
        e.castKind = WoweeSpellCastTime::Channel;
        e.baseCastMs = baseMs;
        // Channels are normally not haste-clamped; min/max
        // stay 0 and the engine treats baseCastMs as the
        // total channel duration.
        e.iconColorRGBA = packRgba(180, 100, 240);   // purple
        c.entries.push_back(e);
    };
    add(100, "TickEvery1s",  3000,
        "Channel — 3s total, ticks every 1s (Drain Life).");
    add(101, "TickEvery2s",  6000,
        "Channel — 6s total, ticks every 2s (Mind Flay).");
    add(102, "TickEvery3s",  9000,
        "Channel — 9s total, ticks every 3s (Tranquility).");
    return c;
}

WoweeSpellCastTime WoweeSpellCastTimeLoader::makeRamp(
    const std::string& catalogName) {
    WoweeSpellCastTime c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, int32_t baseMs,
                    int32_t perLevelMs, int32_t minMs, int32_t maxMs,
                    const char* desc) {
        WoweeSpellCastTime::Entry e;
        e.castTimeId = id; e.name = name; e.description = desc;
        e.castKind = WoweeSpellCastTime::Cast;
        e.baseCastMs = baseMs;
        e.perLevelMs = perLevelMs;
        e.minCastMs = minMs;
        e.maxCastMs = maxMs;
        e.iconColorRGBA = packRgba(240, 100, 180);   // pink
        c.entries.push_back(e);
    };
    // baseCastMs is the level-1 value; +perLevelMs per
    // character level, clamped to [minMs, maxMs] for haste
    // and end-game scaling.
    add(200, "ScalingShort",   500,  10,  500,  2000,
        "Level-scaled short cast: 0.5s + 10ms/lvl (clamps "
        "0.5..2.0s).");
    add(201, "ScalingMedium", 1000,  20, 1000,  3000,
        "Level-scaled medium cast: 1.0s + 20ms/lvl (clamps "
        "1.0..3.0s).");
    add(202, "ScalingLong",   2000,  30, 2000,  5000,
        "Level-scaled long cast: 2.0s + 30ms/lvl (clamps "
        "2.0..5.0s).");
    add(203, "ScalingHuge",   3000,  50, 3000, 10000,
        "Level-scaled huge cast: 3.0s + 50ms/lvl (clamps "
        "3.0..10.0s).");
    return c;
}

} // namespace pipeline
} // namespace wowee
