// OpcodeTable load from JSON, toWire/fromWire mapping
#include <catch_amalgamated.hpp>
#include "game/opcode_table.hpp"
#include <fstream>
#include <filesystem>
#include <cstdio>

using wowee::game::OpcodeTable;
using wowee::game::LogicalOpcode;

// Helper: write a temporary JSON file and return its path.
// Uses the executable's directory to avoid permission issues.
static std::string writeTempJson(const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / "wowee_test_opcodes.json";
    std::ofstream f(path);
    f << content;
    f.close();
    return path.string();
}

TEST_CASE("OpcodeTable loadFromJson basic mapping", "[opcode_table]") {
    // CMSG_PING and SMSG_PONG are canonical opcodes present in the generated enum.
    std::string json = R"({
        "CMSG_PING": "0x1DC",
        "SMSG_PONG": "0x1DD"
    })";
    auto path = writeTempJson(json);

    OpcodeTable table;
    REQUIRE(table.loadFromJson(path));

    REQUIRE(table.size() == 2);
    REQUIRE(table.hasOpcode(LogicalOpcode::CMSG_PING));
    REQUIRE(table.toWire(LogicalOpcode::CMSG_PING) == 0x1DC);
    REQUIRE(table.toWire(LogicalOpcode::SMSG_PONG) == 0x1DD);

    std::remove(path.c_str());
}

TEST_CASE("OpcodeTable fromWire reverse lookup", "[opcode_table]") {
    std::string json = R"({ "CMSG_PING": "0x1DC" })";
    auto path = writeTempJson(json);

    OpcodeTable table;
    table.loadFromJson(path);

    auto result = table.fromWire(0x1DC);
    REQUIRE(result.has_value());
    REQUIRE(*result == LogicalOpcode::CMSG_PING);

    std::remove(path.c_str());
}

TEST_CASE("OpcodeTable unknown wire returns nullopt", "[opcode_table]") {
    std::string json = R"({ "CMSG_PING": "0x1DC" })";
    auto path = writeTempJson(json);

    OpcodeTable table;
    table.loadFromJson(path);

    auto result = table.fromWire(0x9999);
    REQUIRE_FALSE(result.has_value());

    std::remove(path.c_str());
}

TEST_CASE("OpcodeTable unknown logical returns 0xFFFF", "[opcode_table]") {
    std::string json = R"({ "CMSG_PING": "0x1DC" })";
    auto path = writeTempJson(json);

    OpcodeTable table;
    table.loadFromJson(path);

    // SMSG_AUTH_CHALLENGE should not be in this table
    REQUIRE(table.toWire(LogicalOpcode::SMSG_AUTH_CHALLENGE) == 0xFFFF);

    std::remove(path.c_str());
}

TEST_CASE("OpcodeTable loadFromJson nonexistent file", "[opcode_table]") {
    OpcodeTable table;
    REQUIRE_FALSE(table.loadFromJson("/nonexistent/path/opcodes.json"));
    REQUIRE(table.size() == 0);
}

TEST_CASE("OpcodeTable failed reload preserves existing data", "[opcode_table]") {
    std::string json = R"({ "CMSG_PING": "0x1DC" })";
    auto path = writeTempJson(json);

    OpcodeTable table;
    REQUIRE(table.loadFromJson(path));
    REQUIRE(table.size() == 1);
    REQUIRE(table.toWire(LogicalOpcode::CMSG_PING) == 0x1DC);

    REQUIRE_FALSE(table.loadFromJson("/nonexistent/path/opcodes.json"));
    REQUIRE(table.size() == 1);
    REQUIRE(table.toWire(LogicalOpcode::CMSG_PING) == 0x1DC);
    auto result = table.fromWire(0x1DC);
    REQUIRE(result.has_value());
    REQUIRE(*result == LogicalOpcode::CMSG_PING);

    std::remove(path.c_str());
}

TEST_CASE("OpcodeTable logicalToName returns enum name", "[opcode_table]") {
    const char* name = OpcodeTable::logicalToName(LogicalOpcode::CMSG_PING);
    REQUIRE(name != nullptr);
    REQUIRE(std::string(name) == "CMSG_PING");
}

TEST_CASE("OpcodeTable decimal wire values", "[opcode_table]") {
    std::string json = R"({ "CMSG_PING": "476" })";
    auto path = writeTempJson(json);

    OpcodeTable table;
    REQUIRE(table.loadFromJson(path));
    REQUIRE(table.toWire(LogicalOpcode::CMSG_PING) == 476);

    std::remove(path.c_str());
}

TEST_CASE("Global active opcode table", "[opcode_table]") {
    OpcodeTable table;
    std::string json = R"({ "CMSG_PING": "0x1DC" })";
    auto path = writeTempJson(json);
    table.loadFromJson(path);

    wowee::game::setActiveOpcodeTable(&table);
    REQUIRE(wowee::game::getActiveOpcodeTable() == &table);
    REQUIRE(wowee::game::wireOpcode(LogicalOpcode::CMSG_PING) == 0x1DC);

    // Reset
    wowee::game::setActiveOpcodeTable(nullptr);
    REQUIRE(wowee::game::wireOpcode(LogicalOpcode::CMSG_PING) == 0xFFFF);

    std::remove(path.c_str());
}
