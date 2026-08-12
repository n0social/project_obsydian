#include "ui/ui_raid_icons.hpp"

#include "core/application.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "rendering/vk_context.hpp"

#include <array>
#include <string>

namespace wowee {
namespace ui {

VkDescriptorSet getRaidTargetIcon(uint8_t icon, pipeline::AssetManager* assetManager) {
    if (icon >= kRaidTargetIconCount || !assetManager) return VK_NULL_HANDLE;

    static std::array<VkDescriptorSet, kRaidTargetIconCount> cache{};
    if (cache[icon]) return cache[icon];

    // Blizzard numbers the files 1-8 in the same order as the icon indices.
    const std::string path = "Interface\\TargetingFrame\\UI-RaidTargetingIcon_" +
                             std::to_string(icon + 1) + ".blp";
    auto blpData = assetManager->readFile(path);
    if (blpData.empty()) return VK_NULL_HANDLE;

    auto image = pipeline::BLPLoader::load(blpData);
    if (!image.isValid()) return VK_NULL_HANDLE;

    auto* window = core::Application::getInstance().getWindow();
    auto* vkCtx = window ? window->getVkContext() : nullptr;
    if (!vkCtx) return VK_NULL_HANDLE;

    // Only a successful upload is cached: a transient failure (descriptor pool
    // pressure) should be retried rather than blacklisting the icon for good.
    VkDescriptorSet ds = vkCtx->uploadImGuiTexture(image.data.data(), image.width, image.height);
    if (ds) cache[icon] = ds;
    return ds;
}

} // namespace ui
} // namespace wowee
