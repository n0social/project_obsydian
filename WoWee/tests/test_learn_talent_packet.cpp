// CMSG_LEARN_TALENT rank encoding.
//
// The rank field is the index into TalentEntry::RankID and counts from zero,
// while the rest of the client counts a talent's first rank as 1 — that is what
// SMSG_TALENTS_INFO is stored as (rank + 1) and what the talent frame spends.
//
// Sent unconverted, the first rank of an untrained talent arrives as 1, which
// the server reads as the SECOND rank: Player::LearnTalent charges
// `talentRank - currentTalentRank + 1` = 2 points to a player holding 1, and
// the rank below it is missing besides. It returns without a word and answers
// the unchanged talents — a confirmation, the staged point handed straight
// back, and nothing learned.
#include <catch_amalgamated.hpp>
#include "game/world_packets.hpp"
#include "core/application.hpp"

// The builders live in translation units that inline isActiveExpansion(), which
// reaches through the Application singleton. The builder under test never calls
// it, so a null instance satisfies the linker.
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

TEST_CASE("CMSG_LEARN_TALENT counts ranks from zero on the wire", "[talent][packet]") {
    SECTION("a talent's first rank goes out as 0") {
        auto p = LearnTalentPacket::build(1578, 1);
        const auto& b = p.getData();
        CHECK(readU32(b, 0) == 1578u);
        CHECK(readU32(b, 4) == 0u);
    }

    SECTION("the fifth rank goes out as 4, not 5") {
        auto p = LearnTalentPacket::build(1578, 5);
        CHECK(readU32(p.getData(), 4) == 4u);
        // MAX_TALENT_RANK is 5 and the server drops anything >= it, so a
        // five-rank talent's top rank has to leave room under that ceiling.
        CHECK(readU32(p.getData(), 4) < 5u);
    }

    SECTION("the talent id is untouched") {
        auto p = LearnTalentPacket::build(2000, 3);
        CHECK(readU32(p.getData(), 0) == 2000u);
        CHECK(readU32(p.getData(), 4) == 2u);
    }

    SECTION("rank 0 does not wrap to four billion") {
        // Nothing should ask for rank 0 — it is not a rank — but an unsigned
        // decrement there would send 0xFFFFFFFF, which is >= MAX_TALENT_RANK
        // and silently drops the request.
        auto p = LearnTalentPacket::build(1578, 0);
        CHECK(readU32(p.getData(), 4) == 0u);
    }
}
