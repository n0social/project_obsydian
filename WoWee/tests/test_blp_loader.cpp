// BLP loader tests: isValid, format names, invalid data handling
#include <catch_amalgamated.hpp>
#include "pipeline/blp_loader.hpp"

using wowee::pipeline::BLPLoader;
using wowee::pipeline::BLPImage;
using wowee::pipeline::BLPFormat;
using wowee::pipeline::BLPCompression;

TEST_CASE("BLPImage default is invalid", "[blp]") {
    BLPImage img;
    REQUIRE_FALSE(img.isValid());
    REQUIRE(img.width == 0);
    REQUIRE(img.height == 0);
    REQUIRE(img.format == BLPFormat::UNKNOWN);
    REQUIRE(img.compression == BLPCompression::NONE);
}

TEST_CASE("BLPImage isValid with data", "[blp]") {
    BLPImage img;
    img.width = 64;
    img.height = 64;
    img.data.resize(64 * 64 * 4, 0xFF);  // RGBA
    REQUIRE(img.isValid());
}

TEST_CASE("BLPImage isValid requires non-empty data", "[blp]") {
    BLPImage img;
    img.width = 64;
    img.height = 64;
    // data is empty
    REQUIRE_FALSE(img.isValid());
}

TEST_CASE("BLPLoader::load empty data returns invalid", "[blp]") {
    std::vector<uint8_t> empty;
    auto img = BLPLoader::load(empty);
    REQUIRE_FALSE(img.isValid());
}

TEST_CASE("BLPLoader::load too small data returns invalid", "[blp]") {
    std::vector<uint8_t> tiny = {0x42, 0x4C, 0x50}; // BLP but truncated
    auto img = BLPLoader::load(tiny);
    REQUIRE_FALSE(img.isValid());
}

TEST_CASE("BLPLoader::load invalid magic returns invalid", "[blp]") {
    // Provide enough bytes but with wrong magic
    std::vector<uint8_t> bad(256, 0);
    bad[0] = 'N'; bad[1] = 'O'; bad[2] = 'T'; bad[3] = '!';
    auto img = BLPLoader::load(bad);
    REQUIRE_FALSE(img.isValid());
}

TEST_CASE("BLPLoader getFormatName returns non-null", "[blp]") {
    REQUIRE(BLPLoader::getFormatName(BLPFormat::UNKNOWN) != nullptr);
    REQUIRE(BLPLoader::getFormatName(BLPFormat::BLP1) != nullptr);
    REQUIRE(BLPLoader::getFormatName(BLPFormat::BLP2) != nullptr);

    // Check that names are distinct
    REQUIRE(std::string(BLPLoader::getFormatName(BLPFormat::BLP1)) !=
            std::string(BLPLoader::getFormatName(BLPFormat::BLP2)));
}

TEST_CASE("BLPLoader getCompressionName returns non-null", "[blp]") {
    REQUIRE(BLPLoader::getCompressionName(BLPCompression::NONE) != nullptr);
    REQUIRE(BLPLoader::getCompressionName(BLPCompression::DXT1) != nullptr);
    REQUIRE(BLPLoader::getCompressionName(BLPCompression::DXT3) != nullptr);
    REQUIRE(BLPLoader::getCompressionName(BLPCompression::DXT5) != nullptr);
    REQUIRE(BLPLoader::getCompressionName(BLPCompression::PALETTE) != nullptr);
    REQUIRE(BLPLoader::getCompressionName(BLPCompression::ARGB8888) != nullptr);

    // Check that DXT names differ
    REQUIRE(std::string(BLPLoader::getCompressionName(BLPCompression::DXT1)) !=
            std::string(BLPLoader::getCompressionName(BLPCompression::DXT5)));
}

TEST_CASE("BLPImage mipmap storage", "[blp]") {
    BLPImage img;
    img.width = 128;
    img.height = 128;
    img.data.resize(128 * 128 * 4, 0);
    img.mipLevels = 3;

    // Add mipmap data
    img.mipmaps.push_back(std::vector<uint8_t>(64 * 64 * 4, 0));
    img.mipmaps.push_back(std::vector<uint8_t>(32 * 32 * 4, 0));
    img.mipmaps.push_back(std::vector<uint8_t>(16 * 16 * 4, 0));

    REQUIRE(img.isValid());
    REQUIRE(img.mipmaps.size() == 3);
}
