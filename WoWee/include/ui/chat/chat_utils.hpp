#pragma once

#include "game/world_packets.hpp"
#include "game/entity.hpp"
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

namespace wowee {

// Forward declarations
namespace game {
    class GameHandler;
}

namespace ui {
namespace chat_utils {

/** Create a system-type chat message (used 15+ times throughout commands). */
inline game::MessageChatData makeSystemMessage(const std::string& text) {
    game::MessageChatData msg;
    msg.type     = game::ChatType::SYSTEM;
    msg.language = game::ChatLanguage::UNIVERSAL;
    msg.message  = text;
    return msg;
}

/** Trim leading/trailing whitespace from a string. */
inline std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

/** Convert string to lowercase (returns copy). */
inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

/**
 * Replace $g/$G gender, $n/$N name, $c/$C class, $r/$R race,
 * $p/$o/$s/$S pronoun, $b/$B linebreak, and |n linebreak placeholders.
 * Extracted from ChatPanel::replaceGenderPlaceholders (Phase 6.6).
 */
std::string replaceGenderPlaceholders(const std::string& text,
                                       game::GameHandler& gameHandler);

/** Get display name for any entity (Player/Unit/GameObject). */
std::string getEntityDisplayName(const std::shared_ptr<game::Entity>& entity);

} // namespace chat_utils
} // namespace ui
} // namespace wowee
