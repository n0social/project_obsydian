#include <catch_amalgamated.hpp>

#include "pipeline/m2_loader.hpp"
#include "rendering/m2_track_sampler.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// The peasant lumberjack carry model (HumanMalePeasantWood) contains TWO wood
// bundle submeshes; M2 color-alpha animation keeps exactly one visible per
// animation (the second only appears while the bundle is dropped during the
// Death sequence). If the loader stops parsing these tracks, the character
// renderer draws both bundles at once — the "carrying two bundles of wood"
// bug. Both the WotLK (v264, array-of-arrays tracks) and vanilla/Turtle
// (v256, flat tracks with ranges) encodings must produce usable tracks.

using wowee::pipeline::M2Loader;
using wowee::pipeline::M2Model;

namespace {

std::vector<uint8_t> readFile(const std::string& relPath) {
    const auto path = std::filesystem::path(WOWEE_SOURCE_DIR) / relPath;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

// First alpha key of the color track for the given sequence index, or -1.
float firstAlpha(const M2Model& model, size_t colorIdx, size_t seqIdx) {
    if (colorIdx >= model.colorAlphaTracks.size()) return -1.0f;
    const auto& track = model.colorAlphaTracks[colorIdx];
    if (seqIdx >= track.sequences.size()) return -1.0f;
    if (track.sequences[seqIdx].floatValues.empty()) return -1.0f;
    return track.sequences[seqIdx].floatValues[0];
}

} // namespace

TEST_CASE("M2 track sampling respects discrete and linear interpolation", "[m2][track]") {
    wowee::pipeline::M2AnimationTrack track;
    track.sequences.resize(1);
    track.sequences[0].timestamps = {0, 100};
    track.sequences[0].floatValues = {0.0f, 1.0f};

    track.interpolationType = 0;
    CHECK(wowee::rendering::m2_track::sampleFloat(
              track, 0, 50.0f, 0.0f, {}, -1.0f) == Catch::Approx(0.0f));
    CHECK(wowee::rendering::m2_track::sampleFloat(
              track, 0, 100.0f, 0.0f, {}, -1.0f) == Catch::Approx(1.0f));

    track.interpolationType = 1;
    CHECK(wowee::rendering::m2_track::sampleFloat(
              track, 0, 50.0f, 0.0f, {}, -1.0f) == Catch::Approx(0.5f));
}

TEST_CASE("M2 global tracks use their independent wrapped clock", "[m2][track]") {
    wowee::pipeline::M2AnimationTrack track;
    track.interpolationType = 1;
    track.globalSequence = 0;
    track.sequences.resize(1);
    track.sequences[0].timestamps = {0, 1000};
    track.sequences[0].floatValues = {0.0f, 1.0f};

    CHECK(wowee::rendering::m2_track::sampleFloat(
              track, 7, 900.0f, 1250.0f, {1000}, -1.0f) == Catch::Approx(0.25f));
}

TEST_CASE("Sharpened Blade sparkle preserves authored pulse timing", "[m2][transparency]") {
    auto data = readFile("Data/spell/enchantments/sparkle_a.m2");
    if (data.empty()) {
        SUCCEED("model asset not extracted; optional real-asset coverage skipped");
        return;
    }
    M2Model model = M2Loader::load(data);
    REQUIRE(model.globalSequenceDurations.size() >= 1);
    CHECK(model.globalSequenceDurations[0] == 4000);
    REQUIRE(model.textureWeightLookup.size() == 1);
    CHECK(model.textureWeightLookup[0] == 0);
    REQUIRE(model.textureWeightTracks.size() == 1);

    const auto& pulse = model.textureWeightTracks[0];
    CHECK(pulse.interpolationType == 1);
    CHECK(pulse.globalSequence == 0);
    REQUIRE(pulse.sequences.size() == 1);
    CHECK(pulse.sequences[0].timestamps ==
          std::vector<uint32_t>{0, 333, 833, 3500, 4000});
    REQUIRE(pulse.sequences[0].floatValues.size() == 5);

    // The glint is briefly on, fades out, stays absent for most of the cycle,
    // then fades back in. Sampling this track prevents a permanent saturated
    // card and restores the original once-per-four-seconds cadence.
    CHECK(wowee::rendering::m2_track::sampleFloat(
              pulse, 0, 0.0f, 333.0f, model.globalSequenceDurations, -1.0f)
          == Catch::Approx(1.0f));
    CHECK(wowee::rendering::m2_track::sampleFloat(
              pulse, 0, 0.0f, 833.0f, model.globalSequenceDurations, -1.0f)
          == Catch::Approx(0.0f));
    CHECK(wowee::rendering::m2_track::sampleFloat(
              pulse, 0, 0.0f, 2000.0f, model.globalSequenceDurations, -1.0f)
          == Catch::Approx(0.0f));
}

TEST_CASE("WotLK peasant wood model parses per-animation color alpha", "[m2][color]") {
    auto data = readFile("Data/creature/humanmalepeasant/humanmalepeasantwood.m2");
    if (data.empty()) {
        SUCCEED("model asset not extracted; optional real-asset coverage skipped");
        return;
    }
    M2Model model = M2Loader::load(data);
    REQUIRE(model.version >= 264);

    REQUIRE(model.colorAlphas.size() == 2);
    REQUIRE(model.colorAlphaTracks.size() == 2);

    // At-rest values: primary bundle opaque, alternate bundle hidden.
    CHECK(model.colorAlphas[0] == Catch::Approx(1.0f).margin(0.01f));
    CHECK(model.colorAlphas[1] == Catch::Approx(0.0f).margin(0.01f));

    // Sequence 0 is Walk: the carried bundle shows, the dropped one does not.
    CHECK(firstAlpha(model, 0, 0) == Catch::Approx(1.0f).margin(0.01f));
    CHECK(firstAlpha(model, 1, 0) == Catch::Approx(0.0f).margin(0.01f));

    // The final sequence is Death, where the bundles swap (drop animation).
    const size_t deathSeq = model.sequences.size() - 1;
    REQUIRE(model.sequences[deathSeq].id == 1);
    const auto& dropTrack = model.colorAlphaTracks[1].sequences[deathSeq];
    REQUIRE_FALSE(dropTrack.floatValues.empty());
    CHECK(dropTrack.floatValues.back() == Catch::Approx(1.0f).margin(0.01f));
}

TEST_CASE("Vanilla peasant wood model parses color alpha ranges", "[m2][color]") {
    auto data = readFile(
        "Data/expansions/turtle/overlay/creature/humanmalepeasant/HumanMalePeasantWood.m2");
    if (data.empty()) {
        SUCCEED("turtle overlay asset not extracted; optional real-asset coverage skipped");
        return;
    }
    M2Model model = M2Loader::load(data);
    REQUIRE(model.version < 264);

    REQUIRE(model.colorAlphas.size() == 2);
    REQUIRE(model.colorAlphaTracks.size() == 2);

    // Vanilla stores one flat 1→0 (and 0→1) key pair shared across sequences.
    // The at-rest bake must still resolve: bundle 0 visible, bundle 1 hidden.
    CHECK(model.colorAlphas[0] == Catch::Approx(1.0f).margin(0.01f));
    CHECK(model.colorAlphas[1] == Catch::Approx(0.0f).margin(0.01f));

    // Both color tracks are step-interpolated, so the hidden bundle stays at
    // exactly 0 for the whole first key span (the renderer culls on <= 0.01).
    REQUIRE(model.colorAlphaTracks[1].interpolationType == 0);
    CHECK(firstAlpha(model, 1, 0) == Catch::Approx(0.0f).margin(0.01f));
}
