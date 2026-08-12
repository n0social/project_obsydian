// REALM_LIST response parsing across the vanilla / TBC-WotLK layout split.
//
// Vanilla-family servers disagree on the auth protocol byte (vmangos-derived
// 1.12 realms speak protocol 8, stock mangos/cmangos speak 3) while still
// sending the vanilla realm-entry shape, so the realm-list layout is selected
// by expansion rather than by protocol version — and the parser additionally
// recovers in-place when a modern-layout parse visibly shifts the fields.
#include <catch_amalgamated.hpp>
#include "auth/auth_packets.hpp"
#include "network/packet.hpp"

#include <array>
#include <cstring>

using namespace wowee::auth;
namespace network = wowee::network;
using Bytes = std::vector<uint8_t>;

namespace {

void putU8(Bytes& b, uint8_t v)   { b.push_back(v); }
void putU16(Bytes& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
void putU32(Bytes& b, uint32_t v) {
    b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF);
    b.push_back((v >> 16) & 0xFF); b.push_back((v >> 24) & 0xFF);
}
void putF(Bytes& b, float v) { uint32_t u; std::memcpy(&u, &v, 4); putU32(b, u); }
void putStr(Bytes& b, const char* s) {
    while (*s) b.push_back(static_cast<uint8_t>(*s++));
    b.push_back(0);
}

// Header shared by both layouts: size(2) + unknown(4), then the realm count,
// whose width is what differs (uint8 vanilla, uint16 TBC/WotLK).
void putHeader(Bytes& b, uint16_t realmCount, bool legacyVanilla) {
    putU16(b, 0);   // packet size — parser reads and ignores it
    putU32(b, 0);   // unknown
    if (legacyVanilla) putU8(b, static_cast<uint8_t>(realmCount));
    else               putU16(b, realmCount);
}

// Vanilla realm entry: uint32 icon, NO lock byte.
void putVanillaRealm(Bytes& b, const char* name, const char* addr, uint8_t id) {
    putU32(b, 1);            // icon (uint32 in vanilla)
    putU8(b, 0x00);          // flags — no version info
    putStr(b, name);
    putStr(b, addr);
    putF(b, 0.5f);           // population
    putU8(b, 3);             // characters
    putU8(b, 1);             // timezone
    putU8(b, id);
}

// TBC/WotLK realm entry: uint8 icon + lock byte, optional version block.
void putModernRealm(Bytes& b, const char* name, const char* addr, uint8_t id, bool withVersion) {
    putU8(b, 1);                          // icon
    putU8(b, 0);                          // lock
    putU8(b, withVersion ? 0x04 : 0x00);  // flags (0x04 = version info follows)
    putStr(b, name);
    putStr(b, addr);
    putF(b, 0.5f);
    putU8(b, 3);
    putU8(b, 1);
    putU8(b, id);
    if (withVersion) {
        putU8(b, 3); putU8(b, 3); putU8(b, 5);  // 3.3.5
        putU16(b, 12340);
    }
}

} // namespace

TEST_CASE("Realm list: vanilla layout parses with legacy flag", "[realm_list]") {
    Bytes b;
    putHeader(b, 2, /*legacyVanilla=*/true);
    putVanillaRealm(b, "Vanilla Realm", "127.0.0.1:8085", 1);
    putVanillaRealm(b, "Second Realm", "127.0.0.1:8086", 2);

    network::Packet pkt(0, b);
    RealmListResponse resp;
    REQUIRE(RealmListResponseParser::parse(pkt, resp, /*legacyVanillaLayout=*/true));
    REQUIRE(resp.realms.size() == 2);
    CHECK(resp.realms[0].name == "Vanilla Realm");
    CHECK(resp.realms[0].address == "127.0.0.1:8085");
    CHECK(resp.realms[0].id == 1);
    CHECK(resp.realms[1].name == "Second Realm");
    CHECK(resp.realms[1].id == 2);
}

TEST_CASE("Realm list: TBC/WotLK layout parses without legacy flag", "[realm_list]") {
    Bytes b;
    putHeader(b, 1, /*legacyVanilla=*/false);
    putModernRealm(b, "WotLK Realm", "127.0.0.1:8085", 1, /*withVersion=*/false);

    network::Packet pkt(0, b);
    RealmListResponse resp;
    REQUIRE(RealmListResponseParser::parse(pkt, resp, /*legacyVanillaLayout=*/false));
    REQUIRE(resp.realms.size() == 1);
    CHECK(resp.realms[0].name == "WotLK Realm");
    CHECK(resp.realms[0].address == "127.0.0.1:8085");
    CHECK(resp.realms[0].id == 1);
}

TEST_CASE("Realm list: WotLK version block consumed, not leaked into next realm", "[realm_list]") {
    Bytes b;
    putHeader(b, 2, /*legacyVanilla=*/false);
    putModernRealm(b, "Realm One", "127.0.0.1:8085", 1, /*withVersion=*/true);
    putModernRealm(b, "Realm Two", "127.0.0.1:8086", 2, /*withVersion=*/false);

    network::Packet pkt(0, b);
    RealmListResponse resp;
    REQUIRE(RealmListResponseParser::parse(pkt, resp, /*legacyVanillaLayout=*/false));
    REQUIRE(resp.realms.size() == 2);
    CHECK(resp.realms[0].name == "Realm One");
    CHECK(resp.realms[0].hasVersionInfo());
    CHECK(resp.realms[0].build == 12340);
    // The second realm only parses correctly if the version block above was
    // fully consumed — a stray byte shifts every field from here on.
    CHECK(resp.realms[1].name == "Realm Two");
    CHECK(resp.realms[1].address == "127.0.0.1:8086");
    CHECK(resp.realms[1].id == 2);
}

TEST_CASE("Realm list: vanilla body under modern flag recovers in-place", "[realm_list]") {
    // vmangos: auth protocol 8 (so the caller may not set the legacy flag) but
    // a vanilla-shaped realm body. The modern parse mis-slices this — the extra
    // icon bytes swallow the name — and the parser must detect and re-read it.
    Bytes b;
    putHeader(b, 1, /*legacyVanilla=*/false);
    putVanillaRealm(b, "VMangos Realm", "127.0.0.1:8085", 1);

    network::Packet pkt(0, b);
    RealmListResponse resp;
    REQUIRE(RealmListResponseParser::parse(pkt, resp, /*legacyVanillaLayout=*/false));
    REQUIRE(resp.realms.size() == 1);
    CHECK(resp.realms[0].name == "VMangos Realm");
    CHECK(resp.realms[0].address == "127.0.0.1:8085");
    CHECK(resp.realms[0].id == 1);
}

TEST_CASE("Realm list: empty realm list is not an error", "[realm_list]") {
    Bytes b;
    putHeader(b, 0, /*legacyVanilla=*/true);

    network::Packet pkt(0, b);
    RealmListResponse resp;
    REQUIRE(RealmListResponseParser::parse(pkt, resp, /*legacyVanillaLayout=*/true));
    CHECK(resp.realms.empty());
}

TEST_CASE("Logon proof legacy format matches 1.11+ vanilla struct", "[auth_packets]") {
    std::vector<uint8_t> A(32, 0xA5);
    std::vector<uint8_t> M1(20, 0x5A);
    std::array<uint8_t, 20> crc{};

    // A(32) + M1(20) + crc(20) + numKeys(1) + securityFlags(1): mangos-family
    // realmd reads this as a fixed-size struct, so the flags byte must be sent
    // even though the legacy format carries no PIN/token payload.
    auto legacy = LogonProofPacket::buildLegacy(A, M1, &crc);
    CHECK(legacy.getSize() == 32 + 20 + 20 + 1 + 1);

    auto v8 = LogonProofPacket::build(A, M1, 0, &crc, nullptr, nullptr);
    CHECK(v8.getSize() == 32 + 20 + 20 + 1 + 1);
}

TEST_CASE("Logon proof PIN format includes security proof data", "[auth_packets]") {
    std::vector<uint8_t> A(32, 0xA5);
    std::vector<uint8_t> M1(20, 0x5A);
    std::array<uint8_t, 20> crc{};
    std::array<uint8_t, 16> pinSalt{};
    std::array<uint8_t, 20> pinHash{};

    auto v8 = LogonProofPacket::build(A, M1, 0x01, &crc, &pinSalt, &pinHash);
    CHECK(v8.getSize() == 32 + 20 + 20 + 1 + 1 + 16 + 20);
}
