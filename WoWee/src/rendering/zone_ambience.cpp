#include "rendering/lighting_manager.hpp"

#include <algorithm>

namespace wowee::rendering {

namespace {
constexpr uint32_t kDuskwoodZoneId = 10;
constexpr float kDuskwoodVisualTimeHours = 22.0f;
}

float resolveZoneVisualTimeHours(uint32_t zoneId, bool isIndoors, float worldTimeHours) {
    return (zoneId == kDuskwoodZoneId && !isIndoors)
        ? kDuskwoodVisualTimeHours
        : worldTimeHours;
}

void applyZoneAmbienceOverride(uint32_t zoneId, LightingParams& params) {
    if (zoneId != kDuskwoodZoneId) {
        return;
    }

    // Duskwood is canonically trapped beneath a dark, fog-heavy sky. Apply this
    // after the normal DBC/weather blend so clear weather and midday cannot wash
    // the atmosphere out, while retaining darker values supplied by the client.
    params.ambientColor = glm::min(params.ambientColor, glm::vec3(0.20f, 0.22f, 0.26f));
    params.diffuseColor = glm::min(params.diffuseColor, glm::vec3(0.26f, 0.28f, 0.32f));

    params.fogColor = glm::vec3(0.075f, 0.095f, 0.11f);
    params.fogStart = std::min(params.fogStart, 35.0f);
    params.fogEnd = std::min(params.fogEnd, 525.0f);
    params.fogDensity = std::max(params.fogDensity, 0.006f);

    params.skyTopColor = glm::vec3(0.025f, 0.035f, 0.055f);
    params.skyMiddleColor = glm::vec3(0.055f, 0.070f, 0.085f);
    params.skyBand1Color = glm::vec3(0.075f, 0.090f, 0.105f);
    params.skyBand2Color = glm::vec3(0.095f, 0.105f, 0.115f);
    params.cloudDensity = std::max(params.cloudDensity, 0.88f);
    params.horizonGlow = std::min(params.horizonGlow, 0.08f);
}

} // namespace wowee::rendering
