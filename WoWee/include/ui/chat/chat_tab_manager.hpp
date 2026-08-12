#pragma once

#include "game/world_packets.hpp"
#include <imgui.h>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace wowee {
namespace ui {

/**
 * Chat tab definitions, unread tracking, type colors, and type names.
 *
 * Extracted from ChatPanel (Phase 1.3 of chat_panel_ref.md).
 * Owns the tab configuration, unread badge counts, and message filtering.
 */
class ChatTabManager {
public:
    ChatTabManager();

    // ---- Tab access ----
    int getTabCount() const { return static_cast<int>(tabs_.size()); }
    const std::string& getTabName(int idx) const { return tabs_[idx].name; }
    uint64_t getTabTypeMask(int idx) const { return tabs_[idx].typeMask; }
    /** One-line description of what the tab shows (for hover tooltips). */
    static const char* getTabTooltip(int idx);

    // ---- Unread tracking ----
    int getUnreadCount(int idx) const;
    void clearUnread(int idx);
    void markAllRead();
    /** Scan new messages since last call and increment unread counters for non-active tabs. */
    void updateUnread(const std::deque<game::MessageChatData>& history, int activeTab);

    // ---- Message filtering ----
    bool shouldShowMessage(const game::MessageChatData& msg, int tabIndex) const;

    // ---- Chat type helpers (static, no state needed) ----
    static const char* getChatTypeName(game::ChatType type);
    static ImVec4 getChatTypeColor(game::ChatType type);

private:
    struct ChatTab {
        std::string name;
        uint64_t typeMask;
    };
    std::vector<ChatTab> tabs_;
    std::vector<int> unread_;
    size_t seenCount_ = 0;

    void initTabs();
};

} // namespace ui
} // namespace wowee
