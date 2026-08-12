// corpse_marker_layer.hpp — Death corpse marker on the world map.
#pragma once
#include "rendering/world_map/overlay_renderer.hpp"
#include "rendering/vk_texture.hpp"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <memory>

namespace wowee {
namespace rendering {
class VkContext;
}
namespace pipeline { class AssetManager; }
namespace rendering {
namespace world_map {

class CorpseMarkerLayer : public IOverlayLayer {
public:
    ~CorpseMarkerLayer() override;
    void initialize(VkContext* ctx, pipeline::AssetManager* am);
    void clearTexture();
    void setCorpse(bool hasCorpse, glm::vec3 renderPos) {
        hasCorpse_ = hasCorpse;
        corpseRenderPos_ = renderPos;
    }
    void render(const LayerContext& ctx) override;
private:
    void ensureTexture();

    VkContext* vkCtx_ = nullptr;
    pipeline::AssetManager* assetManager_ = nullptr;
    std::unique_ptr<VkTexture> texture_;
    VkDescriptorSet imguiDS_ = VK_NULL_HANDLE;
    bool loadAttempted_ = false;
    bool hasCorpse_ = false;
    glm::vec3 corpseRenderPos_ = {};
};

} // namespace world_map
} // namespace rendering
} // namespace wowee
