#include "pipeline/wowee_world_state_ui.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'W', 'U', 'I'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wwui") {
        base += ".wwui";
    }
    return base;
}

} // namespace

const WoweeWorldStateUI::Entry*
WoweeWorldStateUI::findById(uint32_t worldStateId) const {
    for (const auto& e : entries)
        if (e.worldStateId == worldStateId) return &e;
    return nullptr;
}

const char* WoweeWorldStateUI::displayKindName(uint8_t k) {
    switch (k) {
        case Counter:       return "counter";
        case Timer:         return "timer";
        case FlagIcon:      return "flag-icon";
        case ProgressBar:   return "progress-bar";
        case TwoSidedScore: return "two-sided";
        case Custom:        return "custom";
        default:            return "unknown";
    }
}

const char* WoweeWorldStateUI::panelPositionName(uint8_t p) {
    switch (p) {
        case Top:      return "top";
        case Bottom:   return "bottom";
        case TopLeft:  return "top-left";
        case TopRight: return "top-right";
        case Center:   return "center";
        default:       return "unknown";
    }
}

bool WoweeWorldStateUILoader::save(const WoweeWorldStateUI& cat,
                                    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.worldStateId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.displayKind);
        writePOD(os, e.panelPosition);
        writePOD(os, e.alwaysVisible);
        writePOD(os, e.hideWhenZero);
        writePOD(os, e.mapId);
        writePOD(os, e.areaId);
        writePOD(os, e.variableIndex);
        writePOD(os, e.defaultValue);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeWorldStateUI WoweeWorldStateUILoader::load(
    const std::string& basePath) {
    WoweeWorldStateUI out;
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
        if (!readPOD(is, e.worldStateId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.displayKind) ||
            !readPOD(is, e.panelPosition) ||
            !readPOD(is, e.alwaysVisible) ||
            !readPOD(is, e.hideWhenZero) ||
            !readPOD(is, e.mapId) ||
            !readPOD(is, e.areaId) ||
            !readPOD(is, e.variableIndex) ||
            !readPOD(is, e.defaultValue) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeWorldStateUILoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeWorldStateUI WoweeWorldStateUILoader::makeStarter(
    const std::string& catalogName) {
    WoweeWorldStateUI c;
    c.name = catalogName;
    {
        // Warsong Gulch — two-sided flag capture counter.
        WoweeWorldStateUI::Entry e;
        e.worldStateId = 1; e.name = "WSG Flag Captures";
        e.description = "Two-sided counter for flag captures in WSG.";
        e.iconPath = "Interface/WorldStateFrame/wsg_flag.blp";
        e.displayKind = WoweeWorldStateUI::TwoSidedScore;
        e.panelPosition = WoweeWorldStateUI::Top;
        e.alwaysVisible = 1;
        e.mapId = 489;          // Warsong Gulch
        e.areaId = 3277;
        e.variableIndex = 1;
        c.entries.push_back(e);
    }
    {
        // Arathi Basin — resource counter (5 bases, 0..1600).
        WoweeWorldStateUI::Entry e;
        e.worldStateId = 2; e.name = "AB Resources";
        e.description = "Two-sided resource counter (0..1600).";
        e.iconPath = "Interface/WorldStateFrame/ab_resource.blp";
        e.displayKind = WoweeWorldStateUI::TwoSidedScore;
        e.panelPosition = WoweeWorldStateUI::Top;
        e.alwaysVisible = 1;
        e.mapId = 529;          // Arathi Basin
        e.areaId = 3358;
        e.variableIndex = 2;
        c.entries.push_back(e);
    }
    {
        // EotS — flag carrier icon (single-sided, top-right).
        WoweeWorldStateUI::Entry e;
        e.worldStateId = 3; e.name = "EotS Flag Carrier";
        e.description = "Flag carrier icon (Alliance/Horde) shown "
                         "while a player holds the EotS flag.";
        e.iconPath = "Interface/WorldStateFrame/eots_flag.blp";
        e.displayKind = WoweeWorldStateUI::FlagIcon;
        e.panelPosition = WoweeWorldStateUI::TopRight;
        e.hideWhenZero = 1;
        e.mapId = 566;          // Eye of the Storm
        e.areaId = 3820;
        e.variableIndex = 3;
        c.entries.push_back(e);
    }
    return c;
}

WoweeWorldStateUI WoweeWorldStateUILoader::makeWintergrasp(
    const std::string& catalogName) {
    WoweeWorldStateUI c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t pos, uint32_t variableIndex,
                    int32_t defaultVal, uint8_t alwaysVis,
                    const char* desc) {
        WoweeWorldStateUI::Entry e;
        e.worldStateId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/WorldStateFrame/wg_") +
                      name + ".blp";
        e.displayKind = kind;
        e.panelPosition = pos;
        e.variableIndex = variableIndex;
        e.defaultValue = defaultVal;
        e.alwaysVisible = alwaysVis;
        e.mapId = 571;          // Northrend
        e.areaId = 4197;        // Wintergrasp
        c.entries.push_back(e);
    };
    add(100, "AllianceTanks",  WoweeWorldStateUI::Counter,
        WoweeWorldStateUI::TopLeft,  10, 0, 1,
        "Number of Alliance siege tanks deployed.");
    add(101, "HordeTanks",     WoweeWorldStateUI::Counter,
        WoweeWorldStateUI::TopRight, 11, 0, 1,
        "Number of Horde siege tanks deployed.");
    add(102, "TimeRemaining",  WoweeWorldStateUI::Timer,
        WoweeWorldStateUI::Top,      12, 1800, 1,
        "Seconds remaining in the current battle.");
    add(103, "TowersControlled", WoweeWorldStateUI::TwoSidedScore,
        WoweeWorldStateUI::Bottom,    13, 0, 1,
        "Towers controlled by each faction (0..3).");
    return c;
}

WoweeWorldStateUI WoweeWorldStateUILoader::makeDungeon(
    const std::string& catalogName) {
    WoweeWorldStateUI c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t mapId, uint32_t variableIndex,
                    int32_t defaultVal, uint8_t hideZero,
                    const char* desc) {
        WoweeWorldStateUI::Entry e;
        e.worldStateId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/WorldStateFrame/dungeon_") +
                      name + ".blp";
        e.displayKind = kind;
        e.panelPosition = WoweeWorldStateUI::TopRight;
        e.mapId = mapId;
        e.variableIndex = variableIndex;
        e.defaultValue = defaultVal;
        e.hideWhenZero = hideZero;
        c.entries.push_back(e);
    };
    add(200, "BossProgress",     WoweeWorldStateUI::ProgressBar,
        533, 20, 0, 0,    // Naxxramas
        "Progress bar — bosses defeated this run.");
    add(201, "KeyFragments",     WoweeWorldStateUI::Counter,
        540, 21, 0, 1,    // Shattered Halls
        "Key fragments collected for the dungeon door.");
    add(202, "TreasureHunt",     WoweeWorldStateUI::Counter,
        429, 22, 0, 1,    // Dire Maul
        "Hidden treasures collected during the run.");
    return c;
}

} // namespace pipeline
} // namespace wowee
