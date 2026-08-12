#include "rendering/mount_dust.hpp"
#include "rendering/camera.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/vk_utils.hpp"
#include "core/logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <cmath>
#include <cstring>

namespace wowee {
namespace rendering {

static std::mt19937& rng() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

static float randFloat(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng());
}

MountDust::MountDust() = default;
MountDust::~MountDust() { shutdown(); }

bool MountDust::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout) {
    LOG_INFO("Initializing mount dust effects");

    vkCtx = ctx;
    VkDevice device = vkCtx->getDevice();

    // Load SPIR-V shaders
    VkShaderModule vertModule;
    if (!vertModule.loadFromFile(device, "assets/shaders/mount_dust.vert.spv")) {
        LOG_ERROR("Failed to load mount_dust vertex shader");
        return false;
    }
    VkShaderModule fragModule;
    if (!fragModule.loadFromFile(device, "assets/shaders/mount_dust.frag.spv")) {
        LOG_ERROR("Failed to load mount_dust fragment shader");
        return false;
    }

    VkPipelineShaderStageCreateInfo vertStage = vertModule.stageInfo(VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo fragStage = fragModule.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT);

    // No push constants needed for mount dust (all data is per-vertex)
    pipelineLayout = createPipelineLayout(device, {perFrameLayout}, {});
    if (pipelineLayout == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create mount dust pipeline layout");
        return false;
    }

    // Vertex input: pos(vec3) + size(float) + alpha(float) = 5 floats, stride = 20 bytes
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 5 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrs(3);
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32_SFLOAT;
    attrs[1].offset = 3 * sizeof(float);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32_SFLOAT;
    attrs[2].offset = 4 * sizeof(float);

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({binding}, attrs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS)
        .setColorBlendAttachment(PipelineBuilder::blendAlpha())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device, vkCtx->getPipelineCache());

    vertModule.destroy();
    fragModule.destroy();

    if (pipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create mount dust pipeline");
        return false;
    }

    // Create dynamic mapped vertex buffer
    dynamicVBSize = MAX_DUST_PARTICLES * 5 * sizeof(float);
    AllocatedBuffer buf = createBuffer(vkCtx->getAllocator(), dynamicVBSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    dynamicVB = buf.buffer;
    dynamicVBAlloc = buf.allocation;
    dynamicVBAllocInfo = buf.info;

    if (dynamicVB == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create mount dust dynamic vertex buffer");
        return false;
    }

    particles.reserve(MAX_DUST_PARTICLES);
    vertexData.reserve(MAX_DUST_PARTICLES * 5);

    LOG_INFO("Mount dust effects initialized");
    return true;
}

void MountDust::shutdown() {
    if (vkCtx) {
        VkDevice device = vkCtx->getDevice();
        VmaAllocator allocator = vkCtx->getAllocator();

        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
        if (dynamicVB != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, dynamicVB, dynamicVBAlloc);
            dynamicVB = VK_NULL_HANDLE;
            dynamicVBAlloc = VK_NULL_HANDLE;
        }
    }

    vkCtx = nullptr;
    particles.clear();
}

void MountDust::recreatePipelines() {
    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();

    // Destroy old pipeline (NOT layout)
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }

    VkShaderModule vertModule;
    VkShaderModule fragModule;
    if (!vertModule.loadFromFile(device, "assets/shaders/mount_dust.vert.spv") ||
        !fragModule.loadFromFile(device, "assets/shaders/mount_dust.frag.spv")) {
        LOG_ERROR("MountDust::recreatePipelines: failed to load shader modules");
        return;
    }

    VkPipelineShaderStageCreateInfo vertStage = vertModule.stageInfo(VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo fragStage = fragModule.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT);

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 5 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrs(3);
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32_SFLOAT;
    attrs[1].offset = 3 * sizeof(float);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32_SFLOAT;
    attrs[2].offset = 4 * sizeof(float);

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({binding}, attrs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS)
        .setColorBlendAttachment(PipelineBuilder::blendAlpha())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .setDynamicStates(dynamicStates)
        .build(device, vkCtx->getPipelineCache());

    vertModule.destroy();
    fragModule.destroy();
}

void MountDust::spawnDust(const glm::vec3& position, const glm::vec3& velocity, bool isMoving) {
    if (!isMoving) {
        spawnAccum = 0.0f;
        return;
    }

    // Spawn rate based on speed
    float speed = glm::length(velocity);
    if (speed < 0.1f) return;

    // Spawn dust particles at a rate proportional to speed
    float spawnRate = speed * 8.0f;  // More dust at higher speeds
    spawnAccum += spawnRate * 0.016f;  // Assume ~60 FPS

    while (spawnAccum >= 1.0f && particles.size() < MAX_DUST_PARTICLES) {
        spawnAccum -= 1.0f;

        Particle p;
        // Spawn slightly behind and to the sides of the mount, at ground level
        p.position = position + glm::vec3(
            randFloat(-0.3f, 0.3f),
            randFloat(-0.3f, 0.3f),
            0.2f  // Spawn slightly above ground to ensure visibility
        );

        // Dust rises up and spreads outward
        // Only use horizontal velocity for drift, ignore vertical component
        glm::vec3 horizontalVel = glm::vec3(velocity.x, velocity.y, 0.0f);
        p.velocity = glm::vec3(
            randFloat(-0.3f, 0.3f),  // Random horizontal spread
            randFloat(-0.3f, 0.3f),
            randFloat(1.2f, 2.5f)    // Strong upward movement for visibility
        ) - horizontalVel * 0.15f;  // Drift backward slightly based on horizontal movement only

        p.lifetime = 0.0f;
        p.maxLifetime = randFloat(0.4f, 0.8f);
        p.size = randFloat(8.0f, 16.0f);
        p.alpha = 1.0f;

        particles.push_back(p);
    }
}

void MountDust::update(float deltaTime) {
    // Update existing particles
    for (auto it = particles.begin(); it != particles.end(); ) {
        it->lifetime += deltaTime;

        if (it->lifetime >= it->maxLifetime) {
            it = particles.erase(it);
            continue;
        }

        // Update position
        it->position += it->velocity * deltaTime;

        // Slow down velocity (friction)
        it->velocity *= 0.96f;

        // Fade out
        float t = it->lifetime / it->maxLifetime;
        it->alpha = 1.0f - t;

        // Grow slightly as they fade
        it->size += deltaTime * 12.0f;

        ++it;
    }
}

void MountDust::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet) {
    if (particles.empty() || pipeline == VK_NULL_HANDLE) return;

    // Build vertex data
    vertexData.clear();
    for (const auto& p : particles) {
        vertexData.push_back(p.position.x);
        vertexData.push_back(p.position.y);
        vertexData.push_back(p.position.z);
        vertexData.push_back(p.size);
        vertexData.push_back(p.alpha);
    }

    // Upload to mapped buffer
    VkDeviceSize uploadSize = vertexData.size() * sizeof(float);
    if (uploadSize > 0 && dynamicVBAllocInfo.pMappedData) {
        std::memcpy(dynamicVBAllocInfo.pMappedData, vertexData.data(), uploadSize);
    }

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind per-frame descriptor set (set 0 - camera UBO)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
        0, 1, &perFrameSet, 0, nullptr);

    // Bind vertex buffer
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &dynamicVB, &offset);

    // Draw particles as points
    vkCmdDraw(cmd, static_cast<uint32_t>(particles.size()), 1, 0, 0);
}

} // namespace rendering
} // namespace wowee
