#include "pipeline/wowee_game_tips.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'T', 'P'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wgtp") {
        base += ".wgtp";
    }
    return base;
}

} // namespace

const WoweeGameTip::Entry*
WoweeGameTip::findById(uint32_t tipId) const {
    for (const auto& e : entries) if (e.tipId == tipId) return &e;
    return nullptr;
}

const char* WoweeGameTip::displayKindName(uint8_t k) {
    switch (k) {
        case LoadingScreen: return "loading-screen";
        case Tutorial:      return "tutorial";
        case TooltipHelp:   return "tooltip-help";
        case Hint:          return "hint";
        default:            return "unknown";
    }
}

bool WoweeGameTipLoader::save(const WoweeGameTip& cat,
                              const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.tipId);
        writeStr(os, e.name);
        writeStr(os, e.text);
        writeStr(os, e.iconPath);
        writePOD(os, e.displayKind);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, e.audienceFilter);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.displayWeight);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.conditionId);
        writePOD(os, e.requiredClassMask);
    }
    return os.good();
}

WoweeGameTip WoweeGameTipLoader::load(const std::string& basePath) {
    WoweeGameTip out;
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
        if (!readPOD(is, e.tipId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.text) ||
            !readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.displayKind)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.audienceFilter) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.displayWeight)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.conditionId) ||
            !readPOD(is, e.requiredClassMask)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeGameTipLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeGameTip WoweeGameTipLoader::makeStarter(
    const std::string& catalogName) {
    WoweeGameTip c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* text,
                    uint16_t weight) {
        WoweeGameTip::Entry e;
        e.tipId = id; e.name = name; e.text = text;
        e.iconPath = "Interface/TipOfTheDay/icon_generic.blp";
        e.displayKind = WoweeGameTip::LoadingScreen;
        e.displayWeight = weight;
        c.entries.push_back(e);
    };
    add(1, "CombatHint",
        "Press <Tab> to cycle through nearby enemies. "
        "Right-click to attack.", 1);
    add(2, "MovementHint",
        "Hold both mouse buttons to move forward without "
        "pressing W. Hold right-click to steer with the mouse.", 1);
    add(3, "QuestHint",
        "Yellow exclamation marks (!) above NPCs mean a "
        "quest is available. Yellow question marks (?) mean "
        "a quest is ready to turn in.", 2);
    return c;
}

WoweeGameTip WoweeGameTipLoader::makeNewPlayer(
    const std::string& catalogName) {
    WoweeGameTip c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* text,
                    uint16_t maxLevel) {
        WoweeGameTip::Entry e;
        e.tipId = id; e.name = name; e.text = text;
        e.iconPath = "Interface/TipOfTheDay/icon_newplayer.blp";
        e.displayKind = WoweeGameTip::Tutorial;
        e.audienceFilter = WoweeGameTip::kAudienceNewPlayer |
                            WoweeGameTip::kAudienceAlliance |
                            WoweeGameTip::kAudienceHorde;
        e.minLevel = 1;
        e.maxLevel = maxLevel;
        e.displayWeight = 5;     // weighted higher for new players
        c.entries.push_back(e);
    };
    add(100, "BindHearthstone",
        "Visit an innkeeper to bind your Hearthstone — it's "
        "the easiest way to return home.", 10);
    add(101, "TalentSpec",
        "At level 10 you can spend talent points. Visit your "
        "class trainer to learn how.", 15);
    add(102, "FirstMount",
        "At level 20 you can ride a mount! Save 1 gold and "
        "visit a mount vendor in your faction's capital.", 25);
    add(103, "QuestLog",
        "Press 'L' to open your quest log. You can track up "
        "to 25 active quests at once.", 15);
    add(104, "ProfessionPick",
        "Visit a profession trainer to learn a primary trade. "
        "You can have two primary professions.", 15);
    return c;
}

WoweeGameTip WoweeGameTipLoader::makeAdvanced(
    const std::string& catalogName) {
    WoweeGameTip c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* text,
                    uint8_t kind, uint32_t audience, uint32_t cond,
                    uint16_t weight) {
        WoweeGameTip::Entry e;
        e.tipId = id; e.name = name; e.text = text;
        e.iconPath = "Interface/TipOfTheDay/icon_advanced.blp";
        e.displayKind = kind;
        e.audienceFilter = audience;
        e.minLevel = 70;
        e.maxLevel = 80;
        e.conditionId = cond;
        e.displayWeight = weight;
        c.entries.push_back(e);
    };
    add(200, "RaidMechanic",
        "Raid bosses telegraph their abilities — watch for "
        "ground markers and mechanic announcements.",
        WoweeGameTip::Hint, WoweeGameTip::kAudiencePvE, 0, 3);
    add(201, "PvPArena",
        "Arena teams require a charter signed by 4 players. "
        "Visit an Arena Battlemaster to start one.",
        WoweeGameTip::TooltipHelp, WoweeGameTip::kAudiencePvP, 0, 2);
    add(202, "DailyProfession",
        "Some professions have daily quests at exalted with "
        "your faction. Check Shattrath and Dalaran daily.",
        WoweeGameTip::LoadingScreen,
        WoweeGameTip::kAudienceAll, 0, 2);
    add(203, "DungeonFinder",
        "Press 'I' to open the Dungeon Finder. It will form a "
        "group across servers and teleport you to the dungeon.",
        WoweeGameTip::Tutorial,
        WoweeGameTip::kAudienceAll, 0, 4);
    return c;
}

} // namespace pipeline
} // namespace wowee
