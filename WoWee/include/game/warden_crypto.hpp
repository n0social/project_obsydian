#pragma once

#include <vector>
#include <cstdint>
#include <memory>

namespace wowee {
namespace game {

/**
 * Warden anti-cheat crypto handler.
 * Derives RC4 keys from the 40-byte SRP session key using SHA1Randx,
 * then encrypts/decrypts Warden packet payloads.
 */
class WardenCrypto {
public:
    WardenCrypto();
    ~WardenCrypto();

    /**
     * Initialize Warden crypto from the 40-byte SRP session key.
     * Derives encrypt (client->server) and decrypt (server->client) RC4 keys
     * using the SHA1Randx / WardenKeyGenerator algorithm.
     * @param swapKeys If true, swap encrypt/decrypt key assignment (some cores
     *                 reverse InputKey/OutputKey relative to MaNGOS).
     */
    bool initFromSessionKey(const std::vector<uint8_t>& sessionKey, bool swapKeys = false);

    /**
     * Replace RC4 keys (called after module hash challenge succeeds).
     * @param newEncryptKey 16-byte key for encrypting outgoing packets
     * @param newDecryptKey 16-byte key for decrypting incoming packets
     */
    void replaceKeys(const std::vector<uint8_t>& newEncryptKey,
                     const std::vector<uint8_t>& newDecryptKey);

    /** Decrypt an incoming SMSG_WARDEN_DATA payload. */
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data);

    /** Encrypt an outgoing CMSG_WARDEN_DATA payload. */
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data);

    /**
     * Trial-decrypt without mutating this instance. Used to pick key order
     * before committing RC4 state on the first Warden packet.
     */
    static std::vector<uint8_t> trialDecrypt(const std::vector<uint8_t>& sessionKey,
                                             const std::vector<uint8_t>& data,
                                             bool swapKeys);

    bool isInitialized() const { return initialized_; }

    /**
     * First byte of the client→server (encrypt / server InputKey) RC4 key.
     * MaNGOS/AC XOR check-type bytes with this value and usually append it as
     * the CHEAT_CHECKS_REQUEST trailer — but some cores omit the trailer or
     * leave a different last byte, so parsers must prefer this over packet.back().
     */
    uint8_t checkXorByte() const { return checkXorByte_; }

    bool keysSwapped() const { return keysSwapped_; }

private:
    bool initialized_;
    bool keysSwapped_ = false;
    uint8_t checkXorByte_ = 0;

    // RC4 state for decrypting incoming packets (server->client)
    std::vector<uint8_t> decryptRC4State_;
    uint8_t decryptRC4_i_;
    uint8_t decryptRC4_j_;

    // RC4 state for encrypting outgoing packets (client->server)
    std::vector<uint8_t> encryptRC4State_;
    uint8_t encryptRC4_i_;
    uint8_t encryptRC4_j_;

    void initRC4(const std::vector<uint8_t>& key,
                 std::vector<uint8_t>& state,
                 uint8_t& i, uint8_t& j);

    void processRC4(const uint8_t* input, uint8_t* output, size_t length,
                    std::vector<uint8_t>& state, uint8_t& i, uint8_t& j);

public:
    /**
     * SHA1Randx / WardenKeyGenerator: generates pseudo-random bytes from a seed.
     * Used to derive the 16-byte encrypt and decrypt keys from a seed.
     * Public so GameHandler can use it for module hash key derivation.
     */
    static void sha1RandxGenerate(const std::vector<uint8_t>& seed,
                                  uint8_t* outputEncryptKey,
                                  uint8_t* outputDecryptKey);
};

} // namespace game
} // namespace wowee
