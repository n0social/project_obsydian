#include "pipeline/wowee_item_sets.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'E', 'T'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wset") {
        base += ".wset";
    }
    return base;
}

} // namespace

const WoweeItemSet::Entry*
WoweeItemSet::findById(uint32_t setId) const {
    for (const auto& e : entries) if (e.setId == setId) return &e;
    return nullptr;
}

bool WoweeItemSetLoader::save(const WoweeItemSet& cat,
                              const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.setId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.pieceCount);
        writePOD(os, e.bonusCount);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.requiredClassMask);
        writePOD(os, e.requiredSkillId);
        writePOD(os, e.requiredSkillRank);
        for (size_t k = 0; k < WoweeItemSet::kMaxPieces; ++k) {
            writePOD(os, e.itemIds[k]);
        }
        for (size_t k = 0; k < WoweeItemSet::kMaxBonuses; ++k) {
            writePOD(os, e.bonusThresholds[k]);
        }
        for (size_t k = 0; k < WoweeItemSet::kMaxBonuses; ++k) {
            writePOD(os, e.bonusSpellIds[k]);
        }
    }
    return os.good();
}

WoweeItemSet WoweeItemSetLoader::load(const std::string& basePath) {
    WoweeItemSet out;
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
        if (!readPOD(is, e.setId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.pieceCount) ||
            !readPOD(is, e.bonusCount)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.requiredClassMask) ||
            !readPOD(is, e.requiredSkillId) ||
            !readPOD(is, e.requiredSkillRank)) {
            out.entries.clear(); return out;
        }
        for (size_t k = 0; k < WoweeItemSet::kMaxPieces; ++k) {
            if (!readPOD(is, e.itemIds[k])) {
                out.entries.clear(); return out;
            }
        }
        for (size_t k = 0; k < WoweeItemSet::kMaxBonuses; ++k) {
            if (!readPOD(is, e.bonusThresholds[k])) {
                out.entries.clear(); return out;
            }
        }
        for (size_t k = 0; k < WoweeItemSet::kMaxBonuses; ++k) {
            if (!readPOD(is, e.bonusSpellIds[k])) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeItemSetLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeItemSet WoweeItemSetLoader::makeStarter(
    const std::string& catalogName) {
    WoweeItemSet c;
    c.name = catalogName;
    {
        // Battlegear of Wrath — Warrior tier-2 (8 pieces).
        // Real WoW item / spell IDs from the canonical set.
        WoweeItemSet::Entry e;
        e.setId = 1; e.name = "Battlegear of Wrath";
        e.description = "Warrior tier-2 plate set from Blackwing Lair "
                         "and Molten Core.";
        e.pieceCount = 8;
        e.requiredClassMask = WoweeItemSet::kClassWarrior;
        // 16959..16966 are the canonical 8 piece IDs.
        e.itemIds[0] = 16959; e.itemIds[1] = 16960;
        e.itemIds[2] = 16961; e.itemIds[3] = 16962;
        e.itemIds[4] = 16963; e.itemIds[5] = 16964;
        e.itemIds[6] = 16965; e.itemIds[7] = 16966;
        // 3-piece, 5-piece, 8-piece set bonuses.
        e.bonusCount = 3;
        e.bonusThresholds[0] = 3; e.bonusSpellIds[0] = 23687;
        e.bonusThresholds[1] = 5; e.bonusSpellIds[1] = 23689;
        e.bonusThresholds[2] = 8; e.bonusSpellIds[2] = 23690;
        c.entries.push_back(e);
    }
    {
        // Stormrage Raiment — Druid tier-2 (8 pieces).
        WoweeItemSet::Entry e;
        e.setId = 2; e.name = "Stormrage Raiment";
        e.description = "Druid tier-2 leather set — Onyxia / BWL.";
        e.pieceCount = 8;
        e.requiredClassMask = WoweeItemSet::kClassDruid;
        e.itemIds[0] = 16897; e.itemIds[1] = 16898;
        e.itemIds[2] = 16899; e.itemIds[3] = 16900;
        e.itemIds[4] = 16901; e.itemIds[5] = 16902;
        e.itemIds[6] = 16903; e.itemIds[7] = 16904;
        e.bonusCount = 3;
        e.bonusThresholds[0] = 3; e.bonusSpellIds[0] = 23734;
        e.bonusThresholds[1] = 5; e.bonusSpellIds[1] = 23737;
        e.bonusThresholds[2] = 8; e.bonusSpellIds[2] = 23738;
        c.entries.push_back(e);
    }
    return c;
}

WoweeItemSet WoweeItemSetLoader::makeTier(
    const std::string& catalogName) {
    WoweeItemSet c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t classMask,
                    uint32_t baseItemId, uint32_t bonusSpell2,
                    uint32_t bonusSpell4, uint32_t bonusSpell6,
                    const char* desc) {
        WoweeItemSet::Entry e;
        e.setId = id; e.name = name; e.description = desc;
        e.requiredClassMask = classMask;
        e.pieceCount = 8;
        for (size_t k = 0; k < 8; ++k) {
            e.itemIds[k] = baseItemId + static_cast<uint32_t>(k);
        }
        // Standard 2 / 4 / 6-piece progression.
        e.bonusCount = 3;
        e.bonusThresholds[0] = 2; e.bonusSpellIds[0] = bonusSpell2;
        e.bonusThresholds[1] = 4; e.bonusSpellIds[1] = bonusSpell4;
        e.bonusThresholds[2] = 6; e.bonusSpellIds[2] = bonusSpell6;
        c.entries.push_back(e);
    };
    add(100, "Tier1Warrior",  WoweeItemSet::kClassWarrior,
        16451, 26460, 26461, 26462,
        "Warrior tier-1 plate (Molten Core).");
    add(101, "Tier1Mage",     WoweeItemSet::kClassMage,
        16811, 26467, 26468, 26469,
        "Mage tier-1 cloth (Molten Core).");
    add(102, "Tier1Rogue",    WoweeItemSet::kClassRogue,
        16723, 26482, 26483, 26484,
        "Rogue tier-1 leather (Molten Core).");
    add(103, "Tier1Paladin",  WoweeItemSet::kClassPaladin,
        16927, 26471, 26472, 26473,
        "Paladin tier-1 holy plate (Molten Core).");
    return c;
}

WoweeItemSet WoweeItemSetLoader::makePvP(
    const std::string& catalogName) {
    WoweeItemSet c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t classMask,
                    uint16_t skillId, uint16_t skillRank,
                    uint32_t baseItemId, uint32_t bonus2,
                    uint32_t bonus4, const char* desc) {
        WoweeItemSet::Entry e;
        e.setId = id; e.name = name; e.description = desc;
        e.requiredClassMask = classMask;
        e.requiredSkillId = skillId;
        e.requiredSkillRank = skillRank;
        // PvP sets typically have 5 pieces (head/shoulder/chest
        // /legs/gloves) — leave slots 5-7 empty.
        e.pieceCount = 5;
        for (size_t k = 0; k < 5; ++k) {
            e.itemIds[k] = baseItemId + static_cast<uint32_t>(k);
        }
        // 2-piece + 4-piece bonuses.
        e.bonusCount = 2;
        e.bonusThresholds[0] = 2; e.bonusSpellIds[0] = bonus2;
        e.bonusThresholds[1] = 4; e.bonusSpellIds[1] = bonus4;
        c.entries.push_back(e);
    };
    // skillId 162 = Unarmed; here we use a hypothetical
    // PvP-ranking skill check (1850 honor / 5000 honor).
    // requiredSkillRank values represent honor thresholds.
    add(200, "GladiatorVindication",
        WoweeItemSet::kClassPlate, 599, 1850, 41868, 35114, 35116,
        "Gladiator's plate set — requires 1850 honor rank.");
    add(201, "Doomcaller",
        WoweeItemSet::kClassMage,  599, 1850, 41888, 35124, 35126,
        "Mage doomcaller PvP set — requires 1850 honor rank.");
    add(202, "Predatory",
        WoweeItemSet::kClassRogue, 599, 1500, 41878, 35134, 35136,
        "Rogue predatory PvP set — requires 1500 honor rank.");
    return c;
}

} // namespace pipeline
} // namespace wowee
