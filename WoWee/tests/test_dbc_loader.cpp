// DBC binary parsing tests with synthetic data
#include <catch_amalgamated.hpp>
#include "pipeline/dbc_loader.hpp"
#include "pipeline/dbc_layout.hpp"
#include <cstring>

using wowee::pipeline::DBCFile;

// Build a minimal valid DBC in memory:
//   Header: "WDBC" + recordCount(uint32) + fieldCount(uint32) + recordSize(uint32) + stringBlockSize(uint32)
//   Records: contiguous fixed-size rows
//   String block: null-terminated strings
static std::vector<uint8_t> buildSyntheticDBC(
    uint32_t numRecords, uint32_t numFields,
    const std::vector<std::vector<uint32_t>>& records,
    const std::string& stringBlock)
{
    const uint32_t recordSize = numFields * 4;
    const uint32_t stringBlockSize = static_cast<uint32_t>(stringBlock.size());

    std::vector<uint8_t> data;
    // Reserve enough space
    data.reserve(20 + numRecords * recordSize + stringBlockSize);

    // Magic
    data.push_back('W'); data.push_back('D'); data.push_back('B'); data.push_back('C');

    auto writeU32 = [&](uint32_t v) {
        data.push_back(static_cast<uint8_t>(v & 0xFF));
        data.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        data.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    };

    writeU32(numRecords);
    writeU32(numFields);
    writeU32(recordSize);
    writeU32(stringBlockSize);

    // Records
    for (const auto& rec : records) {
        for (uint32_t field : rec) {
            writeU32(field);
        }
    }

    // String block
    for (char c : stringBlock) {
        data.push_back(static_cast<uint8_t>(c));
    }

    return data;
}

TEST_CASE("DBCFile default state", "[dbc]") {
    DBCFile dbc;
    REQUIRE_FALSE(dbc.isLoaded());
    REQUIRE(dbc.getRecordCount() == 0);
    REQUIRE(dbc.getFieldCount() == 0);
}

TEST_CASE("DBCFile load valid DBC", "[dbc]") {
    // 2 records, 3 fields each: [id, intVal, stringOffset]
    // String block: "\0Hello\0World\0" → offset 0="" 1="Hello" 7="World"
    std::string strings;
    strings += '\0';             // offset 0: empty string
    strings += "Hello";
    strings += '\0';             // offset 1-6: "Hello"
    strings += "World";
    strings += '\0';             // offset 7-12: "World"

    auto data = buildSyntheticDBC(2, 3,
        {
            {1, 100, 1},   // Record 0: id=1, intVal=100, stringOffset=1 → "Hello"
            {2, 200, 7},   // Record 1: id=2, intVal=200, stringOffset=7 → "World"
        },
        strings);

    DBCFile dbc;
    REQUIRE(dbc.load(data));
    REQUIRE(dbc.isLoaded());
    REQUIRE(dbc.getRecordCount() == 2);
    REQUIRE(dbc.getFieldCount() == 3);
    REQUIRE(dbc.getRecordSize() == 12);
    REQUIRE(dbc.getStringBlockSize() == strings.size());
}

TEST_CASE("DBCFile getUInt32 and getInt32", "[dbc]") {
    auto data = buildSyntheticDBC(1, 2,
        { {42, 0xFFFFFFFF} },
        std::string(1, '\0'));

    DBCFile dbc;
    REQUIRE(dbc.load(data));

    REQUIRE(dbc.getUInt32(0, 0) == 42);
    REQUIRE(dbc.getUInt32(0, 1) == 0xFFFFFFFF);
    REQUIRE(dbc.getInt32(0, 1) == -1);
}

TEST_CASE("DBCFile getFloat", "[dbc]") {
    float testVal = 3.14f;
    uint32_t bits;
    std::memcpy(&bits, &testVal, 4);

    auto data = buildSyntheticDBC(1, 2,
        { {1, bits} },
        std::string(1, '\0'));

    DBCFile dbc;
    REQUIRE(dbc.load(data));
    REQUIRE(dbc.getFloat(0, 1) == Catch::Approx(3.14f));
}

TEST_CASE("DBCFile getString", "[dbc]") {
    std::string strings;
    strings += '\0';
    strings += "TestString";
    strings += '\0';

    auto data = buildSyntheticDBC(1, 2,
        { {1, 1} },  // field 1 = string offset 1 → "TestString"
        strings);

    DBCFile dbc;
    REQUIRE(dbc.load(data));
    REQUIRE(dbc.getString(0, 1) == "TestString");
}

TEST_CASE("DBCFile getStringView", "[dbc]") {
    std::string strings;
    strings += '\0';
    strings += "ViewTest";
    strings += '\0';

    auto data = buildSyntheticDBC(1, 2,
        { {1, 1} },
        strings);

    DBCFile dbc;
    REQUIRE(dbc.load(data));
    REQUIRE(dbc.getStringView(0, 1) == "ViewTest");
}

TEST_CASE("DBCFile findRecordById", "[dbc]") {
    auto data = buildSyntheticDBC(3, 2,
        {
            {10, 100},
            {20, 200},
            {30, 300},
        },
        std::string(1, '\0'));

    DBCFile dbc;
    REQUIRE(dbc.load(data));

    REQUIRE(dbc.findRecordById(10) == 0);
    REQUIRE(dbc.findRecordById(20) == 1);
    REQUIRE(dbc.findRecordById(30) == 2);
    REQUIRE(dbc.findRecordById(99) == -1);
}

TEST_CASE("DBCFile getRecord returns pointer", "[dbc]") {
    auto data = buildSyntheticDBC(1, 2,
        { {0xAB, 0xCD} },
        std::string(1, '\0'));

    DBCFile dbc;
    REQUIRE(dbc.load(data));

    const uint8_t* rec = dbc.getRecord(0);
    REQUIRE(rec != nullptr);

    // First field should be 0xAB in little-endian
    uint32_t val;
    std::memcpy(&val, rec, 4);
    REQUIRE(val == 0xAB);
}

TEST_CASE("DBCFile load too small data", "[dbc]") {
    std::vector<uint8_t> tiny = {'W', 'D', 'B'};
    DBCFile dbc;
    REQUIRE_FALSE(dbc.load(tiny));
}

TEST_CASE("DBCFile load wrong magic", "[dbc]") {
    auto data = buildSyntheticDBC(0, 1, {}, std::string(1, '\0'));
    // Corrupt magic
    data[0] = 'X';

    DBCFile dbc;
    REQUIRE_FALSE(dbc.load(data));
}

TEST_CASE("DBCFile getStringByOffset", "[dbc]") {
    std::string strings;
    strings += '\0';
    strings += "Offset5";  // This would be at offset 1 actually, let me be precise
    strings += '\0';

    auto data = buildSyntheticDBC(1, 1,
        { {0} },
        strings);

    DBCFile dbc;
    REQUIRE(dbc.load(data));
    REQUIRE(dbc.getStringByOffset(1) == "Offset5");
    REQUIRE(dbc.getStringByOffset(0).empty());
}

// ============== JSON DBC Tests ==============

static std::vector<uint8_t> buildJsonDBC(const std::string& json) {
    return std::vector<uint8_t>(json.begin(), json.end());
}

TEST_CASE("JSON DBC basic load", "[dbc][json]") {
    auto data = buildJsonDBC(R"({
        "format": "wowee-dbc-json-1.0",
        "fieldCount": 3,
        "recordCount": 2,
        "records": [
            [1, "Fireball", 100],
            [2, "Frostbolt", 200]
        ]
    })");

    DBCFile dbc;
    REQUIRE(dbc.load(data));
    REQUIRE(dbc.getRecordCount() == 2);
    REQUIRE(dbc.getFieldCount() == 3);
    REQUIRE(dbc.getUInt32(0, 0) == 1);
    REQUIRE(dbc.getString(0, 1) == "Fireball");
    REQUIRE(dbc.getUInt32(0, 2) == 100);
    REQUIRE(dbc.getUInt32(1, 0) == 2);
    REQUIRE(dbc.getString(1, 1) == "Frostbolt");
    REQUIRE(dbc.getUInt32(1, 2) == 200);
}

TEST_CASE("JSON DBC with float values", "[dbc][json]") {
    auto data = buildJsonDBC(R"({
        "fieldCount": 2,
        "records": [
            [1, 3.14]
        ]
    })");

    DBCFile dbc;
    REQUIRE(dbc.load(data));
    REQUIRE(dbc.getUInt32(0, 0) == 1);
    REQUIRE(dbc.getFloat(0, 1) == Catch::Approx(3.14f).margin(0.01f));
}

TEST_CASE("JSON DBC empty records", "[dbc][json]") {
    auto data = buildJsonDBC(R"({"records": []})");
    DBCFile dbc;
    REQUIRE_FALSE(dbc.load(data));
}

TEST_CASE("JSON DBC missing records key", "[dbc][json]") {
    auto data = buildJsonDBC(R"({"format": "test"})");
    DBCFile dbc;
    REQUIRE_FALSE(dbc.load(data));
}

TEST_CASE("JSON DBC findRecordById", "[dbc][json]") {
    auto data = buildJsonDBC(R"({
        "fieldCount": 2,
        "records": [
            [10, "Alpha"],
            [20, "Beta"],
            [30, "Gamma"]
        ]
    })");

    DBCFile dbc;
    REQUIRE(dbc.load(data));
    REQUIRE(dbc.findRecordById(20) == 1);
    REQUIRE(dbc.findRecordById(30) == 2);
    REQUIRE(dbc.findRecordById(99) == -1);
}

// ============== Hardening tests for the recent overflow guards ==============

TEST_CASE("DBCFile::load rejects absurd recordCount header", "[dbc][hardening]") {
    // Hand-build a DBC header that would overflow the recordCount * recordSize
    // multiplication if we used uint32 — recordCount=1B, recordSize=1024.
    // Without the bounds check the resize would be tiny but the memcpy would
    // read TB of memory.
    using namespace wowee::pipeline;
    std::vector<uint8_t> data(20);
    std::memcpy(data.data(), "WDBC", 4);
    uint32_t recordCount = 1'000'000'000;
    uint32_t fieldCount = 256;
    uint32_t recordSize = 1024;
    uint32_t stringBlockSize = 0;
    std::memcpy(data.data() + 4, &recordCount, 4);
    std::memcpy(data.data() + 8, &fieldCount, 4);
    std::memcpy(data.data() + 12, &recordSize, 4);
    std::memcpy(data.data() + 16, &stringBlockSize, 4);

    DBCFile dbc;
    REQUIRE_FALSE(dbc.load(data));
}

TEST_CASE("DBCFile::load rejects absurd fieldCount header", "[dbc][hardening]") {
    using namespace wowee::pipeline;
    std::vector<uint8_t> data(20);
    std::memcpy(data.data(), "WDBC", 4);
    uint32_t recordCount = 10;
    uint32_t fieldCount = 65535;  // > 1024 cap
    uint32_t recordSize = fieldCount * 4;
    uint32_t stringBlockSize = 0;
    std::memcpy(data.data() + 4, &recordCount, 4);
    std::memcpy(data.data() + 8, &fieldCount, 4);
    std::memcpy(data.data() + 12, &recordSize, 4);
    std::memcpy(data.data() + 16, &stringBlockSize, 4);

    DBCFile dbc;
    REQUIRE_FALSE(dbc.load(data));
}

// SpellItemEnchantment.dbc moved its name column across expansions
// (Vanilla=10, TBC=13, WotLK=14). Picking the wrong one reads an integer column
// as a string-block offset, which silently yields a name missing its first
// characters ("Rockbiter 3" → "ockbiter 3") rather than an empty string.
TEST_CASE("detectEnchantmentNameField picks the name column per record width", "[dbc][enchant]") {
    using namespace wowee::pipeline;

    // "\0Rockbiter 3\0" → offset 1 is the real name; offset 2 is the garbled read.
    std::string strings;
    strings += '\0';
    strings += "Rockbiter 3";
    strings += '\0';

    auto buildRecord = [](uint32_t numFields, uint32_t nameField) {
        std::vector<uint32_t> rec(numFields, 0);
        rec[0] = 1;            // ID
        rec[nameField] = 1;    // real name offset
        rec[8] = 2;            // the column the old code read → "ockbiter 3"
        return rec;
    };

    struct Case { uint32_t fieldCount; uint32_t expectedNameField; };
    auto c = GENERATE(Case{21, 10}, Case{34, 13}, Case{38, 14});

    auto data = buildSyntheticDBC(1, c.fieldCount,
                                  {buildRecord(c.fieldCount, c.expectedNameField)}, strings);
    DBCFile dbc;
    REQUIRE(dbc.load(data));

    // No layout: the record width alone must resolve the column.
    uint32_t field = detectEnchantmentNameField(&dbc, nullptr);
    REQUIRE(field == c.expectedNameField);
    REQUIRE(dbc.getString(0, field) == "Rockbiter 3");
    REQUIRE(dbc.getString(0, 8) == "ockbiter 3");  // what the bug produced
}

// An out-of-range layout override must not win — a stale index garbles every name.
TEST_CASE("detectEnchantmentNameField ignores an out-of-range layout override", "[dbc][enchant]") {
    using namespace wowee::pipeline;

    std::string strings;
    strings += '\0';
    strings += "Sharpened (+2 Damage)";
    strings += '\0';

    std::vector<uint32_t> rec(38, 0);
    rec[0] = 40;
    rec[14] = 1;
    auto data = buildSyntheticDBC(1, 38, {rec}, strings);
    DBCFile dbc;
    REQUIRE(dbc.load(data));

    DBCFieldMap layout;
    layout.fields["Name"] = 999;  // out of range
    REQUIRE(detectEnchantmentNameField(&dbc, &layout) == 14);
    REQUIRE(dbc.getString(0, 14) == "Sharpened (+2 Damage)");
}

// Enchant → ItemVisuals → ItemVisualEffects is what puts the glint on a freshly
// sharpened blade. The ItemVisual column moves with the record like the name does
// (Vanilla 19, TBC 30, WotLK 31).
TEST_CASE("resolveEnchantItemVisuals walks enchant to effect model paths", "[dbc][enchant]") {
    using namespace wowee::pipeline;

    std::string strings;
    strings += '\0';
    const uint32_t sparkleOffset = static_cast<uint32_t>(strings.size());
    strings += "Spells\\Enchantments\\Sparkle_A.mdx";
    strings += '\0';

    // WotLK SpellItemEnchantment: enchant 40 (Rough Sharpening Stone) → ItemVisual 28.
    std::vector<uint32_t> enchantRec(38, 0);
    enchantRec[0] = 40;
    enchantRec[31] = 28;
    auto enchantData = buildSyntheticDBC(1, 38, {enchantRec}, std::string(1, '\0'));
    DBCFile sie;
    REQUIRE(sie.load(enchantData));

    // ItemVisuals: ID + 5 effect slots. Slot 0 used, rest empty.
    std::vector<uint32_t> visualRec(6, 0);
    visualRec[0] = 28;
    visualRec[1] = 777;
    auto visualData = buildSyntheticDBC(1, 6, {visualRec}, std::string(1, '\0'));
    DBCFile visuals;
    REQUIRE(visuals.load(visualData));

    // ItemVisualEffects: ID + model path.
    auto effectData = buildSyntheticDBC(1, 2, {{777, sparkleOffset}}, strings);
    DBCFile effects;
    REQUIRE(effects.load(effectData));

    auto models = resolveEnchantItemVisuals(40, &sie, &visuals, &effects, nullptr);
    REQUIRE(models[0] == "Spells\\Enchantments\\Sparkle_A.mdx");
    for (size_t i = 1; i < models.size(); ++i) REQUIRE(models[i].empty());

    // An enchant with no visual (ItemVisual 0) yields nothing rather than slot 0's model.
    std::vector<uint32_t> plainRec(38, 0);
    plainRec[0] = 2629;   // Brilliant Mana Oil — no item visual
    auto plainData = buildSyntheticDBC(1, 38, {plainRec}, std::string(1, '\0'));
    DBCFile plain;
    REQUIRE(plain.load(plainData));
    auto none = resolveEnchantItemVisuals(2629, &plain, &visuals, &effects, nullptr);
    for (const auto& m : none) REQUIRE(m.empty());
}
