#include "ui/chat/chat_tab_manager.hpp"
#include "ui/ui_colors.hpp"
#include <algorithm>

namespace {
    using namespace wowee::ui::colors;
} // namespace

namespace wowee { namespace ui {

ChatTabManager::ChatTabManager() {
    initTabs();
}

void ChatTabManager::initTabs() {
    tabs_.clear();
    // General tab: shows everything
    tabs_.push_back({"General", ~0ULL});
    // Combat tab: system, loot, skills, achievements, and NPC speech/emotes
    tabs_.push_back({"Combat", (1ULL << static_cast<uint8_t>(game::ChatType::SYSTEM)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::LOOT)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::SKILL)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::ACHIEVEMENT)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::GUILD_ACHIEVEMENT)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::MONSTER_SAY)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::MONSTER_YELL)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::MONSTER_EMOTE)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::MONSTER_WHISPER)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::MONSTER_PARTY)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::RAID_BOSS_WHISPER)) |
                                (1ULL << static_cast<uint8_t>(game::ChatType::RAID_BOSS_EMOTE))});
    // Whispers tab
    tabs_.push_back({"Whispers", (1ULL << static_cast<uint8_t>(game::ChatType::WHISPER)) |
                                  (1ULL << static_cast<uint8_t>(game::ChatType::WHISPER_INFORM))});
    // Guild tab: guild and officer chat
    tabs_.push_back({"Guild", (1ULL << static_cast<uint8_t>(game::ChatType::GUILD)) |
                               (1ULL << static_cast<uint8_t>(game::ChatType::OFFICER)) |
                               (1ULL << static_cast<uint8_t>(game::ChatType::GUILD_ACHIEVEMENT))});
    // Trade/LFG tab: channel messages
    tabs_.push_back({"Trade/LFG", (1ULL << static_cast<uint8_t>(game::ChatType::CHANNEL))});

    unread_.assign(tabs_.size(), 0);
    seenCount_ = 0;
}

int ChatTabManager::getUnreadCount(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(unread_.size())) return 0;
    return unread_[idx];
}

void ChatTabManager::clearUnread(int idx) {
    if (idx >= 0 && idx < static_cast<int>(unread_.size()))
        unread_[idx] = 0;
}

void ChatTabManager::markAllRead() {
    std::fill(unread_.begin(), unread_.end(), 0);
}

const char* ChatTabManager::getTabTooltip(int idx) {
    switch (idx) {
        case 0: return "Everything: player chat, channels, system messages";
        case 1: return "Combat log: system, loot, skill-ups, and NPC dialogue";
        case 2: return "Private messages to and from you";
        case 3: return "Guild and officer chat";
        case 4: return "City channels: General, Trade, LookingForGroup, LocalDefense";
        default: return "";
    }
}

void ChatTabManager::updateUnread(const std::deque<game::MessageChatData>& history, int activeTab) {
    // Ensure unread array is sized correctly (guards against late init)
    if (unread_.size() != tabs_.size())
        unread_.assign(tabs_.size(), 0);
    // If history shrank (e.g. cleared), reset
    if (seenCount_ > history.size()) seenCount_ = 0;
    for (size_t mi = seenCount_; mi < history.size(); ++mi) {
        const auto& msg = history[mi];
        // For each non-General (non-0) tab that isn't currently active, check visibility
        for (int ti = 1; ti < static_cast<int>(tabs_.size()); ++ti) {
            if (ti == activeTab) continue;
            if (shouldShowMessage(msg, ti)) {
                unread_[ti]++;
            }
        }
    }
    seenCount_ = history.size();
}

bool ChatTabManager::shouldShowMessage(const game::MessageChatData& msg, int tabIndex) const {
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs_.size())) return true;
    const auto& tab = tabs_[tabIndex];
    if (tab.typeMask == ~0ULL) return true;  // General tab shows all

    uint64_t typeBit = 1ULL << static_cast<uint8_t>(msg.type);

    // For Trade/LFG tab (index 4), also filter by channel name
    if (tabIndex == 4 && msg.type == game::ChatType::CHANNEL) {
        const std::string& ch = msg.channelName;
        if (ch.find("Trade") == std::string::npos &&
            ch.find("General") == std::string::npos &&
            ch.find("LookingForGroup") == std::string::npos &&
            ch.find("Local") == std::string::npos) {
            return false;
        }
        return true;
    }

    return (tab.typeMask & typeBit) != 0;
}

// ---- Static chat type helpers ----

const char* ChatTabManager::getChatTypeName(game::ChatType type) {
    switch (type) {
        case game::ChatType::SAY: return "Say";
        case game::ChatType::YELL: return "Yell";
        case game::ChatType::EMOTE: return "Emote";
        case game::ChatType::TEXT_EMOTE: return "Emote";
        case game::ChatType::PARTY: return "Party";
        case game::ChatType::GUILD: return "Guild";
        case game::ChatType::OFFICER: return "Officer";
        case game::ChatType::RAID: return "Raid";
        case game::ChatType::RAID_LEADER: return "Raid Leader";
        case game::ChatType::RAID_WARNING: return "Raid Warning";
        case game::ChatType::BATTLEGROUND: return "Battleground";
        case game::ChatType::BATTLEGROUND_LEADER: return "Battleground Leader";
        case game::ChatType::WHISPER: return "Whisper";
        case game::ChatType::WHISPER_INFORM: return "To";
        case game::ChatType::SYSTEM: return "System";
        case game::ChatType::MONSTER_SAY: return "Say";
        case game::ChatType::MONSTER_YELL: return "Yell";
        case game::ChatType::MONSTER_EMOTE: return "Emote";
        case game::ChatType::CHANNEL: return "Channel";
        case game::ChatType::ACHIEVEMENT: return "Achievement";
        case game::ChatType::GUILD_ACHIEVEMENT: return "Guild Achievement";
        case game::ChatType::DND: return "DND";
        case game::ChatType::AFK: return "AFK";
        case game::ChatType::BG_SYSTEM_NEUTRAL:
        case game::ChatType::BG_SYSTEM_ALLIANCE:
        case game::ChatType::BG_SYSTEM_HORDE: return "System";
        default: return "Unknown";
    }
}

ImVec4 ChatTabManager::getChatTypeColor(game::ChatType type) {
    switch (type) {
        case game::ChatType::SAY:
            return kWhite;
        case game::ChatType::YELL:
            return kRed;
        case game::ChatType::EMOTE:
        case game::ChatType::TEXT_EMOTE:
            return ImVec4(1.0f, 0.7f, 0.3f, 1.0f);  // Orange
        case game::ChatType::PARTY:
            return ImVec4(0.5f, 0.5f, 1.0f, 1.0f);  // Light blue
        case game::ChatType::GUILD:
            return kBrightGreen;
        case game::ChatType::OFFICER:
            return ImVec4(0.3f, 0.8f, 0.3f, 1.0f);  // Dark green
        case game::ChatType::RAID:
            return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange
        case game::ChatType::RAID_LEADER:
            return ImVec4(1.0f, 0.4f, 0.0f, 1.0f);  // Darker orange
        case game::ChatType::RAID_WARNING:
            return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);  // Red
        case game::ChatType::BATTLEGROUND:
            return ImVec4(1.0f, 0.6f, 0.0f, 1.0f);  // Orange-gold
        case game::ChatType::BATTLEGROUND_LEADER:
            return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange
        case game::ChatType::WHISPER:
        case game::ChatType::WHISPER_INFORM:
            return ImVec4(1.0f, 0.5f, 1.0f, 1.0f);  // Pink
        case game::ChatType::SYSTEM:
            return kYellow;
        case game::ChatType::MONSTER_SAY:
            return kWhite;
        case game::ChatType::MONSTER_YELL:
            return kRed;
        case game::ChatType::MONSTER_EMOTE:
            return ImVec4(1.0f, 0.7f, 0.3f, 1.0f);  // Orange
        case game::ChatType::CHANNEL:
            return ImVec4(1.0f, 0.7f, 0.7f, 1.0f);  // Light pink
        case game::ChatType::ACHIEVEMENT:
            return ImVec4(1.0f, 1.0f, 0.0f, 1.0f);  // Bright yellow
        case game::ChatType::GUILD_ACHIEVEMENT:
            return kWarmGold;
        case game::ChatType::SKILL:
            return kCyan;
        case game::ChatType::LOOT:
            return ImVec4(0.8f, 0.5f, 1.0f, 1.0f);  // Light purple
        case game::ChatType::MONSTER_WHISPER:
        case game::ChatType::RAID_BOSS_WHISPER:
            return ImVec4(1.0f, 0.5f, 1.0f, 1.0f);  // Pink
        case game::ChatType::RAID_BOSS_EMOTE:
            return ImVec4(1.0f, 0.7f, 0.3f, 1.0f);  // Orange
        case game::ChatType::MONSTER_PARTY:
            return ImVec4(0.5f, 0.5f, 1.0f, 1.0f);  // Light blue
        case game::ChatType::BG_SYSTEM_NEUTRAL:
            return kWarmGold;
        case game::ChatType::BG_SYSTEM_ALLIANCE:
            return ImVec4(0.3f, 0.6f, 1.0f, 1.0f);  // Blue
        case game::ChatType::BG_SYSTEM_HORDE:
            return kRed;
        case game::ChatType::AFK:
        case game::ChatType::DND:
            return ImVec4(0.85f, 0.85f, 0.85f, 0.8f); // Light gray
        default:
            return kLightGray;
    }
}

} // namespace ui
} // namespace wowee
