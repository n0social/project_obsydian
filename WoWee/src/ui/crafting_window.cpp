// ============================================================
// Crafting window — standalone tradeskill UI (part of WindowManager)
// Opened by casting a profession spell (Cooking, First Aid, ...).
// Recipe list with item icons, skill-based difficulty colors
// (orange/yellow/green/gray), reagent have/need counts, and
// quantity + Create All controls backed by the craft queue.
// ============================================================
#include "ui/window_manager.hpp"
#include "ui/inventory_screen.hpp"
#include "ui/ui_colors.hpp"
#include "game/game_handler.hpp"
#include "pipeline/asset_manager.hpp"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wowee {
namespace ui {

namespace {

using namespace wowee::ui::colors;

// Difficulty palette (classic tradeskill colors)
constexpr ImVec4 kDiffOrange(1.0f, 0.5f, 0.0f, 1.0f);
constexpr ImVec4 kDiffYellow(1.0f, 1.0f, 0.0f, 1.0f);
constexpr ImVec4 kDiffGreen(0.3f, 0.8f, 0.3f, 1.0f);
constexpr ImVec4 kDiffGray(0.5f, 0.5f, 0.5f, 1.0f);

// 0=orange, 1=yellow, 2=green, 3=gray (also the sort order)
int difficultyRank(game::GameHandler& gameHandler, uint32_t spellId) {
    auto cit = gameHandler.spellNameCacheRef().find(spellId);
    if (cit == gameHandler.spellNameCacheRef().end()) return 0;
    const auto& se = cit->second;
    if (se.trivialSkillHigh == 0 && se.trivialSkillLow == 0)
        return 0; // no thresholds = always useful
    auto slIt = gameHandler.spellToSkillLineRef().find(spellId);
    if (slIt == gameHandler.spellToSkillLineRef().end()) return 0;
    auto skIt = gameHandler.getPlayerSkills().find(slIt->second);
    if (skIt == gameHandler.getPlayerSkills().end()) return 0;
    uint32_t skill = skIt->second.effectiveValue();
    if (skill >= se.trivialSkillHigh) return 3;
    if (skill >= se.trivialSkillLow) return 2;
    uint32_t yellowThresh = se.minSkillRank + (se.trivialSkillLow - se.minSkillRank) / 2;
    if (skill >= yellowThresh) return 1;
    return 0;
}

// Total count of an item across the backpack and equipped bags
uint32_t countInventoryItem(const game::Inventory& inv, uint32_t itemId) {
    uint32_t total = 0;
    for (int i = 0; i < inv.getBackpackSize(); ++i) {
        const auto& slot = inv.getBackpackSlot(i);
        if (!slot.empty() && slot.item.itemId == itemId) total += slot.item.stackCount;
    }
    for (int bag = 0; bag < game::Inventory::NUM_BAG_SLOTS; ++bag) {
        int bagSize = inv.getBagSize(bag);
        for (int s = 0; s < bagSize; ++s) {
            const auto& slot = inv.getBagSlot(bag, s);
            if (!slot.empty() && slot.item.itemId == itemId) total += slot.item.stackCount;
        }
    }
    return total;
}

struct RecipeRow {
    uint32_t spellId = 0;
    const char* name = "";
    int diffRank = 0;
    int canMake = 0;       // limited by reagents on hand
    bool hasReagents = false;
};

} // namespace

ImVec4 WindowManager::recipeDifficultyColor(game::GameHandler& gameHandler, uint32_t spellId) {
    switch (difficultyRank(gameHandler, spellId)) {
        case 3:  return kDiffGray;
        case 2:  return kDiffGreen;
        case 1:  return kDiffYellow;
        default: return kDiffOrange;
    }
}

const char* WindowManager::recipeDifficultyLabel(game::GameHandler& gameHandler, uint32_t spellId) {
    switch (difficultyRank(gameHandler, spellId)) {
        case 3:  return "Trivial";
        case 2:  return "Easy";
        case 1:  return "Medium";
        default: return "Optimal";
    }
}

void WindowManager::renderCraftingWindow(game::GameHandler& gameHandler,
                                         SpellIconFn getSpellIcon,
                                         InventoryScreen& inventoryScreen) {
    if (!gameHandler.isCraftingWindowOpen()) return;
    const uint32_t skillLine = gameHandler.getCraftingSkillLine();
    if (skillLine == 0) return;

    gameHandler.loadSpellNameCache();
    auto* assetMgr = services_.assetManager;
    const auto& inventory = gameHandler.getInventory();

    // ---- Collect known recipes of this skill line ----
    std::vector<RecipeRow> recipes;
    for (uint32_t spellId : gameHandler.getKnownSpells()) {
        auto slIt = gameHandler.spellToSkillLineRef().find(spellId);
        if (slIt == gameHandler.spellToSkillLineRef().end() || slIt->second != skillLine)
            continue;
        auto cit = gameHandler.spellNameCacheRef().find(spellId);
        if (cit == gameHandler.spellNameCacheRef().end()) continue;
        const auto& se = cit->second;

        RecipeRow row;
        row.spellId = spellId;
        row.name = se.name.c_str();
        for (const auto& reagent : se.reagents) {
            if (reagent.itemId != 0) { row.hasReagents = true; break; }
        }
        // Skip the window-opener spell itself and passive skill ranks
        if (!row.hasReagents && se.createdItemId == 0) continue;

        row.diffRank = difficultyRank(gameHandler, spellId);
        row.canMake = row.hasReagents ? 999 : 0;
        for (const auto& reagent : se.reagents) {
            if (reagent.itemId == 0 || reagent.count == 0) continue;
            uint32_t have = countInventoryItem(inventory, reagent.itemId);
            row.canMake = std::min(row.canMake, static_cast<int>(have / reagent.count));
        }
        recipes.push_back(row);
    }
    std::sort(recipes.begin(), recipes.end(), [](const RecipeRow& a, const RecipeRow& b) {
        if (a.diffRank != b.diffRank) return a.diffRank < b.diffRank;
        return std::strcmp(a.name, b.name) < 0;
    });

    // ---- Window ----
    const std::string& skillName = gameHandler.getSkillName(skillLine);
    char title[128];
    snprintf(title, sizeof(title), "%s###CraftingWindow",
             skillName.empty() ? "Crafting" : skillName.c_str());

    ImGui::SetNextWindowSize(ImVec2(620.0f, 460.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        // Skill progress header
        auto skIt = gameHandler.getPlayerSkills().find(skillLine);
        if (skIt != gameHandler.getPlayerSkills().end()) {
            uint32_t cur = skIt->second.effectiveValue();
            uint32_t max = skIt->second.maxValue;
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%s %u / %u",
                     skillName.empty() ? "Skill" : skillName.c_str(), cur, max);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.75f, 0.55f, 0.1f, 1.0f));
            ImGui::ProgressBar(max > 0 ? static_cast<float>(cur) / max : 0.0f,
                               ImVec2(-1.0f, 0.0f), overlay);
            ImGui::PopStyleColor();
        }

        // Filters
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##CraftSearch", "Search...",
                                 craftSearchFilter_, sizeof(craftSearchFilter_));
        ImGui::SameLine();
        ImGui::Checkbox("Have reagents", &craftOnlyMakeable_);
        ImGui::Separator();

        // ---- Left: recipe list ----
        // The list pane width is user-draggable via the splitter below and
        // clamped so both panes stay usable as the window is resized. It seeds
        // to a proportion of the window on first open (a fixed width used to keep
        // long recipe names truncated no matter how wide the window got).
        const float availX = ImGui::GetContentRegionAvail().x;
        const float paneHeight = ImGui::GetContentRegionAvail().y;
        constexpr float kSplitterW = 6.0f;
        constexpr float kMinLeft = 180.0f;
        constexpr float kMinRight = 220.0f;
        if (craftListPaneWidth_ <= 0.0f)
            craftListPaneWidth_ = std::max(260.0f, availX * 0.42f);
        const float listWidth = std::clamp(craftListPaneWidth_, kMinLeft,
                                           std::max(kMinLeft, availX - kMinRight - kSplitterW));
        if (ImGui::BeginChild("##RecipeList", ImVec2(listWidth, 0), true)) {
            std::string filter(craftSearchFilter_);
            for (char& c : filter) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            for (const auto& recipe : recipes) {
                if (craftOnlyMakeable_ && recipe.canMake <= 0) continue;
                if (!filter.empty()) {
                    std::string nameLC(recipe.name);
                    for (char& c : nameLC) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (nameLC.find(filter) == std::string::npos) continue;
                }

                ImGui::PushID(static_cast<int>(recipe.spellId));

                // Icon: created item icon preferred, spell icon fallback
                VkDescriptorSet icon = VK_NULL_HANDLE;
                auto cit = gameHandler.spellNameCacheRef().find(recipe.spellId);
                if (cit != gameHandler.spellNameCacheRef().end() && cit->second.createdItemId != 0) {
                    gameHandler.ensureItemInfo(cit->second.createdItemId);
                    const auto* itemInfo = gameHandler.getItemInfo(cit->second.createdItemId);
                    if (itemInfo && itemInfo->displayInfoId)
                        icon = inventoryScreen.getItemIcon(itemInfo->displayInfoId);
                }
                if (!icon && getSpellIcon)
                    icon = getSpellIcon(recipe.spellId, assetMgr);

                if (icon) {
                    ImGui::Image((ImTextureID)(uintptr_t)icon, ImVec2(18, 18));
                    ImGui::SameLine(0, 4);
                }

                char label[160];
                if (recipe.canMake > 0)
                    snprintf(label, sizeof(label), "%s [%d]##sel", recipe.name, recipe.canMake);
                else
                    snprintf(label, sizeof(label), "%s##sel", recipe.name);
                ImVec4 color = recipe.canMake > 0 || !recipe.hasReagents
                    ? recipeDifficultyColor(gameHandler, recipe.spellId)
                    : ImVec4(0.45f, 0.42f, 0.42f, 1.0f); // dim when missing reagents
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                if (ImGui::Selectable(label, craftSelectedRecipe_ == recipe.spellId)) {
                    craftSelectedRecipe_ = recipe.spellId;
                    craftQuantity_ = 1;
                }
                ImGui::PopStyleColor();
                // Selectable clips (doesn't ellipsize) overflowing names, so
                // surface the full name on hover for anything wider than the pane.
                if (ImGui::IsItemHovered() &&
                    ImGui::CalcTextSize(recipe.name).x > ImGui::GetContentRegionAvail().x)
                    ImGui::SetTooltip("%s", recipe.name);
                ImGui::PopID();
            }
            if (recipes.empty())
                ImGui::TextDisabled("No recipes known.");
        }
        ImGui::EndChild();

        // ---- Splitter: drag to resize the list vs detail panes ----
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.7f, 0.7f, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.9f, 0.9f, 0.5f));
        ImGui::Button("##CraftSplitter", ImVec2(kSplitterW, paneHeight));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            craftListPaneWidth_ = listWidth + ImGui::GetIO().MouseDelta.x;
        ImGui::SameLine(0.0f, 0.0f);

        // ---- Right: selected recipe details ----
        if (ImGui::BeginChild("##RecipeDetail", ImVec2(0, 0), true)) {
            const RecipeRow* selected = nullptr;
            for (const auto& recipe : recipes) {
                if (recipe.spellId == craftSelectedRecipe_) { selected = &recipe; break; }
            }

            if (!selected) {
                ImGui::TextDisabled("Select a recipe.");
            } else {
                auto cit = gameHandler.spellNameCacheRef().find(selected->spellId);
                const auto& se = cit->second; // guaranteed: recipes built from cache

                // Name + difficulty
                ImGui::TextColored(recipeDifficultyColor(gameHandler, selected->spellId),
                                   "%s", se.name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)",
                    recipeDifficultyLabel(gameHandler, selected->spellId));

                // Created item with stats
                if (se.createdItemId != 0) {
                    gameHandler.ensureItemInfo(se.createdItemId);
                    const auto* prodInfo = gameHandler.getItemInfo(se.createdItemId);
                    ImGui::TextDisabled("Creates:");
                    ImGui::SameLine(0, 4);
                    if (prodInfo && prodInfo->displayInfoId) {
                        VkDescriptorSet icon = inventoryScreen.getItemIcon(prodInfo->displayInfoId);
                        if (icon) {
                            ImGui::Image((ImTextureID)(uintptr_t)icon, ImVec2(20, 20));
                            ImGui::SameLine(0, 4);
                        }
                    }
                    if (prodInfo && !prodInfo->name.empty()) {
                        ImVec4 nameCol = InventoryScreen::getQualityColor(
                            static_cast<game::ItemQuality>(prodInfo->quality));
                        ImGui::TextColored(nameCol, "%s", prodInfo->name.c_str());
                    } else {
                        ImGui::Text("Item #%u", se.createdItemId);
                    }
                }
                if (!se.description.empty()) {
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()
                        + ImGui::GetContentRegionAvail().x);
                    ImGui::TextDisabled("%s", se.description.c_str());
                    ImGui::PopTextWrapPos();
                }

                // Reagents
                bool hasAllReagents = true;
                if (selected->hasReagents) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Reagents:");
                    for (const auto& reagent : se.reagents) {
                        if (reagent.itemId == 0 || reagent.count == 0) continue;
                        gameHandler.ensureItemInfo(reagent.itemId);
                        const auto* rInfo = gameHandler.getItemInfo(reagent.itemId);
                        uint32_t have = countInventoryItem(inventory, reagent.itemId);
                        bool enough = have >= reagent.count;
                        if (!enough) hasAllReagents = false;
                        ImVec4 haveCol = enough ? kLightGreen : ImVec4(1.0f, 0.6f, 0.6f, 1.0f);
                        ImGui::Indent(12.0f);
                        if (rInfo && rInfo->displayInfoId) {
                            VkDescriptorSet icon = inventoryScreen.getItemIcon(rInfo->displayInfoId);
                            if (icon) {
                                ImGui::Image((ImTextureID)(uintptr_t)icon, ImVec2(16, 16));
                                ImGui::SameLine(0, 4);
                            }
                        }
                        if (rInfo && !rInfo->name.empty())
                            ImGui::Text("%s", rInfo->name.c_str());
                        else
                            ImGui::Text("Item #%u", reagent.itemId);
                        ImGui::SameLine(0, 6);
                        ImGui::TextColored(haveCol, "%u/%u", have, reagent.count);
                        ImGui::Unindent(12.0f);
                    }
                }

                // ---- Craft controls ----
                ImGui::Spacing();
                ImGui::Separator();
                int queueRemaining = gameHandler.getCraftQueueRemaining();
                bool casting = gameHandler.isCasting();
                if (queueRemaining > 0 || casting) {
                    const std::string& castName =
                        gameHandler.getSpellName(gameHandler.getCraftQueueSpellId() != 0
                            ? gameHandler.getCraftQueueSpellId()
                            : selected->spellId);
                    if (queueRemaining > 0)
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                            "Crafting %s... %d remaining",
                            castName.empty() ? "" : castName.c_str(), queueRemaining);
                    if (casting)
                        ImGui::ProgressBar(gameHandler.getCastProgress(), ImVec2(-70.0f, 0.0f));
                    ImGui::SameLine();
                    if (ImGui::Button("Stop")) {
                        gameHandler.cancelCraftQueue();
                        gameHandler.cancelCast();
                    }
                } else {
                    ImGui::SetNextItemWidth(90.0f);
                    ImGui::InputInt("##CraftQty", &craftQuantity_);
                    craftQuantity_ = std::clamp(craftQuantity_, 1, 999);
                    ImGui::SameLine();
                    bool canCraft = hasAllReagents && selected->canMake > 0;
                    if (!canCraft) ImGui::BeginDisabled();
                    if (ImGui::Button("Create")) {
                        int count = std::min(craftQuantity_, selected->canMake);
                        if (count == 1)
                            gameHandler.castSpell(selected->spellId, 0);
                        else
                            gameHandler.startCraftQueue(selected->spellId, count);
                    }
                    ImGui::SameLine();
                    char allLabel[32];
                    snprintf(allLabel, sizeof(allLabel), "Create All (%d)",
                             std::max(selected->canMake, 0));
                    if (ImGui::Button(allLabel)) {
                        gameHandler.startCraftQueue(selected->spellId, selected->canMake);
                    }
                    if (!canCraft) ImGui::EndDisabled();
                    if (!canCraft)
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "Missing reagents");
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    if (!open) {
        gameHandler.closeCraftingWindow();
    }
}

} // namespace ui
} // namespace wowee
