// GameStateAdapter — wraps GameHandler + Renderer to implement IGameState.
// Phase 4.2 of chat_panel_ref.md.
#pragma once

#include "ui/chat/i_game_state.hpp"

namespace wowee {
namespace game { class GameHandler; }
namespace rendering { class Renderer; }

namespace ui {

/**
 * Concrete adapter from GameHandler + Renderer → IGameState.
 * Flatten complex entity/aura queries into the simple IGameState interface.
 */
class GameStateAdapter : public IGameState {
public:
    GameStateAdapter(game::GameHandler& gameHandler, rendering::Renderer* renderer);

    // --- GUIDs ---
    uint64_t getPlayerGuid() const override;
    uint64_t getTargetGuid() const override;
    uint64_t getFocusGuid() const override;
    uint64_t getPetGuid() const override;
    uint64_t getMouseoverGuid() const override;

    // --- Player state ---
    bool isInCombat() const override;
    bool isMounted() const override;
    bool isSwimming() const override;
    bool isFlying() const override;
    bool isCasting() const override;
    bool isChanneling() const override;
    bool isStealthed() const override;
    bool hasPet() const override;
    bool isInGroup() const override;
    bool isInRaid() const override;
    bool isIndoors() const override;

    // --- Numeric ---
    uint8_t getActiveTalentSpec() const override;
    uint32_t getVehicleId() const override;
    uint32_t getCurrentCastSpellId() const override;

    // --- Spell/aura ---
    std::string getSpellName(uint32_t spellId) const override;
    bool hasAuraByName(uint64_t targetGuid, const std::string& spellName,
                       bool wantDebuff) const override;
    bool hasFormAura() const override;

    // --- Entity queries ---
    bool entityExists(uint64_t guid) const override;
    bool entityIsDead(uint64_t guid) const override;
    bool entityIsHostile(uint64_t guid) const override;

private:
    game::GameHandler& gameHandler_;
    rendering::Renderer* renderer_;
};

} // namespace ui
} // namespace wowee
