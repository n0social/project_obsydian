// SMSG_BATTLEFIELD_STATUS's header, which was read two bytes short.
//
// The server writes a stretch of this packet as one uint64 and says so in a
// comment, which is what makes it easy to read as fewer, wider fields than it
// holds:
//
//     uint32 queueSlot, uint8 arenaType, uint8 isArena, uint32 bgTypeId,
//     uint16 0x1F90, uint8 minLevel, uint8 maxLevel, uint32 instanceId,
//     uint8 isRated, uint32 statusId
//
// Read as `uint32 instanceId, uint8 isRated` — five bytes where the server
// sends seven — the status arrives as the top of the instance id joined to the
// bottom of the status, and is never one of the values it is compared against.
// The packet is the same length either way, so nothing runs short and nothing
// reports anything: a queue that exists reads as no queue at all, which is a
// battleground queue with no tracker, no chat line and no invitation.
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

// The bytes AzerothCore's BattlegroundMgr::BuildBattlegroundStatusPacket puts
// on the wire, in its order. Built here rather than asserted against our own
// writer so the test fails if we drift from the server rather than from
// ourselves.
std::vector<uint8_t> serverStatusBytes(uint32_t queueSlot, uint8_t arenaType,
                                       uint32_t bgTypeId, uint8_t minLevel,
                                       uint8_t maxLevel, uint32_t instanceId,
                                       bool isRated, uint32_t statusId,
                                       const std::vector<uint32_t>& tail) {
    std::vector<uint8_t> b;
    auto u32 = [&](uint32_t v) {
        b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
        b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
    };
    auto u16 = [&](uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); };

    u32(queueSlot);
    b.push_back(arenaType);
    b.push_back(arenaType ? 0x0E : 0x00);
    u32(bgTypeId);
    u16(0x1F90);
    b.push_back(minLevel);
    b.push_back(maxLevel);
    u32(instanceId);
    b.push_back(isRated ? 1 : 0);
    u32(statusId);
    for (uint32_t v : tail) u32(v);
    return b;
}

wowee::network::Packet packetOf(const std::vector<uint8_t>& body) {
    return wowee::network::Packet(0x2D4, body);  // SMSG_BATTLEFIELD_STATUS, 3.3.5a
}

}  // namespace

TEST_CASE("SMSG_BATTLEFIELD_STATUS reads the status the server sent",
          "[battleground][packet]") {
    SECTION("queued for Warsong Gulch") {
        // Time1 is the average wait, Time2 the time already spent in queue.
        auto p = packetOf(serverStatusBytes(0, 0, 2, 10, 19, 1, false, 1,
                                            {120000, 65000}));
        BattlefieldStatusData d;
        REQUIRE(BattlefieldStatusPacket::parse(p, d));
        CHECK(d.queueSlot == 0u);
        CHECK(d.bgTypeId == 2u);
        CHECK(d.statusId == 1u);          // read two bytes early this was 65536
        CHECK(d.minLevel == 10);
        CHECK(d.maxLevel == 19);
        CHECK(d.instanceId == 1u);
        CHECK(d.avgWaitMs == 120000u);
        CHECK(d.timeInQueueMs == 65000u);
    }

    SECTION("an instance id large enough to reach the status") {
        // The misread joined the instance id's top byte to the status's bottom
        // half, so an instance id in the millions moved the status as well.
        auto p = packetOf(serverStatusBytes(1, 0, 30, 71, 80, 0x01020304, false, 1,
                                            {1000, 2000}));
        BattlefieldStatusData d;
        REQUIRE(BattlefieldStatusPacket::parse(p, d));
        CHECK(d.statusId == 1u);
        CHECK(d.instanceId == 0x01020304u);
        CHECK(d.minLevel == 71);
        CHECK(d.maxLevel == 80);
    }

    SECTION("an invitation carries a map id before its timeout") {
        // status 2: uint32 mapId, uint64 unused, uint32 time to remove from
        // queue. The timeout used to be read from the map id.
        auto p = packetOf(serverStatusBytes(2, 0, 3, 10, 19, 5, false, 2,
                                            {529, 0, 0, 80000}));
        BattlefieldStatusData d;
        REQUIRE(BattlefieldStatusPacket::parse(p, d));
        CHECK(d.statusId == 2u);
        CHECK(d.mapId == 529u);
        CHECK(d.inviteTimeoutMs == 80000u);
    }

    SECTION("before Wrath the invitation has no eight unused bytes") {
        auto p = packetOf(serverStatusBytes(0, 0, 3, 10, 19, 5, false, 2,
                                            {529, 80000}));
        BattlefieldStatusData d;
        REQUIRE(BattlefieldStatusPacket::parse(p, d, /*classicFormat=*/false,
                                               /*wotlkFormat=*/false));
        CHECK(d.mapId == 529u);
        CHECK(d.inviteTimeoutMs == 80000u);
    }

    SECTION("an arena carries its team size") {
        auto p = packetOf(serverStatusBytes(0, 3, 4, 70, 80, 2, true, 1,
                                            {30000, 1000}));
        BattlefieldStatusData d;
        REQUIRE(BattlefieldStatusPacket::parse(p, d));
        CHECK(d.arenaType == 3);
        CHECK(d.isRated);
        CHECK(d.statusId == 1u);
    }

    SECTION("a truncated packet is refused rather than half read") {
        std::vector<uint8_t> body = serverStatusBytes(0, 0, 2, 10, 19, 1, false, 1, {});
        body.resize(10);
        auto p = packetOf(body);
        BattlefieldStatusData d;
        CHECK_FALSE(BattlefieldStatusPacket::parse(p, d));
    }
}
