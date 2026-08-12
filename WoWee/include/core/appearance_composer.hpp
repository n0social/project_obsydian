#pragma once

#include "game/character.hpp"
#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace wowee {

namespace rendering { class Renderer; }
namespace pipeline { class AssetManager; class DBCLayout; struct M2Model; }
namespace game { class GameHandler; class Inventory; }

namespace core {

class EntitySpawner;

// Default (bare) geoset IDs per equipment group.
// Each group's base is groupNumber * 100; variant 01 is typically bare/default.
constexpr uint16_t kGeosetDefaultConnector = 101;   // Group  1: default hair connector
constexpr uint16_t kGeosetBareForearms     = 401;   // Group  4: no gloves
constexpr uint16_t kGeosetBareShins        = 501;   // Group  5: no boots
constexpr uint16_t kGeosetDefaultEars      = 702;   // Group  7: ears
constexpr uint16_t kGeosetBareSleeves      = 801;   // Group  8: no chest armor sleeves
constexpr uint16_t kGeosetDefaultKneepads  = 902;   // Group  9: kneepads
constexpr uint16_t kGeosetDefaultTabard    = 1201;  // Group 12: tabard base
constexpr uint16_t kGeosetBarePants        = 1301;  // Group 13: no leggings
constexpr uint16_t kGeosetNoCape           = 1501;  // Group 15: no cape
constexpr uint16_t kGeosetWithCape         = 1502;  // Group 15: with cape
constexpr uint16_t kGeosetBareFeet         = 2002;  // Group 20: bare feet

/// Resolved texture paths from CharSections.dbc for player character compositing.
struct PlayerTextureInfo {
    std::string bodySkinPath;
    std::string faceLowerPath;
    std::string faceUpperPath;
    std::string hairTexturePath;
    std::vector<std::string> underwearPaths;
};

/// Handles player character visual appearance: skin compositing, geoset selection,
/// texture path lookups, and equipment weapon rendering.
class AppearanceComposer {
public:
    AppearanceComposer(rendering::Renderer* renderer,
                       pipeline::AssetManager* assetManager,
                       game::GameHandler* gameHandler,
                       pipeline::DBCLayout* dbcLayout,
                       EntitySpawner* entitySpawner);

    // Player model path resolution
    std::string getPlayerModelPath(game::Race race, game::Gender gender) const;

    // Resolve texture paths from CharSections.dbc and fill model texture slots.
    // Call BEFORE charRenderer->loadModel().
    PlayerTextureInfo resolvePlayerTextures(pipeline::M2Model& model,
                                           game::Race race, game::Gender gender,
                                           uint32_t appearanceBytes);

    // Apply composited textures to loaded model instance.
    // Call AFTER charRenderer->loadModel(). Saves skin state for re-compositing.
    void compositePlayerSkin(uint32_t modelSlotId, const PlayerTextureInfo& texInfo);

    // Build default active geosets for player character
    std::unordered_set<uint16_t> buildDefaultPlayerGeosets(uint8_t raceId, uint8_t sexId,
                                                           uint8_t hairStyleId, uint8_t facialId);

    // Equipment weapon loading (reads inventory, attaches weapon M2 models)
    void loadEquippedWeapons();

    // Weapon sheathe state
    void setWeaponsSheathed(bool sheathed) { weaponsSheathed_ = sheathed; }
    bool isWeaponsSheathed() const { return weaponsSheathed_; }
    void toggleWeaponsSheathed() { weaponsSheathed_ = !weaponsSheathed_; }

    // Ranged weapon swap: temporarily show ranged weapon in right hand
    void showRangedWeapon(bool show);
    bool isShowingRanged() const { return showingRanged_; }

    // Mining casts temporarily replace the held main-hand model with a pickaxe.
    void showMiningPick(bool show);

    // Fishing casts temporarily replace the held main-hand model with a pole
    // found in the player's equipped slots or bags.
    void showFishingPole(bool show);

    // Saved skin state accessors (used by game_screen.cpp for equipment re-compositing)
    const std::string& getBodySkinPath() const { return bodySkinPath_; }
    const std::vector<std::string>& getUnderwearPaths() const { return underwearPaths_; }
    uint32_t getSkinTextureSlotIndex() const { return skinTextureSlotIndex_; }
    uint32_t getCloakTextureSlotIndex() const { return cloakTextureSlotIndex_; }

private:
    bool loadWeaponM2(const std::string& m2Path, pipeline::M2Model& outModel);

    /// Attach the equipped head item's model. Other players resolve this through
    /// EntitySpawner; the local character had no equivalent at all.
    void loadEquippedHelm(game::Inventory& inventory);

    // Attach the enchant visual (sharpening-stone glint, weapon glow) of the item in
    // the given equipment slot to the weapon already attached at attachmentId.
    void applyEnchantVisuals(uint32_t charInstanceId, int equipSlotIndex, uint32_t attachmentId);

    rendering::Renderer* renderer_;
    pipeline::AssetManager* assetManager_;
    game::GameHandler* gameHandler_;
    pipeline::DBCLayout* dbcLayout_;
    EntitySpawner* entitySpawner_;

    // Saved at spawn for skin re-compositing on equipment changes
    std::string bodySkinPath_;
    std::vector<std::string> underwearPaths_;
    uint32_t skinTextureSlotIndex_ = 0;
    uint32_t cloakTextureSlotIndex_ = 0;

    bool weaponsSheathed_ = false;
    bool showingRanged_ = false;
    bool showingMiningPick_ = false;
    uint32_t miningPickInstanceId_ = 0;
    bool showingFishingPole_ = false;
};

} // namespace core
} // namespace wowee
