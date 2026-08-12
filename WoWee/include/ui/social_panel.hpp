#pragma once

#include "ui/ui_services.hpp"
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace wowee {
namespace game { class GameHandler; }
namespace pipeline { class AssetManager; }
namespace ui {

class ChatPanel;
class SpellbookScreen;
class InventoryScreen;

/**
 * Social panel manager (extracted from GameScreen)
 *
 * Owns all social/group-related rendering:
 *   party frames, boss frames, guild roster, social/friends frame,
 *   dungeon finder, who window, inspect window.
 */
class SocialPanel {
public:
    SocialPanel() = default;

    // ---- Callback type for spell icon lookup (stays in GameScreen) ----
    using SpellIconFn = std::function<VkDescriptorSet(uint32_t spellId, pipeline::AssetManager*)>;

    // ---- Toggle booleans (written by slash commands / escape handler / keybinds / UI buttons) ----
    bool showSocialFrame_ = false;   // O key toggles social/friends list
    bool showGuildRoster_ = false;
    bool showRaidFrames_ = true;     // F key toggles raid/party frames
    bool showWhoWindow_ = false;
    bool showDungeonFinder_ = false;
    bool showInspectWindow_ = false;

    // ---- Guild roster state ----
    std::string selectedGuildMember_;
    bool showGuildNoteEdit_ = false;
    bool editingOfficerNote_ = false;
    char guildNoteEditBuffer_[256] = {0};
    int guildRosterTab_ = 0;         // 0=Roster, 1=Guild Info
    char guildMotdEditBuffer_[256] = {0};
    bool showMotdEdit_ = false;
    char petitionNameBuffer_[64] = {0};
    char addRankNameBuffer_[64] = {0};
    bool showAddRankModal_ = false;

    // ---- LFG state ----
    uint8_t lfgRoles_ = 0x08;           // default: DPS (0x02=tank, 0x04=healer, 0x08=dps)
    uint32_t lfgSelectedDungeon_ = 861;  // default: random dungeon (entry 861)

    // ---- Public render methods ----
    void renderPartyFrames(game::GameHandler& gameHandler,
                           ChatPanel& chatPanel,
                           SpellIconFn getSpellIcon);
    void renderBossFrames(game::GameHandler& gameHandler,
                          SpellbookScreen& spellbookScreen,
                          SpellIconFn getSpellIcon);
    void renderGuildRoster(game::GameHandler& gameHandler,
                           ChatPanel& chatPanel);
    void renderSocialFrame(game::GameHandler& gameHandler,
                           ChatPanel& chatPanel);
    void renderDungeonFinderWindow(game::GameHandler& gameHandler,
                                   ChatPanel& chatPanel);
    void renderWhoWindow(game::GameHandler& gameHandler,
                         ChatPanel& chatPanel);
    void renderInspectWindow(game::GameHandler& gameHandler,
                             InventoryScreen& inventoryScreen);

    // UIServices injection (singleton breaking)
    void setServices(const UIServices& services) { services_ = services; }

private:
    UIServices services_;
    uint64_t inspectWindowAutoRequestGuid_ = 0;
};

} // namespace ui
} // namespace wowee
