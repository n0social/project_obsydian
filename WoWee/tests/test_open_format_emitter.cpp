// Tests for the asset_extract open-format emitter — verifies that the
// MPQ→loose-files pipeline produces wowee-readable side-files for the
// most common file types without touching the originals.
#include <catch_amalgamated.hpp>
#include "tools/asset_extract/open_format_emitter.hpp"
#include "pipeline/dbc_loader.hpp"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <vector>

using namespace wowee::tools;
namespace fs = std::filesystem;

static const std::string TEST_DIR = "test_emitter_out";

static void cleanup() { fs::remove_all(TEST_DIR); }

TEST_CASE("emitJsonFromDbc produces wowee-DBC-loadable JSON", "[emitter]") {
    fs::create_directories(TEST_DIR);
    std::string dbcPath = TEST_DIR + "/sample.dbc";
    std::string jsonPath = TEST_DIR + "/sample.json";

    // Hand-build a 2-record DBC (3 fields each, no string block) so the
    // round trip is small and deterministic.
    {
        std::ofstream f(dbcPath, std::ios::binary);
        const char magic[4] = {'W','D','B','C'};
        f.write(magic, 4);
        uint32_t recordCount = 2, fieldCount = 3, recordSize = 12, stringBlockSize = 1;
        f.write(reinterpret_cast<const char*>(&recordCount), 4);
        f.write(reinterpret_cast<const char*>(&fieldCount), 4);
        f.write(reinterpret_cast<const char*>(&recordSize), 4);
        f.write(reinterpret_cast<const char*>(&stringBlockSize), 4);
        // 2 records of 3 uint32s
        uint32_t rec[6] = {1, 100, 200,
                           2, 300, 400};
        f.write(reinterpret_cast<const char*>(rec), sizeof(rec));
        char nul = 0;
        f.write(&nul, 1);
    }

    REQUIRE(emitJsonFromDbc(dbcPath, jsonPath));
    REQUIRE(fs::exists(jsonPath));

    // The JSON should round-trip back through DBCFile::loadJSON.
    std::ifstream in(jsonPath, std::ios::binary | std::ios::ate);
    auto sz = in.tellg();
    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), sz);

    wowee::pipeline::DBCFile dbc;
    // Public DBCFile::load detects '{' prefix and dispatches to loadJSON.
    REQUIRE(dbc.load(bytes));
    REQUIRE(dbc.getRecordCount() == 2);
    REQUIRE(dbc.getFieldCount() == 3);
    REQUIRE(dbc.getUInt32(0, 0) == 1);
    REQUIRE(dbc.getUInt32(0, 1) == 100);
    REQUIRE(dbc.getUInt32(1, 2) == 400);

    cleanup();
}

TEST_CASE("emitJsonFromDbc fails gracefully on missing input", "[emitter]") {
    REQUIRE_FALSE(emitJsonFromDbc("does_not_exist.dbc", "should_not_be_written.json"));
    REQUIRE_FALSE(fs::exists("should_not_be_written.json"));
}

TEST_CASE("emitJsonFromDbc fails gracefully on bad magic", "[emitter]") {
    fs::create_directories(TEST_DIR);
    std::string dbcPath = TEST_DIR + "/bad.dbc";
    std::string jsonPath = TEST_DIR + "/bad.json";
    {
        std::ofstream f(dbcPath, std::ios::binary);
        const char junk[20] = {'F','A','I','L', 0};
        f.write(junk, 20);
    }
    REQUIRE_FALSE(emitJsonFromDbc(dbcPath, jsonPath));
    cleanup();
}

TEST_CASE("emitOpenFormats walks a directory and writes side-files", "[emitter]") {
    fs::create_directories(TEST_DIR + "/sub");
    // One DBC in a subdir
    {
        std::ofstream f(TEST_DIR + "/sub/test.dbc", std::ios::binary);
        const char magic[4] = {'W','D','B','C'};
        f.write(magic, 4);
        uint32_t r = 1, fc = 1, rs = 4, sb = 1;
        f.write(reinterpret_cast<const char*>(&r), 4);
        f.write(reinterpret_cast<const char*>(&fc), 4);
        f.write(reinterpret_cast<const char*>(&rs), 4);
        f.write(reinterpret_cast<const char*>(&sb), 4);
        uint32_t v = 42;
        f.write(reinterpret_cast<const char*>(&v), 4);
        char nul = 0;
        f.write(&nul, 1);
    }
    OpenFormatStats stats;
    emitOpenFormats(TEST_DIR, /*png*/ false, /*json*/ true,
                    /*wom*/ false, /*wob*/ false, /*terrain*/ false, stats);
    REQUIRE(stats.jsonDbcOk == 1);
    REQUIRE(stats.jsonDbcFail == 0);
    REQUIRE(fs::exists(TEST_DIR + "/sub/test.json"));
    cleanup();
}
