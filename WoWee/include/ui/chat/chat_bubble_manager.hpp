#pragma once

#include "ui/ui_services.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace wowee {
namespace game { class GameHandler; }
namespace ui {

/**
 * Manages 3D-projected chat bubbles above entities.
 *
 * Extracted from ChatPanel (Phase 1.4 of chat_panel_ref.md).
 * Owns bubble lifecycle: add, update (tick), render (ImGui overlay).
 */
class ChatBubbleManager {
public:
    /** Add or replace a bubble for the given entity. */
    void addBubble(uint64_t senderGuid, const std::string& message, bool isYell);

    /** Render and tick all active bubbles (projects to screen via camera). */
    void render(game::GameHandler& gameHandler, const UIServices& services);

    /** Register the chat-bubble callback on GameHandler (call once per session). */
    void setupCallback(game::GameHandler& gameHandler);

    bool empty() const { return bubbles_.empty(); }

private:
    struct ChatBubble {
        uint64_t senderGuid = 0;
        std::string message;
        float timeRemaining = 0.0f;
        float totalDuration = 0.0f;
        bool isYell = false;
    };
    std::vector<ChatBubble> bubbles_;
    bool callbackSet_ = false;

    static constexpr size_t kMaxBubbles = 10;
};

} // namespace ui
} // namespace wowee
