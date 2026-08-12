#include "pipeline/wowee_crafting_recipes.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'R', 'A'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wcra") {
        base += ".wcra";
    }
    return base;
}

} // namespace

const WoweeCraftingRecipes::Entry*
WoweeCraftingRecipes::findById(uint32_t recipeId) const {
    for (const auto& e : entries)
        if (e.recipeId == recipeId) return &e;
    return nullptr;
}

const WoweeCraftingRecipes::Entry*
WoweeCraftingRecipes::findBySpellId(uint32_t spellId) const {
    for (const auto& e : entries)
        if (e.spellId == spellId) return &e;
    return nullptr;
}

std::vector<const WoweeCraftingRecipes::Entry*>
WoweeCraftingRecipes::findByTradeSkill(uint16_t tradeSkillId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.tradeSkillId == tradeSkillId) out.push_back(&e);
    return out;
}

std::vector<const WoweeCraftingRecipes::Entry*>
WoweeCraftingRecipes::findByProducedItem(uint32_t itemId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.producedItemId == itemId) out.push_back(&e);
    return out;
}

bool WoweeCraftingRecipesLoader::save(
    const WoweeCraftingRecipes& cat,
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
        writePOD(os, e.spellId);
        writeStr(os, e.name);
        writePOD(os, e.tradeSkillId);
        writePOD(os, e.requiredSkillLevel);
        writePOD(os, e.producedItemId);
        writePOD(os, e.producedCount);
        writePOD(os, e.categoryId);
        writePOD(os, e.learnedFromItemId);
        uint32_t reagentCount =
            static_cast<uint32_t>(e.reagents.size());
        writePOD(os, reagentCount);
        for (const auto& r : e.reagents) {
            writePOD(os, r.itemId);
            writePOD(os, r.count);
        }
    }
    return os.good();
}

WoweeCraftingRecipes WoweeCraftingRecipesLoader::load(
    const std::string& basePath) {
    WoweeCraftingRecipes out;
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
        if (!readPOD(is, e.recipeId) ||
            !readPOD(is, e.spellId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.tradeSkillId) ||
            !readPOD(is, e.requiredSkillLevel) ||
            !readPOD(is, e.producedItemId) ||
            !readPOD(is, e.producedCount) ||
            !readPOD(is, e.categoryId) ||
            !readPOD(is, e.learnedFromItemId)) {
            out.entries.clear(); return out;
        }
        uint32_t reagentCount = 0;
        if (!readPOD(is, reagentCount)) {
            out.entries.clear(); return out;
        }
        // Sanity cap — no recipe should have more than
        // 32 reagents; vanilla cap is 8.
        if (reagentCount > 32) {
            out.entries.clear(); return out;
        }
        e.reagents.resize(reagentCount);
        for (auto& r : e.reagents) {
            if (!readPOD(is, r.itemId) ||
                !readPOD(is, r.count)) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeCraftingRecipesLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

// Vanilla trade-skill IDs from SkillLine.dbc:
//   Alchemy=171, Blacksmithing=164,
//   Engineering=202, Enchanting=333,
//   LeatherWorking=165, Tailoring=197,
//   Cooking=185, FirstAid=129.
constexpr uint16_t kAlchemy = 171;
constexpr uint16_t kEngineering = 202;
constexpr uint16_t kBlacksmithing = 164;

WoweeCraftingRecipes::Entry makeRecipe(
    uint32_t recipeId, uint32_t spellId, const char* name,
    uint16_t tradeSkillId, uint16_t requiredSkill,
    uint32_t producedItemId, uint16_t producedCount,
    uint16_t categoryId, uint32_t learnedFromItemId,
    std::vector<WoweeCraftingRecipes::Reagent> reagents) {
    WoweeCraftingRecipes::Entry e;
    e.recipeId = recipeId; e.spellId = spellId;
    e.name = name;
    e.tradeSkillId = tradeSkillId;
    e.requiredSkillLevel = requiredSkill;
    e.producedItemId = producedItemId;
    e.producedCount = producedCount;
    e.categoryId = categoryId;
    e.learnedFromItemId = learnedFromItemId;
    e.reagents = std::move(reagents);
    return e;
}

} // namespace

WoweeCraftingRecipes WoweeCraftingRecipesLoader::makeAlchemyPotions(
    const std::string& catalogName) {
    WoweeCraftingRecipes c;
    c.name = catalogName;
    // Vanilla Alchemy potions. Reagent itemIds are
    // canonical: Peacebloom=2447, Silverleaf=765,
    // Briarthorn=2450, Mageroyal=785,
    // Bruiseweed=2453, Stranglekelp=3820,
    // Liferoot=3357, Goldthorn=3821, Khadgar's
    // Whisker=3358, Empty Vial=3371.
    c.entries.push_back(makeRecipe(
        1, 2330, "Minor Healing Potion", kAlchemy, 1,
        118, 1, 1, 0,
        {{2447, 1}, {765, 1}, {3371, 1}}));
    // Lesser Mana Potion: Mageroyal + Stranglekelp.
    c.entries.push_back(makeRecipe(
        2, 2331, "Lesser Mana Potion", kAlchemy, 100,
        3385, 1, 1, 0,
        {{785, 1}, {3820, 1}, {3371, 1}}));
    // Greater Healing Potion: Liferoot + Khadgar's
    // Whisker.
    c.entries.push_back(makeRecipe(
        3, 11457, "Greater Healing Potion", kAlchemy, 155,
        3928, 1, 1, 0,
        {{3357, 1}, {3358, 1}, {3371, 1}}));
    // Major Mana Potion: Sungrass=8838 + Blindweed
    // =8839 + Crystal Vial=8766 (uses larger vial).
    c.entries.push_back(makeRecipe(
        4, 17580, "Major Mana Potion", kAlchemy, 295,
        13444, 1, 1, 0,
        {{8838, 3}, {8839, 3}, {8766, 1}}));
    return c;
}

WoweeCraftingRecipes WoweeCraftingRecipesLoader::makeEngineering(
    const std::string& catalogName) {
    WoweeCraftingRecipes c;
    c.name = catalogName;
    // Rough Blasting Powder: 1 Rough Stone (2835)
    // for 1 powder. Lowest-skill engineering recipe.
    c.entries.push_back(makeRecipe(
        10, 3918, "Rough Blasting Powder", kEngineering, 1,
        4357, 1, 1, 0,
        {{2835, 1}}));
    // Mechanical Squirrel Box: rough copper-bar
    // recipe — 4 reagents.
    c.entries.push_back(makeRecipe(
        11, 4413, "Mechanical Squirrel Box", kEngineering, 75,
        4401, 1, 1, 0,
        {{2840, 2}, {4399, 1}, {2589, 1}, {4357, 1}}));
    // Target Dummy: 5 reagents — demonstrates
    // variable reagent count within the recipe
    // catalog. Blueprint is itemId 4406.
    c.entries.push_back(makeRecipe(
        12, 4079, "Target Dummy", kEngineering, 75,
        2092, 1, 1, 4406,
        {{2840, 4}, {4361, 2}, {2997, 2}, {2589, 4}, {4357, 1}}));
    return c;
}

WoweeCraftingRecipes WoweeCraftingRecipesLoader::makeBlacksmithing(
    const std::string& catalogName) {
    WoweeCraftingRecipes c;
    c.name = catalogName;
    // Rough Sharpening Stone: 1 Rough Stone -> 1
    // sharpening stone. Skill 1 (default).
    c.entries.push_back(makeRecipe(
        20, 2660, "Rough Sharpening Stone", kBlacksmithing, 1,
        2862, 1, 1, 0,
        {{2835, 1}}));
    // Coarse Grinding Stone: 2 Coarse Stone (2836).
    // Skill 50.
    c.entries.push_back(makeRecipe(
        21, 3326, "Coarse Grinding Stone", kBlacksmithing, 50,
        3486, 1, 1, 0,
        {{2836, 2}}));
    // Heavy Mithril Helm: high-skill plate piece
    // requiring multiple bar types. Skill 235.
    c.entries.push_back(makeRecipe(
        22, 9938, "Heavy Mithril Helm", kBlacksmithing, 235,
        7909, 1, 2, 11163,
        {{3860, 8}, {3859, 1}, {3864, 4}, {3866, 2}}));
    return c;
}

} // namespace pipeline
} // namespace wowee
