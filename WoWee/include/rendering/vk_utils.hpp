#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <limits>
#include <cstdlib>

namespace wowee {
namespace rendering {

class VkContext;

struct AllocatedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
};

struct AllocatedImage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};

// Buffer creation
AllocatedBuffer createBuffer(VmaAllocator allocator, VkDeviceSize size,
    VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

void destroyBuffer(VmaAllocator allocator, AllocatedBuffer& buffer);

// Image creation
AllocatedImage createImage(VkDevice device, VmaAllocator allocator,
    uint32_t width, uint32_t height, VkFormat format,
    VkImageUsageFlags usage, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
    uint32_t mipLevels = 1);

void destroyImage(VkDevice device, VmaAllocator allocator, AllocatedImage& image);

// Image layout transitions
void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

// Staging upload helper — copies CPU data to a GPU-local buffer
AllocatedBuffer uploadBuffer(VkContext& ctx, const void* data, VkDeviceSize size,
    VkBufferUsageFlags usage);

// Check VkResult and log on failure
inline bool vkCheck(VkResult result, [[maybe_unused]] const char* msg) {
    if (result != VK_SUCCESS) {
        // Caller should log the message
        return false;
    }
    return true;
}

// Environment variable utility functions
inline size_t envSizeMBOrDefault(const char* name, size_t defMb) {
#ifdef __ANDROID__
    // Desktop defaults are multi-GB; tablets often have ~3 GB total RAM.
    if (defMb > 384) defMb = 384;
#endif
    const char* v = std::getenv(name);
    if (!v || !*v) return defMb;
    char* end = nullptr;
    unsigned long long mb = std::strtoull(v, &end, 10);
    if (end == v || mb == 0) return defMb;
    if (mb > (std::numeric_limits<size_t>::max() / (1024ull * 1024ull))) return defMb;
    return static_cast<size_t>(mb);
}

inline size_t envSizeOrDefault(const char* name, size_t defValue) {
    const char* v = std::getenv(name);
    if (!v || !*v) return defValue;
    char* end = nullptr;
    unsigned long long n = std::strtoull(v, &end, 10);
    if (end == v || n == 0) return defValue;
    return static_cast<size_t>(n);
}

// Opt-in rendering diagnostics, read once per process. These exist to bisect a
// visual artifact to the subsystem that draws it: turn one off and see whether
// the artifact survives. Any value other than "0" or empty enables the flag.
inline bool envFlagEnabled(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && !(v[0] == '0' && v[1] == '\0');
}

} // namespace rendering
} // namespace wowee
