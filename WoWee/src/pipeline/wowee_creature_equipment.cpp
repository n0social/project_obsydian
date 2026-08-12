#include "pipeline/wowee_creature_equipment.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'E', 'Q'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wceq") {
        base += ".wceq";
    }
    return base;
}

} // namespace

const WoweeCreatureEquipment::Entry*
WoweeCreatureEquipment::findById(uint32_t equipmentId) const {
    for (const auto& e : entries)
        if (e.equipmentId == equipmentId) return &e;
    return nullptr;
}

bool WoweeCreatureEquipmentLoader::save(
    const WoweeCreatureEquipment& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.equipmentId);
        writePOD(os, e.creatureId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.mainHandItemId);
        writePOD(os, e.offHandItemId);
        writePOD(os, e.rangedItemId);
        writePOD(os, e.mainHandSlot);
        writePOD(os, e.offHandSlot);
        writePOD(os, e.rangedSlot);
        writePOD(os, e.equipFlags);
        writePOD(os, e.mainHandVisualId);
    }
    return os.good();
}

WoweeCreatureEquipment WoweeCreatureEquipmentLoader::load(
    const std::string& basePath) {
    WoweeCreatureEquipment out;
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
        if (!readPOD(is, e.equipmentId) ||
            !readPOD(is, e.creatureId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.mainHandItemId) ||
            !readPOD(is, e.offHandItemId) ||
            !readPOD(is, e.rangedItemId) ||
            !readPOD(is, e.mainHandSlot) ||
            !readPOD(is, e.offHandSlot) ||
            !readPOD(is, e.rangedSlot) ||
            !readPOD(is, e.equipFlags) ||
            !readPOD(is, e.mainHandVisualId)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeCreatureEquipmentLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeCreatureEquipment WoweeCreatureEquipmentLoader::makeStarter(
    const std::string& catalogName) {
    WoweeCreatureEquipment c;
    c.name = catalogName;
    {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = 1; e.creatureId = 295;   // Stormwind Guard
        e.name = "GuardSwordAndShield";
        e.description = "Stormwind guard — 1H sword + heater shield.";
        e.mainHandItemId = 1909;     // Long Sword
        e.offHandItemId = 2129;      // Heater Shield
        e.equipFlags = WoweeCreatureEquipment::kFlagShieldOffhand;
        c.entries.push_back(e);
    }
    {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = 2; e.creatureId = 1419;  // Hunter trainer
        e.name = "HunterBowAndOffhand";
        e.description = "Bow with quiver — no weapon in offhand.";
        e.mainHandItemId = 2511;     // Worn Shortsword
        e.rangedItemId = 2504;       // Worn Shortbow
        c.entries.push_back(e);
    }
    {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = 3; e.creatureId = 1416;  // Rogue trainer
        e.name = "RogueDualDagger";
        e.description = "Dual-wielding daggers (mainhand + offhand).";
        e.mainHandItemId = 2092;     // Worn Dagger
        e.offHandItemId = 2092;
        e.equipFlags = WoweeCreatureEquipment::kFlagDualWield;
        c.entries.push_back(e);
    }
    return c;
}

WoweeCreatureEquipment WoweeCreatureEquipmentLoader::makeBosses(
    const std::string& catalogName) {
    WoweeCreatureEquipment c;
    c.name = catalogName;
    auto add = [&](uint32_t eid, uint32_t cid, const char* name,
                    uint32_t mainHand, uint32_t offHand,
                    uint32_t ranged, uint8_t flags,
                    uint32_t visualKitId, const char* desc) {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = eid; e.creatureId = cid;
        e.name = name; e.description = desc;
        e.mainHandItemId = mainHand;
        e.offHandItemId = offHand;
        e.rangedItemId = ranged;
        e.equipFlags = flags;
        e.mainHandVisualId = visualKitId;   // WSVK cross-ref
        c.entries.push_back(e);
    };
    // Iconic boss loadouts. visualKitId values reference WSVK
    // entries — non-zero so the brandished weapon plays its
    // signature glow / aura.
    add(100, 11583, "Onyxian2HSword", 17075, 0, 0,
        WoweeCreatureEquipment::kFlagPolearmTwoHand, 100,
        "Onyxia's caster form — 2H greatsword with smoke trail.");
    add(101, 36597, "FrostmournePrime", 49623, 0, 0,
        WoweeCreatureEquipment::kFlagPolearmTwoHand, 101,
        "The Lich King — Frostmourne with frost runes.");
    add(102, 37955, "SylvanasBow", 0, 0, 50613,
        0, 102,
        "Sylvanas Windrunner — bow only, banshee aura.");
    add(103, 22917, "IllidanDualWarglaives", 32837, 32837, 0,
        WoweeCreatureEquipment::kFlagDualWield, 103,
        "Illidan Stormrage — dual warglaives, fel green glow.");
    return c;
}

WoweeCreatureEquipment WoweeCreatureEquipmentLoader::makeRanged(
    const std::string& catalogName) {
    WoweeCreatureEquipment c;
    c.name = catalogName;
    auto add = [&](uint32_t eid, uint32_t cid, const char* name,
                    uint32_t rangedItem, uint8_t flags,
                    const char* desc) {
        WoweeCreatureEquipment::Entry e;
        e.equipmentId = eid; e.creatureId = cid;
        e.name = name; e.description = desc;
        e.rangedItemId = rangedItem;
        e.equipFlags = flags;
        c.entries.push_back(e);
    };
    add(200, 829,  "DwarfRifleman",   2511, 0,
        "Dwarf rifleman — long rifle in ranged slot only.");
    add(201, 1419, "ElvenLongbow",    2504, 0,
        "Night-elf hunter — long bow in ranged slot.");
    add(202, 6791, "TrollCrossbow",   2508, 0,
        "Troll Sentinel — crossbow in ranged slot.");
    return c;
}

} // namespace pipeline
} // namespace wowee
