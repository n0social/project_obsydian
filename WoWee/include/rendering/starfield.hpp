#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace wowee {
namespace rendering {

class VkContext;

/**
 * Star field renderer
 *
 * Renders a field of stars across the night sky.
 * Stars fade in at dusk and out at dawn.
 */
class StarField {
public:
    StarField();
    ~StarField();

    bool initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout);
    void shutdown();
    void recreatePipelines();

    /**
     * Render the star field
     * @param cmd         Command buffer to record into
     * @param perFrameSet Per-frame descriptor set (set 0, contains camera UBO)
     * @param timeOfDay   Time of day in hours (0-24)
     * @param cloudDensity Optional cloud density from lighting (0-1, reduces star visibility)
     * @param fogDensity   Optional fog density from lighting (reduces star visibility)
     */
    void render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, float timeOfDay,
                float cloudDensity = 0.0f, float fogDensity = 0.0f);

    /**
     * Update star twinkle animation
     */
    void update(float deltaTime);

    /**
     * Enable/disable star rendering
     */
    void setEnabled(bool enabled) { renderingEnabled = enabled; }
    bool isEnabled() const { return renderingEnabled; }

    /**
     * Get number of stars
     */
    int getStarCount() const { return starCount; }

private:
    void generateStars();
    void createStarBuffers();
    void destroyStarBuffers();

    float getStarIntensity(float timeOfDay) const;

    struct Star {
        glm::vec3 position;
        float brightness;    // 0.3 to 1.0
        float twinklePhase;  // 0 to 2π for animation
    };

    std::vector<Star> stars;
    int starCount = 1000;

    VkContext* vkCtx = nullptr;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc = VK_NULL_HANDLE;

    float twinkleTime = 0.0f;
    bool renderingEnabled = true;
};

} // namespace rendering
} // namespace wowee
