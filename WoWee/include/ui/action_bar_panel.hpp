// ============================================================
// ActionBarPanel — extracted from GameScreen
// Owns all action bar rendering: main bar, stance bar, bag bar,
// XP bar, reputation bar, macro resolution.
// ============================================================
#pragma once
#include "ui/ui_services.hpp"
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <string>
#include <vulkan/vulkan.h>

namespace wowee {
namespace game { class GameHandler; }
namespace pipeline { class AssetManager; }
namespace ui {

class ChatPanel;
class SettingsPanel;
class InventoryScreen;
class SpellbookScreen;
class QuestLogScreen;

class ActionBarPanel {
public:
    // Callback type for resolving spell icons (spellId, assetMgr) → VkDescriptorSet
    using SpellIconFn = std::function<VkDescriptorSet(uint32_t, pipeline::AssetManager*)>;

    static constexpr int kFrameXmlActionBarPages = 6;
    static constexpr int kBottomLeftActionPage = 6;
    static constexpr int kRightActionPage = 3;
    static constexpr int kLeftActionPage = 4;

    static int actionSlotForPage(int page, int buttonIndex) {
        return (page - 1) * 12 + buttonIndex;
    }

    // ---- Action bar render methods ----
    void renderActionBar(game::GameHandler& gameHandler,
                         SettingsPanel& settingsPanel,
                         ChatPanel& chatPanel,
                         InventoryScreen& inventoryScreen,
                         SpellbookScreen& spellbookScreen,
                         QuestLogScreen& questLogScreen,
                         SpellIconFn getSpellIcon);
    void renderStanceBar(game::GameHandler& gameHandler,
                         SettingsPanel& settingsPanel,
                         SpellbookScreen& spellbookScreen,
                         SpellIconFn getSpellIcon);
    bool renderBagBar(game::GameHandler& gameHandler,
                      SettingsPanel& settingsPanel,
                      InventoryScreen& inventoryScreen);
    void renderXpBar(game::GameHandler& gameHandler,
                     SettingsPanel& settingsPanel);
    void renderRepBar(game::GameHandler& gameHandler,
                      SettingsPanel& settingsPanel);

    int getMainActionBarPage() const { return mainActionBarPage_; }

    // ---- State owned by this panel ----

    // Action bar error-flash: spellId → wall-clock time (seconds) when the flash ends
    std::unordered_map<uint32_t, float> actionFlashEndTimes_;
    std::unordered_map<uint32_t, float> itemSpellCooldownTotals_;
    static constexpr float kActionFlashDuration = 0.5f;

    // Action bar drag state. actionBarDragSlot_ is the origin slot the carried action
    // was lifted from (-1 = not carrying). While carrying, the origin slot is left
    // empty and the action rides the cursor as (carryType, carryId) with its icon.
    int actionBarDragSlot_ = -1;
    VkDescriptorSet actionBarDragIcon_ = VK_NULL_HANDLE;
    uint8_t  actionBarCarryType_ = 0;   // game::ActionBarSlot::Type of the carried action
    uint32_t actionBarCarryId_ = 0;     // spell/item/macro id of the carried action
    // Press-and-hold pickup: which slot is being held down and when the press began.
    // After holding kActionBarPickupDelay the slot is "picked up" into the drag state.
    int actionBarHoldSlot_ = -1;
    double actionBarHoldStart_ = 0.0;
    // True from the moment a pickup completes until the initiating mouse press is
    // released. While set, that pending release is swallowed everywhere so it can't
    // immediately drop what was just picked up.
    bool actionBarCarryPressActive_ = false;
    static constexpr double kActionBarPickupDelay = 0.6;  // seconds to hold before pickup
    int mainActionBarPage_ = 1;  // FrameXML main pages are 1..6.

    // Bag bar state
    VkDescriptorSet backpackIconTexture_ = VK_NULL_HANDLE;
    VkDescriptorSet emptyBagSlotTexture_ = VK_NULL_HANDLE;
    int bagBarPickedSlot_ = -1;
    int bagBarDragSource_ = -1;

    // Macro editor popup state
    uint32_t macroEditorId_   = 0;
    bool     macroEditorOpen_ = false;
    char     macroEditorBuf_[256] = {};

    // Macro cooldown cache: maps macro ID → resolved primary spell ID
    std::unordered_map<uint32_t, uint32_t> macroPrimarySpellCache_;
    struct MacroRenderInfo {
        std::string sourceText;
        size_t spellCount = 0;
        size_t itemCount = 0;
        uint32_t primarySpellId = 0;
        uint32_t iconSpellId = 0;
        uint32_t itemEntry = 0;
        bool isUse = false;
    };
    std::unordered_map<uint32_t, MacroRenderInfo> macroRenderCache_;
    size_t macroCacheSpellCount_ = 0;

    // UIServices injection (Phase B singleton breaking)
    void setServices(const UIServices& services) { services_ = services; }

private:
    UIServices services_;
    uint32_t resolveMacroPrimarySpellId(uint32_t macroId, game::GameHandler& gameHandler);
    const MacroRenderInfo& resolveMacroRenderInfo(uint32_t macroId, game::GameHandler& gameHandler);
};

} // namespace ui
} // namespace wowee
