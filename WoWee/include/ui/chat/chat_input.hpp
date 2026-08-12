#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace wowee {
namespace ui {

/**
 * Chat input state: buffer, whisper target, sent-history, focus management.
 *
 * Extracted from ChatPanel (Phase 1.2 of chat_panel_ref.md).
 * No UI or network dependencies — pure state management.
 */
class ChatInput {
public:
    // ---- Buffer access ----
    char*       getBuffer()            { return buffer_; }
    const char* getBuffer()      const { return buffer_; }
    size_t      getBufferSize()  const { return sizeof(buffer_); }

    char*       getWhisperBuffer()           { return whisperBuffer_; }
    const char* getWhisperBuffer()     const { return whisperBuffer_; }
    size_t      getWhisperBufferSize() const { return sizeof(whisperBuffer_); }

    void clear()    { buffer_[0] = '\0'; }
    bool isEmpty() const { return buffer_[0] == '\0'; }
    std::string getText() const { return std::string(buffer_); }
    void setText(const std::string& text) {
        strncpy(buffer_, text.c_str(), sizeof(buffer_) - 1);
        buffer_[sizeof(buffer_) - 1] = '\0';
    }

    // ---- Whisper target ----
    std::string getWhisperTarget() const { return std::string(whisperBuffer_); }
    void setWhisperTarget(const std::string& name) {
        strncpy(whisperBuffer_, name.c_str(), sizeof(whisperBuffer_) - 1);
        whisperBuffer_[sizeof(whisperBuffer_) - 1] = '\0';
    }

    // ---- Sent-message history (Up/Down arrow recall) ----
    void pushToHistory(const std::string& msg);
    std::string historyUp();
    std::string historyDown();
    void resetHistoryIndex() { historyIdx_ = -1; }
    int getHistoryIndex() const { return historyIdx_; }
    const std::vector<std::string>& getSentHistory() const { return sentHistory_; }

    // ---- Focus state ----
    bool isActive() const { return active_; }
    void setActive(bool v) { active_ = v; }

    bool shouldFocus() const { return focusRequested_; }
    void requestFocus() { focusRequested_ = true; }
    void clearFocusRequest() { focusRequested_ = false; }

    bool shouldMoveCursorToEnd() const { return moveCursorToEnd_; }
    void requestMoveCursorToEnd() { moveCursorToEnd_ = true; }
    void clearMoveCursorToEnd() { moveCursorToEnd_ = false; }

    // ---- Chat type selection ----
    int  getSelectedChatType() const { return selectedChatType_; }
    void setSelectedChatType(int t) { selectedChatType_ = t; }
    int  getLastChatType() const { return lastChatType_; }
    void setLastChatType(int t) { lastChatType_ = t; }
    int  getSelectedChannelIdx() const { return selectedChannelIdx_; }
    void setSelectedChannelIdx(int i) { selectedChannelIdx_ = i; }

    // ---- Link insertion (shift-click) ----
    void insertLink(const std::string& link);

    // ---- Slash key activation ----
    void activateSlashInput();

private:
    char buffer_[512]        = "";
    char whisperBuffer_[256] = "";
    bool active_             = false;
    bool focusRequested_     = false;
    bool moveCursorToEnd_    = false;

    // Chat type dropdown state
    int selectedChatType_   = 0;  // 0=SAY .. 10=CHANNEL
    int lastChatType_       = 0;
    int selectedChannelIdx_ = 0;

    // Sent-message history
    std::vector<std::string> sentHistory_;
    int historyIdx_ = -1;
    static constexpr int kMaxHistory = 50;
};

} // namespace ui
} // namespace wowee
