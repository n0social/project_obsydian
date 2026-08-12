#include <catch_amalgamated.hpp>

#include "game/expansion_profile.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

void setBuildOverride(const char* value) {
#ifdef _WIN32
    _putenv_s("WOWEE_TEST_AUTH_BUILD", value ? value : "");
#else
    if (value) setenv("WOWEE_TEST_AUTH_BUILD", value, 1);
    else unsetenv("WOWEE_TEST_AUTH_BUILD");
#endif
}

std::filesystem::path writeProfileTree() {
    const auto root = std::filesystem::temp_directory_path() /
                      "wowee_expansion_profile_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "expansions" / "turtle");
    std::ofstream out(root / "expansions" / "turtle" / "expansion.json");
    out << R"({
        "id": "turtle",
        "name": "Turtle WoW",
        "shortName": "Turtle",
        "version": { "major": 1, "minor": 18, "patch": 1 },
        "build": 7272,
        "buildEnv": "WOWEE_TEST_AUTH_BUILD",
        "worldBuild": 5875,
        "protocolVersion": 3
    })";
    return root;
}

} // namespace

TEST_CASE("Expansion profile keeps custom auth and world builds separate",
          "[expansion_profile]") {
    setBuildOverride(nullptr);
    const auto root = writeProfileTree();

    wowee::game::ExpansionRegistry registry;
    REQUIRE(registry.initialize(root.string()) == 1);
    const auto* profile = registry.getProfile("turtle");
    REQUIRE(profile != nullptr);
    CHECK(profile->versionString() == "1.18.1");
    CHECK(profile->build == 7272);
    CHECK(profile->worldBuild == 5875);

    std::filesystem::remove_all(root);
}

TEST_CASE("Expansion profile accepts a validated auth build override",
          "[expansion_profile]") {
    const auto root = writeProfileTree();
    setBuildOverride("7234");

    wowee::game::ExpansionRegistry registry;
    REQUIRE(registry.initialize(root.string()) == 1);
    const auto* profile = registry.getProfile("turtle");
    REQUIRE(profile != nullptr);
    CHECK(profile->build == 7234);
    CHECK(profile->worldBuild == 5875);

    setBuildOverride(nullptr);
    std::filesystem::remove_all(root);
}

TEST_CASE("Expansion profile rejects an invalid auth build override",
          "[expansion_profile]") {
    const auto root = writeProfileTree();
    setBuildOverride("999999");

    wowee::game::ExpansionRegistry registry;
    REQUIRE(registry.initialize(root.string()) == 1);
    const auto* profile = registry.getProfile("turtle");
    REQUIRE(profile != nullptr);
    CHECK(profile->build == 7272);

    setBuildOverride(nullptr);
    std::filesystem::remove_all(root);
}

TEST_CASE("Expansion registry prefers a profile with extracted assets",
          "[expansion_profile]") {
    const auto root = std::filesystem::temp_directory_path() /
                      "wowee_expansion_asset_selection_test";
    std::filesystem::remove_all(root);
    for (const auto& [id, build] : {
             std::pair<const char*, int>{"classic", 5875},
             {"turtle", 7272},
             {"wotlk", 12340}}) {
        const auto dir = root / "expansions" / id;
        std::filesystem::create_directories(dir);
        std::ofstream profile(dir / "expansion.json");
        profile << "{\"id\":\"" << id << "\",\"name\":\"" << id
                << "\",\"shortName\":\"" << id << "\",\"build\":"
                << build << ",\"protocolVersion\":3}";
    }
    std::ofstream(root / "expansions" / "turtle" / "manifest.json") << "{}";

    wowee::game::ExpansionRegistry registry;
    REQUIRE(registry.initialize(root.string()) == 3);
    REQUIRE(registry.getActive() != nullptr);
    CHECK(registry.getActive()->id == "turtle");

    std::filesystem::remove_all(root);
}
