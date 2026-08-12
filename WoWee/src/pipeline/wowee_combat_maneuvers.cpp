#include "pipeline/wowee_combat_maneuvers.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'M', 'G'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wcmg") {
        base += ".wcmg";
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

const WoweeCombatManeuvers::Entry*
WoweeCombatManeuvers::findById(uint32_t groupId) const {
    for (const auto& e : entries)
        if (e.groupId == groupId) return &e;
    return nullptr;
}

std::vector<const WoweeCombatManeuvers::Entry*>
WoweeCombatManeuvers::findByClass(uint32_t classBit) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.classMask & classBit) out.push_back(&e);
    return out;
}

const WoweeCombatManeuvers::Entry*
WoweeCombatManeuvers::findGroupForSpell(uint32_t spellId) const {
    for (const auto& e : entries) {
        for (uint32_t m : e.members) {
            if (m == spellId) return &e;
        }
    }
    return nullptr;
}

bool WoweeCombatManeuversLoader::save(const WoweeCombatManeuvers& cat,
                                       const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.groupId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.classMask);
        writePOD(os, e.categoryKind);
        writePOD(os, e.exclusive);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.iconColorRGBA);
        uint32_t memberCount = static_cast<uint32_t>(e.members.size());
        writePOD(os, memberCount);
        for (uint32_t m : e.members) writePOD(os, m);
    }
    return os.good();
}

WoweeCombatManeuvers WoweeCombatManeuversLoader::load(
    const std::string& basePath) {
    WoweeCombatManeuvers out;
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
        if (!readPOD(is, e.groupId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.classMask) ||
            !readPOD(is, e.categoryKind) ||
            !readPOD(is, e.exclusive) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
        uint32_t memberCount = 0;
        if (!readPOD(is, memberCount)) {
            out.entries.clear(); return out;
        }
        if (memberCount > (1u << 16)) {
            out.entries.clear(); return out;
        }
        e.members.resize(memberCount);
        for (uint32_t k = 0; k < memberCount; ++k) {
            if (!readPOD(is, e.members[k])) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeCombatManeuversLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeCombatManeuvers WoweeCombatManeuversLoader::makeWarrior(
    const std::string& catalogName) {
    using C = WoweeCombatManeuvers;
    WoweeCombatManeuvers c;
    c.name = catalogName;
    C::Entry e;
    e.groupId = 1;
    e.name = "WarriorStances";
    e.description =
        "Warrior combat stances — only one stance active "
        "at a time. Battle Stance is the leveling default; "
        "Defensive is tank-focused; Berserker enables "
        "high-damage abilities at the cost of armor.";
    e.classMask = 1;             // Warrior
    e.categoryKind = C::Stance;
    e.exclusive = 1;
    e.iconColorRGBA = packRgba(220, 60, 60);   // warrior red
    // WoW 3.3.5a stance spell IDs.
    e.members = {
        2457,    // Battle Stance
        71,      // Defensive Stance
        2458,    // Berserker Stance
    };
    c.entries.push_back(e);
    return c;
}

WoweeCombatManeuvers WoweeCombatManeuversLoader::makeDruid(
    const std::string& catalogName) {
    using C = WoweeCombatManeuvers;
    WoweeCombatManeuvers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t kind, uint8_t excl,
                    std::vector<uint32_t> members,
                    const char* desc) {
        C::Entry e;
        e.groupId = id; e.name = name; e.description = desc;
        e.classMask = 1024;        // Druid
        e.categoryKind = kind;
        e.exclusive = excl;
        e.iconColorRGBA = packRgba(255, 125, 10);   // druid orange
        e.members = std::move(members);
        c.entries.push_back(e);
    };
    add(100, "DruidShapeshiftForms",
        C::Form, 1,
        {
            5487,    // Bear Form
            768,     // Cat Form
            783,     // Travel Form
            33891,   // Tree of Life
            24858,   // Moonkin Form
        },
        "Druid ground shapeshift forms — only one active "
        "at a time. Switching to a new form cancels the "
        "previous and breaks invisible/stealth in many "
        "cases.");
    add(101, "DruidFlightForms",
        C::Form, 1,
        {
            33943,   // Flight Form
            40120,   // Swift Flight Form
        },
        "Druid flight forms — separate mutex bucket from "
        "ground shapeshift forms. Only available in flying "
        "zones (Outland, Northrend, post-Cata old world).");
    return c;
}

WoweeCombatManeuvers WoweeCombatManeuversLoader::makeAllMutex(
    const std::string& catalogName) {
    using C = WoweeCombatManeuvers;
    WoweeCombatManeuvers c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t classBit, uint8_t kind,
                    std::vector<uint32_t> members,
                    uint32_t color, const char* desc) {
        C::Entry e;
        e.groupId = id; e.name = name; e.description = desc;
        e.classMask = classBit;
        e.categoryKind = kind;
        e.exclusive = 1;
        e.iconColorRGBA = color;
        e.members = std::move(members);
        c.entries.push_back(e);
    };
    add(200, "WarriorStances", 1, C::Stance,
        { 2457, 71, 2458 },
        packRgba(220, 60, 60),
        "Warrior 3-stance mutex (Battle / Defensive / "
        "Berserker).");
    add(201, "HunterAspects", 4, C::Aspect,
        {
            13165,    // Aspect of the Hawk
            5118,     // Aspect of the Cheetah
            13159,    // Aspect of the Pack
            34074,    // Aspect of the Viper
            61846,    // Aspect of the Dragonhawk
            13161,    // Aspect of the Beast
            20043,    // Aspect of the Wild
        },
        packRgba(170, 210, 100),
        "Hunter aspect mutex — 7 aspects, only one active "
        "at a time. Dragonhawk added in WotLK; Beast and "
        "Wild present since Vanilla.");
    add(202, "DKPresences", 32, C::Presence,
        {
            48263,    // Blood Presence
            48266,    // Frost Presence (note: shares ID
                       // with Frost spec aura in some
                       // builds)
            48265,    // Unholy Presence
        },
        packRgba(140, 50, 80),
        "Death Knight 3-presence mutex (Blood for tanking, "
        "Frost for haste, Unholy for movement+damage).");
    add(203, "DruidShapeshiftForms", 1024, C::Form,
        { 5487, 768, 783, 33891, 24858 },
        packRgba(255, 125, 10),
        "Druid ground shapeshift mutex (Bear / Cat / "
        "Travel / Tree / Moonkin). Same five spells as the "
        "DruidShapeshiftForms entry in the dedicated Druid "
        "catalog.");
    return c;
}

} // namespace pipeline
} // namespace wowee
