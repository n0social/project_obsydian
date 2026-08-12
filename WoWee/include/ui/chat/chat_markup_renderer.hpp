#pragma once

#include "ui/chat/chat_markup_parser.hpp"
#include "ui/ui_services.hpp"
#include <vulkan/vulkan.h>
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

namespace wowee {
namespace game { class GameHandler; }
namespace pipeline { class AssetManager; }
namespace ui {

class InventoryScreen;
class SpellbookScreen;
class QuestLogScreen;

/**
 * Context needed by the renderer to display links, tooltips, and icons.
 */
struct MarkupRenderContext {
    game::GameHandler*  gameHandler   = nullptr;
    InventoryScreen*    inventory     = nullptr;
    SpellbookScreen*    spellbook     = nullptr;
    QuestLogScreen*     questLog      = nullptr;
    pipeline::AssetManager* assetMgr  = nullptr;
    // Spell icon callback — same as ChatPanel::getSpellIcon
    std::function<VkDescriptorSet(uint32_t, pipeline::AssetManager*)> getSpellIcon;
    // Chat input buffer for shift-click link insertion
    char*   chatInputBuffer   = nullptr;
    size_t  chatInputBufSize  = 0;
    bool*   moveCursorToEnd   = nullptr;
};

/**
 * Renders parsed ChatSegments via ImGui.
 *
 * Extracted from ChatPanel::render() inline lambdas (Phase 2.2 of chat_panel_ref.md).
 * Handles: colored text, item/spell/quest/achievement link tooltips+icons,
 * URL click-to-open, shift-click link insertion.
 */
class ChatMarkupRenderer {
public:
    /** Render a list of segments with ImGui. baseColor is the message-type color. */
    void render(const std::vector<ChatSegment>& segments,
                const ImVec4& baseColor,
                const MarkupRenderContext& ctx) const;
};

} // namespace ui
} // namespace wowee
