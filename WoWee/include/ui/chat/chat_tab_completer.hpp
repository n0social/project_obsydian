// ChatTabCompleter — cycling tab-completion state machine.
// Extracted from scattered vars in ChatPanel (Phase 5.1).
#pragma once

#include <string>
#include <vector>

namespace wowee {
namespace ui {

/**
 * Stateful tab-completion engine.
 *
 * The caller gathers candidates and calls startCompletion() or cycle().
 * The completer stores the prefix, sorted matches, and current index.
 */
class ChatTabCompleter {
public:
    /**
     * Start a new completion session with the given candidates.
     * Resets the index to 0.
     */
    void startCompletion(const std::string& prefix, std::vector<std::string> candidates);

    /**
     * Cycle to the next match. Returns true if there are matches.
     * If the prefix changed, the caller should call startCompletion() instead.
     */
    bool next();

    /** Get the current match, or "" if no matches. */
    std::string getCurrentMatch() const;

    /** Get the number of matches in the current session. */
    size_t matchCount() const { return matches_.size(); }

    /** Check if a completion session is active. */
    bool isActive() const { return matchIdx_ >= 0; }

    /** Get the current prefix. */
    const std::string& getPrefix() const { return prefix_; }

    /** Reset the completer (e.g. on text change or arrow key). */
    void reset();

private:
    std::string prefix_;
    std::vector<std::string> matches_;
    int matchIdx_ = -1;
};

} // namespace ui
} // namespace wowee
