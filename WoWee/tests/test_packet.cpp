// Packet read/write round-trip, packed GUID, bounds checks
#include <catch_amalgamated.hpp>
#include "network/packet.hpp"

using wowee::network::Packet;

TEST_CASE("Packet default constructor", "[packet]") {
    Packet p;
    REQUIRE(p.getOpcode() == 0);
    REQUIRE(p.getSize() == 0);
    REQUIRE(p.getReadPos() == 0);
    REQUIRE(p.getRemainingSize() == 0);
    REQUIRE_FALSE(p.hasData());
}

TEST_CASE("Packet opcode constructor", "[packet]") {
    Packet p(0x1DC);
    REQUIRE(p.getOpcode() == 0x1DC);
    REQUIRE(p.getSize() == 0);
}

TEST_CASE("Packet write/read UInt8 round-trip", "[packet]") {
    Packet p(1);
    p.writeUInt8(0);
    p.writeUInt8(127);
    p.writeUInt8(255);
    REQUIRE(p.getSize() == 3);

    REQUIRE(p.readUInt8() == 0);
    REQUIRE(p.readUInt8() == 127);
    REQUIRE(p.readUInt8() == 255);
    REQUIRE_FALSE(p.hasData());
}

TEST_CASE("Packet write/read UInt16 round-trip", "[packet]") {
    Packet p(1);
    p.writeUInt16(0);
    p.writeUInt16(0xBEEF);
    p.writeUInt16(0xFFFF);
    REQUIRE(p.getSize() == 6);

    REQUIRE(p.readUInt16() == 0);
    REQUIRE(p.readUInt16() == 0xBEEF);
    REQUIRE(p.readUInt16() == 0xFFFF);
}

TEST_CASE("Packet write/read UInt32 round-trip", "[packet]") {
    Packet p(1);
    p.writeUInt32(0xDEADBEEF);
    REQUIRE(p.readUInt32() == 0xDEADBEEF);
}

TEST_CASE("Packet write/read UInt64 round-trip", "[packet]") {
    Packet p(1);
    p.writeUInt64(0x0123456789ABCDEFULL);
    REQUIRE(p.readUInt64() == 0x0123456789ABCDEFULL);
}

TEST_CASE("Packet write/read float round-trip", "[packet]") {
    Packet p(1);
    p.writeFloat(3.14f);
    p.writeFloat(-0.0f);
    p.writeFloat(1e10f);
    REQUIRE(p.readFloat() == Catch::Approx(3.14f));
    REQUIRE(p.readFloat() == -0.0f);
    REQUIRE(p.readFloat() == Catch::Approx(1e10f));
}

TEST_CASE("Packet write/read string round-trip", "[packet]") {
    Packet p(1);
    p.writeString("Hello WoW");
    p.writeString("");  // empty string
    REQUIRE(p.readString() == "Hello WoW");
    REQUIRE(p.readString() == "");
}

TEST_CASE("Packet writeBytes / readUInt8 array", "[packet]") {
    Packet p(1);
    const uint8_t buf[] = {0xAA, 0xBB, 0xCC};
    p.writeBytes(buf, 3);
    REQUIRE(p.getSize() == 3);
    REQUIRE(p.readUInt8() == 0xAA);
    REQUIRE(p.readUInt8() == 0xBB);
    REQUIRE(p.readUInt8() == 0xCC);
}

TEST_CASE("Packet packed GUID round-trip", "[packet]") {
    SECTION("Zero GUID") {
        Packet p(1);
        p.writePackedGuid(0);
        REQUIRE(p.hasFullPackedGuid());
        REQUIRE(p.readPackedGuid() == 0);
    }

    SECTION("Low GUID (single byte)") {
        Packet p(1);
        p.writePackedGuid(0x42);
        REQUIRE(p.readPackedGuid() == 0x42);
    }

    SECTION("Full 64-bit GUID") {
        Packet p(1);
        uint64_t guid = 0x0102030405060708ULL;
        p.writePackedGuid(guid);
        REQUIRE(p.readPackedGuid() == guid);
    }

    SECTION("Max GUID") {
        Packet p(1);
        uint64_t guid = 0xFFFFFFFFFFFFFFFFULL;
        p.writePackedGuid(guid);
        REQUIRE(p.readPackedGuid() == guid);
    }
}

TEST_CASE("Packet getRemainingSize and hasRemaining", "[packet]") {
    Packet p(1);
    p.writeUInt32(100);
    p.writeUInt32(200);
    REQUIRE(p.getRemainingSize() == 8);
    REQUIRE(p.hasRemaining(8));
    REQUIRE_FALSE(p.hasRemaining(9));

    p.readUInt32();
    REQUIRE(p.getRemainingSize() == 4);
    REQUIRE(p.hasRemaining(4));
    REQUIRE_FALSE(p.hasRemaining(5));

    p.readUInt32();
    REQUIRE(p.getRemainingSize() == 0);
    REQUIRE(p.hasRemaining(0));
    REQUIRE_FALSE(p.hasRemaining(1));
}

TEST_CASE("Packet setReadPos and skipAll", "[packet]") {
    Packet p(1);
    p.writeUInt8(10);
    p.writeUInt8(20);
    p.writeUInt8(30);

    p.readUInt8(); // pos = 1
    p.setReadPos(0);
    REQUIRE(p.readUInt8() == 10);

    p.skipAll();
    REQUIRE(p.getRemainingSize() == 0);
    REQUIRE_FALSE(p.hasData());
}

TEST_CASE("Packet constructed with data vector", "[packet]") {
    std::vector<uint8_t> raw = {0x01, 0x02, 0x03};
    Packet p(42, raw);
    REQUIRE(p.getOpcode() == 42);
    REQUIRE(p.getSize() == 3);
    REQUIRE(p.readUInt8() == 0x01);
}

TEST_CASE("Packet constructed with rvalue data", "[packet]") {
    Packet p(99, std::vector<uint8_t>{0xFF, 0xFE});
    REQUIRE(p.getSize() == 2);
    REQUIRE(p.readUInt8() == 0xFF);
    REQUIRE(p.readUInt8() == 0xFE);
}

TEST_CASE("Packet mixed types interleaved", "[packet]") {
    Packet p(1);
    p.writeUInt8(0xAA);
    p.writeUInt32(0xDEADBEEF);
    p.writeString("test");
    p.writeFloat(2.5f);
    p.writeUInt16(0x1234);

    REQUIRE(p.readUInt8() == 0xAA);
    REQUIRE(p.readUInt32() == 0xDEADBEEF);
    REQUIRE(p.readString() == "test");
    REQUIRE(p.readFloat() == Catch::Approx(2.5f));
    REQUIRE(p.readUInt16() == 0x1234);
    REQUIRE_FALSE(p.hasData());
}

TEST_CASE("Packet hasFullPackedGuid returns false on empty", "[packet]") {
    Packet p(1);
    REQUIRE_FALSE(p.hasFullPackedGuid());
}

TEST_CASE("Packet getRemainingSize clamps after overshoot", "[packet]") {
    Packet p(1);
    p.writeUInt8(1);
    p.setReadPos(999);
    REQUIRE(p.getRemainingSize() == 0);
    REQUIRE_FALSE(p.hasRemaining(1));
}
