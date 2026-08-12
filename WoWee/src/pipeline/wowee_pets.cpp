#include "pipeline/wowee_pets.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'E', 'T'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wpet") {
        base += ".wpet";
    }
    return base;
}

} // namespace

const WoweePet::Family* WoweePet::findFamily(uint32_t familyId) const {
    for (const auto& f : families) if (f.familyId == familyId) return &f;
    return nullptr;
}

const WoweePet::Minion* WoweePet::findMinion(uint32_t minionId) const {
    for (const auto& m : minions) if (m.minionId == minionId) return &m;
    return nullptr;
}

const char* WoweePet::petTypeName(uint8_t t) {
    switch (t) {
        case Cunning:  return "cunning";
        case Ferocity: return "ferocity";
        case Tenacity: return "tenacity";
        default:       return "unknown";
    }
}

std::string WoweePet::dietMaskName(uint32_t mask) {
    std::string s;
    if (mask & DietMeat)   s += "meat+";
    if (mask & DietFish)   s += "fish+";
    if (mask & DietBread)  s += "bread+";
    if (mask & DietCheese) s += "cheese+";
    if (mask & DietFruit)  s += "fruit+";
    if (mask & DietFungus) s += "fungus+";
    if (s.empty()) return "-";
    s.pop_back();   // drop trailing '+'
    return s;
}

bool WoweePetLoader::save(const WoweePet& cat,
                          const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t famCount = static_cast<uint32_t>(cat.families.size());
    writePOD(os, famCount);
    for (const auto& f : cat.families) {
        writePOD(os, f.familyId);
        writeStr(os, f.name);
        writeStr(os, f.description);
        writeStr(os, f.iconPath);
        writePOD(os, f.petType);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, f.baseAttackSpeed);
        writePOD(os, f.damageMultiplier);
        writePOD(os, f.armorMultiplier);
        writePOD(os, f.dietMask);
        uint8_t abCount = static_cast<uint8_t>(
            f.abilities.size() > 255 ? 255 : f.abilities.size());
        writePOD(os, abCount);
        os.write(reinterpret_cast<const char*>(pad3), 3);
        for (uint8_t k = 0; k < abCount; ++k) {
            const auto& a = f.abilities[k];
            writePOD(os, a.spellId);
            writePOD(os, a.learnedAtLevel);
            writePOD(os, a.rank);
            uint8_t pad = 0;
            writePOD(os, pad);
        }
    }
    uint32_t minCount = static_cast<uint32_t>(cat.minions.size());
    writePOD(os, minCount);
    for (const auto& m : cat.minions) {
        writePOD(os, m.minionId);
        writeStr(os, m.name);
        writePOD(os, m.summonSpellId);
        writePOD(os, m.creatureId);
        uint8_t abCount = static_cast<uint8_t>(
            m.abilities.size() > 255 ? 255 : m.abilities.size());
        writePOD(os, abCount);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        for (uint8_t k = 0; k < abCount; ++k) {
            const auto& a = m.abilities[k];
            writePOD(os, a.spellId);
            writePOD(os, a.rank);
            writePOD(os, a.autocastDefault);
            uint8_t pad2[2] = {0, 0};
            os.write(reinterpret_cast<const char*>(pad2), 2);
        }
    }
    return os.good();
}

WoweePet WoweePetLoader::load(const std::string& basePath) {
    WoweePet out;
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    if (!is) return out;
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return out;
    uint32_t version = 0;
    if (!readPOD(is, version) || version != kVersion) return out;
    if (!readStr(is, out.name)) return out;
    auto fail = [&]() {
        out.families.clear(); out.minions.clear();
        return out;
    };
    uint32_t famCount = 0;
    if (!readPOD(is, famCount)) return out;
    if (famCount > (1u << 20)) return out;
    out.families.resize(famCount);
    for (auto& f : out.families) {
        if (!readPOD(is, f.familyId)) return fail();
        if (!readStr(is, f.name) || !readStr(is, f.description) ||
            !readStr(is, f.iconPath)) return fail();
        if (!readPOD(is, f.petType)) return fail();
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) return fail();
        if (!readPOD(is, f.baseAttackSpeed) ||
            !readPOD(is, f.damageMultiplier) ||
            !readPOD(is, f.armorMultiplier) ||
            !readPOD(is, f.dietMask)) return fail();
        uint8_t abCount = 0;
        if (!readPOD(is, abCount)) return fail();
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) return fail();
        f.abilities.resize(abCount);
        for (uint8_t k = 0; k < abCount; ++k) {
            auto& a = f.abilities[k];
            if (!readPOD(is, a.spellId) ||
                !readPOD(is, a.learnedAtLevel) ||
                !readPOD(is, a.rank)) return fail();
            uint8_t pad = 0;
            if (!readPOD(is, pad)) return fail();
        }
    }
    uint32_t minCount = 0;
    if (!readPOD(is, minCount)) return fail();
    if (minCount > (1u << 20)) return fail();
    out.minions.resize(minCount);
    for (auto& m : out.minions) {
        if (!readPOD(is, m.minionId)) return fail();
        if (!readStr(is, m.name)) return fail();
        if (!readPOD(is, m.summonSpellId) ||
            !readPOD(is, m.creatureId)) return fail();
        uint8_t abCount = 0;
        if (!readPOD(is, abCount)) return fail();
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) return fail();
        m.abilities.resize(abCount);
        for (uint8_t k = 0; k < abCount; ++k) {
            auto& a = m.abilities[k];
            if (!readPOD(is, a.spellId) ||
                !readPOD(is, a.rank) ||
                !readPOD(is, a.autocastDefault)) return fail();
            uint8_t pad2[2];
            is.read(reinterpret_cast<char*>(pad2), 2);
            if (is.gcount() != 2) return fail();
        }
    }
    return out;
}

bool WoweePetLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweePet WoweePetLoader::makeStarter(const std::string& catalogName) {
    WoweePet c;
    c.name = catalogName;
    {
        // familyId=1 matches WCRT::FamWolf.
        WoweePet::Family f;
        f.familyId = 1; f.name = "Wolf";
        f.description = "Pack hunter; favors meat.";
        f.petType = WoweePet::Ferocity;
        f.dietMask = WoweePet::DietMeat;
        f.abilities.push_back({27050, 1,  1});   // Bite r1
        f.abilities.push_back({27047, 16, 1});   // Furious Howl
        f.abilities.push_back({27049, 30, 1});   // Dash
        c.families.push_back(f);
    }
    {
        // familyId=2 matches WCRT::FamCat.
        WoweePet::Family f;
        f.familyId = 2; f.name = "Cat";
        f.description = "Stealthy hunter; favors meat or fish.";
        f.petType = WoweePet::Ferocity;
        f.dietMask = WoweePet::DietMeat | WoweePet::DietFish;
        f.abilities.push_back({27049, 1,  1});   // Claw r1
        f.abilities.push_back({16827, 12, 1});   // Prowl
        f.abilities.push_back({26064, 24, 1});   // Dash
        c.families.push_back(f);
    }
    {
        WoweePet::Minion m;
        m.minionId = 1; m.name = "Imp";
        m.summonSpellId = 688;        // canonical Summon Imp
        m.creatureId = 416;           // canonical Imp creatureId
        m.abilities.push_back({3110,  1, 1});    // Firebolt r1
        m.abilities.push_back({7813,  1, 0});    // Blood Pact (autocast off)
        c.minions.push_back(m);
    }
    return c;
}

WoweePet WoweePetLoader::makeHunter(const std::string& catalogName) {
    WoweePet c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t type,
                    uint32_t diet) {
        WoweePet::Family f;
        f.familyId = id; f.name = name;
        f.petType = type; f.dietMask = diet;
        c.families.push_back(f);
    };
    // familyId values match WCRT::FamilyId enum (1..9).
    add(1, "Wolf",    WoweePet::Ferocity, WoweePet::DietMeat);
    add(2, "Cat",     WoweePet::Ferocity,
        WoweePet::DietMeat | WoweePet::DietFish);
    add(3, "Bear",    WoweePet::Tenacity,
        WoweePet::DietMeat | WoweePet::DietFish | WoweePet::DietFruit);
    add(4, "Boar",    WoweePet::Tenacity,
        WoweePet::DietMeat | WoweePet::DietFungus | WoweePet::DietBread);
    add(5, "Raptor",  WoweePet::Cunning,  WoweePet::DietMeat);
    add(6, "Hyena",   WoweePet::Cunning,  WoweePet::DietMeat);
    add(7, "Spider",  WoweePet::Cunning,
        WoweePet::DietMeat | WoweePet::DietFungus);
    add(9, "Crab",    WoweePet::Tenacity,
        WoweePet::DietMeat | WoweePet::DietFish);
    return c;
}

WoweePet WoweePetLoader::makeWarlock(const std::string& catalogName) {
    WoweePet c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t summonSpell, uint32_t creatureId) {
        WoweePet::Minion m;
        m.minionId = id; m.name = name;
        m.summonSpellId = summonSpell; m.creatureId = creatureId;
        c.minions.push_back(m);
    };
    add(1, "Imp",        688,    416);
    add(2, "Voidwalker", 697,    1860);
    add(3, "Succubus",   712,    1863);
    add(4, "Felhunter",  691,    417);
    add(5, "Felguard",   30146,  17252);
    return c;
}

} // namespace pipeline
} // namespace wowee
