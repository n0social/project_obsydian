#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace wowee {
namespace rendering {

class VkShaderModule {
public:
    VkShaderModule() = default;
    ~VkShaderModule();

    VkShaderModule(const VkShaderModule&) = delete;
    VkShaderModule& operator=(const VkShaderModule&) = delete;
    VkShaderModule(VkShaderModule&& other) noexcept;
    VkShaderModule& operator=(VkShaderModule&& other) noexcept;

    // Load a SPIR-V file from disk
    [[nodiscard]] bool loadFromFile(VkDevice device, const std::string& path);

    // Load from raw SPIR-V bytes
    bool loadFromMemory(VkDevice device, const uint32_t* code, size_t sizeBytes);

    void destroy();

    ::VkShaderModule getModule() const { return module_; }
    bool isValid() const { return module_ != VK_NULL_HANDLE; }

    // Create a VkPipelineShaderStageCreateInfo for this module
    VkPipelineShaderStageCreateInfo stageInfo(VkShaderStageFlagBits stage,
        const char* entryPoint = "main") const;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    ::VkShaderModule module_ = VK_NULL_HANDLE;
};

// Convenience: load a shader stage directly from a .spv file
VkPipelineShaderStageCreateInfo loadShaderStage(VkDevice device,
    const std::string& path, VkShaderStageFlagBits stage);

} // namespace rendering
} // namespace wowee
