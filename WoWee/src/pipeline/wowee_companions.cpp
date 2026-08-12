#include "pipeline/wowee_companions.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'M', 'P'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wcmp") {
        base += ".wcmp";
    }
    return base;
}

} // namespace

const WoweeCompanion::Entry*
WoweeCompanion::findById(uint32_t companionId) const {
    for (const auto& e : entries)
        if (e.companionId == companionId) return &e;
    return nullptr;
}

const char* WoweeCompanion::companionKindName(uint8_t k) {
    switch (k) {
        case Critter:         return "critter";
        case Mechanical:      return "mechanical";
        case DragonHatchling: return "dragon";
        case Demonic:         return "demonic";
        case Spectral:        return "spectral";
        case Elemental:       return "elemental";
        case Plush:           return "plush";
        case UndeadCritter:   return "undead";
        default:              return "unknown";
    }
}

const char* WoweeCompanion::rarityName(uint8_t r) {
    switch (r) {
        case Common:   return "common";
        case Uncommon: return "uncommon";
        case Rare:     return "rare";
        case Epic:     return "epic";
        default:       return "unknown";
    }
}

const char* WoweeCompanion::factionRestrictionName(uint8_t f) {
    switch (f) {
        case AnyFaction:   return "any";
        case AllianceOnly: return "alliance";
        case HordeOnly:    return "horde";
        default:           return "unknown";
    }
}

bool WoweeCompanionLoader::save(const WoweeCompanion& cat,
                                 const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.companionId);
        writePOD(os, e.creatureId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.companionKind);
        writePOD(os, e.rarity);
        writePOD(os, e.factionRestriction);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, e.learnSpellId);
        writePOD(os, e.itemId);
        writePOD(os, e.idleSoundId);
    }
    return os.good();
}

WoweeCompanion WoweeCompanionLoader::load(const std::string& basePath) {
    WoweeCompanion out;
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
        if (!readPOD(is, e.companionId) ||
            !readPOD(is, e.creatureId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.companionKind) ||
            !readPOD(is, e.rarity) ||
            !readPOD(is, e.factionRestriction)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.learnSpellId) ||
            !readPOD(is, e.itemId) ||
            !readPOD(is, e.idleSoundId)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeCompanionLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeCompanion WoweeCompanionLoader::makeStarter(
    const std::string& catalogName) {
    WoweeCompanion c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t creature, const char* name,
                    uint32_t spellId, uint32_t itemId,
                    uint8_t kind, const char* desc) {
        WoweeCompanion::Entry e;
        e.companionId = id; e.creatureId = creature;
        e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Inv_Pet_") +
                      name + ".blp";
        e.companionKind = kind;
        e.rarity = WoweeCompanion::Common;
        e.learnSpellId = spellId;
        e.itemId = itemId;
        c.entries.push_back(e);
    };
    add(1, 7560, "MechanicalSquirrel", 4055, 4401,
        WoweeCompanion::Mechanical,
        "Engineering-built mechanical squirrel — clicks "
        "and chitters as it follows.");
    add(2, 7349, "Cat",                10684, 8491,
        WoweeCompanion::Critter,
        "Generic alley cat — purrs when stationary.");
    add(3, 7547, "PrairieDog",         9484, 7560,
        WoweeCompanion::Critter,
        "Tan prairie dog — pops up to look around "
        "every few seconds.");
    return c;
}

WoweeCompanion WoweeCompanionLoader::makeRare(
    const std::string& catalogName) {
    WoweeCompanion c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t creature, const char* name,
                    uint32_t spellId, uint32_t itemId,
                    uint8_t kind, uint8_t rarity, const char* desc) {
        WoweeCompanion::Entry e;
        e.companionId = id; e.creatureId = creature;
        e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Inv_Pet_") +
                      name + ".blp";
        e.companionKind = kind;
        e.rarity = rarity;
        e.learnSpellId = spellId;
        e.itemId = itemId;
        c.entries.push_back(e);
    };
    add(100, 11325, "MiniDiablo",   23147, 18639,
        WoweeCompanion::Demonic, WoweeCompanion::Epic,
        "Promotional Diablo II tie-in pet.");
    add(101, 11326, "PandaCub",     23163, 18646,
        WoweeCompanion::Critter, WoweeCompanion::Epic,
        "Collector's edition panda cub.");
    add(102, 11327, "Zergling",     23161, 18647,
        WoweeCompanion::Critter, WoweeCompanion::Epic,
        "Promotional StarCraft tie-in pet.");
    add(103, 16599, "Murky",        25746, 21337,
        WoweeCompanion::Critter, WoweeCompanion::Epic,
        "Blizzcon 2005 promotional baby murloc.");
    return c;
}

WoweeCompanion WoweeCompanionLoader::makeFaction(
    const std::string& catalogName) {
    WoweeCompanion c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t creature, const char* name,
                    uint32_t spellId, uint32_t itemId,
                    uint8_t kind, uint8_t faction, const char* desc) {
        WoweeCompanion::Entry e;
        e.companionId = id; e.creatureId = creature;
        e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Inv_Pet_") +
                      name + ".blp";
        e.companionKind = kind;
        e.rarity = WoweeCompanion::Uncommon;
        e.factionRestriction = faction;
        e.learnSpellId = spellId;
        e.itemId = itemId;
        c.entries.push_back(e);
    };
    add(200, 17254, "AllianceLionCub",   29726, 23713,
        WoweeCompanion::Critter, WoweeCompanion::AllianceOnly,
        "Stormwind orphan-week reward — Alliance only.");
    add(201, 17255, "HordeMottledBoar",  29727, 23714,
        WoweeCompanion::Critter, WoweeCompanion::HordeOnly,
        "Orgrimmar orphan-week reward — Horde only.");
    add(202, 33272, "ArgentSquire",      54187, 39286,
        WoweeCompanion::Critter, WoweeCompanion::AnyFaction,
        "Argent Tournament squire — any faction may purchase.");
    return c;
}

} // namespace pipeline
} // namespace wowee
