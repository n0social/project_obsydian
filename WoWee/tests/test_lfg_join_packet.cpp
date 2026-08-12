// CMSG_LFG_JOIN's field widths and its slot list.
//
// Every field of this packet was wrong, and none of it reported anything. The
// server reads
//
//     uint32 Roles, uint8 NoPartialClear, uint8 Achievements,
//     uint8 slotCount, uint32 slots[], uint8 needsCount, uint8 needs[3],
//     string Comment
//
// while the client wrote the roles as a single byte and left the needs out
// altogether. The four bytes taken for Roles therefore swallowed the roles,
// both flags and the slot count; the slot list came out empty; and
// HandleLfgJoinOpcode returns without a word when it is — so every attempt to
// queue for a dungeon was discarded in silence.
#include <catch_amalgamated.hpp>
#include "game/world_packets.hpp"
#include "core/application.hpp"

namespace wowee {
namespace core {
Application* Application::instance = nullptr;
}
}

using namespace wowee::game;

namespace {
uint32_t readU32(const std::vector<uint8_t>& b, size_t off) {
    REQUIRE(b.size() >= off + 4);
    return static_cast<uint32_t>(b[off]) |
           (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) |
           (static_cast<uint32_t>(b[off + 3]) << 24);
}
}

TEST_CASE("CMSG_LFG_JOIN carries every slot at the server's widths",
          "[lfg][packet]") {
    SECTION("one dungeon") {
        auto p = LfgJoinPacket::build({285}, 3);
        const auto& b = p.getData();
        CHECK(readU32(b, 0) == 3u);      // Roles, four bytes
        CHECK(b[4] == 0);                // NoPartialClear
        CHECK(b[5] == 0);                // Achievements
        CHECK(b[6] == 1);                // slot count
        CHECK(readU32(b, 7) == 285u);    // the dungeon
        CHECK(b[11] == 3);               // needs count
        CHECK(b[12] == 0);
        CHECK(b[13] == 0);
        CHECK(b[14] == 0);
        CHECK(b[15] == 0);               // empty comment's terminator
        CHECK(b.size() == 16);
    }

    SECTION("several dungeons, all of them, in the order given") {
        auto p = LfgJoinPacket::build({258, 285, 300}, 7);
        const auto& b = p.getData();
        CHECK(readU32(b, 0) == 7u);
        CHECK(b[6] == 3);
        CHECK(readU32(b, 7) == 258u);
        CHECK(readU32(b, 11) == 285u);
        CHECK(readU32(b, 15) == 300u);
        CHECK(b[19] == 3);               // needs still follow the whole list
    }

    SECTION("the roles field is wide enough for every role bit") {
        // Tank, healer and damage together, which does not fit the byte this
        // used to send if anything above it were ever set.
        auto p = LfgJoinPacket::build({285}, 0x00000007);
        CHECK(readU32(p.getData(), 0) == 0x00000007u);
    }
}
