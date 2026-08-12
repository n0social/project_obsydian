#include "ui/chat/chat_input.hpp"

namespace wowee { namespace ui {

// Push a message to sent-history (skip pure whitespace, cap at kMaxHistory).
void ChatInput::pushToHistory(const std::string& msg) {
    bool allSpace = true;
    for (char c : msg) {
        if (!std::isspace(static_cast<unsigned char>(c))) { allSpace = false; break; }
    }
    if (allSpace) return;

    // Remove duplicate of last entry if identical
    if (sentHistory_.empty() || sentHistory_.back() != msg) {
        sentHistory_.push_back(msg);
        if (static_cast<int>(sentHistory_.size()) > kMaxHistory)
            sentHistory_.erase(sentHistory_.begin());
    }
    historyIdx_ = -1;  // reset browsing position after send
}

// Navigate up through sent-history. Returns the entry or "" if at start.
std::string ChatInput::historyUp() {
    const int histSize = static_cast<int>(sentHistory_.size());
    if (histSize == 0) return "";

    if (historyIdx_ == -1)
        historyIdx_ = histSize - 1;
    else if (historyIdx_ > 0)
        --historyIdx_;

    return sentHistory_[historyIdx_];
}

// Navigate down through sent-history. Returns the entry or "" if past end.
std::string ChatInput::historyDown() {
    const int histSize = static_cast<int>(sentHistory_.size());
    if (histSize == 0 || historyIdx_ == -1) return "";

    ++historyIdx_;
    if (historyIdx_ >= histSize) {
        historyIdx_ = -1;
        return "";
    }
    return sentHistory_[historyIdx_];
}

// Insert a spell / item link into the chat input buffer (shift-click).
void ChatInput::insertLink(const std::string& link) {
    if (link.empty()) return;
    size_t curLen = std::strlen(buffer_);
    if (curLen + link.size() + 1 < sizeof(buffer_)) {
        strncat(buffer_, link.c_str(), sizeof(buffer_) - curLen - 1);
        moveCursorToEnd_ = true;
        focusRequested_  = true;
    }
}

// Activate the input field with a leading '/' (slash key).
void ChatInput::activateSlashInput() {
    focusRequested_ = true;
    buffer_[0] = '/';
    buffer_[1] = '\0';
    moveCursorToEnd_ = true;
}

} // namespace ui
} // namespace wowee
