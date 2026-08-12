#include "pipeline/wowee_keybindings.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'K', 'B', 'D'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wkbd") {
        base += ".wkbd";
    }
    return base;
}

} // namespace

const WoweeKeyBinding::Entry*
WoweeKeyBinding::findById(uint32_t bindingId) const {
    for (const auto& e : entries)
        if (e.bindingId == bindingId) return &e;
    return nullptr;
}

const WoweeKeyBinding::Entry*
WoweeKeyBinding::findByActionName(const std::string& actionName) const {
    for (const auto& e : entries)
        if (e.actionName == actionName) return &e;
    return nullptr;
}

const char* WoweeKeyBinding::categoryName(uint8_t c) {
    switch (c) {
        case Movement:   return "movement";
        case Combat:     return "combat";
        case Targeting:  return "targeting";
        case Camera:     return "camera";
        case UIPanels:   return "ui-panels";
        case Chat:       return "chat";
        case Macro:      return "macro";
        case Bar:        return "bar";
        case Other:      return "other";
        default:         return "unknown";
    }
}

bool WoweeKeyBindingLoader::save(const WoweeKeyBinding& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.bindingId);
        writeStr(os, e.actionName);
        writeStr(os, e.description);
        writeStr(os, e.defaultKey);
        writeStr(os, e.alternateKey);
        writePOD(os, e.category);
        writePOD(os, e.isUserOverridable);
        writePOD(os, e.sortOrder);
        uint8_t pad = 0;
        writePOD(os, pad);
    }
    return os.good();
}

WoweeKeyBinding WoweeKeyBindingLoader::load(const std::string& basePath) {
    WoweeKeyBinding out;
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
        if (!readPOD(is, e.bindingId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.actionName) ||
            !readStr(is, e.description) ||
            !readStr(is, e.defaultKey) ||
            !readStr(is, e.alternateKey)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.category) ||
            !readPOD(is, e.isUserOverridable) ||
            !readPOD(is, e.sortOrder)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) { out.entries.clear(); return out; }
    }
    return out;
}

bool WoweeKeyBindingLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeKeyBinding WoweeKeyBindingLoader::makeStarter(
    const std::string& catalogName) {
    WoweeKeyBinding c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* action, const char* key,
                    const char* desc, uint8_t cat, uint8_t sort) {
        WoweeKeyBinding::Entry e;
        e.bindingId = id; e.actionName = action;
        e.defaultKey = key; e.description = desc;
        e.category = cat; e.sortOrder = sort;
        c.entries.push_back(e);
    };
    add(1, "MOVE_FORWARD",         "W",
        "Move character forward.",
        WoweeKeyBinding::Movement, 0);
    add(2, "TARGET_NEAREST_ENEMY", "TAB",
        "Cycle to the next enemy in front of you.",
        WoweeKeyBinding::Targeting, 0);
    add(3, "TOGGLE_CHARACTER",     "C",
        "Open / close the character pane.",
        WoweeKeyBinding::UIPanels, 0);
    return c;
}

WoweeKeyBinding WoweeKeyBindingLoader::makeMovement(
    const std::string& catalogName) {
    WoweeKeyBinding c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* action, const char* key,
                    const char* alt, const char* desc, uint8_t sort) {
        WoweeKeyBinding::Entry e;
        e.bindingId = id; e.actionName = action;
        e.defaultKey = key; e.alternateKey = alt;
        e.description = desc;
        e.category = WoweeKeyBinding::Movement;
        e.sortOrder = sort;
        c.entries.push_back(e);
    };
    add(10, "MOVE_FORWARD",   "W",   "UP",
        "Move forward.", 0);
    add(11, "MOVE_BACKWARD",  "S",   "DOWN",
        "Move backward.", 1);
    add(12, "STRAFE_LEFT",    "A",   "",
        "Strafe left (sidestep).", 2);
    add(13, "STRAFE_RIGHT",   "D",   "",
        "Strafe right (sidestep).", 3);
    add(14, "TURN_LEFT",      "LEFT", "Q",
        "Turn the character left.", 4);
    add(15, "TURN_RIGHT",     "RIGHT", "E",
        "Turn the character right.", 5);
    add(16, "JUMP",           "SPACE", "",
        "Jump up.", 6);
    add(17, "TOGGLE_AUTORUN", "NUMLOCK", "",
        "Toggle continuous forward movement.", 7);
    return c;
}

WoweeKeyBinding WoweeKeyBindingLoader::makeUIPanels(
    const std::string& catalogName) {
    WoweeKeyBinding c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* action, const char* key,
                    const char* desc, uint8_t sort) {
        WoweeKeyBinding::Entry e;
        e.bindingId = id; e.actionName = action;
        e.defaultKey = key; e.description = desc;
        e.category = WoweeKeyBinding::UIPanels;
        e.sortOrder = sort;
        c.entries.push_back(e);
    };
    add(100, "TOGGLE_CHARACTER",  "C",
        "Open / close character pane.", 0);
    add(101, "TOGGLE_INVENTORY",  "I",
        "Open / close inventory.", 1);
    add(102, "TOGGLE_BAGS",       "B",
        "Open / close bags (alternative).", 2);
    add(103, "TOGGLE_SPELLBOOK",  "P",
        "Open / close spellbook.", 3);
    add(104, "TOGGLE_TALENTS",    "N",
        "Open / close talent tree.", 4);
    add(105, "TOGGLE_QUEST_LOG",  "L",
        "Open / close quest log.", 5);
    add(106, "TOGGLE_FRIENDS",    "O",
        "Open / close social pane (friends + guild).", 6);
    add(107, "TOGGLE_GUILD",      "J",
        "Open / close guild pane.", 7);
    add(108, "TOGGLE_MAIN_MENU",  "ESCAPE",
        "Open main menu (system + logout).", 8);
    add(109, "TOGGLE_CALENDAR",   "K",
        "Open / close in-game calendar.", 9);
    return c;
}

} // namespace pipeline
} // namespace wowee
