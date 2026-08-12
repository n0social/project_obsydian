#include "pipeline/wowee_macros.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'A', 'C'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wmac") {
        base += ".wmac";
    }
    return base;
}

} // namespace

const WoweeMacro::Entry*
WoweeMacro::findById(uint32_t macroId) const {
    for (const auto& e : entries)
        if (e.macroId == macroId) return &e;
    return nullptr;
}

const char* WoweeMacro::macroKindName(uint8_t k) {
    switch (k) {
        case SystemSlash:    return "system-slash";
        case DefaultMacro:   return "default-macro";
        case PlayerTemplate: return "player-template";
        case GuildMacro:     return "guild-macro";
        case SharedMacro:    return "shared-macro";
        default:             return "unknown";
    }
}

bool WoweeMacroLoader::save(const WoweeMacro& cat,
                            const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.macroId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writeStr(os, e.macroBody);
        writeStr(os, e.bindKey);
        writePOD(os, e.macroKind);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, e.requiredClassMask);
        writePOD(os, e.maxLength);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
    }
    return os.good();
}

WoweeMacro WoweeMacroLoader::load(const std::string& basePath) {
    WoweeMacro out;
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
        if (!readPOD(is, e.macroId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath) || !readStr(is, e.macroBody) ||
            !readStr(is, e.bindKey)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.macroKind)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.requiredClassMask) ||
            !readPOD(is, e.maxLength)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
    }
    return out;
}

bool WoweeMacroLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeMacro WoweeMacroLoader::makeStarter(
    const std::string& catalogName) {
    WoweeMacro c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* body,
                    const char* desc) {
        WoweeMacro::Entry e;
        e.macroId = id; e.name = name;
        e.description = desc; e.macroBody = body;
        e.iconPath = "Interface/Icons/INV_Misc_Note_01.blp";
        e.macroKind = WoweeMacro::SystemSlash;
        c.entries.push_back(e);
    };
    add(1, "Sit",   "/sit",
        "Toggle sitting on the ground.");
    add(2, "Dance", "/dance",
        "Perform race-specific dance emote.");
    add(3, "Target",
        "/target [@mouseover]",
        "Target the unit your mouse is currently hovering over.");
    return c;
}

WoweeMacro WoweeMacroLoader::makeCombat(
    const std::string& catalogName) {
    WoweeMacro c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* body,
                    const char* bindKey, const char* desc) {
        WoweeMacro::Entry e;
        e.macroId = id; e.name = name;
        e.description = desc;
        e.macroBody = body;
        e.bindKey = bindKey;
        e.iconPath = std::string("Interface/Icons/Ability_Warrior_") +
                      name + ".blp";
        e.macroKind = WoweeMacro::PlayerTemplate;
        e.requiredClassMask = 1u << 1;   // WCHC Warrior bit
        c.entries.push_back(e);
    };
    // Multi-line macros encoded with \n inside the body.
    add(100, "HeroicStrikeSpam",
        "#showtooltip Heroic Strike\n"
        "/cast Heroic Strike",
        "1",
        "Single-rank Heroic Strike with #showtooltip — bind to 1.");
    add(101, "Charge",
        "#showtooltip Charge\n"
        "/cast [combat] Intercept; Charge",
        "Q",
        "Auto-switches between Charge and Intercept based on "
        "combat state.");
    add(102, "Intercept",
        "/cast [stance:1/3] Berserker Stance\n"
        "/cast Intercept",
        "T",
        "Stance-dance into Berserker if not already, then Intercept.");
    add(103, "VictoryRush",
        "#showtooltip Victory Rush\n"
        "/cast Victory Rush\n"
        "/cast Bloodthirst",
        "F",
        "Victory Rush with Bloodthirst fallback.");
    return c;
}

WoweeMacro WoweeMacroLoader::makeUtility(
    const std::string& catalogName) {
    WoweeMacro c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* body,
                    const char* desc) {
        WoweeMacro::Entry e;
        e.macroId = id; e.name = name;
        e.description = desc;
        e.macroBody = body;
        e.iconPath = std::string("Interface/Icons/INV_Misc_") +
                      name + ".blp";
        e.macroKind = WoweeMacro::DefaultMacro;
        c.entries.push_back(e);
    };
    add(200, "FollowTarget",
        "/follow [@target]",
        "Follow the unit you have currently targeted.");
    add(201, "MassInvite",
        "/inv %target1\n"
        "/inv %target2\n"
        "/inv %target3\n"
        "/inv %target4",
        "Invite up to four players in one click. Replace "
        "%targetN with player names.");
    add(202, "ReleaseCorpse",
        "/script RepopMe()",
        "Release spirit immediately on death.");
    return c;
}

} // namespace pipeline
} // namespace wowee
