#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace wowee {
namespace rendering {

// Builder pattern for VkGraphicsPipeline creation.
// Usage:
//   auto pipeline = PipelineBuilder()
//       .setShaders(vertStage, fragStage)
//       .setVertexInput(bindings, attributes)
//       .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
//       .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT)
//       .setDepthTest(true, true, VK_COMPARE_OP_LESS)
//       .setColorBlendAttachment(PipelineBuilder::blendAlpha())
//       .setLayout(pipelineLayout)
//       .setRenderPass(renderPass)
//       .build(device);

class PipelineBuilder {
public:
    PipelineBuilder();

    // Shader stages
    PipelineBuilder& setShaders(VkPipelineShaderStageCreateInfo vert,
        VkPipelineShaderStageCreateInfo frag);

    // Vertex input
    PipelineBuilder& setVertexInput(
        const std::vector<VkVertexInputBindingDescription>& bindings,
        const std::vector<VkVertexInputAttributeDescription>& attributes);

    // No vertex input (fullscreen quad generated in vertex shader)
    PipelineBuilder& setNoVertexInput();

    // Input assembly
    PipelineBuilder& setTopology(VkPrimitiveTopology topology,
        VkBool32 primitiveRestart = VK_FALSE);

    // Rasterization
    PipelineBuilder& setRasterization(VkPolygonMode polygonMode,
        VkCullModeFlags cullMode,
        VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE);

    // Depth test/write
    PipelineBuilder& setDepthTest(bool enable, bool writeEnable,
        VkCompareOp compareOp = VK_COMPARE_OP_LESS);

    // No depth test (default)
    PipelineBuilder& setNoDepthTest();

    // Depth bias (for shadow maps)
    PipelineBuilder& setDepthBias(float constantFactor, float slopeFactor);

    // Color blend attachment
    PipelineBuilder& setColorBlendAttachment(
        VkPipelineColorBlendAttachmentState blendState);

    // No color attachment (depth-only pass)
    PipelineBuilder& setNoColorAttachment();

    // Multisampling
    PipelineBuilder& setMultisample(VkSampleCountFlagBits samples);
    PipelineBuilder& setAlphaToCoverage(bool enable);

    // Pipeline layout
    PipelineBuilder& setLayout(VkPipelineLayout layout);

    // Render pass
    PipelineBuilder& setRenderPass(VkRenderPass renderPass, uint32_t subpass = 0);

    // Dynamic state
    PipelineBuilder& setDynamicStates(const std::vector<VkDynamicState>& states);

    // Pipeline derivatives — hint driver to share compiled state between similar pipelines
    PipelineBuilder& setFlags(VkPipelineCreateFlags flags);
    PipelineBuilder& setBasePipeline(VkPipeline basePipeline);

    // Build the pipeline (pass a VkPipelineCache for faster creation)
    VkPipeline build(VkDevice device, VkPipelineCache cache = VK_NULL_HANDLE) const;

    // Common blend states
    static VkPipelineColorBlendAttachmentState blendDisabled();
    static VkPipelineColorBlendAttachmentState blendAlpha();
    static VkPipelineColorBlendAttachmentState blendPremultiplied();
    static VkPipelineColorBlendAttachmentState blendAdditive();

private:
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages_;
    std::vector<VkVertexInputBindingDescription> vertexBindings_;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes_;
    VkPrimitiveTopology topology_ = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkBool32 primitiveRestart_ = VK_FALSE;
    VkPolygonMode polygonMode_ = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode_ = VK_CULL_MODE_NONE;
    VkFrontFace frontFace_ = VK_FRONT_FACE_CLOCKWISE;
    bool depthTestEnable_ = false;
    bool depthWriteEnable_ = false;
    VkCompareOp depthCompareOp_ = VK_COMPARE_OP_LESS;
    bool depthBiasEnable_ = false;
    float depthBiasConstant_ = 0.0f;
    float depthBiasSlope_ = 0.0f;
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    bool alphaToCoverage_ = false;
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    uint32_t subpass_ = 0;
    std::vector<VkDynamicState> dynamicStates_;
    VkPipelineCreateFlags flags_ = 0;
    VkPipeline basePipelineHandle_ = VK_NULL_HANDLE;
};

// Helper to create a pipeline layout from descriptor set layouts and push constant ranges
VkPipelineLayout createPipelineLayout(VkDevice device,
    const std::vector<VkDescriptorSetLayout>& setLayouts,
    const std::vector<VkPushConstantRange>& pushConstants = {});

// Helper to create a descriptor set layout from bindings
VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings);

} // namespace rendering
} // namespace wowee
