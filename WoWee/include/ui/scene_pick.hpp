#pragma once

// Ray-picking entities under the cursor, shared by the two callers that need it.
//
// The left-click target picker and the right-click world picker each grew their
// own copy of this traversal. They agreed on everything that matters — how a
// unit is ranked against an object, that a hostile is preferred, that a corpse
// must not outrank the living — and had to be edited in lockstep to stay that
// way, which is exactly how one of them quietly diverges. What genuinely
// differs between them is expressed as parameters instead.

#include <cstdint>
#include <functional>
#include <memory>

#include <glm/glm.hpp>

namespace wowee {
namespace game { class GameHandler; class Entity; }
namespace rendering { struct Ray; }

namespace ui {

/// The parts of picking the two callers disagree about.
struct ScenePickParams {
    /// Fallback hit sphere for a unit with no render bounds. The right-click
    /// picker uses a slightly taller, tighter sphere than the target picker.
    float unitHitRadius = 1.8f;
    float unitHeightOffset = 1.0f;
    /// Skip chair-type game objects. Their fallback sphere is wide enough to be
    /// caught while right-drag-rotating the camera near one, which sits the
    /// player down. Left-clicking a chair to target it still works.
    bool skipChairs = false;
};

/// Everything the traversal found, before either caller applies its own rules.
struct ScenePick {
    uint64_t closestGuid = 0;          ///< Nearest hit of any pickable type.
    float    closestT = 1e30f;

    uint64_t hostileUnitGuid = 0;      ///< Nearest *living* hostile unit.
    float    hostileUnitT = 1e30f;

    uint64_t livingUnitGuid = 0;       ///< Nearest living unit or player, by centre.
    float    livingUnitCenterT = 1e30f;
    uint64_t deadUnitGuid = 0;         ///< Nearest corpse, by centre.
    float    deadUnitCenterT = 1e30f;

    uint64_t objectGuid = 0;           ///< Nearest interactable game object, by centre.
    float    objectCenterT = 1e30f;

    /// The living win; a corpse is still selectable, but only when nothing alive
    /// was under the cursor — which is what makes a player standing on a body
    /// clickable, and what looting and skinning still need.
    uint64_t unitGuid() const { return livingUnitGuid != 0 ? livingUnitGuid : deadUnitGuid; }
    float unitCenterT() const {
        return livingUnitGuid != 0 ? livingUnitCenterT : deadUnitCenterT;
    }

    /// Resolve to the one thing under the cursor: a unit unless an object's
    /// centre is clearly in front of it, so a creature is never lost to a
    /// decorative or backing object behind it. A hostile keeps its priority.
    uint64_t resolve() const;
};

/// Called for every entity the ray hits, so a caller can gather what only it
/// cares about (a hooked fishing bobber, quest objective objects) without
/// repeating the traversal.
using ScenePickHit = std::function<void(uint64_t guid,
                                        const std::shared_ptr<game::Entity>& entity,
                                        float hitT, float centerT)>;

ScenePick pickScene(game::GameHandler& gameHandler,
                    const rendering::Ray& ray,
                    const ScenePickParams& params,
                    const ScenePickHit& onHit = {});

} // namespace ui
} // namespace wowee
