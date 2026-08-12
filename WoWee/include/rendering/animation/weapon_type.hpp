#pragma once

#include <cstdint>

namespace wowee {
namespace rendering {

/// Ranged weapon type for animation selection (bow/gun/crossbow/thrown)
enum class RangedWeaponType : uint8_t { NONE = 0, BOW, GUN, CROSSBOW, THROWN };

// ============================================================================
// WeaponLoadout — extracted from AnimationController
//
// Consolidates the 6 weapon boolean fields + inventory type + ranged type
// into a single value type.
// ============================================================================
struct WeaponLoadout {
    uint32_t inventoryType = 0;
    bool is2HLoose  = false;  // Polearm or staff
    bool isFist     = false;  // Fist weapon
    bool isDagger   = false;  // Dagger (uses pierce variants)
    bool hasOffHand = false;  // Has off-hand weapon (dual wield)
    bool hasShield  = false;  // Has shield equipped (for SHIELD_BASH)
    RangedWeaponType rangedType = RangedWeaponType::NONE;
};

} // namespace rendering
} // namespace wowee
