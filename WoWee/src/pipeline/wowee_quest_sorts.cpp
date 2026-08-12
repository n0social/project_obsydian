#include "pipeline/wowee_quest_sorts.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'Q', 'S', 'O'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wqso") {
        base += ".wqso";
    }
    return base;
}

} // namespace

const WoweeQuestSort::Entry*
WoweeQuestSort::findById(uint32_t sortId) const {
    for (const auto& e : entries) if (e.sortId == sortId) return &e;
    return nullptr;
}

const char* WoweeQuestSort::sortKindName(uint8_t k) {
    switch (k) {
        case General:    return "general";
        case ClassQuest: return "class";
        case Profession: return "profession";
        case Daily:      return "daily";
        case Holiday:    return "holiday";
        case Reputation: return "reputation";
        case Dungeon:    return "dungeon";
        case Raid:       return "raid";
        case Heroic:     return "heroic";
        case Repeatable: return "repeatable";
        case PvP:        return "pvp";
        case Tournament: return "tournament";
        default:         return "unknown";
    }
}

bool WoweeQuestSortLoader::save(const WoweeQuestSort& cat,
                                 const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.sortId);
        writeStr(os, e.name);
        writeStr(os, e.displayName);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.sortKind);
        writePOD(os, e.displayPriority);
        writePOD(os, e.targetProfessionId);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, e.targetClassMask);
        writePOD(os, e.targetFactionId);
    }
    return os.good();
}

WoweeQuestSort WoweeQuestSortLoader::load(const std::string& basePath) {
    WoweeQuestSort out;
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
        if (!readPOD(is, e.sortId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.displayName) ||
            !readStr(is, e.description) || !readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.sortKind) ||
            !readPOD(is, e.displayPriority) ||
            !readPOD(is, e.targetProfessionId)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.targetClassMask) ||
            !readPOD(is, e.targetFactionId)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeQuestSortLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeQuestSort WoweeQuestSortLoader::makeStarter(
    const std::string& catalogName) {
    WoweeQuestSort c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* display,
                    uint8_t kind, uint8_t prio, const char* desc) {
        WoweeQuestSort::Entry e;
        e.sortId = id; e.name = name; e.displayName = display;
        e.description = desc;
        e.sortKind = kind;
        e.displayPriority = prio;
        e.iconPath = std::string("Interface/Icons/INV_Misc_QuestionMark_") +
                      name + ".blp";
        c.entries.push_back(e);
    };
    add(1, "General",      "General Quests",
        WoweeQuestSort::General,    0,
        "Default catch-all category for area / story quests.");
    add(2, "Daily",        "Daily Quests",
        WoweeQuestSort::Daily,      10,
        "Quests that reset every 24 hours.");
    add(3, "Repeatable",   "Repeatable Quests",
        WoweeQuestSort::Repeatable, 20,
        "Non-daily quests that can be repeated infinitely "
        "(turn-in tokens, faction repeatables).");
    return c;
}

WoweeQuestSort WoweeQuestSortLoader::makeClass(
    const std::string& catalogName) {
    WoweeQuestSort c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* className,
                    uint32_t classBit) {
        WoweeQuestSort::Entry e;
        e.sortId = id;
        e.name = std::string("Class") + className;
        e.displayName = std::string(className) + " Quests";
        e.description = std::string(className) +
                         "-only quest line (trainer / class trial).";
        e.iconPath = std::string("Interface/Icons/Class_") +
                      className + ".blp";
        e.sortKind = WoweeQuestSort::ClassQuest;
        e.displayPriority = 1;
        e.targetClassMask = classBit;
        c.entries.push_back(e);
    };
    // Class bits match WCHC.classId enum.
    add(100, "Warrior",     1u << 1);
    add(101, "Paladin",     1u << 2);
    add(102, "Hunter",      1u << 3);
    add(103, "Rogue",       1u << 4);
    add(104, "Priest",      1u << 5);
    add(105, "DeathKnight", 1u << 6);
    add(106, "Shaman",      1u << 7);
    add(107, "Mage",        1u << 8);
    add(108, "Warlock",     1u << 9);
    add(109, "Druid",       1u << 11);
    return c;
}

WoweeQuestSort WoweeQuestSortLoader::makeProfession(
    const std::string& catalogName) {
    WoweeQuestSort c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* profName,
                    uint8_t professionId) {
        WoweeQuestSort::Entry e;
        e.sortId = id;
        e.name = std::string("Prof") + profName;
        e.displayName = std::string(profName) + " Quests";
        e.description = std::string(profName) +
                         "-specific quest (recipe acquisition, "
                         "trainer reward).";
        e.iconPath = std::string("Interface/Icons/Trade_") +
                      profName + ".blp";
        e.sortKind = WoweeQuestSort::Profession;
        e.displayPriority = 2;
        e.targetProfessionId = professionId;
        c.entries.push_back(e);
    };
    // Profession IDs match WTSK.profession enum.
    add(200, "Blacksmithing",  0);
    add(201, "Tailoring",      1);
    add(202, "Engineering",    2);
    add(203, "Alchemy",        3);
    add(204, "Enchanting",     4);
    add(205, "Leatherworking", 5);
    add(206, "Jewelcrafting",  6);
    add(207, "Inscription",    7);
    return c;
}

} // namespace pipeline
} // namespace wowee
