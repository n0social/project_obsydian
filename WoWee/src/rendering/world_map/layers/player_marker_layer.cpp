// player_marker_layer.cpp — Directional player arrow on the world map.
// Uses the WoW worldmapplayericon.blp texture, rendered as a rotated quad.
#include "rendering/world_map/layers/player_marker_layer.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_context.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <cmath>
#include <algorithm>

namespace wowee {
namespace rendering {
namespace world_map {

PlayerMarkerLayer::~PlayerMarkerLayer() {
    if (vkCtx_) {
        VkDevice device = vkCtx_->getDevice();
        VmaAllocator alloc = vkCtx_->getAllocator();
        if (imguiDS_) ImGui_ImplVulkan_RemoveTexture(imguiDS_);
        if (texture_) texture_->destroy(device, alloc);
    }
}

void PlayerMarkerLayer::initialize(VkContext* ctx, pipeline::AssetManager* am) {
    vkCtx_ = ctx;
    assetManager_ = am;
}

void PlayerMarkerLayer::clearTexture() {
    if (vkCtx_) {
        VkDevice device = vkCtx_->getDevice();
        VmaAllocator alloc = vkCtx_->getAllocator();
        if (imguiDS_) { ImGui_ImplVulkan_RemoveTexture(imguiDS_); imguiDS_ = VK_NULL_HANDLE; }
        if (texture_) { texture_->destroy(device, alloc); texture_.reset(); }
    }
    loadAttempted_ = false;
}

void PlayerMarkerLayer::ensureTexture() {
    if (loadAttempted_ || !vkCtx_ || !assetManager_) return;
    loadAttempted_ = true;

    VkDevice device = vkCtx_->getDevice();

    auto blp = assetManager_->loadTexture("Interface\\Minimap\\MinimapArrow.blp");
    if (!blp.isValid()) {
        LOG_WARNING("PlayerMarkerLayer: MinimapArrow.blp not found");
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
    LOG_INFO("PlayerMarkerLayer: loaded MinimapArrow.blp ", blp.width, "x", blp.height);
}

void PlayerMarkerLayer::render(const LayerContext& ctx) {
    if (ctx.currentZoneIdx < 0) return;
    if (ctx.viewLevel != ViewLevel::ZONE && ctx.viewLevel != ViewLevel::CONTINENT) return;
    if (!ctx.zones) return;

    const auto& zone = (*ctx.zones)[ctx.currentZoneIdx];
    ZoneBounds bounds = zone.bounds;
    bool isContinent = zone.areaID == 0;

    if (isContinent) {
        int playerZone = findZoneForPlayer(*ctx.zones, ctx.playerRenderPos, ctx.playerZoneId);
        if (playerZone < 0 || !zoneBelongsToContinent(*ctx.zones, playerZone, ctx.currentZoneIdx))
            return;
        float l, r, t, b;
        if (getContinentProjectionBounds(*ctx.zones, ctx.currentZoneIdx, l, r, t, b)) {
            bounds = {l, r, t, b};
        }
    }

    glm::vec2 playerUV = renderPosToMapUV(ctx.playerRenderPos, bounds, isContinent);
    if (playerUV.x < 0.0f || playerUV.x > 1.0f ||
        playerUV.y < 0.0f || playerUV.y > 1.0f) return;

    float px = ctx.imgMin.x + playerUV.x * ctx.displayW;
    float py = ctx.imgMin.y + playerUV.y * ctx.displayH;

    // WoW yaw: 0° = North (+X in WoW = +Y render), increases counter-clockwise.
    // Screen: +X = right, +Y = down. North on map = up = -Y screen.
    // The BLP arrow points up (north) at 0 rotation, so we rotate by -yaw.
    float yawRad = glm::radians(ctx.playerYawDeg);
    float cosA = std::cos(-yawRad);
    float sinA = std::sin(-yawRad);

    ensureTexture();

    if (imguiDS_) {
        constexpr float ARROW_HALF = 16.0f;

        // 4 corners of the unrotated quad (TL, TR, BR, BL)
        float cx[4] = { -ARROW_HALF,  ARROW_HALF,  ARROW_HALF, -ARROW_HALF };
        float cy[4] = { -ARROW_HALF, -ARROW_HALF,  ARROW_HALF,  ARROW_HALF };

        ImVec2 p[4];
        for (int i = 0; i < 4; i++) {
            p[i].x = px + cx[i] * cosA - cy[i] * sinA;
            p[i].y = py + cx[i] * sinA + cy[i] * cosA;
        }

        ctx.drawList->AddImageQuad(
            reinterpret_cast<ImTextureID>(imguiDS_),
            p[0], p[1], p[2], p[3],
            ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
            IM_COL32_WHITE);
    } else {
        // Fallback: red triangle if texture failed to load
        float adx = -std::cos(yawRad);
        float ady = -std::sin(yawRad);
        float apx_ = -ady, apy_ = adx;
        constexpr float TIP  = 9.0f;
        constexpr float TAIL = 4.0f;
        constexpr float FHALF = 5.0f;
        ImVec2 tip(px + adx * TIP,  py + ady * TIP);
        ImVec2 bl (px - adx * TAIL + apx_ * FHALF,  py - ady * TAIL + apy_ * FHALF);
        ImVec2 br (px - adx * TAIL - apx_ * FHALF,  py - ady * TAIL - apy_ * FHALF);
        ctx.drawList->AddTriangleFilled(tip, bl, br, IM_COL32(255, 40, 40, 255));
        ctx.drawList->AddTriangle(tip, bl, br, IM_COL32(0, 0, 0, 200), 1.5f);
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
