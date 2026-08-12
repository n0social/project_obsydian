#include "pipeline/wowee_objects.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'O', 'T'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wgot") {
        base += ".wgot";
    }
    return base;
}

} // namespace

const WoweeGameObject::Entry* WoweeGameObject::findById(uint32_t objectId) const {
    for (const auto& e : entries) {
        if (e.objectId == objectId) return &e;
    }
    return nullptr;
}

const char* WoweeGameObject::typeName(uint8_t t) {
    switch (t) {
        case Door:        return "door";
        case Button:      return "button";
        case Chest:       return "chest";
        case Container:   return "container";
        case QuestGiver:  return "quest-giver";
        case Text:        return "text";
        case Trap:        return "trap";
        case Goober:      return "goober";
        case Transport:   return "transport";
        case Mailbox:     return "mailbox";
        case MineralNode: return "ore-node";
        case HerbNode:    return "herb-node";
        case FishingNode: return "fishing-node";
        case Mount:       return "mount";
        case Sign:        return "sign";
        case Bonfire:     return "bonfire";
        default:          return "unknown";
    }
}

bool WoweeGameObjectLoader::save(const WoweeGameObject& cat,
                                 const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.objectId);
        writePOD(os, e.displayId);
        writeStr(os, e.name);
        writePOD(os, e.typeId);
        uint8_t pad[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad), 3);
        writePOD(os, e.size);
        writeStr(os, e.castBarCaption);
        writePOD(os, e.requiredSkill);
        writePOD(os, e.requiredSkillValue);
        writePOD(os, e.lockId);
        writePOD(os, e.lootTableId);
        writePOD(os, e.minOpenTimeMs);
        writePOD(os, e.maxOpenTimeMs);
        writePOD(os, e.flags);
    }
    return os.good();
}

WoweeGameObject WoweeGameObjectLoader::load(const std::string& basePath) {
    WoweeGameObject out;
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
        if (!readPOD(is, e.objectId) ||
            !readPOD(is, e.displayId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.typeId)) {
            out.entries.clear(); return out;
        }
        uint8_t pad[3];
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.size) ||
            !readStr(is, e.castBarCaption) ||
            !readPOD(is, e.requiredSkill) ||
            !readPOD(is, e.requiredSkillValue) ||
            !readPOD(is, e.lockId) ||
            !readPOD(is, e.lootTableId) ||
            !readPOD(is, e.minOpenTimeMs) ||
            !readPOD(is, e.maxOpenTimeMs) ||
            !readPOD(is, e.flags)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeGameObjectLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeGameObject WoweeGameObjectLoader::makeStarter(const std::string& catalogName) {
    WoweeGameObject c;
    c.name = catalogName;
    {
        WoweeGameObject::Entry e;
        e.objectId = 1; e.displayId = 100;
        e.name = "Wooden Chest"; e.typeId = WoweeGameObject::Chest;
        e.castBarCaption = "Opening...";
        e.lootTableId = 1;     // matches WLOT.makeStarter creatureId
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 2; e.displayId = 110;
        e.name = "Standard Mailbox"; e.typeId = WoweeGameObject::Mailbox;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 3; e.displayId = 120;
        e.name = "Roadside Sign"; e.typeId = WoweeGameObject::Sign;
        c.entries.push_back(e);
    }
    return c;
}

WoweeGameObject WoweeGameObjectLoader::makeDungeon(const std::string& catalogName) {
    WoweeGameObject c;
    c.name = catalogName;
    {
        WoweeGameObject::Entry e;
        e.objectId = 1500; e.displayId = 130;
        e.name = "Iron Door"; e.typeId = WoweeGameObject::Door;
        e.lockId = 1;          // requires key / lockpick (future WLCK)
        e.flags = WoweeGameObject::Frozen;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 1501; e.displayId = 131;
        e.name = "Pressure Plate"; e.typeId = WoweeGameObject::Button;
        e.minOpenTimeMs = 5000; e.maxOpenTimeMs = 10000;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        // objectId = 2000 deliberately matches WLOT.makeBandit
        // creatureId-keyed loot table.
        e.objectId = 2000; e.displayId = 140;
        e.name = "Bandit Strongbox"; e.typeId = WoweeGameObject::Chest;
        e.castBarCaption = "Opening...";
        e.lootTableId = 2000;       // -> WLOT bandit chest table
        e.lockId = 2;               // light lockpick
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 1502; e.displayId = 141;
        e.name = "Boss Treasure Chest"; e.typeId = WoweeGameObject::Chest;
        e.castBarCaption = "Opening...";
        e.lootTableId = 9999;       // -> WLOT boss table
        e.size = 1.5f;              // visibly bigger than regular
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 1503; e.displayId = 150;
        e.name = "Spike Trap"; e.typeId = WoweeGameObject::Trap;
        e.minOpenTimeMs = 1500; e.maxOpenTimeMs = 1500;
        e.flags = WoweeGameObject::ScriptOnly;
        c.entries.push_back(e);
    }
    return c;
}

WoweeGameObject WoweeGameObjectLoader::makeGather(const std::string& catalogName) {
    WoweeGameObject c;
    c.name = catalogName;
    {
        WoweeGameObject::Entry e;
        e.objectId = 3000; e.displayId = 170;
        e.name = "Peacebloom"; e.typeId = WoweeGameObject::HerbNode;
        e.castBarCaption = "Herbalism";
        e.requiredSkill = 182;          // SkillLine herbalism (canonical id)
        e.requiredSkillValue = 1;
        e.flags = WoweeGameObject::Despawn;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 3001; e.displayId = 171;
        e.name = "Tin Vein"; e.typeId = WoweeGameObject::MineralNode;
        e.castBarCaption = "Mining";
        e.requiredSkill = 186;          // SkillLine mining (canonical id)
        e.requiredSkillValue = 65;
        e.flags = WoweeGameObject::Despawn;
        c.entries.push_back(e);
    }
    {
        WoweeGameObject::Entry e;
        e.objectId = 3002; e.displayId = 172;
        e.name = "Schools of Fish"; e.typeId = WoweeGameObject::FishingNode;
        e.castBarCaption = "Fishing";
        e.requiredSkill = 356;          // SkillLine fishing
        e.requiredSkillValue = 1;
        e.flags = WoweeGameObject::Despawn;
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
