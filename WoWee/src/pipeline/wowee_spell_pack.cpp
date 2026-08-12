#include "pipeline/wowee_spell_pack.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'P', 'K'};
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

void writeU32Vec(std::ofstream& os,
                  const std::vector<uint32_t>& v) {
    uint32_t n = static_cast<uint32_t>(v.size());
    writePOD(os, n);
    if (n > 0) {
        os.write(reinterpret_cast<const char*>(v.data()),
                 static_cast<std::streamsize>(n * sizeof(uint32_t)));
    }
}

bool readU32Vec(std::ifstream& is, std::vector<uint32_t>& v) {
    uint32_t n = 0;
    if (!readPOD(is, n)) return false;
    if (n > 4096) return false;
    v.resize(n);
    if (n > 0) {
        is.read(reinterpret_cast<char*>(v.data()),
                static_cast<std::streamsize>(n * sizeof(uint32_t)));
        if (is.gcount() !=
            static_cast<std::streamsize>(n * sizeof(uint32_t))) {
            v.clear();
            return false;
        }
    }
    return true;
}

std::string normalizePath(std::string base) {
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wspk") {
        base += ".wspk";
    }
    return base;
}

} // namespace

const WoweeSpellPack::Entry*
WoweeSpellPack::findById(uint32_t packId) const {
    for (const auto& e : entries)
        if (e.packId == packId) return &e;
    return nullptr;
}

const WoweeSpellPack::Entry*
WoweeSpellPack::findByClassTab(uint8_t classId,
                                uint8_t tabIndex) const {
    for (const auto& e : entries)
        if (e.classId == classId && e.tabIndex == tabIndex)
            return &e;
    return nullptr;
}

std::vector<const WoweeSpellPack::Entry*>
WoweeSpellPack::findByClass(uint8_t classId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.classId == classId) out.push_back(&e);
    return out;
}

bool WoweeSpellPackLoader::save(const WoweeSpellPack& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.packId);
        writePOD(os, e.classId);
        writePOD(os, e.tabIndex);
        writePOD(os, e.iconIndex);
        writePOD(os, e.pad0);
        writeStr(os, e.tabName);
        writeU32Vec(os, e.spellIds);
    }
    return os.good();
}

WoweeSpellPack WoweeSpellPackLoader::load(
    const std::string& basePath) {
    WoweeSpellPack out;
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
        if (!readPOD(is, e.packId) ||
            !readPOD(is, e.classId) ||
            !readPOD(is, e.tabIndex) ||
            !readPOD(is, e.iconIndex) ||
            !readPOD(is, e.pad0)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.tabName)) {
            out.entries.clear(); return out;
        }
        if (!readU32Vec(is, e.spellIds)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSpellPackLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

// Helper to build one tab entry. classId follows
// vanilla DBC class IDs: Warrior=1, Mage=8, Rogue=4.
struct TabSpec {
    uint32_t packId;
    uint8_t classId;
    uint8_t tabIndex;
    uint8_t iconIndex;
    const char* tabName;
    std::vector<uint32_t> spellIds;
};

WoweeSpellPack makeFromTabs(const std::string& catalogName,
                              std::vector<TabSpec> tabs) {
    using P = WoweeSpellPack;
    WoweeSpellPack c;
    c.name = catalogName;
    for (auto& t : tabs) {
        P::Entry e;
        e.packId = t.packId;
        e.classId = t.classId;
        e.tabIndex = t.tabIndex;
        e.iconIndex = t.iconIndex;
        e.tabName = t.tabName;
        e.spellIds = std::move(t.spellIds);
        c.entries.push_back(std::move(e));
    }
    return c;
}

} // namespace

WoweeSpellPack WoweeSpellPackLoader::makeWarriorPack(
    const std::string& catalogName) {
    // classId=1 (Warrior). Tab 0=General, 1=Arms,
    // 2=Fury, 3=Protection. SpellIds are canonical
    // vanilla low-rank picks: Charge=100, Heroic
    // Strike=78, Mortal Strike=12294, Bloodthirst=23881,
    // Shield Block=2565, etc.
    return makeFromTabs(catalogName, {
        {1001, 1, 0, 1, "General",
            {78,    // Heroic Strike rank 1
             100,   // Charge rank 1
             6673,  // Battle Shout rank 1
             2457,  // Battle Stance
            }},
        {1002, 1, 1, 30, "Arms",
            {12294, // Mortal Strike
             1680,  // Whirlwind
             7384,  // Overpower
            }},
        {1003, 1, 2, 31, "Fury",
            {23881, // Bloodthirst
             5308,  // Execute
             1719,  // Recklessness
            }},
        {1004, 1, 3, 32, "Protection",
            {2565,  // Shield Block
             871,   // Shield Wall
             355,   // Taunt
            }},
    });
}

WoweeSpellPack WoweeSpellPackLoader::makeMagePack(
    const std::string& catalogName) {
    // classId=8 (Mage). Frost tab includes Frostbolt
    // rank 1 (spellId 116) — the canonical "every
    // mage starts with this" spell.
    return makeFromTabs(catalogName, {
        {2001, 8, 0, 5, "General",
            {133,   // Fireball rank 1
             168,   // Frost Armor rank 1
             1459,  // Arcane Intellect rank 1
            }},
        {2002, 8, 1, 50, "Arcane",
            {1449,  // Arcane Explosion rank 1
             5143,  // Arcane Missiles rank 1
             1953,  // Blink
            }},
        {2003, 8, 2, 51, "Fire",
            {2120,  // Flamestrike rank 1
             11366, // Pyroblast rank 1
             2948,  // Scorch rank 1
            }},
        {2004, 8, 3, 52, "Frost",
            {116,   // Frostbolt rank 1 — every mage
                    //  begins here
             122,   // Frost Nova rank 1
             10,    // Blizzard rank 1
            }},
    });
}

WoweeSpellPack WoweeSpellPackLoader::makeRoguePack(
    const std::string& catalogName) {
    // classId=4 (Rogue). Combat tab seeded with
    // poison-application + lethality picks.
    return makeFromTabs(catalogName, {
        {3001, 4, 0, 7, "General",
            {1752,  // Sinister Strike rank 1
             1784,  // Stealth rank 1
             921,   // Pickpocket
            }},
        {3002, 4, 1, 70, "Assassination",
            {703,   // Garrote rank 1
             8676,  // Ambush rank 1
             2098,  // Eviscerate rank 1
            }},
        {3003, 4, 2, 71, "Combat",
            {2983,  // Sprint rank 1
             1856,  // Vanish rank 1
             8647,  // Expose Armor rank 1
            }},
        {3004, 4, 3, 72, "Subtlety",
            {1857,  // Vanish rank 2
             5277,  // Evasion
             14185, // Preparation
            }},
    });
}

} // namespace pipeline
} // namespace wowee
