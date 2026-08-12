#include "pipeline/wowee_mounts.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'O', 'U'};
constexpr uint32_t kVersion = 1;
constexpr uint32_t kRidingSkillId = 762;    // canonical SkillLine "Riding"

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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wmou") {
        base += ".wmou";
    }
    return base;
}

} // namespace

const WoweeMount::Entry* WoweeMount::findById(uint32_t mountId) const {
    for (const auto& e : entries) if (e.mountId == mountId) return &e;
    return nullptr;
}

const char* WoweeMount::kindName(uint8_t k) {
    switch (k) {
        case Ground:   return "ground";
        case Flying:   return "flying";
        case Swimming: return "swimming";
        case Hybrid:   return "hybrid";
        case Aquatic:  return "aquatic";
        default:       return "unknown";
    }
}

const char* WoweeMount::factionName(uint8_t f) {
    switch (f) {
        case Both:     return "both";
        case Alliance: return "alliance";
        case Horde:    return "horde";
        default:       return "unknown";
    }
}

const char* WoweeMount::categoryName(uint8_t c) {
    switch (c) {
        case Common:      return "common";
        case Epic:        return "epic";
        case Racial:      return "racial";
        case Event:       return "event";
        case Achievement: return "achievement";
        case Pvp:         return "pvp";
        case Quest:       return "quest";
        case ClassMount:  return "class";
        default:          return "unknown";
    }
}

bool WoweeMountLoader::save(const WoweeMount& cat,
                            const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.mountId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.displayId);
        writePOD(os, e.summonSpellId);
        writePOD(os, e.itemIdToLearn);
        writePOD(os, e.requiredSkillId);
        writePOD(os, e.requiredSkillRank);
        writePOD(os, e.speedPercent);
        writePOD(os, e.mountKind);
        writePOD(os, e.factionId);
        writePOD(os, e.categoryId);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, e.raceMask);
    }
    return os.good();
}

WoweeMount WoweeMountLoader::load(const std::string& basePath) {
    WoweeMount out;
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
        if (!readPOD(is, e.mountId)) { out.entries.clear(); return out; }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.displayId) ||
            !readPOD(is, e.summonSpellId) ||
            !readPOD(is, e.itemIdToLearn) ||
            !readPOD(is, e.requiredSkillId) ||
            !readPOD(is, e.requiredSkillRank) ||
            !readPOD(is, e.speedPercent) ||
            !readPOD(is, e.mountKind) ||
            !readPOD(is, e.factionId) ||
            !readPOD(is, e.categoryId)) {
            out.entries.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.raceMask)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeMountLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeMount WoweeMountLoader::makeStarter(const std::string& catalogName) {
    WoweeMount c;
    c.name = catalogName;
    {
        WoweeMount::Entry e;
        e.mountId = 1; e.name = "Brown Horse";
        e.description = "A common riding horse.";
        e.summonSpellId = 458;          // canonical Apprentice Riding mount
        e.itemIdToLearn = 5656;         // item that teaches it
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = 75;
        e.speedPercent = 60;            // +60% ground speed
        e.mountKind = WoweeMount::Ground;
        e.factionId = WoweeMount::Alliance;
        c.entries.push_back(e);
    }
    {
        WoweeMount::Entry e;
        e.mountId = 2; e.name = "Swift Gryphon";
        e.description = "Faster flying gryphon for journeyman flyers.";
        e.summonSpellId = 32242;
        e.itemIdToLearn = 25470;
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = 225;
        e.speedPercent = 280;           // +280% flying (epic flyer)
        e.mountKind = WoweeMount::Flying;
        e.factionId = WoweeMount::Alliance;
        e.categoryId = WoweeMount::Epic;
        c.entries.push_back(e);
    }
    {
        WoweeMount::Entry e;
        e.mountId = 3; e.name = "Riding Turtle";
        e.description = "A serene ambulatory turtle. Slow but steady.";
        e.summonSpellId = 30174;
        e.itemIdToLearn = 23720;
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = 75;
        e.speedPercent = 60;            // ground-level swimming-style mount
        e.mountKind = WoweeMount::Aquatic;
        c.entries.push_back(e);
    }
    return c;
}

WoweeMount WoweeMountLoader::makeRacial(const std::string& catalogName) {
    WoweeMount c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t fac,
                    uint32_t race, uint32_t spellId,
                    uint32_t itemId, uint16_t rank) {
        WoweeMount::Entry e;
        e.mountId = id; e.name = name;
        e.summonSpellId = spellId; e.itemIdToLearn = itemId;
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = rank;
        e.speedPercent = 60;
        e.mountKind = WoweeMount::Ground;
        e.factionId = fac; e.categoryId = WoweeMount::Racial;
        e.raceMask = race;
        c.entries.push_back(e);
    };
    // Alliance racial mounts.
    add(100, "Pinto", WoweeMount::Alliance, 1u << 0,  470,  2414, 75);   // Human
    add(101, "Brown Ram", WoweeMount::Alliance, 1u << 2, 6648, 5872, 75); // Dwarf
    add(102, "Striped Frostsaber", WoweeMount::Alliance, 1u << 3, 10789, 8629, 75); // NightElf
    add(103, "Grey Mechanostrider", WoweeMount::Alliance, 1u << 6, 17453, 13321, 75); // Gnome
    // Horde racial mounts.
    add(200, "Dire Wolf", WoweeMount::Horde, 1u << 1, 458, 1132, 75);    // Orc (re-uses item)
    add(201, "Skeletal Horse", WoweeMount::Horde, 1u << 4, 17463, 13332, 75); // Undead
    return c;
}

WoweeMount WoweeMountLoader::makeFlying(const std::string& catalogName) {
    WoweeMount c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint16_t speed,
                    uint16_t rankReq, uint8_t cat,
                    uint32_t spellId, uint32_t itemId) {
        WoweeMount::Entry e;
        e.mountId = id; e.name = name;
        e.summonSpellId = spellId; e.itemIdToLearn = itemId;
        e.requiredSkillId = kRidingSkillId;
        e.requiredSkillRank = rankReq;
        e.speedPercent = speed;
        e.mountKind = WoweeMount::Flying;
        e.categoryId = cat;
        c.entries.push_back(e);
    };
    add(300, "Common Hippogryph",     60,  225, WoweeMount::Common,      32235, 25470);
    add(301, "Cenarion War Hippogryph", 100, 225, WoweeMount::Epic,        32240, 25471);
    add(302, "Bronze Drake",         280,  300, WoweeMount::Achievement, 59569, 43951);
    add(303, "Vicious War Wolf",     310,  300, WoweeMount::Pvp,         60424, 44083);
    return c;
}

} // namespace pipeline
} // namespace wowee
