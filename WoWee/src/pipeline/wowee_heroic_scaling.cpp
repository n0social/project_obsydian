#include "pipeline/wowee_heroic_scaling.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'H', 'R', 'D'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".whrd") {
        base += ".whrd";
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

const WoweeHeroicScaling::Entry*
WoweeHeroicScaling::findById(uint32_t scalingId) const {
    for (const auto& e : entries)
        if (e.scalingId == scalingId) return &e;
    return nullptr;
}

const WoweeHeroicScaling::Entry*
WoweeHeroicScaling::findForInstance(uint32_t mapId,
                                       uint32_t difficultyId) const {
    for (const auto& e : entries) {
        if (e.mapId == mapId && e.difficultyId == difficultyId)
            return &e;
    }
    return nullptr;
}

bool WoweeHeroicScalingLoader::save(const WoweeHeroicScaling& cat,
                                      const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.scalingId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mapId);
        writePOD(os, e.difficultyId);
        writePOD(os, e.itemLevelDelta);
        writePOD(os, e.bonusQualityChance);
        writePOD(os, e.dropChanceMultiplier);
        writePOD(os, e.heroicTokenItemId);
        writePOD(os, e.bonusEmblemCount);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeHeroicScaling WoweeHeroicScalingLoader::load(
    const std::string& basePath) {
    WoweeHeroicScaling out;
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
        if (!readPOD(is, e.scalingId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.difficultyId) ||
            !readPOD(is, e.itemLevelDelta) ||
            !readPOD(is, e.bonusQualityChance) ||
            !readPOD(is, e.dropChanceMultiplier) ||
            !readPOD(is, e.heroicTokenItemId) ||
            !readPOD(is, e.bonusEmblemCount) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeHeroicScalingLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeHeroicScaling WoweeHeroicScalingLoader::makeWotLK5manHeroic(
    const std::string& catalogName) {
    using H = WoweeHeroicScaling;
    WoweeHeroicScaling c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint32_t tokenId, const char* desc) {
        H::Entry e;
        e.scalingId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.difficultyId = 1;        // 5-man Heroic
        e.itemLevelDelta = 13;
        e.bonusQualityChance = 200;   // 2% bonus quality
        e.dropChanceMultiplier = 1.0f;
        e.heroicTokenItemId = tokenId;
        e.bonusEmblemCount = 2;       // 2× Emblem of
                                       // Heroism per boss
        e.iconColorRGBA = packRgba(180, 220, 100);   // 5man green
        c.entries.push_back(e);
    };
    // mapIds from WoTLK 3.3.5a Map.dbc.
    // Token: Emblem of Heroism (itemId 40752).
    add(1, "UtgardeKeepHeroic",   574, 40752,
        "Utgarde Keep 5-man Heroic — +13 ilvl over "
        "Normal, 2× Emblems of Heroism per boss, 2%% "
        "chance for bonus +1-tier quality drop.");
    add(2, "TheNexusHeroic",      576, 40752,
        "The Nexus 5-man Heroic — same +13/2×/2%% "
        "scaling. First Northrend instance with Heroic "
        "queue popularity.");
    add(3, "AzjolNerubHeroic",    601, 40752,
        "Azjol-Nerub 5-man Heroic — same scaling. "
        "Three-boss instance, fast Emblems of Heroism "
        "farm.");
    add(4, "AhnkahetHeroic",      619, 40752,
        "Ahn'kahet: The Old Kingdom 5-man Heroic — "
        "same scaling. Sister-instance to Azjol-Nerub.");
    add(5, "DrakTharonHeroic",    600, 40752,
        "Drak'Tharon Keep 5-man Heroic — same scaling. "
        "Alliance/Horde-shared instance in Grizzly Hills.");
    return c;
}

WoweeHeroicScaling WoweeHeroicScalingLoader::makeRaid25Heroic(
    const std::string& catalogName) {
    using H = WoweeHeroicScaling;
    WoweeHeroicScaling c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t map,
                    uint32_t difficulty, uint32_t tokenId,
                    int16_t ilvlDelta, const char* desc) {
        H::Entry e;
        e.scalingId = id; e.name = name; e.description = desc;
        e.mapId = map;
        e.difficultyId = difficulty;
        e.itemLevelDelta = ilvlDelta;
        e.bonusQualityChance = 500;   // 5% bonus quality
                                       // (raid heroic is
                                       // more generous)
        e.dropChanceMultiplier = 1.5f;
        e.heroicTokenItemId = tokenId;
        e.bonusEmblemCount = 1;       // 1× extra emblem
                                       // (base loot
                                       // already gives 1)
        e.iconColorRGBA = packRgba(220, 80, 100);   // raid red
        c.entries.push_back(e);
    };
    // Tokens: Emblem of Conquest (40753), Emblem of
    // Triumph (47241), Emblem of Frost (49426).
    add(100, "Naxx25Heroic",   533, 4, 40753, 13,
        "Naxxramas 25H — +13 ilvl, 1.5× rare drop "
        "chance, 5%% bonus quality, +1 Emblem of "
        "Conquest per boss.");
    add(101, "EoE25Heroic",    616, 4, 40753, 13,
        "The Eye of Eternity 25H — same +13/1.5×/5%% "
        "scaling. Single-boss instance (Malygos), "
        "high-token-density per hour.");
    add(102, "Ulduar25Heroic", 603, 4, 47241, 26,
        "Ulduar 25H — +26 ilvl over Normal, 1.5× rare "
        "drops, 5%% bonus quality, Emblem of Triumph.");
    add(103, "ICC25Heroic",    631, 4, 49426, 26,
        "Icecrown Citadel 25H — +26 ilvl, 1.5× rare "
        "drops, 5%% bonus quality, Emblem of Frost. "
        "Endgame WotLK content.");
    return c;
}

WoweeHeroicScaling WoweeHeroicScalingLoader::makeChallengeMode(
    const std::string& catalogName) {
    using H = WoweeHeroicScaling;
    WoweeHeroicScaling c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t difficulty,
                    int16_t ilvlDelta, uint16_t bonusQual,
                    float dropMult, uint8_t emblems,
                    uint32_t color, const char* desc) {
        H::Entry e;
        e.scalingId = id; e.name = name; e.description = desc;
        e.mapId = 0;            // 0 = applies to any map
                                 // with this difficulty
        e.difficultyId = difficulty;
        e.itemLevelDelta = ilvlDelta;
        e.bonusQualityChance = bonusQual;
        e.dropChanceMultiplier = dropMult;
        e.heroicTokenItemId = 0;     // challenge-mode
                                      // uses transmog
                                      // weekly cache, not
                                      // a token reward
        e.bonusEmblemCount = emblems;
        e.iconColorRGBA = color;
        c.entries.push_back(e);
    };
    // Anachronistic for WotLK (challenge mode came in
    // Mists of Pandaria) but useful template for custom
    // servers that backport the system.
    add(200, "ChallengeModeBronze", 100,
        13, 100, 1.0f, 0,
        packRgba(180, 130, 80),
        "Challenge Mode Bronze tier — completion within "
        "Bronze time threshold. +13 ilvl, 1%% bonus "
        "quality, no extra emblems. Modeled after MoP "
        "challenge-mode rewards (anachronistic for "
        "WotLK; template for custom servers).");
    add(201, "ChallengeModeSilver", 101,
        20, 250, 1.0f, 1,
        packRgba(200, 200, 200),
        "Challenge Mode Silver tier — +20 ilvl, 2.5%% "
        "bonus quality, 1× extra emblem. Faster "
        "completion than Bronze threshold.");
    add(202, "ChallengeModeGold",   102,
        26, 500, 1.0f, 2,
        packRgba(220, 200, 80),
        "Challenge Mode Gold tier — +26 ilvl, 5%% bonus "
        "quality, 2× extra emblems. Top tier (transmog "
        "set + mount unlock at category completion).");
    return c;
}

} // namespace pipeline
} // namespace wowee
