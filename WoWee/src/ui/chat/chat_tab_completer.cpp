// ChatTabCompleter — cycling tab-completion state machine.
// Extracted from ChatPanel tab-completion logic (Phase 5.1).
#include "ui/chat/chat_tab_completer.hpp"

namespace wowee { namespace ui {

void ChatTabCompleter::startCompletion(const std::string& prefix,
                                       std::vector<std::string> candidates) {
    prefix_ = prefix;
    matches_ = std::move(candidates);
    matchIdx_ = matches_.empty() ? -1 : 0;
}

bool ChatTabCompleter::next() {
    if (matches_.empty()) return false;
    ++matchIdx_;
    if (matchIdx_ >= static_cast<int>(matches_.size()))
        matchIdx_ = 0;
    return true;
}

std::string ChatTabCompleter::getCurrentMatch() const {
    if (matchIdx_ < 0 || matchIdx_ >= static_cast<int>(matches_.size()))
        return "";
    return matches_[matchIdx_];
}

void ChatTabCompleter::reset() {
    prefix_.clear();
    matches_.clear();
    matchIdx_ = -1;
}

} // namespace ui
} // namespace wowee
