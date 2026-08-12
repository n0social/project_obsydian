#include "pipeline/wowee_creatures.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'R', 'T'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wcrt") {
        base += ".wcrt";
    }
    return base;
}

} // namespace

const WoweeCreature::Entry* WoweeCreature::findById(uint32_t creatureId) const {
    for (const auto& e : entries) {
        if (e.creatureId == creatureId) return &e;
    }
    return nullptr;
}

const char* WoweeCreature::typeName(uint8_t t) {
    switch (t) {
        case Beast:      return "beast";
        case Dragon:     return "dragon";
        case Demon:      return "demon";
        case Elemental:  return "elemental";
        case Giant:      return "giant";
        case Undead:     return "undead";
        case Humanoid:   return "humanoid";
        case Critter:    return "critter";
        case Mechanical: return "mechanical";
        default:         return "unknown";
    }
}

const char* WoweeCreature::familyName(uint8_t f) {
    switch (f) {
        case FamNone:    return "-";
        case FamWolf:    return "wolf";
        case FamCat:     return "cat";
        case FamBear:    return "bear";
        case FamBoar:    return "boar";
        case FamRaptor:  return "raptor";
        case FamHyena:   return "hyena";
        case FamSpider:  return "spider";
        case FamGorilla: return "gorilla";
        case FamCrab:    return "crab";
        default:         return "?";
    }
}

bool WoweeCreatureLoader::save(const WoweeCreature& cat,
                               const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.creatureId);
        writePOD(os, e.displayId);
        writeStr(os, e.name);
        writeStr(os, e.subname);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.baseHealth);
        writePOD(os, e.healthPerLevel);
        writePOD(os, e.baseMana);
        writePOD(os, e.manaPerLevel);
        writePOD(os, e.factionId);
        writePOD(os, e.npcFlags);
        writePOD(os, e.typeId);
        writePOD(os, e.familyId);
        uint8_t pad[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad), 2);
        writePOD(os, e.damageMin);
        writePOD(os, e.damageMax);
        writePOD(os, e.attackSpeedMs);
        writePOD(os, e.baseArmor);
        writePOD(os, e.walkSpeed);
        writePOD(os, e.runSpeed);
        writePOD(os, e.gossipId);
        writePOD(os, e.equippedMain);
        writePOD(os, e.equippedOffhand);
        writePOD(os, e.equippedRanged);
        writePOD(os, e.aiFlags);
    }
    return os.good();
}

WoweeCreature WoweeCreatureLoader::load(const std::string& basePath) {
    WoweeCreature out;
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
        if (!readPOD(is, e.creatureId) ||
            !readPOD(is, e.displayId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.subname)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.baseHealth) ||
            !readPOD(is, e.healthPerLevel) ||
            !readPOD(is, e.baseMana) ||
            !readPOD(is, e.manaPerLevel) ||
            !readPOD(is, e.factionId) ||
            !readPOD(is, e.npcFlags) ||
            !readPOD(is, e.typeId) ||
            !readPOD(is, e.familyId)) {
            out.entries.clear(); return out;
        }
        uint8_t pad[2];
        is.read(reinterpret_cast<char*>(pad), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.damageMin) ||
            !readPOD(is, e.damageMax) ||
            !readPOD(is, e.attackSpeedMs) ||
            !readPOD(is, e.baseArmor) ||
            !readPOD(is, e.walkSpeed) ||
            !readPOD(is, e.runSpeed) ||
            !readPOD(is, e.gossipId) ||
            !readPOD(is, e.equippedMain) ||
            !readPOD(is, e.equippedOffhand) ||
            !readPOD(is, e.equippedRanged) ||
            !readPOD(is, e.aiFlags)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeCreatureLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeCreature WoweeCreatureLoader::makeStarter(const std::string& catalogName) {
    WoweeCreature c;
    c.name = catalogName;
    {
        WoweeCreature::Entry e;
        e.creatureId = 4001; e.displayId = 50;
        e.name = "Bartleby"; e.subname = "Innkeeper";
        e.minLevel = 30; e.maxLevel = 30;
        e.baseHealth = 1500; e.healthPerLevel = 0;
        e.factionId = 35;
        e.npcFlags = WoweeCreature::Innkeeper |
                      WoweeCreature::Vendor |
                      WoweeCreature::Repair;
        e.typeId = WoweeCreature::Humanoid;
        e.aiFlags = WoweeCreature::AiPassive;
        c.entries.push_back(e);
    }
    return c;
}

WoweeCreature WoweeCreatureLoader::makeBandit(const std::string& catalogName) {
    WoweeCreature c;
    c.name = catalogName;
    {
        WoweeCreature::Entry e;
        // creatureId = 1000 deliberately matches WSPN.makeCamp
        // and WLOT.makeBandit so a content pack already wires
        // together end-to-end.
        e.creatureId = 1000; e.displayId = 1024;
        e.name = "Defias Bandit"; e.subname = "";
        e.minLevel = 5; e.maxLevel = 7;
        e.baseHealth = 80; e.healthPerLevel = 12;
        e.factionId = 14;     // hostile
        e.typeId = WoweeCreature::Humanoid;
        e.damageMin = 4; e.damageMax = 9;
        e.attackSpeedMs = 1800;
        e.baseArmor = 50;
        e.runSpeed = 1.14f;
        // Equipped weapon = WIT itemId 1001 (apprentice sword
        // from makeWeapons preset).
        e.equippedMain = 1001;
        e.aiFlags = WoweeCreature::AiAggressive |
                     WoweeCreature::AiCallHelp |
                     WoweeCreature::AiFleeLowHp;
        c.entries.push_back(e);
    }
    return c;
}

WoweeCreature WoweeCreatureLoader::makeMerchants(const std::string& catalogName) {
    WoweeCreature c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t disp, const char* name,
                   const char* sub, uint32_t flags) {
        WoweeCreature::Entry e;
        e.creatureId = id; e.displayId = disp;
        e.name = name; e.subname = sub;
        e.minLevel = 30; e.maxLevel = 30;
        e.baseHealth = 1500; e.factionId = 35;
        e.npcFlags = flags;
        e.typeId = WoweeCreature::Humanoid;
        e.aiFlags = WoweeCreature::AiPassive;
        c.entries.push_back(e);
    };
    // creatureIds 4001/4002/4003 deliberately match the WSPN
    // makeVillage labels (innkeeper / smith / alchemist).
    add(4001, 50, "Mayle Crassell", "Innkeeper",
        WoweeCreature::Innkeeper | WoweeCreature::Vendor |
        WoweeCreature::Repair);
    add(4002, 51, "Hank Steelarm", "Blacksmith",
        WoweeCreature::Vendor | WoweeCreature::Repair |
        WoweeCreature::Trainer);
    add(4003, 52, "Sera Goldroot", "Alchemist",
        WoweeCreature::Vendor | WoweeCreature::Trainer);
    return c;
}

} // namespace pipeline
} // namespace wowee
