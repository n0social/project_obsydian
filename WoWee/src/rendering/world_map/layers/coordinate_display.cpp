// coordinate_display.cpp — WoW coordinates under cursor on the world map.
// Extracted from WorldMap::renderImGuiOverlay (Phase 8 of refactoring plan).
#include "rendering/world_map/layers/coordinate_display.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include <imgui.h>
#include <cstdio>
#include <cmath>

namespace wowee {
namespace rendering {
namespace world_map {

void CoordinateDisplay::render(const LayerContext& ctx) {
    if (ctx.currentZoneIdx < 0) return;
    if (ctx.viewLevel != ViewLevel::ZONE && ctx.viewLevel != ViewLevel::CONTINENT) return;
    if (!ctx.zones) return;

    auto& io = ImGui::GetIO();
    ImVec2 mp = io.MousePos;
    if (mp.x < ctx.imgMin.x || mp.x > ctx.imgMin.x + ctx.displayW ||
        mp.y < ctx.imgMin.y || mp.y > ctx.imgMin.y + ctx.displayH)
        return;

    float mu = (mp.x - ctx.imgMin.x) / ctx.displayW;
    float mv = (mp.y - ctx.imgMin.y) / ctx.displayH;

    const auto& zone = (*ctx.zones)[ctx.currentZoneIdx];
    float left = zone.bounds.locLeft, right = zone.bounds.locRight;
    float top = zone.bounds.locTop, bottom = zone.bounds.locBottom;
    if (zone.areaID == 0) {
        float l, r, t, b;
        getContinentProjectionBounds(*ctx.zones, ctx.currentZoneIdx, l, r, t, b);
        left = l; right = r; top = t; bottom = b;
    }

    float hWowX = left - mu * (left - right);
    float hWowY = top  - mv * (top  - bottom);

    char coordBuf[32];
    snprintf(coordBuf, sizeof(coordBuf), "%.0f, %.0f", hWowX, hWowY);
    ImVec2 coordSz = ImGui::CalcTextSize(coordBuf);
    float cx = ctx.imgMin.x + ctx.displayW - coordSz.x - 8.0f;
    float cy = ctx.imgMin.y + ctx.displayH - coordSz.y - 8.0f;
    ctx.drawList->AddText(ImVec2(cx + 1.0f, cy + 1.0f), IM_COL32(0, 0, 0, 180), coordBuf);
    ctx.drawList->AddText(ImVec2(cx, cy), IM_COL32(220, 210, 150, 230), coordBuf);
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
