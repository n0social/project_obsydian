// SRP6a challenge/proof smoke tests
#include <catch_amalgamated.hpp>
#include "auth/srp.hpp"
#include "auth/crypto.hpp"

using wowee::auth::SRP;
using wowee::auth::Crypto;

// WoW 3.3.5a uses well-known SRP6a parameters.
// Generator g = 7, N = a large 32-byte safe prime.
// We use the canonical WoW values for integration-level tests.

static const std::vector<uint8_t> kWoWGenerator = { 7 };

// WoW's 32-byte large safe prime (little-endian)
static const std::vector<uint8_t> kWoWPrime = {
    0xB7, 0x9B, 0x3E, 0x2A, 0x87, 0x82, 0x3C, 0xAB,
    0x8F, 0x5E, 0xBF, 0xBF, 0x8E, 0xB1, 0x01, 0x08,
    0x53, 0x50, 0x06, 0x29, 0x8B, 0x5B, 0xAD, 0xBD,
    0x5B, 0x53, 0xE1, 0x89, 0x5E, 0x64, 0x4B, 0x89
};

TEST_CASE("SRP initialize stores credentials", "[srp]") {
    SRP srp;
    // Should not throw
    REQUIRE_NOTHROW(srp.initialize("TEST", "PASSWORD"));
}

TEST_CASE("SRP initializeWithHash accepts pre-computed hash", "[srp]") {
    // Pre-compute SHA1("TEST:PASSWORD")
    auto hash = Crypto::sha1(std::string("TEST:PASSWORD"));
    REQUIRE(hash.size() == 20);

    SRP srp;
    REQUIRE_NOTHROW(srp.initializeWithHash("TEST", hash));
}

TEST_CASE("SRP feed produces A and M1 of correct sizes", "[srp]") {
    SRP srp;
    srp.initialize("TEST", "PASSWORD");

    // Fabricate a server B (32 bytes, non-zero to avoid SRP abort)
    std::vector<uint8_t> B(32, 0);
    B[0] = 0x42; // Non-zero

    std::vector<uint8_t> salt(32, 0xAA);

    srp.feed(B, kWoWGenerator, kWoWPrime, salt);

    auto A = srp.getA();
    auto M1 = srp.getM1();
    auto K = srp.getSessionKey();

    // A should be 32 bytes (same size as N)
    REQUIRE(A.size() == 32);
    // M1 is SHA1 → 20 bytes
    REQUIRE(M1.size() == 20);
    // K is the interleaved session key → 40 bytes
    REQUIRE(K.size() == 40);
}

TEST_CASE("SRP A is non-zero", "[srp]") {
    SRP srp;
    srp.initialize("PLAYER", "SECRET");

    std::vector<uint8_t> B(32, 0);
    B[3] = 0x01;
    std::vector<uint8_t> salt(32, 0xBB);

    srp.feed(B, kWoWGenerator, kWoWPrime, salt);

    auto A = srp.getA();
    bool allZero = true;
    for (auto b : A) {
        if (b != 0) { allZero = false; break; }
    }
    REQUIRE_FALSE(allZero);
}

TEST_CASE("SRP different passwords produce different M1", "[srp]") {
    auto runSrp = [](const std::string& pass) {
        SRP srp;
        srp.initialize("TESTUSER", pass);
        std::vector<uint8_t> B(32, 0);
        B[0] = 0x11;
        std::vector<uint8_t> salt(32, 0xCC);
        srp.feed(B, kWoWGenerator, kWoWPrime, salt);
        return srp.getM1();
    };

    auto m1a = runSrp("PASSWORD1");
    auto m1b = runSrp("PASSWORD2");
    REQUIRE(m1a != m1b);
}

TEST_CASE("SRP verifyServerProof rejects wrong proof", "[srp]") {
    SRP srp;
    srp.initialize("TEST", "PASSWORD");

    std::vector<uint8_t> B(32, 0);
    B[0] = 0x55;
    std::vector<uint8_t> salt(32, 0xDD);

    srp.feed(B, kWoWGenerator, kWoWPrime, salt);

    // Random 20 bytes should not match the expected M2
    std::vector<uint8_t> fakeM2(20, 0xFF);
    REQUIRE_FALSE(srp.verifyServerProof(fakeM2));
}

TEST_CASE("SRP setUseHashedK changes behavior", "[srp]") {
    auto runWithHashedK = [](bool useHashed) {
        SRP srp;
        srp.setUseHashedK(useHashed);
        srp.initialize("TEST", "PASSWORD");
        std::vector<uint8_t> B(32, 0);
        B[0] = 0x22;
        std::vector<uint8_t> salt(32, 0xEE);
        srp.feed(B, kWoWGenerator, kWoWPrime, salt);
        return srp.getM1();
    };

    auto m1_default = runWithHashedK(false);
    auto m1_hashed = runWithHashedK(true);
    // Different k derivation → different M1
    REQUIRE(m1_default != m1_hashed);
}
