#include <catch_amalgamated.hpp>

#include "core/application.hpp"
#include "game/packet_parsers.hpp"
#include "game/world_packets.hpp"

namespace wowee::core {
Application* Application::instance = nullptr;
}

using namespace wowee::game;

TEST_CASE("Auction mail subjects decode into localized-style item text", "[mail][auction]") {
    AuctionMailSubject subject;
    REQUIRE(parseAuctionMailSubject("4705:0:1:38784081:1", subject));
    CHECK(subject.itemEntry == 4705);
    CHECK(subject.response == 1);
    CHECK(subject.lotId == 38784081);
    CHECK(subject.itemCount == 1);
    CHECK(formatAuctionMailSubject(subject, "Test Boots") == "Auction won: Test Boots");

    static constexpr const char* expected[] = {
        "Outbid on Test Boots",
        "Auction won: Test Boots",
        "Auction successful: Test Boots",
        "Auction expired: Test Boots",
        "Auction cancelled: Test Boots",
        "Auction cancelled: Test Boots",
        "Sale Pending: Test Boots",
    };
    for (uint32_t response = 0; response < 7; ++response) {
        subject.response = response;
        CHECK(formatAuctionMailSubject(subject, "Test Boots") == expected[response]);
    }

    CHECK_FALSE(parseAuctionMailSubject("ordinary player subject", subject));
    CHECK_FALSE(parseAuctionMailSubject("4705:1:1:38784081:1", subject));
    CHECK_FALSE(parseAuctionMailSubject("4705:0:7:38784081:1", subject));

    REQUIRE(parseAuctionMailSubject("4705:0:3", subject));
    CHECK(formatAuctionMailSubject(subject, "Test Boots") == "Auction expired: Test Boots");
}

TEST_CASE("Auction mail invoice body decodes into a money breakdown", "[mail][auction]") {
    AuctionMailInvoice invoice;

    // Auction-won body: ownerGuid:bid:buyout, with the width-16 space padding
    // and trailing zero fields some cores append.
    REQUIRE(parseAuctionMailBody("           88a79:6000:6000:0:0:0:0", invoice));
    CHECK(invoice.ownerGuidLow == 0x88a79);
    CHECK(invoice.bid == 6000);
    CHECK(invoice.buyout == 6000);
    CHECK(invoice.deposit == 0);
    CHECK(invoice.consignment == 0);

    // Minimal three-field won body (legacy cores).
    REQUIRE(parseAuctionMailBody("1a2b:500:0", invoice));
    CHECK(invoice.ownerGuidLow == 0x1a2b);
    CHECK(invoice.bid == 500);
    CHECK(invoice.buyout == 0);

    // Successful-sale body carries deposit and consignment (AH cut).
    REQUIRE(parseAuctionMailBody("ff:10000:12000:250:600", invoice));
    CHECK(invoice.bid == 10000);
    CHECK(invoice.buyout == 12000);
    CHECK(invoice.deposit == 250);
    CHECK(invoice.consignment == 600);

    // Non-invoice bodies (ordinary player mail) must not parse.
    CHECK_FALSE(parseAuctionMailBody("Hey, thanks for the trade!", invoice));
    CHECK_FALSE(parseAuctionMailBody("88a79:6000", invoice));  // too few fields
    CHECK_FALSE(parseAuctionMailBody("", invoice));
    CHECK_FALSE(parseAuctionMailBody("88a79::6000", invoice)); // empty field
    CHECK_FALSE(parseAuctionMailBody("zzz:1:2", invoice));     // bad hex guid
    CHECK_FALSE(parseAuctionMailBody("ff:1x:2", invoice));     // bad decimal
}

TEST_CASE("WotLK mail list parses uint32 attached money and inclusive entry size", "[mail]") {
    wowee::network::Packet entry;
    entry.writeUInt32(0x12345678);          // message id
    entry.writeUInt8(0);                    // normal player mail
    entry.writeUInt64(0x1122334455667788);  // sender
    entry.writeUInt32(54321);               // COD
    entry.writeUInt32(0);                   // pre-3.3.3 item text field
    entry.writeUInt32(41);                  // stationery
    entry.writeUInt32(1234567);             // attached money
    entry.writeUInt32(1);                   // read flag
    entry.writeFloat(12.5f);                 // days remaining
    entry.writeUInt32(0);                   // mail template
    entry.writeString("Test subject");
    entry.writeString("Test body");
    entry.writeUInt8(0);                    // attachments

    wowee::network::Packet packet;
    packet.writeUInt32(1); // total count
    packet.writeUInt8(1);  // shown count
    packet.writeUInt16(static_cast<uint16_t>(entry.getSize() + 2));
    packet.writeBytes(entry.getData().data(), entry.getSize());

    WotlkPacketParsers parsers;
    std::vector<MailMessage> inbox;
    REQUIRE(parsers.parseMailList(packet, inbox));
    REQUIRE(inbox.size() == 1);
    CHECK(inbox[0].messageId == 0x12345678);
    CHECK(inbox[0].senderGuid == 0x1122334455667788);
    CHECK(inbox[0].cod == 54321);
    CHECK(inbox[0].stationeryId == 41);
    CHECK(inbox[0].money == 1234567);
    CHECK(inbox[0].flags == 1);
    CHECK(inbox[0].expirationTime == Catch::Approx(12.5f));
    CHECK(inbox[0].subject == "Test subject");
    CHECK(inbox[0].body == "Test body");
    CHECK(packet.getRemainingSize() == 0);
}

TEST_CASE("WotLK send mail writes uint32 money and COD plus legacy zeros", "[mail]") {
    auto built = SendMailPacket::build(0x1020304050607080, "Receiver", "Subject", "Body",
                                       1234567, 7654321, {});
    wowee::network::Packet packet(built.getOpcode(), built.getData());

    CHECK(packet.readUInt64() == 0x1020304050607080);
    CHECK(packet.readString() == "Receiver");
    CHECK(packet.readString() == "Subject");
    CHECK(packet.readString() == "Body");
    CHECK(packet.readUInt32() == 0);
    CHECK(packet.readUInt32() == 0);
    CHECK(packet.readUInt8() == 0);
    CHECK(packet.readUInt32() == 1234567);
    CHECK(packet.readUInt32() == 7654321);
    CHECK(packet.readUInt64() == 0);
    CHECK(packet.readUInt8() == 0);
    CHECK(packet.getRemainingSize() == 0);
}

TEST_CASE("TBC mail list parses attached money and item wire layout", "[mail][tbc]") {
    wowee::network::Packet entry;
    entry.writeUInt32(0x87654321);          // message id
    entry.writeUInt8(0);                    // normal player mail
    entry.writeUInt64(0x8877665544332211);  // sender
    entry.writeUInt32(24680);               // COD
    entry.writeUInt32(13579);               // item text id
    entry.writeUInt32(0);                   // package
    entry.writeUInt32(41);                  // stationery
    entry.writeUInt32(765432);              // attached money
    entry.writeUInt32(0);                   // flags
    entry.writeFloat(7.25f);                 // days remaining
    entry.writeUInt32(0);                   // mail template
    entry.writeString("TBC subject");
    entry.writeUInt8(1);                    // attachment count
    entry.writeUInt8(3);                    // attachment slot
    entry.writeUInt32(0x10203040);          // item GUID low
    entry.writeUInt32(19019);               // item entry
    for (uint32_t enchantSlot = 0; enchantSlot < 6; ++enchantSlot) {
        entry.writeUInt32(enchantSlot + 10);   // charges
        entry.writeUInt32(enchantSlot + 20);   // duration
        entry.writeUInt32(enchantSlot == 0 ? 2673 : 0); // enchant id
    }
    entry.writeUInt32(44);                  // random property
    entry.writeUInt32(55);                  // suffix factor
    entry.writeUInt8(7);                    // stack count
    entry.writeUInt32(6);                   // spell charges
    entry.writeUInt32(100);                 // max durability
    entry.writeUInt32(83);                  // current durability

    wowee::network::Packet packet;
    packet.writeUInt8(1);
    packet.writeUInt16(static_cast<uint16_t>(entry.getSize() + 2));
    packet.writeBytes(entry.getData().data(), entry.getSize());

    TbcPacketParsers parsers;
    std::vector<MailMessage> inbox;
    REQUIRE(parsers.parseMailList(packet, inbox));
    REQUIRE(inbox.size() == 1);
    CHECK(inbox[0].messageId == 0x87654321);
    CHECK(inbox[0].senderGuid == 0x8877665544332211);
    CHECK(inbox[0].cod == 24680);
    CHECK(inbox[0].stationeryId == 41);
    CHECK(inbox[0].money == 765432);
    CHECK(inbox[0].expirationTime == Catch::Approx(7.25f));
    CHECK(inbox[0].subject == "TBC subject");
    REQUIRE(inbox[0].attachments.size() == 1);
    CHECK(inbox[0].attachments[0].slot == 3);
    CHECK(inbox[0].attachments[0].itemGuidLow == 0x10203040);
    CHECK(inbox[0].attachments[0].itemId == 19019);
    CHECK(inbox[0].attachments[0].enchantId == 2673);
    CHECK(inbox[0].attachments[0].randomPropertyId == 44);
    CHECK(inbox[0].attachments[0].randomSuffix == 55);
    CHECK(inbox[0].attachments[0].stackCount == 7);
    CHECK(inbox[0].attachments[0].chargesOrDurability == 6);
    CHECK(inbox[0].attachments[0].maxDurability == 100);
    CHECK(packet.getRemainingSize() == 0);
}

TEST_CASE("TBC mail list accepts overstated non-player sender size", "[mail][tbc]") {
    wowee::network::Packet entry;
    entry.writeUInt32(99);       // message id
    entry.writeUInt8(2);         // auction mail
    entry.writeUInt32(1234);     // auction sender id
    entry.writeUInt32(0);        // COD
    entry.writeUInt32(0);        // item text id
    entry.writeUInt32(0);        // package
    entry.writeUInt32(62);       // auction stationery
    entry.writeUInt32(40000);    // attached money
    entry.writeUInt32(0);        // flags
    entry.writeFloat(30.0f);      // days remaining
    entry.writeUInt32(0);        // template
    entry.writeString("Auction successful");
    entry.writeUInt8(0);         // attachments

    wowee::network::Packet packet;
    packet.writeUInt8(1);
    // CMaNGOS budgets an 8-byte sender here but emits the uint32 above.
    packet.writeUInt16(static_cast<uint16_t>(entry.getSize() + 2 + 4));
    packet.writeBytes(entry.getData().data(), entry.getSize());

    TbcPacketParsers parsers;
    std::vector<MailMessage> inbox;
    REQUIRE(parsers.parseMailList(packet, inbox));
    REQUIRE(inbox.size() == 1);
    CHECK(inbox[0].messageType == 2);
    CHECK(inbox[0].senderEntry == 1234);
    CHECK(inbox[0].money == 40000);
    CHECK(inbox[0].stationeryId == 62);
    CHECK(inbox[0].subject == "Auction successful");
    CHECK(packet.getRemainingSize() == 0);
}

// ============================================================
// Auction list result parsing (SMSG_AUCTION_LIST_RESULT)
// ============================================================

namespace {

// Mirrors the servers' BuildAuctionInfo: vmangos (1 slot, id only, no flags),
// cmangos-tbc (6 id/duration/charges triplets + flags), AzerothCore
// (7 triplets + flags).
void writeAuctionEntry(wowee::network::Packet& p, uint32_t auctionId,
                       int enchantSlots, bool vanillaLayout) {
    p.writeUInt32(auctionId);
    p.writeUInt32(2589);                     // itemEntry (Linen Cloth)
    if (vanillaLayout) {
        p.writeUInt32(7000 + auctionId);     // permanent enchant id only
    } else {
        for (int s = 0; s < enchantSlots; ++s) {
            p.writeUInt32(s == 0 ? 7000 + auctionId : 0);  // enchant id
            p.writeUInt32(0);                // enchant duration
            p.writeUInt32(0);                // enchant charges
        }
    }
    p.writeUInt32(0xFFFFFF85);               // randomPropertyId (negative suffix)
    p.writeUInt32(11);                       // suffixFactor
    p.writeUInt32(20);                       // stack count
    p.writeUInt32(0);                        // spell charges
    if (!vanillaLayout) p.writeUInt32(0);    // item flags
    p.writeUInt64(0x0000000000000ABC);       // owner guid
    p.writeUInt32(500);                      // start bid
    p.writeUInt32(25);                       // min bid increment
    p.writeUInt32(9000);                     // buyout
    p.writeUInt32(172800000);                // time left ms
    p.writeUInt64(0x0000000000000DEF);       // bidder guid
    p.writeUInt32(525);                      // current bid
}

void checkAuctionEntry(const AuctionEntry& e, uint32_t auctionId) {
    CHECK(e.auctionId == auctionId);
    CHECK(e.itemEntry == 2589);
    CHECK(e.enchantId == 7000 + auctionId);
    CHECK(e.randomPropertyId == 0xFFFFFF85);
    CHECK(e.suffixFactor == 11);
    CHECK(e.stackCount == 20);
    CHECK(e.ownerGuid == 0xABC);
    CHECK(e.startBid == 500);
    CHECK(e.minBidIncrement == 25);
    CHECK(e.buyoutPrice == 9000);
    CHECK(e.timeLeftMs == 172800000);
    CHECK(e.bidderGuid == 0xDEF);
    CHECK(e.currentBid == 525);
}

} // namespace

TEST_CASE("WotLK auction list parses 7 enchant slots per entry", "[auction]") {
    wowee::network::Packet packet;
    packet.writeUInt32(2);
    writeAuctionEntry(packet, 101, 7, false);
    writeAuctionEntry(packet, 102, 7, false);
    packet.writeUInt32(37);   // total count
    packet.writeUInt32(300);  // search delay

    AuctionListResult result;
    REQUIRE(AuctionListResultParser::parse(packet, result, 7));
    REQUIRE(result.auctions.size() == 2);
    checkAuctionEntry(result.auctions[0], 101);
    checkAuctionEntry(result.auctions[1], 102);
    CHECK(result.totalCount == 37);
    CHECK(result.searchDelay == 300);
    CHECK(packet.getRemainingSize() == 0);
}

TEST_CASE("TBC auction list parses 6 enchant slots per entry", "[auction]") {
    wowee::network::Packet packet;
    packet.writeUInt32(2);
    writeAuctionEntry(packet, 201, 6, false);
    writeAuctionEntry(packet, 202, 6, false);
    packet.writeUInt32(8);
    packet.writeUInt32(300);

    AuctionListResult result;
    REQUIRE(AuctionListResultParser::parse(packet, result, 6));
    REQUIRE(result.auctions.size() == 2);
    checkAuctionEntry(result.auctions[0], 201);
    checkAuctionEntry(result.auctions[1], 202);
    CHECK(result.totalCount == 8);
    CHECK(packet.getRemainingSize() == 0);
}

TEST_CASE("Vanilla auction list parses id-only enchant, no flags, no delay", "[auction]") {
    wowee::network::Packet packet;
    packet.writeUInt32(2);
    writeAuctionEntry(packet, 301, 1, true);
    writeAuctionEntry(packet, 302, 1, true);
    packet.writeUInt32(5);  // total count — vanilla sends no search delay

    AuctionListResult result;
    REQUIRE(AuctionListResultParser::parse(packet, result, 1));
    REQUIRE(result.auctions.size() == 2);
    checkAuctionEntry(result.auctions[0], 301);
    checkAuctionEntry(result.auctions[1], 302);
    CHECK(result.totalCount == 5);
    CHECK(result.searchDelay == 0);
    CHECK(packet.getRemainingSize() == 0);
}

// SMSG_ITEM_QUERY_SINGLE_RESPONSE comes in two shapes: some servers send
// BuyCount between Flags2 and BuyPrice, some do not, and the client has to work
// out which from the bytes. Guessing from InventoryType alone does not work — on
// a server without BuyCount that read lands on AllowableClass, and a
// class-restricted item's mask is a small, plausible-looking number. Priest-only
// is 16, which is INVTYPE_CLOAK, so a priest robe claimed to be a cloak.
namespace {

// Everything up to and including Quality; identical in both layouts.
void writeItemHeader(wowee::network::Packet& p, uint32_t entry) {
    p.writeUInt32(entry);
    p.writeUInt32(4);     // Class: Armor
    p.writeUInt32(1);     // SubClass: Cloth
    p.writeUInt32(0xFFFFFFFF);  // SoundOverrideSubclass
    p.writeString("Friar's Robes of the Light");
    p.writeString("");
    p.writeString("");
    p.writeString("");
    p.writeUInt32(12345); // DisplayInfoID
    p.writeUInt32(2);     // Quality: Uncommon
}

// The fields after InventoryType, which is what makes a wrong guess detectable.
void writeItemTail(wowee::network::Packet& p, uint32_t allowableClass) {
    p.writeUInt32(allowableClass);
    p.writeUInt32(0xFFFFFFFF);  // AllowableRace: all
    p.writeUInt32(29);          // ItemLevel
    p.writeUInt32(24);          // RequiredLevel
    for (int i = 0; i < 10; ++i) p.writeUInt32(0);  // skill..containerSlots
    p.writeUInt32(0);           // statsCount
}

} // namespace

TEST_CASE("item query detects the BuyCount layout from more than one field",
          "[packet][item]") {
    using namespace wowee::game;

    constexpr uint32_t kPriestOnly = 16;   // the mask that reads as INVTYPE_CLOAK
    constexpr uint32_t kRobe = 20;

    SECTION("server without BuyCount, class-restricted item") {
        wowee::network::Packet p;
        writeItemHeader(p, 7048);
        p.writeUInt32(0);        // Flags
        p.writeUInt32(0);        // Flags2
        p.writeUInt32(1000);     // BuyPrice   (no BuyCount on this server)
        p.writeUInt32(250);      // SellPrice
        p.writeUInt32(kRobe);    // InventoryType
        writeItemTail(p, kPriestOnly);

        ItemQueryResponseData data;
        REQUIRE(ItemQueryResponseParser::parse(p, data));
        CHECK(data.inventoryType == kRobe);          // not 16
        CHECK(data.allowableClass == kPriestOnly);
        CHECK(data.itemLevel == 29);
        CHECK(data.requiredLevel == 24);
    }

    SECTION("server with BuyCount, same item") {
        wowee::network::Packet p;
        writeItemHeader(p, 7048);
        p.writeUInt32(0);        // Flags
        p.writeUInt32(0);        // Flags2
        p.writeUInt32(1);        // BuyCount
        p.writeUInt32(1000);     // BuyPrice
        p.writeUInt32(250);      // SellPrice
        p.writeUInt32(kRobe);    // InventoryType
        writeItemTail(p, kPriestOnly);

        ItemQueryResponseData data;
        REQUIRE(ItemQueryResponseParser::parse(p, data));
        CHECK(data.inventoryType == kRobe);
        CHECK(data.allowableClass == kPriestOnly);
        CHECK(data.itemLevel == 29);
    }

    SECTION("an item usable by every class still resolves either way") {
        for (bool withBuyCount : {false, true}) {
            wowee::network::Packet p;
            writeItemHeader(p, 7049);
            p.writeUInt32(0);
            p.writeUInt32(0);
            if (withBuyCount) p.writeUInt32(1);
            p.writeUInt32(1000);
            p.writeUInt32(250);
            p.writeUInt32(5);            // InventoryType: Chest
            writeItemTail(p, 0xFFFFFFFF);  // all classes

            ItemQueryResponseData data;
            INFO("withBuyCount: " << withBuyCount);
            REQUIRE(ItemQueryResponseParser::parse(p, data));
            CHECK(data.inventoryType == 5);
        }
    }
}
