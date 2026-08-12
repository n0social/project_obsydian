#include "pipeline/wowee_trade_skills.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'S', 'K'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wtsk") {
        base += ".wtsk";
    }
    return base;
}

} // namespace

const WoweeTradeSkill::Entry*
WoweeTradeSkill::findById(uint32_t recipeId) const {
    for (const auto& e : entries)
        if (e.recipeId == recipeId) return &e;
    return nullptr;
}

const char* WoweeTradeSkill::professionName(uint8_t p) {
    switch (p) {
        case Blacksmithing:  return "blacksmithing";
        case Tailoring:      return "tailoring";
        case Engineering:    return "engineering";
        case Alchemy:        return "alchemy";
        case Enchanting:     return "enchanting";
        case Leatherworking: return "leatherworking";
        case Jewelcrafting:  return "jewelcrafting";
        case Inscription:    return "inscription";
        case Mining:         return "mining";
        case Skinning:       return "skinning";
        case Herbalism:      return "herbalism";
        case Cooking:        return "cooking";
        case FirstAid:       return "first-aid";
        case Fishing:        return "fishing";
        default:             return "unknown";
    }
}

bool WoweeTradeSkillLoader::save(const WoweeTradeSkill& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.recipeId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.profession);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, e.skillId);
        writePOD(os, e.orangeRank);
        writePOD(os, e.yellowRank);
        writePOD(os, e.greenRank);
        writePOD(os, e.grayRank);
        writePOD(os, e.craftSpellId);
        writePOD(os, e.producedItemId);
        writePOD(os, e.producedMinCount);
        writePOD(os, e.producedMaxCount);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.toolItemId);
        for (size_t k = 0; k < WoweeTradeSkill::kMaxReagents; ++k) {
            writePOD(os, e.reagentItemId[k]);
        }
        for (size_t k = 0; k < WoweeTradeSkill::kMaxReagents; ++k) {
            writePOD(os, e.reagentCount[k]);
        }
    }
    return os.good();
}

WoweeTradeSkill WoweeTradeSkillLoader::load(const std::string& basePath) {
    WoweeTradeSkill out;
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
        if (!readPOD(is, e.recipeId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.profession)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.skillId) ||
            !readPOD(is, e.orangeRank) ||
            !readPOD(is, e.yellowRank) ||
            !readPOD(is, e.greenRank) ||
            !readPOD(is, e.grayRank) ||
            !readPOD(is, e.craftSpellId) ||
            !readPOD(is, e.producedItemId) ||
            !readPOD(is, e.producedMinCount) ||
            !readPOD(is, e.producedMaxCount)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.toolItemId)) {
            out.entries.clear(); return out;
        }
        for (size_t k = 0; k < WoweeTradeSkill::kMaxReagents; ++k) {
            if (!readPOD(is, e.reagentItemId[k])) {
                out.entries.clear(); return out;
            }
        }
        for (size_t k = 0; k < WoweeTradeSkill::kMaxReagents; ++k) {
            if (!readPOD(is, e.reagentCount[k])) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeTradeSkillLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeTradeSkill WoweeTradeSkillLoader::makeStarter(
    const std::string& catalogName) {
    WoweeTradeSkill c;
    c.name = catalogName;
    {
        // Coarse Sharpening Stone — Blacksmithing 75.
        WoweeTradeSkill::Entry e;
        e.recipeId = 1; e.name = "Coarse Sharpening Stone";
        e.description = "Use stone on a weapon to apply +2 damage "
                         "for 30 minutes.";
        e.iconPath = "Interface/Icons/Inv_Stone_Sharpening_03.blp";
        e.profession = WoweeTradeSkill::Blacksmithing;
        e.skillId = 164;             // WSKL Blacksmithing skillId
        e.orangeRank = 75; e.yellowRank = 95;
        e.greenRank = 115; e.grayRank = 135;
        e.craftSpellId = 3326;       // canonical craft spellId
        e.producedItemId = 2862;     // canonical item
        e.producedMinCount = 1; e.producedMaxCount = 1;
        e.toolItemId = 5956;         // Blacksmith Hammer
        e.reagentItemId[0] = 2836;   // Coarse Stone
        e.reagentCount[0] = 1;
        c.entries.push_back(e);
    }
    {
        // Linen Bandage — First Aid 1.
        WoweeTradeSkill::Entry e;
        e.recipeId = 2; e.name = "Linen Bandage";
        e.description = "Heal target for 66 health over 8 seconds.";
        e.iconPath = "Interface/Icons/Inv_Misc_Bandage_15.blp";
        e.profession = WoweeTradeSkill::FirstAid;
        e.skillId = 129;             // WSKL First Aid skillId
        e.orangeRank = 1; e.yellowRank = 30;
        e.greenRank = 60; e.grayRank = 90;
        e.craftSpellId = 3275;
        e.producedItemId = 1251;
        e.producedMinCount = 1; e.producedMaxCount = 1;
        e.reagentItemId[0] = 2589;   // Linen Cloth
        e.reagentCount[0] = 1;
        c.entries.push_back(e);
    }
    {
        // Minor Healing Potion — Alchemy 1.
        WoweeTradeSkill::Entry e;
        e.recipeId = 3; e.name = "Minor Healing Potion";
        e.description = "Restores 70 to 90 health.";
        e.iconPath = "Interface/Icons/Inv_Potion_50.blp";
        e.profession = WoweeTradeSkill::Alchemy;
        e.skillId = 171;             // WSKL Alchemy skillId
        e.orangeRank = 1; e.yellowRank = 55;
        e.greenRank = 85; e.grayRank = 115;
        e.craftSpellId = 2330;
        e.producedItemId = 118;
        e.producedMinCount = 1; e.producedMaxCount = 2;
        e.toolItemId = 4470;         // Empty Vial / Alchemist's Lab
        e.reagentItemId[0] = 765;    // Silverleaf
        e.reagentCount[0] = 1;
        e.reagentItemId[1] = 2453;   // Briarthorn
        e.reagentCount[1] = 1;
        c.entries.push_back(e);
    }
    return c;
}

WoweeTradeSkill WoweeTradeSkillLoader::makeBlacksmithing(
    const std::string& catalogName) {
    WoweeTradeSkill c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint16_t orange,
                    uint16_t yellow, uint16_t green, uint16_t gray,
                    uint32_t spellId, uint32_t itemId, uint32_t tool,
                    uint32_t r1, uint8_t r1c,
                    uint32_t r2, uint8_t r2c,
                    uint32_t r3, uint8_t r3c, const char* desc) {
        WoweeTradeSkill::Entry e;
        e.recipeId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Inv_") + name + ".blp";
        e.profession = WoweeTradeSkill::Blacksmithing;
        e.skillId = 164;
        e.orangeRank = orange; e.yellowRank = yellow;
        e.greenRank = green; e.grayRank = gray;
        e.craftSpellId = spellId; e.producedItemId = itemId;
        e.toolItemId = tool;
        e.reagentItemId[0] = r1; e.reagentCount[0] = r1c;
        e.reagentItemId[1] = r2; e.reagentCount[1] = r2c;
        e.reagentItemId[2] = r3; e.reagentCount[2] = r3c;
        c.entries.push_back(e);
    };
    add(100, "RoughSharpeningStone", 1, 25, 50, 75, 2660, 2862, 5956,
        2835, 1, 0, 0, 0, 0,
        "Apply to weapon — minor temp damage buff.");
    add(101, "CopperChainBelt",      50, 70, 90, 110, 2664, 2386, 5956,
        2840, 4, 0, 0, 0, 0,
        "Light chain belt for early-level warriors.");
    add(102, "RunedCopperBracers",   100, 120, 140, 160, 2667, 2406, 5956,
        2840, 6, 818, 1, 0, 0,
        "Bracers with a minor magic enhancement.");
    add(103, "IronforgeBreastplate", 195, 215, 235, 255, 9959, 7915, 5956,
        2842, 8, 3858, 4, 0, 0,
        "Heavy iron breastplate — Ironforge guard standard issue.");
    add(104, "TruesilverChampion",   265, 285, 305, 325, 16728, 12793, 5956,
        7910, 10, 7910, 5, 12808, 1,
        "Pinnacle 60-era plate — requires arcanite reagents.");
    return c;
}

WoweeTradeSkill WoweeTradeSkillLoader::makeAlchemy(
    const std::string& catalogName) {
    WoweeTradeSkill c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint16_t orange,
                    uint16_t yellow, uint16_t green, uint16_t gray,
                    uint32_t spellId, uint32_t itemId,
                    uint8_t produceMin, uint8_t produceMax,
                    uint32_t r1, uint8_t r1c,
                    uint32_t r2, uint8_t r2c, const char* desc) {
        WoweeTradeSkill::Entry e;
        e.recipeId = id; e.name = name; e.description = desc;
        e.iconPath = std::string("Interface/Icons/Inv_Potion_") +
                      name + ".blp";
        e.profession = WoweeTradeSkill::Alchemy;
        e.skillId = 171;
        e.orangeRank = orange; e.yellowRank = yellow;
        e.greenRank = green; e.grayRank = gray;
        e.craftSpellId = spellId; e.producedItemId = itemId;
        e.producedMinCount = produceMin; e.producedMaxCount = produceMax;
        e.toolItemId = 4470;     // Empty Vial / Alchemist Lab
        e.reagentItemId[0] = r1; e.reagentCount[0] = r1c;
        e.reagentItemId[1] = r2; e.reagentCount[1] = r2c;
        c.entries.push_back(e);
    };
    add(200, "MinorHealing",    1,   55,  85, 115,  2330,   118, 1, 2,
        765, 1, 2453, 1, "Restores 70 to 90 health.");
    add(201, "Swiftness",       60,  85, 115, 145,  2336,   858, 1, 1,
        2447, 1, 2452, 1, "Free-action / move speed buff.");
    add(202, "LesserMana",      90, 115, 140, 165,  2331,  3385, 1, 2,
        785, 1, 2453, 1, "Restores 140 to 180 mana.");
    add(203, "GreaterHealing", 155, 175, 200, 225,  3171,  1710, 1, 2,
        3819, 1, 3820, 1, "Restores 455 to 585 health.");
    add(204, "FlaskOfTheTitans", 300, 320, 340, 360, 17636, 13510, 1, 1,
        13463, 30, 13468, 10,
        "2-hour flask — +400 max health, persists through death.");
    return c;
}

} // namespace pipeline
} // namespace wowee
