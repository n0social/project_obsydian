// Quest reward packet parsing across expansions.
//
// Layouts emulated here byte-for-byte from the server serializers:
//   Classic — vmangos  src/game/Server/Packets/Quest.cpp
//   TBC     — cmangos-tbc  src/game/Entities/GossipDef.cpp
//   WotLK   — azerothcore-wotlk  src/server/game/Entities/Creature/GossipDef.cpp
// Reward items were previously misparsed (wrong offsets / parallel-array
// assumptions / phantom 4.x portrait strings), showing garbage item numbers.
#include <catch_amalgamated.hpp>
#include "game/world_packets.hpp"
#include "game/packet_parsers.hpp"
#include "core/application.hpp"

#include <cstring>

// Parsers under test are era-parameterized or expansion-agnostic; none touch
// the Application singleton, so a null instance satisfies the linker.
namespace wowee {
namespace core {
Application* Application::instance = nullptr;
}
}

using namespace wowee::game;
namespace network = wowee::network;
using Bytes = std::vector<uint8_t>;

namespace {

void putU8(Bytes& b, uint8_t v)   { b.push_back(v); }
void putU32(Bytes& b, uint32_t v) {
    b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF);
    b.push_back((v >> 16) & 0xFF); b.push_back((v >> 24) & 0xFF);
}
void putU64(Bytes& b, uint64_t v) {
    putU32(b, static_cast<uint32_t>(v));
    putU32(b, static_cast<uint32_t>(v >> 32));
}
void putF(Bytes& b, float v) {
    uint32_t u; std::memcpy(&u, &v, 4); putU32(b, u);
}
void putStr(Bytes& b, const char* s) {
    while (*s) b.push_back(static_cast<uint8_t>(*s++));
    b.push_back(0);
}

} // namespace

// ============================================================
// SMSG_QUEST_QUERY_RESPONSE — quest log rewards
// ============================================================

TEST_CASE("Quest query rewards: Classic layout (vmangos)", "[quest_rewards]") {
    Bytes b;
    putU32(b, 100);   // questId
    putU32(b, 2);     // questMethod
    putU32(b, 5);     // questLevel
    putU32(b, 12);    // zoneOrSort
    putU32(b, 0);     // type
    putU32(b, 0);     // repObjectiveFaction
    putU32(b, 0);     // repObjectiveValue
    putU32(b, 0);     // requiredOppositeFaction
    putU32(b, 0);     // requiredOppositeValue
    putU32(b, 101);   // nextQuestInChain
    putU32(b, 150);   // rewOrReqMoney
    putU32(b, 900);   // rewMoneyMaxLevel
    putU32(b, 0);     // rewSpell
    putU32(b, 0);     // srcItemId
    putU32(b, 8);     // questFlags
    // 4 reward (id, count) pairs — fixed loop, interleaved
    putU32(b, 1234); putU32(b, 1);
    putU32(b, 0);    putU32(b, 0);
    putU32(b, 0);    putU32(b, 0);
    putU32(b, 0);    putU32(b, 0);
    // 6 choice (id, count) pairs — fixed loop, interleaved
    putU32(b, 2345); putU32(b, 2);
    putU32(b, 3456); putU32(b, 1);
    putU32(b, 0);    putU32(b, 0);
    putU32(b, 0);    putU32(b, 0);
    putU32(b, 0);    putU32(b, 0);
    putU32(b, 0);    putU32(b, 0);
    putU32(b, 0);     // pointMapId
    putF(b, 0.0f);    // pointX
    putF(b, 0.0f);    // pointY
    putU32(b, 0);     // pointOpt
    putStr(b, "Test Quest");
    putStr(b, "Objectives");
    putStr(b, "Details");
    putStr(b, "");

    auto r = QuestQueryRewardsParser::parse(b, /*questLogStride=*/3);
    REQUIRE(r.valid);
    CHECK(r.rewardMoney == 150);
    CHECK(r.itemId[0] == 1234);
    CHECK(r.itemCount[0] == 1);
    CHECK(r.itemId[1] == 0);
    CHECK(r.choiceItemId[0] == 2345);
    CHECK(r.choiceItemCount[0] == 2);
    CHECK(r.choiceItemId[1] == 3456);
    CHECK(r.choiceItemCount[1] == 1);
    CHECK(r.choiceItemId[2] == 0);
}

TEST_CASE("Quest query rewards: TBC layout (cmangos-tbc)", "[quest_rewards]") {
    Bytes b;
    putU32(b, 200);   // questId
    putU32(b, 2);     // questMethod
    putU32(b, 61);    // questLevel
    putU32(b, 3520);  // zoneOrSort
    putU32(b, 0);     // type
    putU32(b, 0);     // suggestedPlayers        (TBC addition)
    putU32(b, 0);     // repObjectiveFaction
    putU32(b, 0);     // repObjectiveValue
    putU32(b, 0);     // requiredOppositeFaction
    putU32(b, 0);     // requiredOppositeValue
    putU32(b, 0);     // nextQuestInChain
    putU32(b, 7700);  // rewOrReqMoney
    putU32(b, 9000);  // rewMoneyMaxLevel
    putU32(b, 0);     // rewSpell
    putU32(b, 0);     // rewSpellCast            (TBC addition)
    putU32(b, 0);     // rewHonorableKills       (TBC addition)
    putU32(b, 0);     // srcItemId
    putU32(b, 8);     // questFlags
    putU32(b, 0);     // charTitleId             (TBC addition)
    // 4 reward pairs, interleaved
    putU32(b, 25406); putU32(b, 1);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 0);     putU32(b, 0);
    // 6 choice pairs, interleaved
    putU32(b, 25407); putU32(b, 1);
    putU32(b, 25408); putU32(b, 1);
    putU32(b, 25409); putU32(b, 1);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 530); putF(b, 1.0f); putF(b, 2.0f); putU32(b, 0); // POI
    putStr(b, "Through the Dark Portal");
    putStr(b, "Objectives");
    putStr(b, "Details");
    putStr(b, "");

    auto r = QuestQueryRewardsParser::parse(b, /*questLogStride=*/4);
    REQUIRE(r.valid);
    CHECK(r.rewardMoney == 7700);
    CHECK(r.itemId[0] == 25406);
    CHECK(r.choiceItemId[0] == 25407);
    CHECK(r.choiceItemId[1] == 25408);
    CHECK(r.choiceItemId[2] == 25409);
    CHECK(r.choiceItemCount[2] == 1);
    CHECK(r.choiceItemId[3] == 0);
}

TEST_CASE("Quest query rewards: WotLK layout (AzerothCore)", "[quest_rewards]") {
    Bytes b;
    putU32(b, 300);   // questId
    putU32(b, 2);     // questMethod
    putU32(b, 70);    // questLevel
    putU32(b, 68);    // minLevel                (WotLK addition)
    putU32(b, 3537);  // zoneOrSort
    putU32(b, 0);     // type
    putU32(b, 0);     // suggestedPlayers
    putU32(b, 0);     // repObjectiveFaction
    putU32(b, 0);     // repObjectiveValue
    putU32(b, 0);     // repObjectiveFaction2
    putU32(b, 0);     // repObjectiveValue2
    putU32(b, 0);     // nextQuestInChain
    putU32(b, 5);     // xpId
    putU32(b, 47400); // moneyReward
    putU32(b, 19800); // rewMoneyMaxLevel
    putU32(b, 0);     // rewSpell
    putU32(b, 0);     // rewSpellCast
    putU32(b, 0);     // rewHonorAddition
    putF(b, 0.0f);    // rewHonorMultiplier
    putU32(b, 0);     // srcItemId
    putU32(b, 8);     // flags
    putU32(b, 0);     // charTitleId
    putU32(b, 0);     // playersSlain
    putU32(b, 0);     // bonusTalents
    putU32(b, 0);     // rewArenaPoints
    putU32(b, 0);     // reviewRepShowMask
    // 4 reward pairs, interleaved
    putU32(b, 35953); putU32(b, 2);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 0);     putU32(b, 0);
    // 6 choice pairs, interleaved
    putU32(b, 36926); putU32(b, 1);
    putU32(b, 36927); putU32(b, 1);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 0);     putU32(b, 0);
    putU32(b, 0);     putU32(b, 0);
    // 3 × 5 reputation arrays
    for (int i = 0; i < 15; ++i) putU32(b, 0);
    putU32(b, 571); putF(b, 1.0f); putF(b, 2.0f); putU32(b, 0); // POI
    putStr(b, "The Last Rites");
    putStr(b, "Objectives");
    putStr(b, "Details");
    putStr(b, "Area");
    putStr(b, "Completed");

    auto r = QuestQueryRewardsParser::parse(b, /*questLogStride=*/5);
    REQUIRE(r.valid);
    CHECK(r.rewardMoney == 47400);
    CHECK(r.itemId[0] == 35953);
    CHECK(r.itemCount[0] == 2);
    CHECK(r.choiceItemId[0] == 36926);
    CHECK(r.choiceItemId[1] == 36927);
    CHECK(r.choiceItemId[2] == 0);
}

TEST_CASE("Quest query rewards: implausible data rejected", "[quest_rewards]") {
    // Payload long enough but reward region contains string bytes (layout mismatch)
    Bytes b;
    putU32(b, 400); putU32(b, 2);
    for (int i = 0; i < 20; ++i) putStr(b, "Lorem ipsum dolor sit amet");
    auto r = QuestQueryRewardsParser::parse(b, /*questLogStride=*/3);
    CHECK_FALSE(r.valid);
}

// ============================================================
// SMSG_QUESTGIVER_QUEST_DETAILS — accept dialog rewards
// ============================================================

TEST_CASE("Quest details: Classic layout (vmangos)", "[quest_rewards]") {
    Bytes b;
    putU64(b, 0x123456);        // npcGuid
    putU32(b, 100);             // questId
    putStr(b, "Test Quest");
    putStr(b, "Some details");
    putStr(b, "Some objectives");
    putU32(b, 1);               // activateAccept — uint32 in vanilla
    // choice items: count + count × (id, count, display) — only non-empty slots
    putU32(b, 2);
    putU32(b, 2345); putU32(b, 1); putU32(b, 7001);
    putU32(b, 3456); putU32(b, 5); putU32(b, 7002);
    // reward items
    putU32(b, 1);
    putU32(b, 1234); putU32(b, 1); putU32(b, 7003);
    putU32(b, 150);             // money
    putU32(b, 0);               // rewSpell
    putU32(b, 4);               // emote count — LAST, after rewards
    for (int i = 0; i < 4; ++i) { putU32(b, 0); putU32(b, 0); }

    network::Packet pkt(0, b);
    QuestDetailsData d;
    REQUIRE(TbcPacketParsers::parseQuestDetailsPreWotlk(pkt, d, /*hasSuggestedPlayers=*/false));
    CHECK(d.questId == 100);
    CHECK(d.title == "Test Quest");
    REQUIRE(d.rewardChoiceItems.size() == 2);
    CHECK(d.rewardChoiceItems[0].itemId == 2345);
    CHECK(d.rewardChoiceItems[1].itemId == 3456);
    CHECK(d.rewardChoiceItems[1].count == 5);
    REQUIRE(d.rewardItems.size() == 1);
    CHECK(d.rewardItems[0].itemId == 1234);
    CHECK(d.rewardMoney == 150);
}

TEST_CASE("Quest details: Classic hidden rewards", "[quest_rewards]") {
    Bytes b;
    putU64(b, 0x123456);
    putU32(b, 101);
    putStr(b, "Hidden Quest");
    putStr(b, "d"); putStr(b, "o");
    putU32(b, 0);               // activateAccept
    putU32(b, 0);               // choice count hidden
    putU32(b, 0);               // item count hidden
    putU32(b, 0);               // money hidden
    putU32(b, 0);               // rewSpell
    putU32(b, 4);
    for (int i = 0; i < 4; ++i) { putU32(b, 0); putU32(b, 0); }

    network::Packet pkt(0, b);
    QuestDetailsData d;
    REQUIRE(TbcPacketParsers::parseQuestDetailsPreWotlk(pkt, d, false));
    CHECK(d.rewardChoiceItems.empty());
    CHECK(d.rewardItems.empty());
    CHECK(d.rewardMoney == 0);
}

TEST_CASE("Quest details: TBC layout (cmangos-tbc)", "[quest_rewards]") {
    Bytes b;
    putU64(b, 0x123456);
    putU32(b, 200);
    putStr(b, "TBC Quest");
    putStr(b, "d"); putStr(b, "o");
    putU32(b, 1);               // activateAccept — uint32
    putU32(b, 3);               // suggestedPlayers (TBC only)
    putU32(b, 1);
    putU32(b, 25407); putU32(b, 1); putU32(b, 8001);
    putU32(b, 0);               // reward item count
    putU32(b, 7700);            // money
    putU32(b, 0);               // honor
    putU32(b, 0);               // rewSpell
    putU32(b, 0);               // rewSpellCast
    putU32(b, 0);               // charTitleBitIndex
    putU32(b, 4);
    for (int i = 0; i < 4; ++i) { putU32(b, 0); putU32(b, 0); }

    network::Packet pkt(0, b);
    QuestDetailsData d;
    REQUIRE(TbcPacketParsers::parseQuestDetailsPreWotlk(pkt, d, /*hasSuggestedPlayers=*/true));
    CHECK(d.suggestedPlayers == 3);
    REQUIRE(d.rewardChoiceItems.size() == 1);
    CHECK(d.rewardChoiceItems[0].itemId == 25407);
    CHECK(d.rewardItems.empty());
    CHECK(d.rewardMoney == 7700);
}

TEST_CASE("Quest details: WotLK layout (AzerothCore)", "[quest_rewards]") {
    Bytes b;
    putU64(b, 0x123456);        // npcGuid
    putU64(b, 0);               // informUnit (GetDivider)
    putU32(b, 300);             // questId
    putStr(b, "WotLK Quest");
    putStr(b, "Some details");
    putStr(b, "Some objectives");
    putU8(b, 1);                // activateAccept — uint8 in WotLK
    putU32(b, 8);               // flags
    putU32(b, 0);               // suggestedPlayers
    putU8(b, 0);                // isFinished
    // choice items: VARIABLE count (only non-empty slots serialized)
    putU32(b, 2);
    putU32(b, 36926); putU32(b, 1); putU32(b, 9001);
    putU32(b, 36927); putU32(b, 1); putU32(b, 9002);
    // reward items
    putU32(b, 1);
    putU32(b, 35953); putU32(b, 2); putU32(b, 9003);
    putU32(b, 47400);           // money
    putU32(b, 20000);           // xp
    putU32(b, 0);               // honor
    putF(b, 0.0f);              // honor multiplier
    putU32(b, 0);               // rewSpell
    putU32(b, 0);               // rewSpellCast
    putU32(b, 0);               // titleId
    putU32(b, 0);               // bonusTalents
    putU32(b, 0);               // arenaPoints
    putU32(b, 0);               // unk
    for (int i = 0; i < 15; ++i) putU32(b, 0); // reputation arrays
    putU32(b, 4);               // emote count
    for (int i = 0; i < 4; ++i) { putU32(b, 0); putU32(b, 0); }

    network::Packet pkt(0, b);
    QuestDetailsData d;
    REQUIRE(QuestDetailsParser::parse(pkt, d));
    CHECK(d.questId == 300);
    CHECK(d.title == "WotLK Quest");
    REQUIRE(d.rewardChoiceItems.size() == 2);
    CHECK(d.rewardChoiceItems[0].itemId == 36926);
    CHECK(d.rewardChoiceItems[1].itemId == 36927);
    REQUIRE(d.rewardItems.size() == 1);
    CHECK(d.rewardItems[0].itemId == 35953);
    CHECK(d.rewardItems[0].count == 2);
    CHECK(d.rewardMoney == 47400);
    CHECK(d.rewardXp == 20000);
}

// ============================================================
// SMSG_QUESTGIVER_OFFER_REWARD — completion dialog rewards
// ============================================================

TEST_CASE("Offer reward: Classic layout (vmangos)", "[quest_rewards]") {
    Bytes b;
    putU64(b, 0x123456);
    putU32(b, 100);
    putStr(b, "Test Quest");
    putStr(b, "Well done, $n!");
    putU32(b, 1);               // autoFinish — uint32, 4-byte prefix
    putU32(b, 1);               // emote count
    putU32(b, 0); putU32(b, 1); // delay, emote
    putU32(b, 2);               // choice count
    putU32(b, 2345); putU32(b, 1); putU32(b, 7001);
    putU32(b, 3456); putU32(b, 5); putU32(b, 7002);
    putU32(b, 0);               // reward item count
    putU32(b, 500);             // money
    putU32(b, 8);               // quest flags (trailing)
    putU32(b, 0);               // rewSpell (trailing)

    network::Packet pkt(0, b);
    QuestOfferRewardData d;
    REQUIRE(QuestOfferRewardParser::parse(pkt, d, QuestPacketEra::CLASSIC));
    CHECK(d.questId == 100);
    REQUIRE(d.choiceRewards.size() == 2);
    CHECK(d.choiceRewards[0].itemId == 2345);
    CHECK(d.choiceRewards[1].itemId == 3456);
    CHECK(d.choiceRewards[1].count == 5);
    CHECK(d.fixedRewards.empty());
    CHECK(d.rewardMoney == 500);
    CHECK(d.rewardXp == 0);     // no XP field pre-WotLK
}

TEST_CASE("Offer reward: TBC layout (cmangos-tbc)", "[quest_rewards]") {
    Bytes b;
    putU64(b, 0x123456);
    putU32(b, 200);
    putStr(b, "TBC Quest");
    putStr(b, "Reward text");
    putU32(b, 1);               // autoFinish
    putU32(b, 0);               // suggestedPlayers — 8-byte prefix
    putU32(b, 0);               // emote count
    putU32(b, 1);               // choice count
    putU32(b, 25407); putU32(b, 1); putU32(b, 8001);
    putU32(b, 1);               // reward count
    putU32(b, 25406); putU32(b, 1); putU32(b, 8002);
    putU32(b, 7700);            // money
    putU32(b, 0);               // honor (trailing)
    putU32(b, 0x08);            // unused (trailing)
    putU32(b, 0);               // rewSpell (trailing)
    putU32(b, 0);               // rewSpellCast (trailing)
    putU32(b, 0);               // charTitle (trailing)

    network::Packet pkt(0, b);
    QuestOfferRewardData d;
    REQUIRE(QuestOfferRewardParser::parse(pkt, d, QuestPacketEra::TBC));
    REQUIRE(d.choiceRewards.size() == 1);
    CHECK(d.choiceRewards[0].itemId == 25407);
    REQUIRE(d.fixedRewards.size() == 1);
    CHECK(d.fixedRewards[0].itemId == 25406);
    CHECK(d.rewardMoney == 7700);
    CHECK(d.rewardXp == 0);
}

TEST_CASE("Offer reward: WotLK layout (AzerothCore)", "[quest_rewards]") {
    Bytes b;
    putU64(b, 0x123456);
    putU32(b, 300);
    putStr(b, "WotLK Quest");
    putStr(b, "Reward text");
    putU8(b, 1);                // autoFinish — uint8 in WotLK, no portraits
    putU32(b, 8);               // flags
    putU32(b, 0);               // suggestedPlayers
    putU32(b, 1);               // emote count
    putU32(b, 0); putU32(b, 1); // delay, emote
    putU32(b, 2);               // choice count — VARIABLE entries follow
    putU32(b, 36926); putU32(b, 1); putU32(b, 9001);
    putU32(b, 36927); putU32(b, 1); putU32(b, 9002);
    putU32(b, 1);               // reward count
    putU32(b, 35953); putU32(b, 2); putU32(b, 9003);
    putU32(b, 47400);           // money
    putU32(b, 20000);           // xp
    putU32(b, 0);               // honor
    putF(b, 0.0f);              // honor multiplier
    putU32(b, 0x08);            // unused
    putU32(b, 0);               // rewSpell
    putU32(b, 0);               // rewSpellCast
    putU32(b, 0);               // titleId
    putU32(b, 0);               // bonusTalents
    putU32(b, 0);               // arenaPoints
    putU32(b, 0);               // unk
    for (int i = 0; i < 15; ++i) putU32(b, 0); // reputation arrays

    network::Packet pkt(0, b);
    QuestOfferRewardData d;
    REQUIRE(QuestOfferRewardParser::parse(pkt, d, QuestPacketEra::WOTLK));
    REQUIRE(d.choiceRewards.size() == 2);
    CHECK(d.choiceRewards[0].itemId == 36926);
    CHECK(d.choiceRewards[1].itemId == 36927);
    REQUIRE(d.fixedRewards.size() == 1);
    CHECK(d.fixedRewards[0].itemId == 35953);
    CHECK(d.rewardMoney == 47400);
    CHECK(d.rewardXp == 20000);
}
