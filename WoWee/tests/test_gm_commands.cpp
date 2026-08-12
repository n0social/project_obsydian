// Tests for GM command data table and completion helpers.
#include <catch_amalgamated.hpp>
#include "ui/chat/gm_command_data.hpp"
#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace wowee::ui;

// ---------------------------------------------------------------------------
// Data table sanity checks
// ---------------------------------------------------------------------------

TEST_CASE("GM command table is non-empty", "[gm_commands]") {
    REQUIRE(kGmCommands.size() > 50);
}

TEST_CASE("GM command entries have required fields", "[gm_commands]") {
    for (const auto& cmd : kGmCommands) {
        INFO("command: " << cmd.name);
        REQUIRE(!cmd.name.empty());
        REQUIRE(!cmd.syntax.empty());
        REQUIRE(!cmd.help.empty());
        REQUIRE(cmd.security <= 4);
    }
}

TEST_CASE("GM command names are unique", "[gm_commands]") {
    std::set<std::string> seen;
    for (const auto& cmd : kGmCommands) {
        std::string name(cmd.name);
        INFO("duplicate: " << name);
        REQUIRE(seen.insert(name).second);
    }
}

TEST_CASE("GM command syntax starts with dot-prefix", "[gm_commands]") {
    for (const auto& cmd : kGmCommands) {
        INFO("command: " << cmd.name << " syntax: " << cmd.syntax);
        REQUIRE(cmd.syntax.size() > 1);
        REQUIRE(cmd.syntax[0] == '.');
    }
}

// ---------------------------------------------------------------------------
// Completion helper (inline, matches chat_panel.cpp logic)
// ---------------------------------------------------------------------------

static std::vector<std::string> getGmCompletions(const std::string& prefix) {
    std::vector<std::string> results;
    for (const auto& cmd : kGmCommands) {
        std::string dotName = "." + std::string(cmd.name);
        if (dotName.size() >= prefix.size() &&
            dotName.compare(0, prefix.size(), prefix) == 0) {
            results.push_back(dotName);
        }
    }
    std::sort(results.begin(), results.end());
    return results;
}

TEST_CASE("GM completions for '.gm' include gm subcommands", "[gm_commands]") {
    auto results = getGmCompletions(".gm");
    REQUIRE(!results.empty());
    // Should contain .gm, .gm on, .gm off, .gm fly, etc.
    REQUIRE(std::find(results.begin(), results.end(), ".gm") != results.end());
    REQUIRE(std::find(results.begin(), results.end(), ".gm on") != results.end());
    REQUIRE(std::find(results.begin(), results.end(), ".gm off") != results.end());
}

TEST_CASE("GM completions for '.tele' include teleport commands", "[gm_commands]") {
    auto results = getGmCompletions(".tele");
    REQUIRE(!results.empty());
    REQUIRE(std::find(results.begin(), results.end(), ".tele") != results.end());
}

TEST_CASE("GM completions for '.add' include additem", "[gm_commands]") {
    auto results = getGmCompletions(".add");
    REQUIRE(!results.empty());
    REQUIRE(std::find(results.begin(), results.end(), ".additem") != results.end());
}

TEST_CASE("GM completions for '.xyz' returns empty (no match)", "[gm_commands]") {
    auto results = getGmCompletions(".xyz");
    REQUIRE(results.empty());
}

TEST_CASE("GM completions are sorted", "[gm_commands]") {
    auto results = getGmCompletions(".ch");
    REQUIRE(results.size() > 1);
    REQUIRE(std::is_sorted(results.begin(), results.end()));
}

TEST_CASE("GM completions for '.' returns all commands", "[gm_commands]") {
    auto results = getGmCompletions(".");
    REQUIRE(results.size() == kGmCommands.size());
}

TEST_CASE("Key GM commands exist in table", "[gm_commands]") {
    std::set<std::string> names;
    for (const auto& cmd : kGmCommands) {
        names.insert(std::string(cmd.name));
    }
    // Check essential commands
    CHECK(names.count("gm"));
    CHECK(names.count("gm on"));
    CHECK(names.count("tele"));
    CHECK(names.count("go xyz"));
    CHECK(names.count("additem"));
    CHECK(names.count("levelup"));
    CHECK(names.count("revive"));
    CHECK(names.count("die"));
    CHECK(names.count("cheat god"));
    CHECK(names.count("cast"));
    CHECK(names.count("learn"));
    CHECK(names.count("modify money"));
    CHECK(names.count("lookup item"));
    CHECK(names.count("npc add"));
    CHECK(names.count("ban account"));
    CHECK(names.count("kick"));
    CHECK(names.count("server info"));
    CHECK(names.count("help"));
    CHECK(names.count("commands"));
    CHECK(names.count("save"));
    CHECK(names.count("summon"));
    CHECK(names.count("appear"));
}
