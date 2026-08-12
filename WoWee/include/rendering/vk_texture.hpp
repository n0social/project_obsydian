#pragma once

#include "rendering/vk_utils.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <cstdint>

namespace wowee {
namespace rendering {

class VkContext;

class VkTexture {
public:
    VkTexture() = default;
    ~VkTexture();

    VkTexture(const VkTexture&) = delete;
    VkTexture& operator=(const VkTexture&) = delete;
    VkTexture(VkTexture&& other) noexcept;
    VkTexture& operator=(VkTexture&& other) noexcept;

    // Upload RGBA8 pixel data to GPU
    bool upload(VkContext& ctx, const uint8_t* pixels, uint32_t width, uint32_t height,
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM, bool generateMips = true);

    // Upload with pre-existing mip data (array of mip levels)
    bool uploadMips(VkContext& ctx, const uint8_t* const* mipData, const uint32_t* mipSizes,
        uint32_t mipCount, uint32_t width, uint32_t height,
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

    // Create a depth/stencil texture (no upload)
    bool createDepth(VkContext& ctx, uint32_t width, uint32_t height,
        VkFormat format = VK_FORMAT_D32_SFLOAT);

    // Create sampler with specified filtering
    bool createSampler(VkDevice device,
        VkFilter minFilter = VK_FILTER_LINEAR,
        VkFilter magFilter = VK_FILTER_LINEAR,
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        float maxAnisotropy = 16.0f);

    // Overload with separate S/T address modes
    bool createSampler(VkDevice device,
        VkFilter filter,
        VkSamplerAddressMode addressModeU,
        VkSamplerAddressMode addressModeV,
        float maxAnisotropy = 16.0f);

    // Create a comparison sampler (for shadow mapping)
    bool createShadowSampler(VkDevice device);

    void destroy(VkDevice device, VmaAllocator allocator);

    VkImage getImage() const { return image_.image; }
    VkImageView getImageView() const { return image_.imageView; }
    VkSampler getSampler() const { return sampler_; }
    uint32_t getWidth() const { return image_.extent.width; }
    uint32_t getHeight() const { return image_.extent.height; }
    VkFormat getFormat() const { return image_.format; }
    uint32_t getMipLevels() const { return mipLevels_; }
    bool isValid() const { return image_.image != VK_NULL_HANDLE && sampler_ != VK_NULL_HANDLE; }

    // Write descriptor info for binding
    VkDescriptorImageInfo descriptorInfo(VkImageLayout layout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) const;

private:
    void generateMipmaps(VkContext& ctx, VkFormat format, uint32_t width, uint32_t height);
    // Shared sampler finalization: prefer the global cache, fall back to direct creation
    bool finalizeSampler(VkDevice device, const VkSamplerCreateInfo& samplerInfo);

    AllocatedImage image_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    uint32_t mipLevels_ = 1;
    bool ownsSampler_ = true; // false when sampler comes from VkContext cache
};

} // namespace rendering
} // namespace wowee
