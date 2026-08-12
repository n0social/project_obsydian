// IGameState — abstract interface for game state queries used by macro evaluation.
// Allows unit testing with mock state. Phase 4.1 of chat_panel_ref.md.
#pragma once

#include <cstdint>
#include <string>

namespace wowee {
namespace ui {

/**
 * Read-only view of game state for macro conditional evaluation.
 *
 * All entity/aura queries are flattened to simple types so callers
 * don't need to depend on game::Entity, game::Unit, etc.
 */
class IGameState {
public:
    virtual ~IGameState() = default;

    // --- GUIDs ---
    virtual uint64_t getPlayerGuid() const = 0;
    virtual uint64_t getTargetGuid() const = 0;
    virtual uint64_t getFocusGuid() const = 0;
    virtual uint64_t getPetGuid() const = 0;
    virtual uint64_t getMouseoverGuid() const = 0;

    // --- Player state booleans ---
    virtual bool isInCombat() const = 0;
    virtual bool isMounted() const = 0;
    virtual bool isSwimming() const = 0;
    virtual bool isFlying() const = 0;
    virtual bool isCasting() const = 0;
    virtual bool isChanneling() const = 0;
    virtual bool isStealthed() const = 0;
    virtual bool hasPet() const = 0;
    virtual bool isInGroup() const = 0;
    virtual bool isInRaid() const = 0;
    virtual bool isIndoors() const = 0;

    // --- Numeric state ---
    virtual uint8_t getActiveTalentSpec() const = 0;   // 0-based index
    virtual uint32_t getVehicleId() const = 0;
    virtual uint32_t getCurrentCastSpellId() const = 0;

    // --- Spell/aura queries ---
    virtual std::string getSpellName(uint32_t spellId) const = 0;

    /** Check if target (or player if guid==playerGuid) has a buff/debuff by name. */
    virtual bool hasAuraByName(uint64_t targetGuid, const std::string& spellName,
                               bool wantDebuff) const = 0;

    /** Check if player has a form/stance aura (permanent aura, maxDurationMs == -1). */
    virtual bool hasFormAura() const = 0;

    // --- Entity queries (flattened, no Entity* exposure) ---
    virtual bool entityExists(uint64_t guid) const = 0;
    virtual bool entityIsDead(uint64_t guid) const = 0;
    virtual bool entityIsHostile(uint64_t guid) const = 0;
};

} // namespace ui
} // namespace wowee
