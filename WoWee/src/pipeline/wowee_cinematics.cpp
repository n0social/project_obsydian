#include "pipeline/wowee_cinematics.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'M', 'S'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wcms") {
        base += ".wcms";
    }
    return base;
}

} // namespace

const WoweeCinematic::Entry*
WoweeCinematic::findById(uint32_t cinematicId) const {
    for (const auto& e : entries) if (e.cinematicId == cinematicId) return &e;
    return nullptr;
}

const char* WoweeCinematic::kindName(uint8_t k) {
    switch (k) {
        case PreRenderedVideo: return "video";
        case CameraFlythrough: return "camera";
        case TextCrawl:        return "text-crawl";
        case StillImage:       return "image";
        case Slideshow:        return "slideshow";
        default:               return "unknown";
    }
}

const char* WoweeCinematic::triggerKindName(uint8_t t) {
    switch (t) {
        case Manual:            return "manual";
        case QuestStart:        return "quest-start";
        case QuestEnd:          return "quest-end";
        case ClassStart:        return "class-start";
        case ZoneEntry:         return "zone-entry";
        case DungeonClear:      return "dungeon-clear";
        case Login:             return "login";
        case AchievementGained: return "achievement";
        case LevelUp:           return "level-up";
        default:                return "unknown";
    }
}

bool WoweeCinematicLoader::save(const WoweeCinematic& cat,
                                const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.cinematicId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.mediaPath);
        writePOD(os, e.kind);
        writePOD(os, e.triggerKind);
        writePOD(os, e.skippable);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, e.durationSeconds);
        writePOD(os, e.triggerTargetId);
        writePOD(os, e.soundtrackId);
    }
    return os.good();
}

WoweeCinematic WoweeCinematicLoader::load(const std::string& basePath) {
    WoweeCinematic out;
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
        if (!readPOD(is, e.cinematicId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.mediaPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.kind) ||
            !readPOD(is, e.triggerKind) ||
            !readPOD(is, e.skippable)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.durationSeconds) ||
            !readPOD(is, e.triggerTargetId) ||
            !readPOD(is, e.soundtrackId)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeCinematicLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeCinematic WoweeCinematicLoader::makeStarter(const std::string& catalogName) {
    WoweeCinematic c;
    c.name = catalogName;
    {
        WoweeCinematic::Entry e;
        e.cinematicId = 1; e.name = "Realm Intro";
        e.description = "Pre-rendered intro played on character login.";
        e.kind = WoweeCinematic::PreRenderedVideo;
        e.triggerKind = WoweeCinematic::Login;
        e.mediaPath = "Movies/Intro/realm_intro.ogv";
        e.durationSeconds = 90;
        e.skippable = 1;
        e.soundtrackId = 2;       // WSND.makeStarter music id
        c.entries.push_back(e);
    }
    {
        WoweeCinematic::Entry e;
        e.cinematicId = 2; e.name = "Quest Cutscene";
        e.description = "In-engine camera flythrough on quest accept.";
        e.kind = WoweeCinematic::CameraFlythrough;
        e.triggerKind = WoweeCinematic::QuestStart;
        e.mediaPath = "Cinematics/quest_001_camera.m2";
        e.durationSeconds = 30;
        e.skippable = 1;
        e.triggerTargetId = 1;    // WQT questId 1
        c.entries.push_back(e);
    }
    {
        WoweeCinematic::Entry e;
        e.cinematicId = 3; e.name = "Login Splash";
        e.description = "Static splash image shown on title screen.";
        e.kind = WoweeCinematic::StillImage;
        e.triggerKind = WoweeCinematic::Manual;
        e.mediaPath = "Splash/login_image.png";
        e.durationSeconds = 5;
        c.entries.push_back(e);
    }
    return c;
}

WoweeCinematic WoweeCinematicLoader::makeIntros(const std::string& catalogName) {
    WoweeCinematic c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t classId,
                    const char* media) {
        WoweeCinematic::Entry e;
        e.cinematicId = id; e.name = name;
        e.description = std::string("First-login intro for the ") +
                         name + " class.";
        e.kind = WoweeCinematic::CameraFlythrough;
        e.triggerKind = WoweeCinematic::ClassStart;
        e.mediaPath = media;
        e.durationSeconds = 45;
        e.skippable = 1;
        // triggerTargetId values match WCHC class IDs
        // (1=Warrior, 3=Hunter, 4=Rogue, 8=Mage).
        e.triggerTargetId = classId;
        c.entries.push_back(e);
    };
    add(100, "Warrior", 1, "Cinematics/intro_warrior.m2");
    add(101, "Hunter",  3, "Cinematics/intro_hunter.m2");
    add(102, "Rogue",   4, "Cinematics/intro_rogue.m2");
    add(103, "Mage",    8, "Cinematics/intro_mage.m2");
    return c;
}

WoweeCinematic WoweeCinematicLoader::makeQuestCinematics(const std::string& catalogName) {
    WoweeCinematic c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint8_t kind, const char* name,
                    uint32_t questId, uint8_t triggerKind,
                    uint32_t durSec) {
        WoweeCinematic::Entry e;
        e.cinematicId = id; e.name = name;
        e.kind = WoweeCinematic::CameraFlythrough;
        e.triggerKind = triggerKind;
        e.mediaPath = std::string("Cinematics/quest_") +
                       std::to_string(questId) + ".m2";
        e.durationSeconds = durSec;
        e.skippable = 1;
        e.triggerTargetId = questId;
        (void)kind;
        c.entries.push_back(e);
    };
    // questIds 1 / 100 / 102 match WQT.makeStarter + makeChain.
    add(200, 0, "Bandit Trouble Intro", 1,
        WoweeCinematic::QuestStart, 25);
    add(201, 0, "Investigate Camp Briefing", 100,
        WoweeCinematic::QuestStart, 30);
    add(202, 0, "Bandit Trouble Resolution", 102,
        WoweeCinematic::QuestEnd, 40);
    return c;
}

} // namespace pipeline
} // namespace wowee
