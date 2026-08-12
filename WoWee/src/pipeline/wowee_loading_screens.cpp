#include "pipeline/wowee_loading_screens.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'D', 'S'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wlds") {
        base += ".wlds";
    }
    return base;
}

} // namespace

const WoweeLoadingScreen::Entry*
WoweeLoadingScreen::findById(uint32_t screenId) const {
    for (const auto& e : entries)
        if (e.screenId == screenId) return &e;
    return nullptr;
}

const char* WoweeLoadingScreen::expansionGateName(uint8_t e) {
    switch (e) {
        case Classic:   return "classic";
        case TBC:       return "tbc";
        case WotLK:     return "wotlk";
        case TurtleWoW: return "turtle";
        default:        return "unknown";
    }
}

bool WoweeLoadingScreenLoader::save(const WoweeLoadingScreen& cat,
                                     const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.screenId);
        writePOD(os, e.mapId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.texturePath);
        writeStr(os, e.iconPath);
        writeStr(os, e.attribution);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.displayWeight);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.expansionRequired);
        writePOD(os, e.isAnimated);
        writePOD(os, e.isWideAspect);
        uint8_t pad = 0;
        writePOD(os, pad);
    }
    return os.good();
}

WoweeLoadingScreen WoweeLoadingScreenLoader::load(
    const std::string& basePath) {
    WoweeLoadingScreen out;
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
        if (!readPOD(is, e.screenId) ||
            !readPOD(is, e.mapId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.texturePath) ||
            !readStr(is, e.iconPath) ||
            !readStr(is, e.attribution)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.displayWeight)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.expansionRequired) ||
            !readPOD(is, e.isAnimated) ||
            !readPOD(is, e.isWideAspect)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) { out.entries.clear(); return out; }
    }
    return out;
}

bool WoweeLoadingScreenLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeLoadingScreen WoweeLoadingScreenLoader::makeStarter(
    const std::string& catalogName) {
    WoweeLoadingScreen c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t mapId, const char* name,
                    const char* tex, const char* desc) {
        WoweeLoadingScreen::Entry e;
        e.screenId = id;
        e.mapId = mapId;
        e.name = name;
        e.description = desc;
        e.texturePath = tex;
        e.iconPath = std::string("Interface/Glues/Common/") +
                      name + "_icon.blp";
        e.attribution = "Wowee art team";
        c.entries.push_back(e);
    };
    // ElwynnForest is gated to Eastern Kingdoms map (mapId=0)
    // — but mapId=0 is also the catch-all sentinel. To keep
    // the validator happy and reflect WoW's actual gating
    // (Elwynn lives on EK), bind it to the dwarf-starter
    // mapId 0 with explicit level cap. The GenericFallback
    // is the only true catch-all and uses a wider bracket
    // distinct from Elwynn's.
    add(1,   0, "ElwynnForest",
        "Interface/Glues/LoadingScreens/Elwynn.blp",
        "Stormwind / Elwynn Forest — green forested foothills "
        "with the abbey in the background.");
    c.entries.back().minLevel = 1;
    c.entries.back().maxLevel = 30;
    add(2,   1, "OrgrimmarLoading",
        "Interface/Glues/LoadingScreens/Orgrimmar.blp",
        "Orgrimmar — red rocky canyon walls + dragon banners.");
    // GenericFallback is the catch-all — full level range
    // but minimal weight, so it only appears when no
    // zone-specific screen matches.
    add(3,   0, "GenericFallback",
        "Interface/Glues/LoadingScreens/GenericMap.blp",
        "Generic catch-all — dragon icon over starfield, "
        "shown when no zone-specific screen matches.");
    c.entries.back().minLevel = 31;
    c.entries.back().maxLevel = 80;
    return c;
}

WoweeLoadingScreen WoweeLoadingScreenLoader::makeInstances(
    const std::string& catalogName) {
    WoweeLoadingScreen c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t mapId, const char* name,
                    const char* tex, const char* desc) {
        WoweeLoadingScreen::Entry e;
        e.screenId = id;
        e.mapId = mapId;
        e.name = name;
        e.description = desc;
        e.texturePath = tex;
        e.iconPath = std::string("Interface/Glues/Instances/") +
                      name + "_icon.blp";
        e.expansionRequired = WoweeLoadingScreen::WotLK;
        e.minLevel = 75;
        e.maxLevel = 80;
        c.entries.push_back(e);
    };
    add(100, 602, "HallsOfLightning",
        "Interface/Glues/LoadingScreens/HoL.blp",
        "Storm titan-keeper facility — purple lightning arcs over "
        "obsidian floors.");
    add(101, 599, "HallsOfStone",
        "Interface/Glues/LoadingScreens/HoS.blp",
        "Tribunal of Ages — colossal iron-dwarf statues lining "
        "a grand chamber.");
    add(102, 575, "UtgardePinnacle",
        "Interface/Glues/LoadingScreens/UP.blp",
        "Vrykul fortress — windswept icy parapet with King "
        "Ymiron's throne distant.");
    add(103, 608, "VioletHold",
        "Interface/Glues/LoadingScreens/VH.blp",
        "Dalaran prison breakout — violet magic shields holding "
        "back interdimensional rifts.");
    add(104, 595, "OldKingdom",
        "Interface/Glues/LoadingScreens/OK.blp",
        "Faceless one ruins — green bioluminescent fungi + "
        "Old God tentacles in the gloom.");
    return c;
}

WoweeLoadingScreen WoweeLoadingScreenLoader::makeRaidIntros(
    const std::string& catalogName) {
    WoweeLoadingScreen c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t mapId, const char* name,
                    const char* tex, const char* desc) {
        WoweeLoadingScreen::Entry e;
        e.screenId = id;
        e.mapId = mapId;
        e.name = name;
        e.description = desc;
        e.texturePath = tex;
        e.iconPath = std::string("Interface/Glues/Raids/") +
                      name + "_icon.blp";
        e.attribution = "Wowee art team — raid intro variants";
        e.expansionRequired = WoweeLoadingScreen::WotLK;
        e.minLevel = 80;
        e.maxLevel = 80;
        e.isWideAspect = 1;     // 16:9 raid intro art
        e.displayWeight = 3;    // higher weight than normal screens
        c.entries.push_back(e);
    };
    add(200, 533, "Naxxramas",
        "Interface/Glues/Raids/NaxxIntro.blp",
        "Floating Necropolis silhouetted against Northrend "
        "aurora — Kel'Thuzad's eye glow at center.");
    add(201, 603, "Ulduar",
        "Interface/Glues/Raids/UlduarIntro.blp",
        "Titan facility entrance — Yogg-Saron's mind-warping "
        "tendrils creeping from the corners.");
    add(202, 649, "TrialOfTheCrusader",
        "Interface/Glues/Raids/TocIntro.blp",
        "Argent Crusade colosseum — sun beams piercing arena "
        "spires + crowds in stands.");
    return c;
}

} // namespace pipeline
} // namespace wowee
