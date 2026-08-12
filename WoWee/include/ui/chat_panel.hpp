#pragma once

#include "game/game_handler.hpp"
#include "ui/ui_services.hpp"
#include "ui/chat/chat_settings.hpp"
#include "ui/chat/chat_input.hpp"
#include "ui/chat/chat_tab_manager.hpp"
#include "ui/chat/chat_bubble_manager.hpp"
#include "ui/chat/cast_sequence_tracker.hpp"
#include "ui/chat/chat_markup_parser.hpp"
#include "ui/chat/chat_markup_renderer.hpp"
#include "ui/chat/chat_command_registry.hpp"
#include "ui/chat/chat_tab_completer.hpp"
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering { class Renderer; }
namespace ui {

class InventoryScreen;
class SpellbookScreen;
class QuestLogScreen;

/**
 * Self-contained chat UI panel extracted from GameScreen.
 *
 * Owns all chat state: input buffer, sent-history, tab filtering,
 * slash-command parsing, chat bubbles, and chat-related settings.
 */
class ChatPanel {
public:
    ChatPanel();

    // ---- Main entry points (called by GameScreen) ----

    /**
     * Render the chat window (tabs, history, input, etc.)
     */
    void render(game::GameHandler& gameHandler,
               InventoryScreen& inventoryScreen,
               SpellbookScreen& spellbookScreen,
               QuestLogScreen& questLogScreen);

    /**
     * Render 3D-projected chat bubbles above entities.
     */
    void renderBubbles(game::GameHandler& gameHandler);

    /**
     * Register one-shot callbacks on GameHandler (call once per session).
     * Sets up the chat-bubble callback.
     */
    void setupCallbacks(game::GameHandler& gameHandler);

    // ---- Input helpers (called by GameScreen keybind handling) ----

    bool isChatInputActive() const { return chatInputActive_; }

    /** Insert a spell / item link into the chat input buffer (shift-click). */
    void insertChatLink(const std::string& link);

    /** Activate the input field with a leading '/' (slash key). */
    void activateSlashInput();

    /** Activate (focus) the input field (Enter key). */
    void activateInput();

    /** Request that the chat input be focused next frame. */
    void requestRefocus() { refocusChatInput_ = true; }

    /** Set up a whisper to the given player name and focus input. */
    void setWhisperTarget(const std::string& name);

    /** Execute a macro body (one line per 'click'). */
    void executeMacroText(game::GameHandler& gameHandler,
                          const std::string& macroText);

    // ---- Slash-command side-effects ----
    // GameScreen reads these each frame, then clears them.

    struct SlashCommands {
        bool showInspect   = false;
        bool toggleThreat  = false;
        bool showBgScore   = false;
        bool showGmTicket  = false;
        bool showWho       = false;
        bool toggleCombatLog = false;
        bool takeScreenshot = false;
    };

    /** Return accumulated slash-command flags and reset them. */
    SlashCommands consumeSlashCommands();

    // ---- Chat settings (delegated to ChatSettings) ----

    ChatSettings settings;
    int   activeChatTab       = 0;

    // Legacy accessors — forward to settings struct for external code
    // (GameScreen save/load reads these directly)
    bool& chatShowTimestamps       = settings.showTimestamps;
    int&  chatFontSize             = settings.fontSize;
    bool& chatAutoJoinGeneral      = settings.autoJoinGeneral;
    bool& chatAutoJoinTrade        = settings.autoJoinTrade;
    bool& chatAutoJoinLocalDefense = settings.autoJoinLocalDefense;
    bool& chatAutoJoinLFG          = settings.autoJoinLFG;
    bool& chatAutoJoinLocal        = settings.autoJoinLocal;

    /** Spell icon lookup callback — set by GameScreen each frame before render(). */
    std::function<VkDescriptorSet(uint32_t, pipeline::AssetManager*)> getSpellIcon;

    /** Persist-settings callback — set once by GameScreen so the in-window
     *  quick menu can save appearance changes immediately. */
    std::function<void()> saveSettingsFn;

    /** Render the "Chat" tab inside the Settings window (delegates to settings). */
    void renderSettingsTab(std::function<void()> saveSettingsFn) {
        settings.renderSettingsTab(std::move(saveSettingsFn));
    }

    /** Reset all chat settings to defaults (delegates to settings). */
    void restoreDefaults() { settings.restoreDefaults(); }

    // UIServices injection (Phase B singleton breaking)
    void setServices(const UIServices& services) { services_ = services; }

    // ---- Accessors for command system (Phase 3) ----
    char* getChatInputBuffer() { return chatInputBuffer_; }
    size_t getChatInputBufferSize() const { return sizeof(chatInputBuffer_); }
    char* getWhisperTargetBuffer() { return whisperTargetBuffer_; }
    size_t getWhisperTargetBufferSize() const { return sizeof(whisperTargetBuffer_); }
    int  getSelectedChatType() const { return selectedChatType_; }
    void setSelectedChatType(int t) { selectedChatType_ = t; }
    int  getSelectedChannelIdx() const { return selectedChannelIdx_; }
    bool& macroStopped() { return macroStopped_; }
    CastSequenceTracker& getCastSeqTracker() { return castSeqTracker_; }
    SlashCommands& getSlashCmds() { return slashCmds_; }
    UIServices& getServices() { return services_; }
    ChatCommandRegistry& getCommandRegistry() { return commandRegistry_; }

private:
    // Injected UI services (Phase B singleton breaking)
    UIServices services_;

    // ---- Chat input state ----
    // A ChatInput class exists at include/ui/chat/chat_input.hpp and is the
    // intended eventual home for these fields, but the Phase-6 ChatPanel
    // decomposition (6.2/6.6/6.7) shipped without migrating the input
    // buffers — chat_panel*.cpp still reads/writes them directly. Keep here
    // until a follow-up extraction lands.
    char chatInputBuffer_[512] = "";
    char whisperTargetBuffer_[256] = "";
    bool chatInputActive_ = false;
    int  chatInputCooldown_ = 0;  // frames to suppress re-activation after send
    int  selectedChatType_ = 0;  // 0=SAY .. 10=CHANNEL
    int  lastChatType_     = 0;
    int  selectedChannelIdx_ = 0;
    bool chatInputMoveCursorToEnd_ = false;
    bool refocusChatInput_ = false;

    // Sent-message history (Up/Down arrow recall)
    std::vector<std::string> chatSentHistory_;
    int chatHistoryIdx_ = -1;

    // ---- History search filter ----
    bool chatFilterActive_ = false;
    char chatFilterBuffer_[128] = "";
    int  chatFilterMatches_ = 0;   // result count from the previous frame
    bool refocusFilterInput_ = false;

    // Programmatic tab switch (Ctrl+wheel / quick menu); applied next frame
    // via ImGuiTabItemFlags_SetSelected, -1 = none pending.
    int pendingChatTab_ = -1;

    /** Sync the input chat type to a newly activated tab (Guild tab → guild
     *  chat, Whispers tab → whisper with last sender, Trade/LFG → channel). */
    void onTabActivated(int tab, game::GameHandler& gameHandler);

    /** Tab key with an empty input: cycle Say → Party → Guild → Whisper →
     *  Channel through the types currently available to the player. */
    void cycleChatType(bool backwards);

    // Macro stop flag
    bool macroStopped_ = false;

    // /castsequence state (delegated to CastSequenceTracker, Phase 1.5)
    CastSequenceTracker castSeqTracker_;

    // Command registry (Phase 3 — replaces if/else chain)
    ChatCommandRegistry commandRegistry_;
    void registerAllCommands();

    // Markup parser + renderer (Phase 2)
    ChatMarkupParser markupParser_;
    ChatMarkupRenderer markupRenderer_;

    // Per-message render cache. A chat line's formatted text and parsed
    // segments are immutable once built (modulo sender-name resolution and
    // the timestamp toggle), so formatting + markup parsing runs once per
    // message instead of once per message per frame.
    struct CachedChatLine {
        std::string senderNameUsed;  // rebuild when a name query resolves
        bool tsEnabled = false;      // rebuild when timestamp toggle flips
        bool isMention = false;
        std::string fullMsg;         // plain text (for Copy Message)
        std::vector<ChatSegment> segments;
    };
    std::unordered_map<uint64_t, CachedChatLine> chatLineCache_;
    std::string chatCacheSelfName_;  // cache cleared when this changes

    // Tab-completion (Phase 5 — delegated to ChatTabCompleter)
    ChatTabCompleter tabCompleter_;

    // Mention notification
    size_t chatMentionSeenCount_ = 0;

    // ---- Chat tabs (delegated to ChatTabManager) ----
    ChatTabManager tabManager_;

    // ---- Chat window visual state ----
    bool  chatScrolledUp_          = false;
    bool  chatForceScrollToBottom_ = false;
    // windowLocked is in settings.windowLocked (kept in sync via reference)
    bool& chatWindowLocked_        = settings.windowLocked;
    ImVec2 chatWindowPos_          = ImVec2(0.0f, 0.0f);
    bool  chatWindowPosInit_       = false;

    // ---- Chat bubbles (delegated to ChatBubbleManager) ----
    ChatBubbleManager bubbleManager_;

    // ---- Whisper toast state (populated in render, rendered by GameScreen/ToastManager) ----
    // Whisper scanning lives here because it's tightly coupled to chat history iteration.
    size_t whisperSeenCount_ = 0;

    // ---- Helpers ----
    void sendChatMessage(game::GameHandler& gameHandler);
    static int inputTextCallback(ImGuiInputTextCallbackData* data);
    void detectChannelPrefix(game::GameHandler& gameHandler);

    // Cached game handler for input callback (set each frame in render)
    game::GameHandler* cachedGameHandler_ = nullptr;

    // Join channel input buffer
    char joinChannelBuffer_[128] = "";

    // Slash command flags (accumulated, consumed by GameScreen)
    SlashCommands slashCmds_;
};

} // namespace ui
} // namespace wowee
