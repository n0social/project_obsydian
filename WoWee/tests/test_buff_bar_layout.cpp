#include <catch_amalgamated.hpp>

#include "ui/buff_bar_layout.hpp"

using namespace wowee::ui;
using M = BuffBarMetrics;

namespace {

constexpr float k1080pW = 1920.0f;
constexpr float k1080pH = 1080.0f;

// Right edge of the bar. It must never reach the minimap.
float rightEdge(const BuffBarLayout& l) { return l.barX + l.barWidth; }

// The minimap's left edge, in screen pixels.
float minimapLeft(float screenW) { return screenW - M::kMinimapLeftEdge; }

} // namespace

TEST_CASE("Icons are 25% larger than the old 32px at the reference height", "[ui][buffbar]") {
    const auto l = computeBuffBarLayout(k1080pW, k1080pH, 1.0f, 5, 0);
    REQUIRE(l.iconSize == Catch::Approx(40.0f));
    REQUIRE(l.iconSize == Catch::Approx(32.0f * 1.25f));
}

TEST_CASE("Icon size tracks the window height", "[ui][buffbar]") {
    const auto small = computeBuffBarLayout(1280.0f, 720.0f, 1.0f, 5, 0);
    const auto ref   = computeBuffBarLayout(k1080pW, k1080pH, 1.0f, 5, 0);
    const auto big   = computeBuffBarLayout(3840.0f, 2160.0f, 1.0f, 5, 0);

    REQUIRE(small.iconSize < ref.iconSize);
    REQUIRE(big.iconSize > ref.iconSize);

    // Auto scaling is clamped so the bar never becomes absurd on extreme displays.
    REQUIRE(small.iconSize >= M::kBaseIconSize * M::kMinAutoScale);
    REQUIRE(big.iconSize <= M::kBaseIconSize * M::kMaxAutoScale);
}

TEST_CASE("The user scale multiplies the automatic scale", "[ui][buffbar]") {
    const auto normal = computeBuffBarLayout(k1080pW, k1080pH, 1.0f, 5, 0);
    const auto larger = computeBuffBarLayout(k1080pW, k1080pH, 1.5f, 5, 0);
    const auto smaller = computeBuffBarLayout(k1080pW, k1080pH, 0.75f, 5, 0);

    REQUIRE(larger.iconSize == Catch::Approx(normal.iconSize * 1.5f));
    REQUIRE(smaller.iconSize == Catch::Approx(normal.iconSize * 0.75f));
}

TEST_CASE("The row never overlaps the minimap", "[ui][buffbar]") {
    // Whatever the resolution, scale, or aura count, the bar has to stop before the
    // minimap: it sits at the top right, which is exactly where the row wants to be.
    for (float w : {1280.0f, 1600.0f, k1080pW, 2560.0f, 3840.0f}) {
        for (float h : {720.0f, 900.0f, k1080pH, 1440.0f, 2160.0f}) {
            for (float scale : {0.75f, 1.0f, 1.5f}) {
                for (int auras : {0, 1, 8, 40, 200}) {
                    const auto l = computeBuffBarLayout(w, h, scale, auras, 0);
                    INFO("w=" << w << " h=" << h << " scale=" << scale << " auras=" << auras);
                    REQUIRE(rightEdge(l) <= minimapLeft(w));
                }
            }
        }
    }
}

TEST_CASE("The row never runs off the left of the screen", "[ui][buffbar]") {
    for (float w : {1024.0f, 1280.0f, k1080pW}) {
        for (int auras : {0, 40, 500}) {
            const auto l = computeBuffBarLayout(w, k1080pH, 1.5f, auras, 0);
            INFO("w=" << w << " auras=" << auras);
            REQUIRE(l.barX >= M::kScreenMargin);
        }
    }
}

TEST_CASE("Nothing wraps: the row drops what will not fit", "[ui][buffbar]") {
    // A player can carry far more auras than a single row can hold. The overflow is
    // dropped rather than pushed onto a second row.
    const auto l = computeBuffBarLayout(k1080pW, k1080pH, 1.0f, 500, 0);
    REQUIRE(l.iconCount == l.maxIcons);
    REQUIRE(l.auraShown == l.maxIcons);
    REQUIRE(l.iconCount < 500);
}

TEST_CASE("Weapon enchants claim their slots before auras", "[ui][buffbar]") {
    // Regression: a sharpening stone must not be pushed off the row by a wall of buffs.
    const auto l = computeBuffBarLayout(k1080pW, k1080pH, 1.0f, 500, 2);
    REQUIRE(l.enchantShown == 2);
    REQUIRE(l.auraShown == l.maxIcons - 2);
    REQUIRE(l.iconCount == l.maxIcons);
}

TEST_CASE("Enchants alone still get a row", "[ui][buffbar]") {
    // Regression: the bar used to bail out when there were no auras, so a weapon
    // enchant on its own never showed at all.
    const auto l = computeBuffBarLayout(k1080pW, k1080pH, 1.0f, 0, 1);
    REQUIRE(l.enchantShown == 1);
    REQUIRE(l.iconCount == 1);
    REQUIRE(l.barWidth > 0.0f);
}

TEST_CASE("An empty bar still has room for the Dismiss Pet button", "[ui][buffbar]") {
    const auto l = computeBuffBarLayout(k1080pW, k1080pH, 1.0f, 0, 0);
    REQUIRE(l.iconCount == 0);
    REQUIRE(l.barWidth >= M::kMinBarWidth);
}

TEST_CASE("Bar width matches the icons it holds", "[ui][buffbar]") {
    const auto l = computeBuffBarLayout(k1080pW, k1080pH, 1.0f, 6, 0);
    REQUIRE(l.iconCount == 6);
    const float expected = M::kWindowPadding + 6 * l.iconSize + 5 * l.iconSpacing;
    REQUIRE(l.barWidth == Catch::Approx(expected));
}

TEST_CASE("Counts are never negative", "[ui][buffbar]") {
    const auto l = computeBuffBarLayout(k1080pW, k1080pH, 1.0f, -3, -1);
    REQUIRE(l.auraShown >= 0);
    REQUIRE(l.enchantShown >= 0);
    REQUIRE(l.iconCount >= 0);
}
