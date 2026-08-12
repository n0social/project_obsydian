#pragma once

#include <cstdint>
#include <vector>
#include "rendering/animation/anim_capability_set.hpp"

namespace wowee {
namespace rendering {

class Renderer;

// ============================================================================
// AnimCapabilityProbe
//
// Scans a model's animation sequences once and caches the results in an
// AnimCapabilitySet. All animation selection then uses the probed set
// instead of per-frame hasAnimation() calls.
// ============================================================================
class AnimCapabilityProbe {
public:
    AnimCapabilityProbe() = default;

    /// Probe all animation capabilities for the given character instance.
    /// Returns a fully-populated AnimCapabilitySet.
    static AnimCapabilitySet probe(Renderer* renderer, uint32_t instanceId);

    /// Probe mount animation capabilities (separate model).
    static AnimCapabilitySet probeMountModel(Renderer* renderer, uint32_t mountInstanceId);

private:
    /// Pick the first available animation from candidates for the given instance.
    /// Returns 0 if none available.
    static uint32_t pickFirst(Renderer* renderer, uint32_t instanceId,
                              const uint32_t* candidates, size_t count);
};

} // namespace rendering
} // namespace wowee
