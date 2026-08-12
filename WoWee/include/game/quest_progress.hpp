#pragma once

#include <array>
#include <cstdint>

namespace wowee::game {

// PLAYER_QUEST_LOG counter layouts differ by expansion:
// Classic packs four 6-bit counters beside the state byte, TBC stores four
// byte counters, and WotLK stores four uint16 counters across two fields.
inline std::array<uint32_t, 4> decodeQuestObjectiveCounts(
    uint8_t questLogStride, uint32_t firstWord, uint32_t secondWord = 0) {
    if (questLogStride >= 5) {
        return {
            firstWord & 0xFFFFu,
            (firstWord >> 16) & 0xFFFFu,
            secondWord & 0xFFFFu,
            (secondWord >> 16) & 0xFFFFu,
        };
    }
    if (questLogStride == 4) {
        return {
            firstWord & 0xFFu,
            (firstWord >> 8) & 0xFFu,
            (firstWord >> 16) & 0xFFu,
            (firstWord >> 24) & 0xFFu,
        };
    }
    return {
        firstWord & 0x3Fu,
        (firstWord >> 6) & 0x3Fu,
        (firstWord >> 12) & 0x3Fu,
        (firstWord >> 18) & 0x3Fu,
    };
}

inline uint8_t questObjectiveCountFieldOffset(uint8_t questLogStride) {
    // Classic combines state and counts in field 1. TBC/WotLK have a separate
    // state field and begin counters at field 2.
    return questLogStride <= 3 ? 1 : 2;
}

inline uint32_t normalizeQuestObjectiveEntry(uint32_t wireEntry) {
    // SMSG_QUESTUPDATE_ADD_KILL marks game-object entries with the high bit.
    return wireEntry & 0x7FFFFFFFu;
}

inline bool isQuestSlotComplete(uint8_t questLogStride, uint32_t stateField) {
    const uint32_t state = questLogStride <= 3 ? (stateField >> 24) & 0xFFu
                                               : stateField;
    return (state & 0x1u) != 0;
}

} // namespace wowee::game
