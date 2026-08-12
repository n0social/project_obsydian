#include <catch_amalgamated.hpp>

#include "game/quest_progress.hpp"

using wowee::game::decodeQuestObjectiveCounts;
using wowee::game::normalizeQuestObjectiveEntry;
using wowee::game::isQuestSlotComplete;
using wowee::game::questObjectiveCountFieldOffset;

TEST_CASE("Classic quest counters use four packed 6-bit values", "[quest][progress]") {
    const uint32_t packed = 5u | (12u << 6) | (31u << 12) | (63u << 18)
                          | (1u << 24); // state byte must not leak into counts
    REQUIRE(decodeQuestObjectiveCounts(3, packed) ==
            std::array<uint32_t, 4>{5, 12, 31, 63});
    REQUIRE(questObjectiveCountFieldOffset(3) == 1);
}

TEST_CASE("TBC quest counters use four byte values", "[quest][progress]") {
    const uint32_t packed = 7u | (16u << 8) | (42u << 16) | (200u << 24);
    REQUIRE(decodeQuestObjectiveCounts(4, packed) ==
            std::array<uint32_t, 4>{7, 16, 42, 200});
    REQUIRE(questObjectiveCountFieldOffset(4) == 2);
}

TEST_CASE("WotLK quest counters use four uint16 values", "[quest][progress]") {
    const uint32_t first = 5u | (300u << 16);
    const uint32_t second = 16u | (1024u << 16);
    REQUIRE(decodeQuestObjectiveCounts(5, first, second) ==
            std::array<uint32_t, 4>{5, 300, 16, 1024});
    REQUIRE(questObjectiveCountFieldOffset(5) == 2);
}

TEST_CASE("Quest update game-object entries discard the wire marker", "[quest][progress]") {
    REQUIRE(normalizeQuestObjectiveEntry(1234u) == 1234u);
    REQUIRE(normalizeQuestObjectiveEntry(0x80000000u | 5678u) == 5678u);
}

TEST_CASE("Classic completion state does not alias the first kill count", "[quest][progress]") {
    const uint32_t oneKillNotComplete = 1u;
    const uint32_t oneKillComplete = 1u | (1u << 24);
    REQUIRE_FALSE(isQuestSlotComplete(3, oneKillNotComplete));
    REQUIRE(isQuestSlotComplete(3, oneKillComplete));
    REQUIRE(isQuestSlotComplete(4, 1u));
    REQUIRE(isQuestSlotComplete(5, 1u));
}
