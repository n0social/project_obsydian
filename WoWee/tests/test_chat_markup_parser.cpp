// Tests for ChatMarkupParser — WoW markup parsing into typed segments.
// Phase 2.3 of chat_panel_ref.md.

#include <catch_amalgamated.hpp>
#include "ui/chat/chat_markup_parser.hpp"

using namespace wowee::ui;

// ── Plain text ──────────────────────────────────────────────

TEST_CASE("Plain text produces single Text segment", "[chat_markup]") {
    ChatMarkupParser parser;
    auto segs = parser.parse("Hello world");
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].type == SegmentType::Text);
    CHECK(segs[0].text == "Hello world");
}

TEST_CASE("Empty string produces no segments", "[chat_markup]") {
    ChatMarkupParser parser;
    auto segs = parser.parse("");
    CHECK(segs.empty());
}

// ── Color codes ─────────────────────────────────────────────

TEST_CASE("parseWowColor extracts AARRGGBB correctly", "[chat_markup]") {
    // |cFF00FF00 → alpha=1.0, red=0.0, green=1.0, blue=0.0
    std::string s = "|cFF00FF00some text|r";
    ImVec4 c = ChatMarkupParser::parseWowColor(s, 0);
    CHECK(c.x == Catch::Approx(0.0f).margin(0.01f));  // red
    CHECK(c.y == Catch::Approx(1.0f).margin(0.01f));  // green
    CHECK(c.z == Catch::Approx(0.0f).margin(0.01f));  // blue
    CHECK(c.w == Catch::Approx(1.0f).margin(0.01f));  // alpha
}

TEST_CASE("parseWowColor with half values", "[chat_markup]") {
    // |cFF808080 → gray (128/255 ≈ 0.502)
    std::string s = "|cFF808080";
    ImVec4 c = ChatMarkupParser::parseWowColor(s, 0);
    CHECK(c.x == Catch::Approx(0.502f).margin(0.01f));
    CHECK(c.y == Catch::Approx(0.502f).margin(0.01f));
    CHECK(c.z == Catch::Approx(0.502f).margin(0.01f));
}

TEST_CASE("Colored text segment: |cAARRGGBB...text...|r", "[chat_markup]") {
    ChatMarkupParser parser;
    auto segs = parser.parse("|cFFFF0000Red text|r");
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].type == SegmentType::ColoredText);
    CHECK(segs[0].text == "Red text");
    CHECK(segs[0].color.x == Catch::Approx(1.0f).margin(0.01f));  // red
    CHECK(segs[0].color.y == Catch::Approx(0.0f).margin(0.01f));  // green
    CHECK(segs[0].color.z == Catch::Approx(0.0f).margin(0.01f));  // blue
}

TEST_CASE("Colored text without |r includes rest of string", "[chat_markup]") {
    ChatMarkupParser parser;
    auto segs = parser.parse("|cFF00FF00Green forever");
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].type == SegmentType::ColoredText);
    CHECK(segs[0].text == "Green forever");
}

TEST_CASE("Mixed plain and colored text", "[chat_markup]") {
    ChatMarkupParser parser;
    auto segs = parser.parse("Hello |cFFFF0000world|r!");
    REQUIRE(segs.size() == 3);
    CHECK(segs[0].type == SegmentType::Text);
    CHECK(segs[0].text == "Hello ");
    CHECK(segs[1].type == SegmentType::ColoredText);
    CHECK(segs[1].text == "world");
    CHECK(segs[2].type == SegmentType::Text);
    CHECK(segs[2].text == "!");
}

// ── Item links ──────────────────────────────────────────────

TEST_CASE("Item link: |cFFAARRGG|Hitem:ID:...|h[Name]|h|r", "[chat_markup]") {
    ChatMarkupParser parser;
    std::string raw = "|cff1eff00|Hitem:19019:0:0:0:0:0:0:0:80|h[Thunderfury]|h|r";
    auto segs = parser.parse(raw);
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].type == SegmentType::ItemLink);
    CHECK(segs[0].text == "Thunderfury");
    CHECK(segs[0].id == 19019);
    CHECK_FALSE(segs[0].rawLink.empty());
}

TEST_CASE("Bare item link without color prefix", "[chat_markup]") {
    ChatMarkupParser parser;
    std::string raw = "|Hitem:12345:0:0:0|h[Some Item]|h";
    auto segs = parser.parse(raw);
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].type == SegmentType::ItemLink);
    CHECK(segs[0].text == "Some Item");
    CHECK(segs[0].id == 12345);
}

TEST_CASE("Item link with text before and after", "[chat_markup]") {
    ChatMarkupParser parser;
    std::string raw = "Check this: |cff0070dd|Hitem:50000:0:0:0|h[Cool Sword]|h|r nice!";
    auto segs = parser.parse(raw);
    REQUIRE(segs.size() == 3);
    CHECK(segs[0].type == SegmentType::Text);
    CHECK(segs[0].text == "Check this: ");
    CHECK(segs[1].type == SegmentType::ItemLink);
    CHECK(segs[1].text == "Cool Sword");
    CHECK(segs[1].id == 50000);
    CHECK(segs[2].type == SegmentType::Text);
    CHECK(segs[2].text == " nice!");
}

// ── Spell links ─────────────────────────────────────────────

TEST_CASE("Spell link: |Hspell:ID:RANK|h[Name]|h", "[chat_markup]") {
    ChatMarkupParser parser;
    std::string raw = "|cff71d5ff|Hspell:48461:0|h[Wrath]|h|r";
    auto segs = parser.parse(raw);
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].type == SegmentType::SpellLink);
    CHECK(segs[0].text == "Wrath");
    CHECK(segs[0].id == 48461);
}

// ── Quest links ─────────────────────────────────────────────

TEST_CASE("Quest link with level extraction", "[chat_markup]") {
    ChatMarkupParser parser;
    std::string raw = "|cff808080|Hquest:9876:70|h[The Last Stand]|h|r";
    auto segs = parser.parse(raw);
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].type == SegmentType::QuestLink);
    CHECK(segs[0].text == "The Last Stand");
    CHECK(segs[0].id == 9876);
    CHECK(segs[0].extra == 70);  // quest level
}

// ── Achievement links ───────────────────────────────────────

TEST_CASE("Achievement link", "[chat_markup]") {
    ChatMarkupParser parser;
    std::string raw = "|cffffff00|Hachievement:2136:0:0:0:0:0:0:0:0:0|h[Glory of the Hero]|h|r";
    auto segs = parser.parse(raw);
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].type == SegmentType::AchievementLink);
    CHECK(segs[0].text == "Glory of the Hero");
    CHECK(segs[0].id == 2136);
}

// ── URLs ────────────────────────────────────────────────────

TEST_CASE("URL is detected as Url segment", "[chat_markup]") {
    ChatMarkupParser parser;
    auto segs = parser.parse("Visit https://example.com for info");
    REQUIRE(segs.size() == 3);
    CHECK(segs[0].type == SegmentType::Text);
    CHECK(segs[0].text == "Visit ");
    CHECK(segs[1].type == SegmentType::Url);
    CHECK(segs[1].text == "https://example.com");
    CHECK(segs[2].type == SegmentType::Text);
    CHECK(segs[2].text == " for info");
}

TEST_CASE("URL at end of message", "[chat_markup]") {
    ChatMarkupParser parser;
    auto segs = parser.parse("Link: https://example.com/path?q=1");
    REQUIRE(segs.size() == 2);
    CHECK(segs[1].type == SegmentType::Url);
    CHECK(segs[1].text == "https://example.com/path?q=1");
}

// ── Edge cases ──────────────────────────────────────────────

TEST_CASE("Short |c without enough hex digits treated as literal", "[chat_markup]") {
    ChatMarkupParser parser;
    auto segs = parser.parse("|c short");
    REQUIRE(segs.size() == 2);
    CHECK(segs[0].type == SegmentType::Text);
    CHECK(segs[0].text == "|c");
    CHECK(segs[1].type == SegmentType::Text);
    CHECK(segs[1].text == " short");
}

TEST_CASE("Multiple item links in one message", "[chat_markup]") {
    ChatMarkupParser parser;
    std::string raw =
        "|cff1eff00|Hitem:100:0|h[Sword]|h|r and |cff0070dd|Hitem:200:0|h[Shield]|h|r";
    auto segs = parser.parse(raw);
    REQUIRE(segs.size() == 3);
    CHECK(segs[0].type == SegmentType::ItemLink);
    CHECK(segs[0].text == "Sword");
    CHECK(segs[0].id == 100);
    CHECK(segs[1].type == SegmentType::Text);
    CHECK(segs[1].text == " and ");
    CHECK(segs[2].type == SegmentType::ItemLink);
    CHECK(segs[2].text == "Shield");
    CHECK(segs[2].id == 200);
}

TEST_CASE("parseWowColor with truncated string returns white", "[chat_markup]") {
    ImVec4 c = ChatMarkupParser::parseWowColor("|cFF", 0);
    CHECK(c.x == 1.0f);
    CHECK(c.y == 1.0f);
    CHECK(c.z == 1.0f);
    CHECK(c.w == 1.0f);
}
