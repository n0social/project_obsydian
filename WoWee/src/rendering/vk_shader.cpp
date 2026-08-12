#include "rendering/vk_shader.hpp"
#include "core/logger.hpp"
#include <fstream>

namespace wowee {
namespace rendering {

VkShaderModule::~VkShaderModule() {
    destroy();
}

VkShaderModule::VkShaderModule(VkShaderModule&& other) noexcept
    : device_(other.device_), module_(other.module_) {
    other.module_ = VK_NULL_HANDLE;
}

VkShaderModule& VkShaderModule::operator=(VkShaderModule&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        module_ = other.module_;
        other.module_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VkShaderModule::loadFromFile(VkDevice device, const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open shader file: ", path);
        return false;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    // SPIR-V is a stream of 32-bit words — file size must be a multiple of 4
    if (fileSize == 0 || fileSize % 4 != 0) {
        LOG_ERROR("Invalid SPIR-V file size (", fileSize, "): ", path);
        return false;
    }

    std::vector<uint32_t> code(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), fileSize);
    file.close();

    return loadFromMemory(device, code.data(), fileSize);
}

bool VkShaderModule::loadFromMemory(VkDevice device, const uint32_t* code, size_t sizeBytes) {
    destroy();
    device_ = device;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = sizeBytes;
    createInfo.pCode = code;

    if (vkCreateShaderModule(device_, &createInfo, nullptr, &module_) != VK_SUCCESS) {
        LOG_ERROR("Failed to create shader module");
        return false;
    }

    return true;
}

void VkShaderModule::destroy() {
    if (module_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, module_, nullptr);
        module_ = VK_NULL_HANDLE;
    }
}

VkPipelineShaderStageCreateInfo VkShaderModule::stageInfo(
    VkShaderStageFlagBits stage, const char* entryPoint) const
{
    VkPipelineShaderStageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage = stage;
    info.module = module_;
    info.pName = entryPoint;
    return info;
}

VkPipelineShaderStageCreateInfo loadShaderStage(VkDevice device,
    const std::string& path, VkShaderStageFlagBits stage)
{
    // This creates a temporary module — caller must keep it alive while pipeline is created.
    // Prefer using VkShaderModule directly for proper lifetime management.
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

    std::ifstream file(path, std::ios::ate | std::ios::binary);
    std::vector<uint32_t> code;
    if (file.is_open()) {
        size_t fileSize = static_cast<size_t>(file.tellg());
        code.resize(fileSize / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(code.data()), fileSize);
        moduleInfo.codeSize = fileSize;
        moduleInfo.pCode = code.data();
    }

    ::VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &moduleInfo, nullptr, &module);

    VkPipelineShaderStageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage = stage;
    info.module = module;
    info.pName = "main";
    return info;
}

} // namespace rendering
} // namespace wowee
