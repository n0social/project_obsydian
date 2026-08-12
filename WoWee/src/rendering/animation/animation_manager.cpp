// Renamed from PlayerAnimator/NpcAnimator dual-map → unified CharacterAnimator registry.
// NpcAnimator methods removed — all characters use CharacterAnimator.
#include "rendering/animation/animation_manager.hpp"

namespace wowee {
namespace rendering {

// ── Character animators ──────────────────────────────────────────────────────

CharacterAnimator& AnimationManager::getOrCreate(uint32_t instanceId) {
    auto it = animators_.find(instanceId);
    if (it != animators_.end()) return *it->second;

    auto [inserted, _] = animators_.emplace(instanceId, std::make_unique<CharacterAnimator>());
    return *inserted->second;
}

CharacterAnimator* AnimationManager::get(uint32_t instanceId) {
    auto it = animators_.find(instanceId);
    return it != animators_.end() ? it->second.get() : nullptr;
}

void AnimationManager::remove(uint32_t instanceId) {
    animators_.erase(instanceId);
}

// ── Update all ───────────────────────────────────────────────────────────────

void AnimationManager::updateAll(float dt) {
    for (auto& [id, animator] : animators_) {
        animator->update(dt);
    }
}

} // namespace rendering
} // namespace wowee
