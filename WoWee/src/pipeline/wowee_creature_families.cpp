#include "pipeline/wowee_creature_families.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'E', 'F'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wcef") {
        base += ".wcef";
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

const WoweeCreatureFamily::Entry*
WoweeCreatureFamily::findById(uint32_t familyId) const {
    for (const auto& e : entries)
        if (e.familyId == familyId) return &e;
    return nullptr;
}

const char* WoweeCreatureFamily::familyKindName(uint8_t k) {
    switch (k) {
        case Beast:     return "beast";
        case Demon:     return "demon";
        case Undead:    return "undead";
        case Elemental: return "elemental";
        case NotPet:    return "not-pet";
        case Exotic:    return "exotic";
        default:        return "unknown";
    }
}

const char* WoweeCreatureFamily::petTalentTreeName(uint8_t t) {
    switch (t) {
        case TreeNone: return "none";
        case Ferocity: return "ferocity";
        case Tenacity: return "tenacity";
        case Cunning:  return "cunning";
        default:       return "unknown";
    }
}

bool WoweeCreatureFamilyLoader::save(const WoweeCreatureFamily& cat,
                                      const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.familyId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.familyKind);
        writePOD(os, e.petTalentTree);
        writePOD(os, e.minLevelForTame);
        writePOD(os, e.pad0);
        writePOD(os, e.skillLine);
        writePOD(os, e.petFoodTypes);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeCreatureFamily WoweeCreatureFamilyLoader::load(
    const std::string& basePath) {
    WoweeCreatureFamily out;
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
        if (!readPOD(is, e.familyId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.familyKind) ||
            !readPOD(is, e.petTalentTree) ||
            !readPOD(is, e.minLevelForTame) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.skillLine) ||
            !readPOD(is, e.petFoodTypes) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeCreatureFamilyLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeCreatureFamily WoweeCreatureFamilyLoader::makeStarter(
    const std::string& catalogName) {
    using F = WoweeCreatureFamily;
    WoweeCreatureFamily c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t tree, uint8_t minLvl, uint32_t skill,
                    uint32_t foods,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        F::Entry e;
        e.familyId = id; e.name = name; e.description = desc;
        e.familyKind = kind;
        e.petTalentTree = tree;
        e.minLevelForTame = minLvl;
        e.skillLine = skill;
        e.petFoodTypes = foods;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "Bear",   F::Beast, F::Tenacity, 10,  208,
        F::Meat | F::Fish | F::Fruit | F::Fungus | F::Raw,
        140, 100,  60, "Bear — tenacity tank pet, omnivore.");
    add(2, "Cat",    F::Beast, F::Ferocity, 10,  209,
        F::Meat | F::Fish | F::Raw,
        220, 180,  60, "Cat — ferocity DPS pet, carnivore.");
    add(3, "Wolf",   F::Beast, F::Ferocity, 10,  210,
        F::Meat | F::Raw,
        180, 180, 180, "Wolf — ferocity DPS pet, meat-only.");
    add(4, "Boar",   F::Beast, F::Tenacity, 10,  211,
        F::Meat | F::Fruit | F::Fungus | F::Bread,
        160, 120, 100, "Boar — tenacity tank pet, ravenous omnivore.");
    add(5, "Crab",   F::Beast, F::Tenacity, 10,  212,
        F::Fish | F::Meat | F::Raw,
        120, 180, 200, "Crab — tenacity tank pet, prefers fish.");
    return c;
}

WoweeCreatureFamily WoweeCreatureFamilyLoader::makeFerocity(
    const std::string& catalogName) {
    using F = WoweeCreatureFamily;
    WoweeCreatureFamily c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t minLvl,
                    uint32_t skill, uint32_t foods,
                    const char* desc) {
        F::Entry e;
        e.familyId = id; e.name = name; e.description = desc;
        e.familyKind = F::Beast;
        e.petTalentTree = F::Ferocity;
        e.minLevelForTame = minLvl;
        e.skillLine = skill;
        e.petFoodTypes = foods;
        e.iconColorRGBA = packRgba(220, 60, 60);   // red — DPS
        c.entries.push_back(e);
    };
    add(100, "Cat",       10, 209, F::Meat | F::Fish | F::Raw,
        "Cat — fast attack speed, claws hit hard.");
    add(101, "Wolf",      10, 210, F::Meat | F::Raw,
        "Wolf — Furious Howl pack buff (10% AP raid-wide).");
    add(102, "Raptor",    10, 213, F::Meat | F::Raw,
        "Raptor — bleed effect on melee strikes.");
    add(103, "Devilsaur", 30, 214, F::Meat | F::Raw,
        "Devilsaur — Monstrous Bite armor reduction.");
    return c;
}

WoweeCreatureFamily WoweeCreatureFamilyLoader::makeExotic(
    const std::string& catalogName) {
    using F = WoweeCreatureFamily;
    WoweeCreatureFamily c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t tree,
                    uint8_t minLvl, uint32_t skill, uint32_t foods,
                    const char* desc) {
        F::Entry e;
        e.familyId = id; e.name = name; e.description = desc;
        e.familyKind = F::Exotic;
        e.petTalentTree = tree;
        e.minLevelForTame = minLvl;
        e.skillLine = skill;
        e.petFoodTypes = foods;
        e.iconColorRGBA = packRgba(200, 100, 240);   // purple — exotic
        c.entries.push_back(e);
    };
    add(200, "Worm",       F::Tenacity, 50, 220,
        F::Meat | F::Fungus | F::Raw,
        "Worm — exotic tenacity, Acid Spit reduces target armor.");
    add(201, "Devilsaur",  F::Ferocity, 60, 214,
        F::Meat | F::Raw,
        "Devilsaur — exotic, Monstrous Bite + huge HP pool.");
    add(202, "Chimaera",   F::Cunning,  60, 221,
        F::Meat | F::Raw,
        "Chimaera — exotic cunning, Froststorm Breath chain frost.");
    add(203, "CoreHound",  F::Ferocity, 60, 222,
        F::Meat | F::Raw,
        "Core Hound — exotic, Lava Breath + Ancient Hysteria "
        "raid bloodlust.");
    return c;
}

} // namespace pipeline
} // namespace wowee
