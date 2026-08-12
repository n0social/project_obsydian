#include <catch_amalgamated.hpp>

#include "core/application.hpp"
#include "game/world_packets.hpp"

namespace wowee::core {
Application* Application::instance = nullptr;
}

using namespace wowee::game;

namespace {

// Server -> client broadcasts arrive as opcode + payload; the parser is handed
// a packet positioned at the payload, so build them the same way here.
wowee::network::Packet makePacket(const std::vector<uint8_t>& payload) {
    return wowee::network::Packet(0, payload);
}

void appendUInt64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

} // namespace

TEST_CASE("Full raid target list carries only the icons that are set", "[raid][packets]") {
    // type 1, then (icon, guid) for the two marked units — NOT a fixed 8 entries.
    std::vector<uint8_t> payload{1};
    payload.push_back(7);                       // Skull
    appendUInt64(payload, 0xF130000123456789ull);
    payload.push_back(2);                       // Diamond
    appendUInt64(payload, 0xF130000ABCDEF012ull);

    auto packet = makePacket(payload);
    RaidTargetUpdateData data;
    REQUIRE(RaidTargetUpdateParser::parse(packet, data));

    CHECK(data.fullList);
    REQUIRE(data.marks.size() == 2);
    CHECK(data.marks[0].first == 7);
    CHECK(data.marks[0].second == 0xF130000123456789ull);
    CHECK(data.marks[1].first == 2);
    CHECK(data.marks[1].second == 0xF130000ABCDEF012ull);
}

TEST_CASE("WotLK single mark skips the setter GUID", "[raid][packets][wotlk]") {
    // type 0, whoGuid, icon, targetGuid — the 3.3.5a layout.
    std::vector<uint8_t> payload{0};
    appendUInt64(payload, 0x0000000000008951ull);  // whoGuid: the player marking
    payload.push_back(6);                          // Cross
    appendUInt64(payload, 0xF130000DEADBEEFull);

    auto packet = makePacket(payload);
    RaidTargetUpdateData data;
    REQUIRE(RaidTargetUpdateParser::parse(packet, data));

    CHECK_FALSE(data.fullList);
    REQUIRE(data.marks.size() == 1);
    CHECK(data.marks[0].first == 6);
    CHECK(data.marks[0].second == 0xF130000DEADBEEFull);
}

TEST_CASE("Classic and TBC single mark has no setter GUID", "[raid][packets][classic]") {
    // type 0, icon, targetGuid — pre-WotLK omits whoGuid entirely.
    std::vector<uint8_t> payload{0};
    payload.push_back(0);                          // Star
    appendUInt64(payload, 0xF130000CAFEBABEull);

    auto packet = makePacket(payload);
    RaidTargetUpdateData data;
    REQUIRE(RaidTargetUpdateParser::parse(packet, data));

    CHECK_FALSE(data.fullList);
    REQUIRE(data.marks.size() == 1);
    CHECK(data.marks[0].first == 0);
    CHECK(data.marks[0].second == 0xF130000CAFEBABEull);
}

TEST_CASE("Clearing a raid mark decodes as a zero GUID", "[raid][packets]") {
    SECTION("WotLK") {
        std::vector<uint8_t> payload{0};
        appendUInt64(payload, 0);   // whoGuid empty when the server clears a slot
        payload.push_back(3);
        appendUInt64(payload, 0);
        auto packet = makePacket(payload);
        RaidTargetUpdateData data;
        REQUIRE(RaidTargetUpdateParser::parse(packet, data));
        REQUIRE(data.marks.size() == 1);
        CHECK(data.marks[0].first == 3);
        CHECK(data.marks[0].second == 0);
    }
    SECTION("classic") {
        std::vector<uint8_t> payload{0};
        payload.push_back(3);
        appendUInt64(payload, 0);
        auto packet = makePacket(payload);
        RaidTargetUpdateData data;
        REQUIRE(RaidTargetUpdateParser::parse(packet, data));
        REQUIRE(data.marks.size() == 1);
        CHECK(data.marks[0].first == 3);
        CHECK(data.marks[0].second == 0);
    }
}

TEST_CASE("An empty raid target list clears every icon", "[raid][packets]") {
    // Sent when the last mark is removed: type 1 with no entries at all.
    auto packet = makePacket({1});
    RaidTargetUpdateData data;
    REQUIRE(RaidTargetUpdateParser::parse(packet, data));
    CHECK(data.fullList);
    CHECK(data.marks.empty());
}

TEST_CASE("Truncated raid target updates are rejected", "[raid][packets]") {
    RaidTargetUpdateData data;

    auto empty = makePacket({});
    CHECK_FALSE(RaidTargetUpdateParser::parse(empty, data));

    // type 0 with fewer bytes than even the classic layout needs
    auto shortSet = makePacket({0, 4, 0xAA, 0xBB});
    CHECK_FALSE(RaidTargetUpdateParser::parse(shortSet, data));
}
