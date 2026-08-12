// Unit tests for ChatTabCompleter (Phase 5).
#include <catch_amalgamated.hpp>
#include "ui/chat/chat_tab_completer.hpp"

using wowee::ui::ChatTabCompleter;

TEST_CASE("ChatTabCompleter — initial state", "[chat][tab]") {
    ChatTabCompleter tc;
    CHECK_FALSE(tc.isActive());
    CHECK(tc.matchCount() == 0);
    CHECK(tc.getCurrentMatch().empty());
    CHECK(tc.getPrefix().empty());
}

TEST_CASE("ChatTabCompleter — start with empty candidates", "[chat][tab]") {
    ChatTabCompleter tc;
    tc.startCompletion("foo", {});
    CHECK_FALSE(tc.isActive());
    CHECK(tc.matchCount() == 0);
    CHECK(tc.getCurrentMatch().empty());
    CHECK(tc.getPrefix() == "foo");
}

TEST_CASE("ChatTabCompleter — single candidate", "[chat][tab]") {
    ChatTabCompleter tc;
    tc.startCompletion("h", {"/help"});
    REQUIRE(tc.isActive());
    CHECK(tc.matchCount() == 1);
    CHECK(tc.getCurrentMatch() == "/help");
    // Cycling wraps to the same entry
    tc.next();
    CHECK(tc.getCurrentMatch() == "/help");
}

TEST_CASE("ChatTabCompleter — multiple candidates cycle", "[chat][tab]") {
    ChatTabCompleter tc;
    tc.startCompletion("s", {"/say", "/sit", "/stand"});
    REQUIRE(tc.isActive());
    CHECK(tc.matchCount() == 3);
    CHECK(tc.getCurrentMatch() == "/say");
    tc.next();
    CHECK(tc.getCurrentMatch() == "/sit");
    tc.next();
    CHECK(tc.getCurrentMatch() == "/stand");
    // Wraps around
    tc.next();
    CHECK(tc.getCurrentMatch() == "/say");
}

TEST_CASE("ChatTabCompleter — reset clears state", "[chat][tab]") {
    ChatTabCompleter tc;
    tc.startCompletion("s", {"/say", "/sit"});
    REQUIRE(tc.isActive());
    tc.reset();
    CHECK_FALSE(tc.isActive());
    CHECK(tc.matchCount() == 0);
    CHECK(tc.getCurrentMatch().empty());
    CHECK(tc.getPrefix().empty());
}

TEST_CASE("ChatTabCompleter — new prefix restarts", "[chat][tab]") {
    ChatTabCompleter tc;
    tc.startCompletion("s", {"/say", "/sit"});
    tc.next(); // now at /sit
    CHECK(tc.getCurrentMatch() == "/sit");
    // Start new session with different prefix
    tc.startCompletion("h", {"/help", "/helm"});
    CHECK(tc.getPrefix() == "h");
    CHECK(tc.getCurrentMatch() == "/help");
    CHECK(tc.matchCount() == 2);
}

TEST_CASE("ChatTabCompleter — next on empty returns false", "[chat][tab]") {
    ChatTabCompleter tc;
    CHECK_FALSE(tc.next());
    tc.startCompletion("x", {});
    CHECK_FALSE(tc.next());
}

TEST_CASE("ChatTabCompleter — next returns true when cycling", "[chat][tab]") {
    ChatTabCompleter tc;
    tc.startCompletion("a", {"/afk", "/assist"});
    CHECK(tc.next());
    CHECK(tc.getCurrentMatch() == "/assist");
}

TEST_CASE("ChatTabCompleter — prefix is preserved after cycling", "[chat][tab]") {
    ChatTabCompleter tc;
    tc.startCompletion("inv", {"/invite"});
    tc.next();
    tc.next();
    CHECK(tc.getPrefix() == "inv");
}

TEST_CASE("ChatTabCompleter — start same prefix resets index", "[chat][tab]") {
    ChatTabCompleter tc;
    tc.startCompletion("s", {"/say", "/sit", "/stand"});
    tc.next(); // /sit
    tc.next(); // /stand
    // Re-start with same prefix but new candidate list
    tc.startCompletion("s", {"/say", "/shout"});
    CHECK(tc.getCurrentMatch() == "/say");
    CHECK(tc.matchCount() == 2);
}
