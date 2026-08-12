#pragma once

#include <imgui.h>
#include <algorithm>

namespace wowee::ui {

// Shared shell for the realm and character selection screens. Keeping their
// maximum logical size and footer geometry in one place prevents controls from
// jumping when moving between login stages or using an ultrawide/high-DPI view.
struct SelectionScreenLayout {
    float scale = 1.0f;
    ImVec2 windowPos{};
    ImVec2 windowSize{};

    float footerHeight() const {
#ifdef __ANDROID__
        return 110.0f * scale;
#else
        return 88.0f * scale;
#endif
    }
    float gap() const {
#ifdef __ANDROID__
        return 16.0f * scale;
#else
        return 10.0f * scale;
#endif
    }
    ImVec2 button(float width = 120.0f) const {
#ifdef __ANDROID__
        return ImVec2(width * scale, 52.0f * scale);
#else
        return ImVec2(width * scale, 38.0f * scale);
#endif
    }
    ImVec2 primaryButton(float width = 180.0f) const {
#ifdef __ANDROID__
        return ImVec2(width * scale, 58.0f * scale);
#else
        return ImVec2(width * scale, 42.0f * scale);
#endif
    }
};

inline SelectionScreenLayout makeSelectionScreenLayout(const ImGuiViewport& vp) {
    SelectionScreenLayout layout;

#ifdef __ANDROID__
    // Full-bleed landscape on tablet (Tab A9+ ~1920x1200). Prefer filling the
    // drawable; scale widgets large enough for finger + couch distance.
    layout.scale = std::clamp(std::min(vp.Size.x / 1100.0f, vp.Size.y / 700.0f), 1.35f, 1.85f);
    layout.windowSize = vp.Size;
    layout.windowPos = vp.Pos;
#else
    const float resolutionScale = std::min(vp.Size.x / 1920.0f,
                                           vp.Size.y / 1080.0f);
    layout.scale = std::clamp(resolutionScale, 1.0f, 1.5f);

    const float margin = 28.0f * layout.scale;
    const ImVec2 available(std::max(720.0f, vp.Size.x - margin * 2.0f),
                           std::max(540.0f, vp.Size.y - margin * 2.0f));
    const ImVec2 preferred(1600.0f * layout.scale, 900.0f * layout.scale);
    layout.windowSize = ImVec2(std::min(available.x, preferred.x),
                               std::min(available.y, preferred.y));
    layout.windowPos = ImVec2(vp.Pos.x + (vp.Size.x - layout.windowSize.x) * 0.5f,
                              vp.Pos.y + (vp.Size.y - layout.windowSize.y) * 0.5f);
#endif
    return layout;
}

} // namespace wowee::ui
