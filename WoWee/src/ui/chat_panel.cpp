#include "ui/chat_panel.hpp"
#include "ui/chat/chat_utils.hpp"
#include "ui/chat/macro_evaluator.hpp"
#include "ui/chat/game_state_adapter.hpp"
#include "ui/chat/input_modifier_adapter.hpp"
#include "ui/chat/gm_command_data.hpp"
#include "ui/inventory_screen.hpp"
#include "ui/spellbook_screen.hpp"
#include "ui/quest_log_screen.hpp"
#include "ui/ui_colors.hpp"
#include "rendering/vk_context.hpp"
#include "core/application.hpp"
#include "addons/addon_manager.hpp"
#include "core/coordinates.hpp"
#include "core/input.hpp"
#include "rendering/renderer.hpp"
#include "rendering/animation_controller.hpp"
#include "rendering/camera.hpp"
#include "rendering/camera_controller.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/audio_engine.hpp"
#include "audio/ui_sound_manager.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/dbc_layout.hpp"
#include "game/expansion_profile.hpp"
#include "game/character.hpp"
#include "core/logger.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <chrono>
#include <ctime>
#include <unordered_set>
#include <unordered_map>

namespace {
    // Common ImGui colors (aliases)
    using namespace wowee::ui::colors;
    constexpr auto& kColorRed        = kRed;
    constexpr auto& kColorBrightGreen= kBrightGreen;
    constexpr auto& kColorDarkGray   = kDarkGray;

    // ---------------------------------------------------------------------------
    // formatChatMessage — build the display string for a single chat message.
    // Extracted from ChatPanel::render() message loop (Phase 6.2).
    // ---------------------------------------------------------------------------
    std::string formatChatMessage(
        const wowee::game::MessageChatData& msg,
        const std::string& processedMessage,
        const std::string& resolvedSenderName,
        const std::string& tsPrefix,
        wowee::game::GameHandler& gameHandler)
    {
        using CT = wowee::game::ChatType;

        // Build chat tag prefix: <GM>, <AFK>, <DND>
        std::string tagPrefix;
        if (msg.chatTag & 0x04) tagPrefix = "<GM> ";
        else if (msg.chatTag & 0x01) tagPrefix = "<AFK> ";
        else if (msg.chatTag & 0x02) tagPrefix = "<DND> ";

        if (msg.type == CT::ACHIEVEMENT || msg.type == CT::GUILD_ACHIEVEMENT) {
            // Achievement packets carry only a sender GUID. Name resolution is
            // asynchronous, so expand %s here—where resolvedSenderName is current—
            // rather than permanently baking an early GUID/unknown fallback into
            // chat history when the packet first arrives.
            std::string rendered = processedMessage;
            const std::string earner = resolvedSenderName.empty()
                ? "Someone" : resolvedSenderName;
            size_t pos = 0;
            while ((pos = rendered.find("%s", pos)) != std::string::npos) {
                rendered.replace(pos, 2, earner);
                pos += earner.size();
            }
            return tsPrefix + rendered;
        }
        if (msg.type == CT::SYSTEM || msg.type == CT::TEXT_EMOTE)
            return tsPrefix + processedMessage;

        if (!resolvedSenderName.empty()) {
            if (msg.type == CT::SAY || msg.type == CT::MONSTER_SAY || msg.type == CT::MONSTER_PARTY)
                return tsPrefix + tagPrefix + resolvedSenderName + " says: " + processedMessage;
            if (msg.type == CT::YELL || msg.type == CT::MONSTER_YELL)
                return tsPrefix + tagPrefix + resolvedSenderName + " yells: " + processedMessage;
            if (msg.type == CT::WHISPER || msg.type == CT::MONSTER_WHISPER || msg.type == CT::RAID_BOSS_WHISPER)
                return tsPrefix + tagPrefix + resolvedSenderName + " whispers: " + processedMessage;
            if (msg.type == CT::WHISPER_INFORM) {
                const std::string& target = !msg.receiverName.empty() ? msg.receiverName : resolvedSenderName;
                return tsPrefix + "To " + target + ": " + processedMessage;
            }
            if (msg.type == CT::MONSTER_EMOTE || msg.type == CT::RAID_BOSS_EMOTE)
                return tsPrefix + processedMessage;
            if (msg.type == CT::EMOTE)
                return tsPrefix + tagPrefix + resolvedSenderName + " " + processedMessage;
            if (msg.type == CT::CHANNEL && !msg.channelName.empty()) {
                int chIdx = gameHandler.getChannelIndex(msg.channelName);
                std::string chDisplay = chIdx > 0
                    ? "[" + std::to_string(chIdx) + ". " + msg.channelName + "]"
                    : "[" + msg.channelName + "]";
                return tsPrefix + chDisplay + " [" + tagPrefix + resolvedSenderName + "]: " + processedMessage;
            }
            return tsPrefix + "[" + std::string(wowee::ui::ChatTabManager::getChatTypeName(msg.type)) + "] " + tagPrefix + resolvedSenderName + ": " + processedMessage;
        }

        bool isGroupType =
            msg.type == CT::PARTY || msg.type == CT::GUILD ||
            msg.type == CT::OFFICER || msg.type == CT::RAID ||
            msg.type == CT::RAID_LEADER || msg.type == CT::RAID_WARNING ||
            msg.type == CT::BATTLEGROUND || msg.type == CT::BATTLEGROUND_LEADER;
        if (isGroupType)
            return tsPrefix + "[" + std::string(wowee::ui::ChatTabManager::getChatTypeName(msg.type)) + "] " + processedMessage;
        return tsPrefix + processedMessage;
    }

    // ---------------------------------------------------------------------------
    // Input chat-type helpers. Indices match the selectedChatType_ dropdown:
    // 0=Say 1=Yell 2=Party 3=Guild 4=Whisper 5=Raid 6=Officer 7=BG 8=RW
    // 9=Instance 10=Channel
    // ---------------------------------------------------------------------------
    ImVec4 chatTypeIndexColor(int typeIdx) {
        switch (typeIdx) {
            case 1:  return kColorRed;                          // Yell
            case 2:  return wowee::ui::colors::kLightBlue;      // Party
            case 3:  return kColorBrightGreen;                  // Guild
            case 4:  return ImVec4(1.0f, 0.5f, 1.0f, 1.0f);     // Whisper
            case 5:  return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);     // Raid
            case 6:  return kColorBrightGreen;                  // Officer
            case 7:  return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);     // BG
            case 8:  return ImVec4(1.0f, 0.3f, 0.0f, 1.0f);     // Raid Warning
            case 9:  return wowee::ui::colors::kLightBlue;      // Instance
            case 10: return ImVec4(0.3f, 0.9f, 0.9f, 1.0f);     // Channel
            default: return wowee::ui::colors::kWhite;          // Say
        }
    }

    // Whether a chat type can currently be used, and why not when it can't.
    // Returned reason is shown grayed-out in the type picker.
    const char* chatTypeUnavailableReason(int typeIdx, wowee::game::GameHandler& gh) {
        switch (typeIdx) {
            case 2: case 9:                       // Party, Instance
                return gh.isInGroup() ? nullptr : "not in a group";
            case 5: case 7: case 8:               // Raid, BG, Raid Warning
                return gh.isInGroup() ? nullptr : "not in a raid";
            case 3: case 6:                       // Guild, Officer
                return gh.isInGuild() ? nullptr : "not in a guild";
            case 10:                              // Channel
                return gh.getJoinedChannels().empty() ? "no channels joined" : nullptr;
            default:
                return nullptr;                   // Say, Yell, Whisper
        }
    }
}

namespace wowee { namespace ui {

ChatPanel::ChatPanel() {
    // ChatTabManager constructor handles tab initialization
    registerAllCommands();
}

// Tab init and filtering moved to ChatTabManager (Phase 1.3)


void ChatPanel::render(game::GameHandler& gameHandler,
                       InventoryScreen& inventoryScreen,
                       SpellbookScreen& spellbookScreen,
                       QuestLogScreen& questLogScreen) {
    // Cache game handler for input callback lambda (player name tab-completion)
    cachedGameHandler_ = &gameHandler;

    auto* window = services_.window;
    auto* assetMgr = services_.assetManager;
    float screenW = window ? static_cast<float>(window->getWidth()) : 1280.0f;
    float screenH = window ? static_cast<float>(window->getHeight()) : 720.0f;
    // Use the persisted size when the user has resized the window, otherwise fall
    // back to a responsive default sized against the current screen.
    float chatW = settings.windowWidth  > 0.0f ? settings.windowWidth  : std::min(500.0f, screenW * 0.4f);
    float chatH = settings.windowHeight > 0.0f ? settings.windowHeight : 220.0f;
    float chatX = 8.0f;
    float chatY = screenH - chatH - 80.0f;  // Above action bar
    if (!chatWindowPosInit_) {
        chatWindowPos_ = ImVec2(chatX, chatY);
        chatWindowPosInit_ = true;
    }
    // The window is always resizable via its grips. Seed the size on first use, then
    // let the user drag it; the size is captured below so it persists. Locking only
    // pins the position (top-left, so the bottom-right grip stays usable) and blocks
    // moving — it does NOT disable resizing.
    ImGui::SetNextWindowSize(ImVec2(chatW, chatH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 120.0f), ImVec2(screenW, screenH));
    if (chatWindowLocked_) {
        ImGui::SetNextWindowPos(chatWindowPos_, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(chatWindowPos_, ImGuiCond_FirstUseEver);
    }
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavInputs;
    if (chatWindowLocked_) {
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.08f, settings.backgroundAlpha));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, settings.backgroundAlpha * 0.5f));
    ImGui::Begin("Chat", nullptr, flags);

    // Persist the current size (resizable in either lock state) so a manual resize
    // survives locking and restart. Position is only tracked while unlocked.
    ImVec2 sz = ImGui::GetWindowSize();
    settings.windowWidth  = sz.x;
    settings.windowHeight = sz.y;
    if (!chatWindowLocked_) {
        chatWindowPos_ = ImGui::GetWindowPos();
    }

    // Update unread counts via ChatTabManager (Phase 1.3)
    tabManager_.updateUnread(gameHandler.getChatHistory(), activeChatTab);

    // Ctrl + mouse wheel anywhere over the chat window cycles tabs.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().KeyCtrl) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            int n = tabManager_.getTabCount();
            int dir = wheel > 0.0f ? -1 : 1;
            pendingChatTab_ = ((activeChatTab + dir) % n + n) % n;
        }
    }

    // Chat tabs (rendered via ChatTabManager)
    if (ImGui::BeginTabBar("ChatTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int i = 0; i < tabManager_.getTabCount(); ++i) {
            int unread = tabManager_.getUnreadCount(i);
            // Format directly into a stack buffer when an unread count is present;
            // otherwise pass the tab name reference straight through.
            const std::string& baseName = tabManager_.getTabName(i);
            char tabLabelBuf[96];
            const char* tabLabel;
            if (i > 0 && unread > 0) {
                std::snprintf(tabLabelBuf, sizeof(tabLabelBuf), "%s (%d)###%s",
                              baseName.c_str(), unread, baseName.c_str());
                tabLabel = tabLabelBuf;
            } else {
                std::snprintf(tabLabelBuf, sizeof(tabLabelBuf), "%s###%s",
                              baseName.c_str(), baseName.c_str());
                tabLabel = tabLabelBuf;
            }
            // Flash tab text color when unread messages exist
            bool hasUnread = (i > 0 && unread > 0);
            if (hasUnread) {
                float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f * pulse, 0.2f * pulse, 1.0f));
            }
            ImGuiTabItemFlags tabFlags = (pendingChatTab_ == i) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(tabLabel, nullptr, tabFlags)) {
                if (activeChatTab != i) {
                    activeChatTab = i;
                    // Clear unread count when tab becomes active
                    tabManager_.clearUnread(i);
                    // Point the input at this tab's natural destination
                    onTabActivated(i, gameHandler);
                }
                ImGui::EndTabItem();
            }
            if (hasUnread) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s%s", ChatTabManager::getTabTooltip(i),
                                  "\n(Ctrl+wheel cycles tabs, right-click for options)");
            }
            if (ImGui::BeginPopupContextItem()) {
                if (unread > 0 && ImGui::MenuItem("Mark as read"))
                    tabManager_.clearUnread(i);
                if (ImGui::MenuItem("Mark all tabs as read"))
                    tabManager_.markAllRead();
                ImGui::EndPopup();
            }
        }
        pendingChatTab_ = -1;

        // Trailing "Find" toggle — search box over the visible history
        if (ImGui::TabItemButton(chatFilterActive_ ? "Find*" : "Find",
                                 ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
            chatFilterActive_ = !chatFilterActive_;
            if (chatFilterActive_) refocusFilterInput_ = true;
            else chatFilterBuffer_[0] = '\0';
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Search the chat history");

        // Trailing quick-settings menu
        if (ImGui::TabItemButton("...", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
            ImGui::OpenPopup("ChatQuickMenu");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Chat options");
        if (ImGui::BeginPopup("ChatQuickMenu")) {
            bool changed = false;
            changed |= ImGui::Checkbox("Timestamps", &settings.showTimestamps);
            ImGui::SetNextItemWidth(120);
            changed |= ImGui::Combo("Text size", &settings.fontSize, "Small\0Medium\0Large\0");
            ImGui::SetNextItemWidth(120);
            changed |= ImGui::SliderFloat("Background", &settings.backgroundAlpha, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::Checkbox("Fade old messages", &settings.fadeMessages);
            if (settings.fadeMessages) {
                ImGui::SetNextItemWidth(120);
                changed |= ImGui::SliderFloat("Fade after", &settings.messageFadeTime, 5.0f, 120.0f, "%.0fs");
            }
            changed |= ImGui::Checkbox("Lock window", &settings.windowLocked);
            ImGui::Separator();
            if (ImGui::MenuItem("Mark all tabs as read"))
                tabManager_.markAllRead();
            ImGui::TextDisabled("More in Settings > Chat");
            if (changed && saveSettingsFn) saveSettingsFn();
            ImGui::EndPopup();
        }

        ImGui::EndTabBar();
    }

    // Search filter row (toggled by the Find button)
    if (chatFilterActive_) {
        if (refocusFilterInput_) {
            ImGui::SetKeyboardFocusHere();
            refocusFilterInput_ = false;
        }
        ImGui::SetNextItemWidth(-130);
        ImGui::InputTextWithHint("##ChatFilter", "Find in chat...",
                                 chatFilterBuffer_, sizeof(chatFilterBuffer_));
        ImGui::SameLine();
        if (chatFilterBuffer_[0] != '\0')
            ImGui::Text("%d match%s", chatFilterMatches_, chatFilterMatches_ == 1 ? "" : "es");
        else
            ImGui::TextDisabled("type to search");
        ImGui::SameLine();
        if (ImGui::SmallButton("X##CloseFilter")) {
            chatFilterActive_ = false;
            chatFilterBuffer_[0] = '\0';
        }
    }

    // Chat history
    const auto& chatHistory = gameHandler.getChatHistory();

    // Apply chat font size scaling
    float chatScale = chatFontSize == 0 ? 0.85f : (chatFontSize == 2 ? 1.2f : 1.0f);
    ImGui::SetWindowFontScale(chatScale);

    ImGui::BeginChild("ChatHistory", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2 - 4), true, ImGuiWindowFlags_HorizontalScrollbar);
    bool chatHistoryHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // Markup parsing and rendering delegated to ChatMarkupParser / ChatMarkupRenderer (Phase 2)
    MarkupRenderContext markupCtx;
    markupCtx.gameHandler     = &gameHandler;
    markupCtx.inventory       = &inventoryScreen;
    markupCtx.spellbook       = &spellbookScreen;
    markupCtx.questLog        = &questLogScreen;
    markupCtx.assetMgr        = assetMgr;
    markupCtx.getSpellIcon    = getSpellIcon;
    markupCtx.chatInputBuffer = chatInputBuffer_;
    markupCtx.chatInputBufSize = sizeof(chatInputBuffer_);
    markupCtx.moveCursorToEnd = &chatInputMoveCursorToEnd_;

    // Determine local player name for mention detection (case-insensitive)
    std::string selfNameLower;
    {
        const auto* ch = gameHandler.getActiveCharacter();
        if (ch && !ch->name.empty()) {
            selfNameLower = ch->name;
            for (auto& c : selfNameLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    // Scan NEW messages (beyond chatMentionSeenCount_) for mentions and play notification sound
    if (!selfNameLower.empty() && chatHistory.size() > chatMentionSeenCount_) {
        for (size_t mi = chatMentionSeenCount_; mi < chatHistory.size(); ++mi) {
            const auto& mMsg = chatHistory[mi];
            // Skip outgoing whispers, system, and monster messages
            if (mMsg.type == game::ChatType::WHISPER_INFORM ||
                mMsg.type == game::ChatType::SYSTEM) continue;
            // Case-insensitive search in message body
            std::string bodyLower = mMsg.message;
            for (auto& c : bodyLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (bodyLower.find(selfNameLower) != std::string::npos) {
                if (auto* ac = services_.audioCoordinator) {
                    if (auto* ui = ac->getUiSoundManager())
                        ui->playWhisperReceived();
                }
                break; // play at most once per scan pass
            }
        }
        chatMentionSeenCount_ = chatHistory.size();
    } else if (chatHistory.size() <= chatMentionSeenCount_) {
        chatMentionSeenCount_ = chatHistory.size();  // reset if history was cleared
    }

    // Whisper toast scanning left in GameScreen (will move to ToastManager later)

    auto nowTime = std::chrono::system_clock::now();

    // Cached lines embed the mention highlight, which depends on the local
    // player's name — invalidate everything when it changes (login/char swap).
    if (chatCacheSelfName_ != selfNameLower) {
        chatCacheSelfName_ = selfNameLower;
        chatLineCache_.clear();
    }
    // Prune cache entries for messages that scrolled out of the history window.
    if (!chatHistory.empty() && chatLineCache_.size() > chatHistory.size() * 2) {
        const uint64_t minUid = chatHistory.front().uid;
        for (auto it = chatLineCache_.begin(); it != chatLineCache_.end();) {
            if (it->first < minUid) it = chatLineCache_.erase(it);
            else ++it;
        }
    }

    // Case-folded needle for the history search filter
    std::string filterLower;
    if (chatFilterActive_ && chatFilterBuffer_[0] != '\0') {
        filterLower = chatFilterBuffer_;
        for (auto& c : filterLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    int filterMatches = 0;

    int chatMsgIdx = 0;
    for (const auto& msg : chatHistory) {
        if (!tabManager_.shouldShowMessage(msg, activeChatTab)) continue;

        // Compute message age for fade-out (suspended while searching)
        float msgAlpha = 1.0f;
        if (settings.fadeMessages && filterLower.empty() && !chatHistoryHovered && !chatInputActive_) {
            auto elapsed = std::chrono::duration<float>(nowTime - msg.timestamp).count();
            float fadeStart = settings.messageFadeTime;
            float fadeDuration = 5.0f;
            if (elapsed > fadeStart) {
                msgAlpha = 1.0f - std::min(1.0f, (elapsed - fadeStart) / fadeDuration);
                if (msgAlpha <= 0.01f) continue;
            }
        }

        const std::string& resolvedSenderName = [&]() -> const std::string& {
            if (!msg.senderName.empty()) return msg.senderName;
            if (msg.senderGuid == 0) return msg.senderName;
            const std::string& cached = gameHandler.lookupName(msg.senderGuid);
            if (!cached.empty()) return cached;
            return msg.senderName;
        }();

        ImVec4 color = ChatTabManager::getChatTypeColor(msg.type);
        color.w *= msgAlpha;

        // Fetch the formatted + parsed line from the cache; build it on first
        // sight of the message (or when the sender name / timestamp toggle
        // changed since it was built).
        CachedChatLine* line = nullptr;
        {
            auto cIt = chatLineCache_.find(msg.uid);
            if (cIt != chatLineCache_.end() &&
                cIt->second.tsEnabled == chatShowTimestamps &&
                cIt->second.senderNameUsed == resolvedSenderName) {
                line = &cIt->second;
            }
        }
        if (!line) {
            std::string processedMessage = chat_utils::replaceGenderPlaceholders(msg.message, gameHandler);

            std::string tsPrefix;
            if (chatShowTimestamps) {
                auto tt = std::chrono::system_clock::to_time_t(msg.timestamp);
                std::tm tm{};
#ifdef _WIN32
                localtime_s(&tm, &tt);
#else
                localtime_r(&tt, &tm);
#endif
                char tsBuf[16];
                snprintf(tsBuf, sizeof(tsBuf), "[%02d:%02d] ", tm.tm_hour, tm.tm_min);
                tsPrefix = tsBuf;
            }

            std::string fullMsg = formatChatMessage(msg, processedMessage, resolvedSenderName, tsPrefix, gameHandler);

            CachedChatLine built;
            built.senderNameUsed = resolvedSenderName;
            built.tsEnabled = chatShowTimestamps;
            if (!selfNameLower.empty() &&
                msg.type != game::ChatType::WHISPER_INFORM &&
                msg.type != game::ChatType::SYSTEM &&
                fullMsg.size() >= selfNameLower.size()) {
                const size_t nlen = selfNameLower.size();
                const size_t last = fullMsg.size() - nlen;
                for (size_t i = 0; i <= last && !built.isMention; ++i) {
                    bool match = true;
                    for (size_t j = 0; j < nlen; ++j) {
                        unsigned char a = static_cast<unsigned char>(fullMsg[i + j]);
                        unsigned char b = static_cast<unsigned char>(selfNameLower[j]);
                        if (std::tolower(a) != b) { match = false; break; }
                    }
                    if (match) built.isMention = true;
                }
            }
            built.segments = markupParser_.parse(fullMsg);
            built.fullMsg = std::move(fullMsg);
            line = &chatLineCache_.insert_or_assign(msg.uid, std::move(built)).first->second;
        }
        const bool isMention = line->isMention;

        // History search: show only lines containing the needle
        if (!filterLower.empty()) {
            std::string lineLower = line->fullMsg;
            for (auto& c : lineLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lineLower.find(filterLower) == std::string::npos) continue;
            ++filterMatches;
        }

        // Alternate row tinting for readability
        if (chatMsgIdx % 2 == 1) {
            ImVec2 rowStart = ImGui::GetCursorScreenPos();
            float availW = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(
                rowStart, ImVec2(rowStart.x + availW, rowStart.y + ImGui::GetTextLineHeight() + 2),
                IM_COL32(255, 255, 255, static_cast<int>(8 * msgAlpha)));
        }

        ImGui::PushID(chatMsgIdx++);
        ImGui::BeginGroup();
        {
            ImVec4 renderColor = isMention ? ImVec4(1.0f, 0.9f, 0.35f, msgAlpha) : color;
            markupRenderer_.render(line->segments, renderColor, markupCtx);
        }
        ImGui::EndGroup();
        if (isMention) {
            ImVec2 rMin = ImGui::GetItemRectMin();
            ImVec2 rMax = ImGui::GetItemRectMax();
            float availW = ImGui::GetContentRegionAvail().x + ImGui::GetCursorScreenPos().x - rMin.x;
            ImGui::GetWindowDrawList()->AddRectFilled(
                rMin, ImVec2(rMin.x + availW, rMax.y),
                IM_COL32(255, 200, 50, static_cast<int>(45 * msgAlpha)));
        }

        // Right-click context menu
        bool isPlayerMsg = !resolvedSenderName.empty() &&
            msg.type != game::ChatType::SYSTEM &&
            msg.type != game::ChatType::TEXT_EMOTE &&
            msg.type != game::ChatType::MONSTER_SAY &&
            msg.type != game::ChatType::MONSTER_YELL &&
            msg.type != game::ChatType::MONSTER_WHISPER &&
            msg.type != game::ChatType::MONSTER_EMOTE &&
            msg.type != game::ChatType::MONSTER_PARTY &&
            msg.type != game::ChatType::RAID_BOSS_WHISPER &&
            msg.type != game::ChatType::RAID_BOSS_EMOTE;

        if (ImGui::BeginPopupContextItem("ChatMsgCtx")) {
            // When/where header: exact time + chat type (channel name if any)
            {
                auto tt = std::chrono::system_clock::to_time_t(msg.timestamp);
                std::tm tm{};
#ifdef _WIN32
                localtime_s(&tm, &tt);
#else
                localtime_r(&tt, &tm);
#endif
                const char* typeName = (msg.type == game::ChatType::CHANNEL && !msg.channelName.empty())
                    ? msg.channelName.c_str()
                    : ChatTabManager::getChatTypeName(msg.type);
                ImGui::TextDisabled("%02d:%02d:%02d - %s", tm.tm_hour, tm.tm_min, tm.tm_sec, typeName);
                ImGui::Separator();
            }
            if (isPlayerMsg) {
                ImGui::TextDisabled("%s", resolvedSenderName.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Whisper")) {
                    selectedChatType_ = 4;
                    strncpy(whisperTargetBuffer_, resolvedSenderName.c_str(), sizeof(whisperTargetBuffer_) - 1);
                    whisperTargetBuffer_[sizeof(whisperTargetBuffer_) - 1] = '\0';
                    refocusChatInput_ = true;
                }
                if (ImGui::MenuItem("Invite to Group")) {
                    gameHandler.inviteToGroup(resolvedSenderName);
                }
                if (ImGui::MenuItem("Add Friend")) {
                    gameHandler.addFriend(resolvedSenderName);
                }
                if (ImGui::MenuItem("Ignore")) {
                    gameHandler.addIgnore(resolvedSenderName);
                }
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Copy Message")) {
                ImGui::SetClipboardText(line->fullMsg.c_str());
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    chatFilterMatches_ = filterMatches;

    // Auto-scroll to bottom; track whether user has scrolled up
    {
        float scrollY    = ImGui::GetScrollY();
        float scrollMaxY = ImGui::GetScrollMaxY();
        bool atBottom = (scrollMaxY <= 0.0f) || (scrollY >= scrollMaxY - 2.0f);
        if (atBottom || chatForceScrollToBottom_) {
            ImGui::SetScrollHereY(1.0f);
            chatScrolledUp_ = false;
            chatForceScrollToBottom_ = false;
        } else {
            chatScrolledUp_ = true;
        }
    }

    ImGui::EndChild();

    // Reset font scale after chat history
    ImGui::SetWindowFontScale(1.0f);

    // "Jump to bottom" indicator when scrolled up
    if (chatScrolledUp_) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.35f, 0.7f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f,  0.9f, 1.0f));
        if (ImGui::SmallButton("  v  New messages  ")) {
            chatForceScrollToBottom_ = true;
        }
        ImGui::PopStyleColor(2);
    }

    // Chat input row — compact WoW-style: [Type] input
    const char* chatTypes[] = { "Say", "Yell", "Party", "Guild", "Whisper", "Raid", "Officer", "Battleground", "Raid Warning", "Instance", "Channel" };
    const char* chatTypeLabels[] = { "[Say]", "[Yell]", "[Party]", "[Guild]", "[Whisper]", "[Raid]", "[Officer]", "[BG]", "[RW]", "[Instance]", "[Channel]" };

    ImVec4 inputColor = chatTypeIndexColor(selectedChatType_);

    // Type selector: colored button that opens an availability-aware picker
    ImGui::PushStyleColor(ImGuiCol_Text, inputColor);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.25f));
    if (ImGui::SmallButton(chatTypeLabels[selectedChatType_]))
        ImGui::OpenPopup("ChatTypePopup");
    ImGui::PopStyleColor(4);
    ImGui::SetItemTooltip("Where your message goes.\nClick to change - or press Tab in the empty input.");
    if (ImGui::BeginPopup("ChatTypePopup")) {
        for (int i = 0; i < 11; ++i) {
            const char* reason = chatTypeUnavailableReason(i, gameHandler);
            if (reason) {
                char label[64];
                std::snprintf(label, sizeof(label), "%s  (%s)", chatTypes[i], reason);
                ImGui::PushStyleColor(ImGuiCol_Text, kColorDarkGray);
                ImGui::Selectable(label, false, ImGuiSelectableFlags_Disabled);
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, chatTypeIndexColor(i));
                if (ImGui::Selectable(chatTypes[i], selectedChatType_ == i))
                    selectedChatType_ = i;
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();

    // Auto-fill whisper target when switching to WHISPER mode
    if (selectedChatType_ == 4 && lastChatType_ != 4) {
        // Just switched to WHISPER mode
        if (gameHandler.hasTarget()) {
            auto target = gameHandler.getTarget();
            if (target && target->getType() == game::ObjectType::PLAYER) {
                auto player = std::static_pointer_cast<game::Player>(target);
                if (!player->getName().empty()) {
                    strncpy(whisperTargetBuffer_, player->getName().c_str(), sizeof(whisperTargetBuffer_) - 1);
                    whisperTargetBuffer_[sizeof(whisperTargetBuffer_) - 1] = '\0';
                }
            }
        }
    }
    lastChatType_ = selectedChatType_;

    // Show whisper target inline
    if (selectedChatType_ == 4) {
        ImGui::PushStyleColor(ImGuiCol_Text, inputColor);
        ImGui::Text("To:");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputText("##WhisperTarget", whisperTargetBuffer_, sizeof(whisperTargetBuffer_));
        ImGui::SameLine();
    }

    // Show channel picker inline — numbered like the /1../9 shortcuts
    if (selectedChatType_ == 10) {
        const auto& channels = gameHandler.getJoinedChannels();
        if (channels.empty()) {
            ImGui::TextDisabled("(no channels)");
            ImGui::SameLine();
        } else {
            if (selectedChannelIdx_ >= static_cast<int>(channels.size())) selectedChannelIdx_ = 0;
            char chLabel[96];
            std::snprintf(chLabel, sizeof(chLabel), "%d. %s",
                          selectedChannelIdx_ + 1, channels[selectedChannelIdx_].c_str());
            ImGui::SetNextItemWidth(130);
            if (ImGui::BeginCombo("##ChannelPicker", chLabel)) {
                for (int ci = 0; ci < static_cast<int>(channels.size()); ++ci) {
                    bool selected = (ci == selectedChannelIdx_);
                    std::snprintf(chLabel, sizeof(chLabel), "%d. %s", ci + 1, channels[ci].c_str());
                    if (ImGui::Selectable(chLabel, selected)) selectedChannelIdx_ = ci;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Joined channels - type /1../9 to switch quickly");
            ImGui::SameLine();
        }
    }

    ImGui::SetNextItemWidth(-1);
    if (refocusChatInput_) {
        ImGui::SetKeyboardFocusHere();
        refocusChatInput_ = false;
    }

    detectChannelPrefix(gameHandler);

    ImGui::PushStyleColor(ImGuiCol_Text, inputColor);

    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_CallbackAlways |
                                     ImGuiInputTextFlags_CallbackHistory |
                                     ImGuiInputTextFlags_CallbackCompletion;

    // Contextual placeholder: says where the message will go and how to switch
    char inputHint[128];
    if (selectedChatType_ == 4) {
        if (whisperTargetBuffer_[0] != '\0')
            std::snprintf(inputHint, sizeof(inputHint), "Whisper %.100s...", whisperTargetBuffer_);
        else
            std::snprintf(inputHint, sizeof(inputHint), "Whisper: enter a name in To: first");
    } else if (selectedChatType_ == 10) {
        std::snprintf(inputHint, sizeof(inputHint), "Message the channel... (Tab switches, /help lists commands)");
    } else {
        std::snprintf(inputHint, sizeof(inputHint), "%s... (Tab switches, / for commands)",
                      chatTypes[selectedChatType_]);
    }

    if (ImGui::InputTextWithHint("##ChatInput", inputHint, chatInputBuffer_, sizeof(chatInputBuffer_), inputFlags, &ChatPanel::inputTextCallback, this)) {
        sendChatMessage(gameHandler);
        // Close chat input on send so movement keys work immediately.
        refocusChatInput_ = false;
        chatInputCooldown_ = 2;  // suppress Enter re-opening chat for 2 frames
        ImGui::ClearActiveID();
    }
    ImGui::PopStyleColor();

    if (chatInputCooldown_ > 0) --chatInputCooldown_;

    if (ImGui::IsItemActive()) {
        chatInputActive_ = true;
    } else {
        chatInputActive_ = false;
    }

    // Click in chat history area (received messages) → focus input.
    if (chatHistoryHovered && ImGui::IsMouseClicked(0)) {
        refocusChatInput_ = true;
    }

    ImGui::End();
    ImGui::PopStyleColor(2);  // WindowBg, ChildBg
}

// ---------------------------------------------------------------------------
// detectChannelPrefix — auto-detect /say, /party, /whisper etc. prefixes
// and switch the chat type dropdown + strip the prefix from the input buffer.
// Extracted from render() (Phase 6.2).
// ---------------------------------------------------------------------------
void ChatPanel::detectChannelPrefix(game::GameHandler& gameHandler) {
    std::string buf(chatInputBuffer_);
    if (buf.size() < 2 || buf[0] != '/') return;

    size_t sp = buf.find(' ', 1);
    if (sp == std::string::npos) return;

    std::string cmd = buf.substr(1, sp - 1);
    for (char& c : cmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    int detected = -1;
    bool isReply = false;
    if (cmd == "s" || cmd == "say") detected = 0;
    else if (cmd == "y" || cmd == "yell" || cmd == "shout") detected = 1;
    else if (cmd == "p" || cmd == "party") detected = 2;
    else if (cmd == "g" || cmd == "guild") detected = 3;
    else if (cmd == "w" || cmd == "whisper" || cmd == "tell" || cmd == "t") detected = 4;
    else if (cmd == "r" || cmd == "reply") { detected = 4; isReply = true; }
    else if (cmd == "raid" || cmd == "rsay" || cmd == "ra") detected = 5;
    else if (cmd == "o" || cmd == "officer" || cmd == "osay") detected = 6;
    else if (cmd == "bg" || cmd == "battleground") detected = 7;
    else if (cmd == "rw" || cmd == "raidwarning") detected = 8;
    else if (cmd == "i" || cmd == "instance") detected = 9;
    else if (cmd.size() == 1 && cmd[0] >= '1' && cmd[0] <= '9') detected = 10;

    if (detected < 0 || (selectedChatType_ == detected && detected != 10 && !isReply)) return;

    if (detected == 10) {
        int chanIdx = cmd[0] - '1';
        const auto& chans = gameHandler.getJoinedChannels();
        if (chanIdx >= 0 && chanIdx < static_cast<int>(chans.size()))
            selectedChannelIdx_ = chanIdx;
    }
    selectedChatType_ = detected;
    std::string remaining = buf.substr(sp + 1);

    if (detected == 4 && isReply) {
        std::string lastSender = gameHandler.getLastWhisperSender();
        if (!lastSender.empty()) {
            strncpy(whisperTargetBuffer_, lastSender.c_str(), sizeof(whisperTargetBuffer_) - 1);
            whisperTargetBuffer_[sizeof(whisperTargetBuffer_) - 1] = '\0';
        }
    } else if (detected == 4) {
        size_t msgStart = remaining.find(' ');
        if (msgStart != std::string::npos) {
            std::string wTarget = remaining.substr(0, msgStart);
            strncpy(whisperTargetBuffer_, wTarget.c_str(), sizeof(whisperTargetBuffer_) - 1);
            whisperTargetBuffer_[sizeof(whisperTargetBuffer_) - 1] = '\0';
            remaining = remaining.substr(msgStart + 1);
        } else {
            strncpy(whisperTargetBuffer_, remaining.c_str(), sizeof(whisperTargetBuffer_) - 1);
            whisperTargetBuffer_[sizeof(whisperTargetBuffer_) - 1] = '\0';
            remaining = "";
        }
    }
    strncpy(chatInputBuffer_, remaining.c_str(), sizeof(chatInputBuffer_) - 1);
    chatInputBuffer_[sizeof(chatInputBuffer_) - 1] = '\0';
    chatInputMoveCursorToEnd_ = true;
}

// ---------------------------------------------------------------------------
// inputTextCallback — static ImGui input text callback for tab-completion,
// cursor management, and sent-message history (Up/Down arrows).
// Extracted from render() inline lambda (Phase 6.2).
// ---------------------------------------------------------------------------
int ChatPanel::inputTextCallback(ImGuiInputTextCallbackData* data) {
    auto* self = static_cast<ChatPanel*>(data->UserData);
    if (!self) return 0;

    // Cursor-to-end after channel switch
    if (self->chatInputMoveCursorToEnd_) {
        int len = static_cast<int>(std::strlen(data->Buf));
        data->CursorPos = len;
        data->SelectionStart = len;
        data->SelectionEnd = len;
        self->chatInputMoveCursorToEnd_ = false;
    }

    // Tab: slash-command autocomplete (Phase 5); with an empty input it
    // cycles the chat type instead (Shift+Tab cycles backwards).
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        if (data->BufTextLen == 0) {
            self->cycleChatType(ImGui::GetIO().KeyShift);
            return 0;
        }
        if (data->BufTextLen > 0 && data->Buf[0] == '/') {
            std::string fullBuf(data->Buf, data->BufTextLen);
            size_t spacePos = fullBuf.find(' ');
            std::string word = (spacePos != std::string::npos) ? fullBuf.substr(0, spacePos) : fullBuf;
            std::string rest = (spacePos != std::string::npos) ? fullBuf.substr(spacePos) : "";

            std::string lowerCmd = word.substr(1);
            for (auto& ch : lowerCmd) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            if (!self->tabCompleter_.isActive() || self->tabCompleter_.getPrefix() != lowerCmd) {
                auto candidates = self->commandRegistry_.getCompletions(lowerCmd);
                for (auto& c : candidates) c = "/" + c;
                self->tabCompleter_.startCompletion(lowerCmd, std::move(candidates));
            } else {
                self->tabCompleter_.next();
            }

            std::string match = self->tabCompleter_.getCurrentMatch();
            if (!match.empty()) {
                if (self->tabCompleter_.matchCount() == 1 && rest.empty())
                    match += ' ';
                std::string newBuf = match + rest;
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, newBuf.c_str());
            }
        } else if (data->BufTextLen > 1 && data->Buf[0] == '.') {
            // GM dot-command tab-completion
            std::string fullBuf(data->Buf, data->BufTextLen);
            size_t spacePos = fullBuf.find(' ');
            std::string word = (spacePos != std::string::npos) ? fullBuf.substr(0, spacePos) : fullBuf;
            std::string rest = (spacePos != std::string::npos) ? fullBuf.substr(spacePos) : "";

            std::string lowerDot = word;
            for (auto& ch : lowerDot) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

            if (!self->tabCompleter_.isActive() || self->tabCompleter_.getPrefix() != lowerDot) {
                std::vector<std::string> candidates;
                for (const auto& entry : kGmCommands) {
                    std::string dotName = "." + std::string(entry.name);
                    if (dotName.size() >= lowerDot.size() &&
                        dotName.compare(0, lowerDot.size(), lowerDot) == 0) {
                        candidates.push_back(dotName);
                    }
                }
                std::sort(candidates.begin(), candidates.end());
                self->tabCompleter_.startCompletion(lowerDot, std::move(candidates));
            } else {
                self->tabCompleter_.next();
            }

            std::string match = self->tabCompleter_.getCurrentMatch();
            if (!match.empty()) {
                if (self->tabCompleter_.matchCount() == 1 && rest.empty())
                    match += ' ';
                std::string newBuf = match + rest;
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, newBuf.c_str());
            }
        } else if (data->BufTextLen > 0) {
            // Player name tab-completion
            std::string fullBuf(data->Buf, data->BufTextLen);
            size_t spacePos = fullBuf.find(' ');
            bool isNameCommand = false;
            std::string namePrefix;
            size_t replaceStart = 0;

            if (fullBuf[0] == '/' && spacePos != std::string::npos) {
                std::string cmd = fullBuf.substr(0, spacePos);
                for (char& c : cmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (cmd == "/w" || cmd == "/whisper" || cmd == "/invite" ||
                    cmd == "/trade" || cmd == "/duel" || cmd == "/follow" ||
                    cmd == "/inspect" || cmd == "/friend" || cmd == "/removefriend" ||
                    cmd == "/ignore" || cmd == "/unignore" || cmd == "/who" ||
                    cmd == "/t" || cmd == "/target" || cmd == "/kick" ||
                    cmd == "/uninvite" || cmd == "/ginvite" || cmd == "/gkick") {
                    namePrefix = fullBuf.substr(spacePos + 1);
                    size_t nameSpace = namePrefix.find(' ');
                    if (nameSpace == std::string::npos) {
                        isNameCommand = true;
                        replaceStart = spacePos + 1;
                    }
                }
            }

            if (isNameCommand && !namePrefix.empty()) {
                std::string lowerPrefix = namePrefix;
                for (char& c : lowerPrefix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                if (!self->tabCompleter_.isActive() || self->tabCompleter_.getPrefix() != lowerPrefix) {
                    std::vector<std::string> candidates;
                    auto* gh = self->cachedGameHandler_;
                    if (!gh) return 0;
                    for (const auto& m : gh->getPartyData().members) {
                        if (m.name.empty()) continue;
                        std::string lname = m.name;
                        for (char& c : lname) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (lname.compare(0, lowerPrefix.size(), lowerPrefix) == 0)
                            candidates.push_back(m.name);
                    }
                    for (const auto& c : gh->getContacts()) {
                        if (!c.isFriend() || c.name.empty()) continue;
                        std::string lname = c.name;
                        for (char& cc : lname) cc = static_cast<char>(std::tolower(static_cast<unsigned char>(cc)));
                        if (lname.compare(0, lowerPrefix.size(), lowerPrefix) == 0) {
                            bool dup = false;
                            for (const auto& em : candidates)
                                if (em == c.name) { dup = true; break; }
                            if (!dup) candidates.push_back(c.name);
                        }
                    }
                    for (const auto& [guid, entity] : gh->getEntityManager().getEntities()) {
                        if (!entity || entity->getType() != game::ObjectType::PLAYER) continue;
                        auto player = std::static_pointer_cast<game::Player>(entity);
                        if (player->getName().empty()) continue;
                        std::string lname = player->getName();
                        for (char& cc : lname) cc = static_cast<char>(std::tolower(static_cast<unsigned char>(cc)));
                        if (lname.compare(0, lowerPrefix.size(), lowerPrefix) == 0) {
                            bool dup = false;
                            for (const auto& em : candidates)
                                if (em == player->getName()) { dup = true; break; }
                            if (!dup) candidates.push_back(player->getName());
                        }
                    }
                    if (!gh->getLastWhisperSender().empty()) {
                        std::string lname = gh->getLastWhisperSender();
                        for (char& cc : lname) cc = static_cast<char>(std::tolower(static_cast<unsigned char>(cc)));
                        if (lname.compare(0, lowerPrefix.size(), lowerPrefix) == 0) {
                            bool dup = false;
                            for (const auto& em : candidates)
                                if (em == gh->getLastWhisperSender()) { dup = true; break; }
                            if (!dup) candidates.insert(candidates.begin(), gh->getLastWhisperSender());
                        }
                    }
                    self->tabCompleter_.startCompletion(lowerPrefix, std::move(candidates));
                } else {
                    self->tabCompleter_.next();
                }

                std::string match = self->tabCompleter_.getCurrentMatch();
                if (!match.empty()) {
                    std::string prefix = fullBuf.substr(0, replaceStart);
                    std::string newBuf = prefix + match;
                    if (self->tabCompleter_.matchCount() == 1) newBuf += ' ';
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, newBuf.c_str());
                }
            }
        }
        return 0;
    }

    // Up/Down arrow: cycle through sent message history
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        self->tabCompleter_.reset();

        const int histSize = static_cast<int>(self->chatSentHistory_.size());
        if (histSize == 0) return 0;

        if (data->EventKey == ImGuiKey_UpArrow) {
            if (self->chatHistoryIdx_ == -1)
                self->chatHistoryIdx_ = histSize - 1;
            else if (self->chatHistoryIdx_ > 0)
                --self->chatHistoryIdx_;
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (self->chatHistoryIdx_ == -1) return 0;
            ++self->chatHistoryIdx_;
            if (self->chatHistoryIdx_ >= histSize) {
                self->chatHistoryIdx_ = -1;
                data->DeleteChars(0, data->BufTextLen);
                return 0;
            }
        }

        if (self->chatHistoryIdx_ >= 0 && self->chatHistoryIdx_ < histSize) {
            const std::string& entry = self->chatSentHistory_[self->chatHistoryIdx_];
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, entry.c_str());
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// cycleChatType — Tab key with an empty input walks the chat types that are
// actually available right now: Say → Party → Guild → Whisper → Channel.
// ---------------------------------------------------------------------------
void ChatPanel::cycleChatType(bool backwards) {
    auto* gh = cachedGameHandler_;
    if (!gh) return;

    // Candidate ring in cycle order (indices into selectedChatType_)
    static constexpr int kCycleOrder[] = {0 /*Say*/, 2 /*Party*/, 3 /*Guild*/, 4 /*Whisper*/, 10 /*Channel*/};
    std::vector<int> ring;
    for (int t : kCycleOrder)
        if (!chatTypeUnavailableReason(t, *gh)) ring.push_back(t);
    if (ring.empty()) return;

    int cur = -1;
    for (size_t i = 0; i < ring.size(); ++i)
        if (ring[i] == selectedChatType_) { cur = static_cast<int>(i); break; }

    int n = static_cast<int>(ring.size());
    int next = (cur < 0) ? 0 : ((cur + (backwards ? -1 : 1)) % n + n) % n;
    selectedChatType_ = ring[next];

    // Entering whisper mode with no target: reuse the last whisper partner
    if (selectedChatType_ == 4 && whisperTargetBuffer_[0] == '\0') {
        const std::string& last = gh->getLastWhisperSender();
        if (!last.empty()) {
            strncpy(whisperTargetBuffer_, last.c_str(), sizeof(whisperTargetBuffer_) - 1);
            whisperTargetBuffer_[sizeof(whisperTargetBuffer_) - 1] = '\0';
        }
    }
}

// ---------------------------------------------------------------------------
// onTabActivated — switching to a tab points the input at that tab's natural
// destination, so Whispers/Guild/Trade tabs are ready to type into.
// ---------------------------------------------------------------------------
void ChatPanel::onTabActivated(int tab, game::GameHandler& gameHandler) {
    switch (tab) {
        case 2: { // Whispers
            selectedChatType_ = 4;
            if (whisperTargetBuffer_[0] == '\0') {
                const std::string& last = gameHandler.getLastWhisperSender();
                if (!last.empty()) {
                    strncpy(whisperTargetBuffer_, last.c_str(), sizeof(whisperTargetBuffer_) - 1);
                    whisperTargetBuffer_[sizeof(whisperTargetBuffer_) - 1] = '\0';
                }
            }
            break;
        }
        case 3: // Guild
            if (gameHandler.isInGuild()) selectedChatType_ = 3;
            break;
        case 4: { // Trade/LFG — pick the first city channel if one is joined
            const auto& chans = gameHandler.getJoinedChannels();
            if (!chans.empty()) {
                selectedChatType_ = 10;
                for (int ci = 0; ci < static_cast<int>(chans.size()); ++ci) {
                    const std::string& ch = chans[ci];
                    if (ch.find("Trade") != std::string::npos ||
                        ch.find("General") != std::string::npos ||
                        ch.find("LookingForGroup") != std::string::npos) {
                        selectedChannelIdx_ = ci;
                        break;
                    }
                }
            }
            break;
        }
        default: // General / Combat keep whatever the player was using
            break;
    }
}

// --- Command registration (calls into each command group file) ---
// Forward declarations of registration functions from command files
void registerSystemCommands(ChatCommandRegistry& reg);
void registerSocialCommands(ChatCommandRegistry& reg);
void registerChannelCommands(ChatCommandRegistry& reg);
void registerCombatCommands(ChatCommandRegistry& reg);
void registerGroupCommands(ChatCommandRegistry& reg);
void registerGuildCommands(ChatCommandRegistry& reg);
void registerTargetCommands(ChatCommandRegistry& reg);
void registerEmoteCommands(ChatCommandRegistry& reg);
void registerMiscCommands(ChatCommandRegistry& reg);
void registerHelpCommands(ChatCommandRegistry& reg);
void registerGmCommands(ChatCommandRegistry& reg);

void ChatPanel::registerAllCommands() {
    registerSystemCommands(commandRegistry_);
    registerSocialCommands(commandRegistry_);
    registerChannelCommands(commandRegistry_);
    registerCombatCommands(commandRegistry_);
    registerGroupCommands(commandRegistry_);
    registerGuildCommands(commandRegistry_);
    registerTargetCommands(commandRegistry_);
    registerEmoteCommands(commandRegistry_);
    registerMiscCommands(commandRegistry_);
    registerHelpCommands(commandRegistry_);
    registerGmCommands(commandRegistry_);
}

// renderBubbles delegates to ChatBubbleManager (Phase 1.4)
void ChatPanel::renderBubbles(game::GameHandler& gameHandler) {
    bubbleManager_.render(gameHandler, services_);
}

// setupCallbacks delegates to ChatBubbleManager (Phase 1.4)
void ChatPanel::setupCallbacks(game::GameHandler& gameHandler) {
    bubbleManager_.setupCallback(gameHandler);
}

void ChatPanel::insertChatLink(const std::string& link) {
    if (link.empty()) return;
    size_t curLen = strlen(chatInputBuffer_);
    if (curLen + link.size() + 1 <= sizeof(chatInputBuffer_)) {
        strncat(chatInputBuffer_, link.c_str(), sizeof(chatInputBuffer_) - curLen - 1);
        chatInputMoveCursorToEnd_ = true;
        refocusChatInput_ = true;
    }
}

void ChatPanel::activateSlashInput() {
    refocusChatInput_ = true;
    chatInputBuffer_[0] = '/';
    chatInputBuffer_[1] = '\0';
    chatInputMoveCursorToEnd_ = true;
}

void ChatPanel::activateInput() {
    if (chatInputCooldown_ > 0) return;
    refocusChatInput_ = true;
}

void ChatPanel::setWhisperTarget(const std::string& name) {
    selectedChatType_ = 4;  // WHISPER
    strncpy(whisperTargetBuffer_, name.c_str(), sizeof(whisperTargetBuffer_) - 1);
    whisperTargetBuffer_[sizeof(whisperTargetBuffer_) - 1] = '\0';
    refocusChatInput_ = true;
}

ChatPanel::SlashCommands ChatPanel::consumeSlashCommands() {
    SlashCommands result = slashCmds_;
    slashCmds_ = {};
    return result;
}

namespace {
    bool isPortBotTarget(const std::string& target) {
        std::string t = chat_utils::toLower(chat_utils::trim(target));
        return t == "portbot" || t == "gmbot" || t == "telebot";
    }

    std::string buildPortBotCommand(const std::string& rawInput) {
        std::string input = chat_utils::trim(rawInput);
        if (input.empty()) return "";

        std::string lower = chat_utils::toLower(input);
        if (lower == "help" || lower == "?") return "__help__";

        if (lower.rfind(".tele ", 0) == 0 || lower.rfind(".go ", 0) == 0) return input;
        if (lower.rfind("xyz ", 0) == 0) return ".go " + input;

        if (lower == "sw" || lower == "stormwind") return ".tele stormwind";
        if (lower == "if" || lower == "ironforge") return ".tele ironforge";
        if (lower == "darn" || lower == "darnassus") return ".tele darnassus";
        if (lower == "org" || lower == "orgrimmar") return ".tele orgrimmar";
        if (lower == "tb" || lower == "thunderbluff") return ".tele thunderbluff";
        if (lower == "uc" || lower == "undercity") return ".tele undercity";
        if (lower == "shatt" || lower == "shattrath") return ".tele shattrath";
        if (lower == "dal" || lower == "dalaran") return ".tele dalaran";

        return ".tele " + input;
    }
} // anonymous namespace

// Collect all non-comment, non-empty lines from a macro body.
static std::vector<std::string> allMacroCommands(const std::string& macroText) {
    std::vector<std::string> cmds;
    size_t pos = 0;
    while (pos <= macroText.size()) {
        size_t nl = macroText.find('\n', pos);
        std::string line = (nl != std::string::npos) ? macroText.substr(pos, nl - pos) : macroText.substr(pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos) line = line.substr(start);
        if (!line.empty() && line.front() != '#')
            cmds.push_back(std::move(line));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return cmds;
}

// Execute all non-comment lines of a macro body in sequence.
void ChatPanel::executeMacroText(game::GameHandler& gameHandler,
                                  const std::string& macroText) {
    macroStopped_ = false;
    for (const auto& cmd : allMacroCommands(macroText)) {
        strncpy(chatInputBuffer_, cmd.c_str(), sizeof(chatInputBuffer_) - 1);
        chatInputBuffer_[sizeof(chatInputBuffer_) - 1] = '\0';
        sendChatMessage(gameHandler);
        if (macroStopped_) break;
    }
    macroStopped_ = false;
}

void ChatPanel::sendChatMessage(game::GameHandler& gameHandler) {
    if (strlen(chatInputBuffer_) == 0) return;
    std::string input(chatInputBuffer_);

    // Save to sent-message history (skip pure whitespace, cap at 50 entries)
    {
        bool allSpace = true;
        for (char c : input) { if (!std::isspace(static_cast<unsigned char>(c))) { allSpace = false; break; } }
        if (!allSpace) {
            if (chatSentHistory_.empty() || chatSentHistory_.back() != input) {
                chatSentHistory_.push_back(input);
                if (chatSentHistory_.size() > 50)
                    chatSentHistory_.erase(chatSentHistory_.begin());
            }
        }
    }
    chatHistoryIdx_ = -1;

    game::ChatType type = game::ChatType::SAY;
    std::string message = input;
    std::string target;

    // GM dot-prefix commands (.gm, .tele, .additem, etc.)
    if (input.size() > 1 && input[0] == '.') {
        LOG_INFO("GM command: '", input, "' — sending as SAY to server");
        gameHandler.sendChatMessage(game::ChatType::SAY, input, "");

        std::string dotCmd = input;
        size_t sp = dotCmd.find(' ');
        std::string cmdPart = (sp != std::string::npos)
            ? dotCmd.substr(1, sp - 1) : dotCmd.substr(1);
        for (char& c : cmdPart) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        std::string feedback;
        for (const auto& entry : kGmCommands) {
            if (entry.name == cmdPart) {
                feedback = "Sent: " + input + "  (" + std::string(entry.help) + ")";
                break;
            }
        }
        if (feedback.empty())
            feedback = "Sent: " + input
                + "  (requires GM access — server console: account set gmlevel <user> 3 -1)";
        gameHandler.addLocalChatMessage(chat_utils::makeSystemMessage(feedback));
        chatInputBuffer_[0] = '\0';
        return;
    }

    // Slash commands
    if (input.size() > 1 && input[0] == '/') {
        std::string command = input.substr(1);
        size_t spacePos = command.find(' ');
        std::string cmd = (spacePos != std::string::npos) ? command.substr(0, spacePos) : command;
        std::string cmdLower = cmd;
        for (char& c : cmdLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        // /run <lua code>
        if ((cmdLower == "run" || cmdLower == "script") && spacePos != std::string::npos) {
            std::string luaCode = command.substr(spacePos + 1);
            auto* am = services_.addonManager;
            if (am) {
                am->runScript(luaCode);
            } else {
                gameHandler.addUIError("Addon system not initialized.");
            }
            chatInputBuffer_[0] = '\0';
            return;
        }

        // /dump <expression>
        if ((cmdLower == "dump" || cmdLower == "print") && spacePos != std::string::npos) {
            std::string expr = command.substr(spacePos + 1);
            auto* am = services_.addonManager;
            if (am && am->isInitialized()) {
                std::string wrapped = "local __v = " + expr +
                    "; if type(__v) == 'table' then "
                    "  local parts = {} "
                    "  for k,v in pairs(__v) do parts[#parts+1] = tostring(k)..'='..tostring(v) end "
                    "  print('{' .. table.concat(parts, ', ') .. '}') "
                    "else print(tostring(__v)) end";
                am->runScript(wrapped);
            } else {
                game::MessageChatData errMsg;
                errMsg.type = game::ChatType::SYSTEM;
                errMsg.language = game::ChatLanguage::UNIVERSAL;
                errMsg.message = "Addon system not initialized.";
                gameHandler.addLocalChatMessage(errMsg);
            }
            chatInputBuffer_[0] = '\0';
            return;
        }

        // Addon slash commands (SlashCmdList)
        {
            auto* am = services_.addonManager;
            if (am && am->isInitialized()) {
                std::string slashCmd = "/" + cmdLower;
                std::string slashArgs;
                if (spacePos != std::string::npos) slashArgs = command.substr(spacePos + 1);
                if (am->getLuaEngine()->dispatchSlashCommand(slashCmd, slashArgs)) {
                    chatInputBuffer_[0] = '\0';
                    return;
                }
            }
        }

        // Dispatch through command registry (Phase 3.11)
        std::string args;
        if (spacePos != std::string::npos)
            args = command.substr(spacePos + 1);

        ChatCommandContext ctx{gameHandler, services_, *this, args, cmdLower};
        ChatCommandResult result = commandRegistry_.dispatch(cmdLower, ctx);
        if (result.handled) {
            if (result.clearInput)
                chatInputBuffer_[0] = '\0';
            return;
        }

        // Emote fallthrough — dynamic DBC lookup for emote text.
        {
            std::string targetName;
            const std::string* targetNamePtr = nullptr;
            if (gameHandler.hasTarget()) {
                auto targetEntity = gameHandler.getTarget();
                if (targetEntity) {
                    targetName = chat_utils::getEntityDisplayName(targetEntity);
                    if (!targetName.empty()) targetNamePtr = &targetName;
                }
            }

            std::string emoteText = rendering::AnimationController::getEmoteText(cmdLower, targetNamePtr);
            if (!emoteText.empty()) {
                auto* renderer = services_.renderer;
                if (renderer) {
                    if (auto* ac = renderer->getAnimationController()) ac->playEmote(cmdLower);
                }

                uint32_t dbcId = rendering::AnimationController::getEmoteDbcId(cmdLower);
                if (dbcId != 0) {
                    uint64_t targetGuid = gameHandler.hasTarget() ? gameHandler.getTargetGuid() : 0;
                    gameHandler.sendTextEmote(dbcId, targetGuid);
                }

                game::MessageChatData msg;
                msg.type = game::ChatType::TEXT_EMOTE;
                msg.language = game::ChatLanguage::COMMON;
                msg.message = emoteText;
                gameHandler.addLocalChatMessage(msg);

                chatInputBuffer_[0] = '\0';
                return;
            }
        }

        // Unrecognized slash command — fall through to dropdown chat type
        message = input;
    }

    // Determine chat type from dropdown selection
    switch (selectedChatType_) {
        case 0: type = game::ChatType::SAY; break;
        case 1: type = game::ChatType::YELL; break;
        case 2: type = game::ChatType::PARTY; break;
        case 3: type = game::ChatType::GUILD; break;
        case 4: type = game::ChatType::WHISPER; target = whisperTargetBuffer_; break;
        case 5: type = game::ChatType::RAID; break;
        case 6: type = game::ChatType::OFFICER; break;
        case 7: type = game::ChatType::BATTLEGROUND; break;
        case 8: type = game::ChatType::RAID_WARNING; break;
        case 9: type = game::ChatType::PARTY; break;
        case 10: {
            const auto& chans = gameHandler.getJoinedChannels();
            if (!chans.empty() && selectedChannelIdx_ < static_cast<int>(chans.size())) {
                type = game::ChatType::CHANNEL;
                target = chans[selectedChannelIdx_];
            } else { type = game::ChatType::SAY; }
            break;
        }
        default: type = game::ChatType::SAY; break;
    }

    // PortBot whisper interception
    if (type == game::ChatType::WHISPER && isPortBotTarget(target)) {
        std::string cmd = buildPortBotCommand(message);
        game::MessageChatData msg;
        msg.type = game::ChatType::SYSTEM;
        msg.language = game::ChatLanguage::UNIVERSAL;
        if (cmd.empty() || cmd == "__help__") {
            msg.message = "PortBot: /w PortBot <dest>. Aliases: sw if darn org tb uc shatt dal. Also supports '.tele ...' or 'xyz x y z [map [o]]'.";
            gameHandler.addLocalChatMessage(msg);
            chatInputBuffer_[0] = '\0';
            return;
        }

        gameHandler.sendChatMessage(game::ChatType::SAY, cmd, "");
        msg.message = "PortBot executed: " + cmd;
        gameHandler.addLocalChatMessage(msg);
        chatInputBuffer_[0] = '\0';
        return;
    }

    // Validate whisper has a target
    if (type == game::ChatType::WHISPER && target.empty()) {
        game::MessageChatData msg;
        msg.type = game::ChatType::SYSTEM;
        msg.language = game::ChatLanguage::UNIVERSAL;
        msg.message = "You must specify a player name for whisper.";
        gameHandler.addLocalChatMessage(msg);
        chatInputBuffer_[0] = '\0';
        return;
    }

    if (!message.empty()) {
        gameHandler.sendChatMessage(type, message, target);
    }
    chatInputBuffer_[0] = '\0';
}

} // namespace ui
} // namespace wowee
