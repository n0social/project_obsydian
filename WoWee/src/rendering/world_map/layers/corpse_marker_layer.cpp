// corpse_marker_layer.cpp — Death corpse tombstone marker on the world map.
// Uses Rotating-MinimapCorpseArrow.blp from the game data.
#include "rendering/world_map/layers/corpse_marker_layer.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_context.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

namespace wowee {
namespace rendering {
namespace world_map {

CorpseMarkerLayer::~CorpseMarkerLayer() {
    if (vkCtx_) {
        VkDevice device = vkCtx_->getDevice();
        VmaAllocator alloc = vkCtx_->getAllocator();
        if (imguiDS_) ImGui_ImplVulkan_RemoveTexture(imguiDS_);
        if (texture_) texture_->destroy(device, alloc);
    }
}

void CorpseMarkerLayer::initialize(VkContext* ctx, pipeline::AssetManager* am) {
    vkCtx_ = ctx;
    assetManager_ = am;
}

void CorpseMarkerLayer::clearTexture() {
    if (vkCtx_) {
        VkDevice device = vkCtx_->getDevice();
        VmaAllocator alloc = vkCtx_->getAllocator();
        if (imguiDS_) { ImGui_ImplVulkan_RemoveTexture(imguiDS_); imguiDS_ = VK_NULL_HANDLE; }
        if (texture_) { texture_->destroy(device, alloc); texture_.reset(); }
    }
    loadAttempted_ = false;
}

void CorpseMarkerLayer::ensureTexture() {
    if (loadAttempted_ || !vkCtx_ || !assetManager_) return;
    loadAttempted_ = true;

    VkDevice device = vkCtx_->getDevice();

    auto blp = assetManager_->loadTexture("Interface\\Minimap\\Rotating-MinimapCorpseArrow.blp");
    if (!blp.isValid()) {
        LOG_WARNING("CorpseMarkerLayer: Rotating-MinimapCorpseArrow.blp not found");
        return;
    }
    auto tex = std::make_unique<VkTexture>();
    if (!tex->upload(*vkCtx_, blp.data.data(), blp.width, blp.height,
                     VK_FORMAT_R8G8B8A8_UNORM, false))
        return;
    if (!tex->createSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1.0f)) {
        tex->destroy(device, vkCtx_->getAllocator());
        return;
    }
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        tex->getSampler(), tex->getImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!ds) {
        tex->destroy(device, vkCtx_->getAllocator());
        return;
    }
    texture_ = std::move(tex);
    imguiDS_ = ds;
    LOG_INFO("CorpseMarkerLayer: loaded corpse icon ", blp.width, "x", blp.height);
}

void CorpseMarkerLayer::render(const LayerContext& ctx) {
    if (!hasCorpse_) return;
    if (ctx.currentZoneIdx < 0) return;
    if (ctx.viewLevel != ViewLevel::ZONE && ctx.viewLevel != ViewLevel::CONTINENT) return;
    if (!ctx.zones) return;

    const auto& zone = (*ctx.zones)[ctx.currentZoneIdx];
    ZoneBounds bounds = zone.bounds;
    bool isContinent = zone.areaID == 0;
    if (isContinent) {
        float l, r, t, b;
        if (getContinentProjectionBounds(*ctx.zones, ctx.currentZoneIdx, l, r, t, b)) {
            bounds = {l, r, t, b};
        }
    }

    glm::vec2 uv = renderPosToMapUV(corpseRenderPos_, bounds, isContinent);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return;

    float cx = ctx.imgMin.x + uv.x * ctx.displayW;
    float cy = ctx.imgMin.y + uv.y * ctx.displayH;

    ensureTexture();

    constexpr float ICON_HALF = 12.0f;

    if (imguiDS_) {
        ctx.drawList->AddImage(
            reinterpret_cast<ImTextureID>(imguiDS_),
            ImVec2(cx - ICON_HALF, cy - ICON_HALF),
            ImVec2(cx + ICON_HALF, cy + ICON_HALF),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32_WHITE);
    } else {
        // Fallback: bone-white X if texture failed to load
        constexpr float R = 5.0f;
        constexpr float T = 1.8f;
        ctx.drawList->AddLine(ImVec2(cx - R, cy - R), ImVec2(cx + R, cy + R),
                              IM_COL32(0, 0, 0, 220), T + 1.5f);
        ctx.drawList->AddLine(ImVec2(cx + R, cy - R), ImVec2(cx - R, cy + R),
                              IM_COL32(0, 0, 0, 220), T + 1.5f);
        ctx.drawList->AddLine(ImVec2(cx - R, cy - R), ImVec2(cx + R, cy + R),
                              IM_COL32(230, 220, 200, 240), T);
        ctx.drawList->AddLine(ImVec2(cx + R, cy - R), ImVec2(cx - R, cy + R),
                              IM_COL32(230, 220, 200, 240), T);
    }

    // Tooltip on hover
    ImVec2 mp = ImGui::GetMousePos();
    float dx = mp.x - cx, dy = mp.y - cy;
    if (dx * dx + dy * dy < ICON_HALF * ICON_HALF) {
        ImGui::SetTooltip("Your corpse");
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
