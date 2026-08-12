// overlay_renderer.cpp — ImGui overlay orchestrator for the world map.
// Extracted from WorldMap::renderImGuiOverlay (Phase 8 of refactoring plan).
#include "rendering/world_map/overlay_renderer.hpp"

namespace wowee {
namespace rendering {
namespace world_map {

void OverlayRenderer::addLayer(std::unique_ptr<IOverlayLayer> layer) {
    layers_.push_back(std::move(layer));
}

void OverlayRenderer::render(const LayerContext& ctx) {
    for (auto& layer : layers_) {
        layer->render(ctx);
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
