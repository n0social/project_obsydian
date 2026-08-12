#include "rendering/starfield.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/vk_utils.hpp"
#include "core/logger.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <random>
#include <vector>

namespace wowee {
namespace rendering {

// Day/night cycle thresholds (hours, 24h clock) for star visibility.
// Stars fade in over 2 hours at dusk, stay full during night, fade out at dawn.
static constexpr float kDuskStart = 18.0f;  // stars begin fading in
static constexpr float kNightStart = 20.0f; // full star visibility
static constexpr float kDawnStart = 4.0f;   // stars begin fading out
static constexpr float kDawnEnd = 6.0f;     // stars fully gone
static constexpr float kFadeDuration = 2.0f;

StarField::StarField() = default;

StarField::~StarField() {
    shutdown();
}

bool StarField::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout) {
    LOG_INFO("Initializing star field");

    vkCtx = ctx;
    VkDevice device = vkCtx->getDevice();

    // Load SPIR-V shaders
    VkShaderModule vertModule;
    if (!vertModule.loadFromFile(device, "assets/shaders/starfield.vert.spv")) {
        LOG_ERROR("Failed to load starfield vertex shader");
        return false;
    }

    VkShaderModule fragModule;
    if (!fragModule.loadFromFile(device, "assets/shaders/starfield.frag.spv")) {
        LOG_ERROR("Failed to load starfield fragment shader");
        return false;
    }

    VkPipelineShaderStageCreateInfo vertStage = vertModule.stageInfo(VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo fragStage = fragModule.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT);

    // Push constants: float time + float intensity = 8 bytes
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 2;  // time, intensity

    // Pipeline layout: set 0 = per-frame UBO, push constants
    pipelineLayout = createPipelineLayout(device, {perFrameLayout}, {pushRange});
    if (pipelineLayout == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create starfield pipeline layout");
        return false;
    }

    // Vertex input: binding 0, stride = 5 * sizeof(float)
    //   location 0: vec3  pos          (offset  0)
    //   location 1: float brightness   (offset 12)
    //   location 2: float twinklePhase (offset 16)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 5 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
    posAttr.offset = 0;

    VkVertexInputAttributeDescription brightnessAttr{};
    brightnessAttr.location = 1;
    brightnessAttr.binding = 0;
    brightnessAttr.format = VK_FORMAT_R32_SFLOAT;
    brightnessAttr.offset = 3 * sizeof(float);

    VkVertexInputAttributeDescription twinkleAttr{};
    twinkleAttr.location = 2;
    twinkleAttr.binding = 0;
    twinkleAttr.format = VK_FORMAT_R32_SFLOAT;
    twinkleAttr.offset = 4 * sizeof(float);

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({binding}, {posAttr, brightnessAttr, twinkleAttr})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)  // depth test, no write (stars behind sky)
        .setColorBlendAttachment(PipelineBuilder::blendAdditive())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .build(device, vkCtx->getPipelineCache());

    vertModule.destroy();
    fragModule.destroy();

    if (pipeline == VK_NULL_HANDLE) {
        LOG_ERROR("Failed to create starfield pipeline");
        return false;
    }

    // Generate star positions and upload to GPU
    generateStars();
    createStarBuffers();

    LOG_INFO("Star field initialized: ", starCount, " stars");
    return true;
}

void StarField::recreatePipelines() {
    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();

    if (pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }

    VkShaderModule vertModule;
    if (!vertModule.loadFromFile(device, "assets/shaders/starfield.vert.spv")) {
        LOG_ERROR("StarField::recreatePipelines: failed to load vertex shader");
        return;
    }
    VkShaderModule fragModule;
    if (!fragModule.loadFromFile(device, "assets/shaders/starfield.frag.spv")) {
        LOG_ERROR("StarField::recreatePipelines: failed to load fragment shader");
        vertModule.destroy();
        return;
    }

    VkPipelineShaderStageCreateInfo vertStage = vertModule.stageInfo(VK_SHADER_STAGE_VERTEX_BIT);
    VkPipelineShaderStageCreateInfo fragStage = fragModule.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT);

    // Vertex input (same as initialize)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 5 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
    posAttr.offset = 0;

    VkVertexInputAttributeDescription brightnessAttr{};
    brightnessAttr.location = 1;
    brightnessAttr.binding = 0;
    brightnessAttr.format = VK_FORMAT_R32_SFLOAT;
    brightnessAttr.offset = 3 * sizeof(float);

    VkVertexInputAttributeDescription twinkleAttr{};
    twinkleAttr.location = 2;
    twinkleAttr.binding = 0;
    twinkleAttr.format = VK_FORMAT_R32_SFLOAT;
    twinkleAttr.offset = 4 * sizeof(float);

    pipeline = PipelineBuilder()
        .setShaders(vertStage, fragStage)
        .setVertexInput({binding}, {posAttr, brightnessAttr, twinkleAttr})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setColorBlendAttachment(PipelineBuilder::blendAdditive())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(vkCtx->getImGuiRenderPass())
        .build(device, vkCtx->getPipelineCache());

    vertModule.destroy();
    fragModule.destroy();

    if (pipeline == VK_NULL_HANDLE) {
        LOG_ERROR("StarField::recreatePipelines: failed to create pipeline");
    }
}

void StarField::shutdown() {
    destroyStarBuffers();

    if (vkCtx) {
        VkDevice device = vkCtx->getDevice();
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
    }

    vkCtx = nullptr;
    stars.clear();
}

void StarField::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet,
                       float timeOfDay, float cloudDensity, float fogDensity) {
    if (!renderingEnabled || pipeline == VK_NULL_HANDLE || vertexBuffer == VK_NULL_HANDLE
        || stars.empty()) {
        return;
    }

    // Compute intensity from time of day then attenuate for clouds/fog
    float intensity = getStarIntensity(timeOfDay);
    intensity *= (1.0f - glm::clamp(cloudDensity * 0.7f, 0.0f, 1.0f));
    intensity *= (1.0f - glm::clamp(fogDensity * 0.3f, 0.0f, 1.0f));

    if (intensity <= 0.01f) {
        return;
    }

    // Push constants: time and intensity
    struct StarPushConstants {
        float time;
        float intensity;
    };
    StarPushConstants push{twinkleTime, intensity};

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind per-frame descriptor set (set 0 — camera UBO with view/projection)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
        0, 1, &perFrameSet, 0, nullptr);

    vkCmdPushConstants(cmd, pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(push), &push);

    // Bind vertex buffer
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);

    // Draw all stars as individual points
    vkCmdDraw(cmd, static_cast<uint32_t>(starCount), 1, 0, 0);
}

void StarField::update(float deltaTime) {
    twinkleTime += deltaTime;
}

void StarField::generateStars() {
    stars.clear();
    stars.reserve(starCount);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> phiDist(0.0f, M_PI / 2.0f);  // 0–90° (upper hemisphere)
    std::uniform_real_distribution<float> thetaDist(0.0f, 2.0f * M_PI);  // 0–360°
    std::uniform_real_distribution<float> brightnessDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> twinkleDist(0.0f, 2.0f * M_PI);

    const float radius = 900.0f;  // Slightly larger than skybox

    for (int i = 0; i < starCount; i++) {
        Star star;

        float phi   = phiDist(gen);    // Elevation angle
        float theta = thetaDist(gen);  // Azimuth angle

        float x = radius * std::sin(phi) * std::cos(theta);
        float y = radius * std::sin(phi) * std::sin(theta);
        float z = radius * std::cos(phi);

        star.position     = glm::vec3(x, y, z);
        star.brightness   = brightnessDist(gen);
        star.twinklePhase = twinkleDist(gen);

        stars.push_back(star);
    }

    LOG_DEBUG("Generated ", stars.size(), " stars");
}

void StarField::createStarBuffers() {
    // Interleaved vertex data: pos.x, pos.y, pos.z, brightness, twinklePhase
    std::vector<float> vertexData;
    vertexData.reserve(stars.size() * 5);

    for (const auto& star : stars) {
        vertexData.push_back(star.position.x);
        vertexData.push_back(star.position.y);
        vertexData.push_back(star.position.z);
        vertexData.push_back(star.brightness);
        vertexData.push_back(star.twinklePhase);
    }

    VkDeviceSize bufferSize = vertexData.size() * sizeof(float);

    // Upload via staging buffer to GPU-local memory
    AllocatedBuffer gpuBuf = uploadBuffer(*vkCtx, vertexData.data(), bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    vertexBuffer = gpuBuf.buffer;
    vertexAlloc  = gpuBuf.allocation;
}

void StarField::destroyStarBuffers() {
    if (vkCtx && vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vkCtx->getAllocator(), vertexBuffer, vertexAlloc);
        vertexBuffer = VK_NULL_HANDLE;
        vertexAlloc  = VK_NULL_HANDLE;
    }
}

float StarField::getStarIntensity(float timeOfDay) const {
    // Full night
    if (timeOfDay >= kNightStart || timeOfDay < kDawnStart) {
        return 1.0f;
    }
    // Fade in at dusk
    if (timeOfDay >= kDuskStart) {
        return (timeOfDay - kDuskStart) / kFadeDuration;
    }
    // Fade out at dawn
    if (timeOfDay < kDawnEnd) {
        return 1.0f - (timeOfDay - kDawnStart) / kFadeDuration;
    }
    // Daytime: no stars
    return 0.0f;
}

} // namespace rendering
} // namespace wowee
