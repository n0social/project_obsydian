#include "core/entity_spawner.hpp"
#include "core/helm_visual.hpp"

// M2 attachment 11 is the helm; 0 is the shield mount.
namespace { constexpr uint32_t kAttachHelm = 11; }
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include "rendering/renderer.hpp"
#include "rendering/animation_controller.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/wmo_renderer.hpp"
#include "rendering/m2_renderer.hpp"
#include "audio/npc_voice_manager.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/wmo_loader.hpp"
#include "rendering/animation/animation_ids.hpp"
#include "rendering/animation/emote_registry.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_layout.hpp"
#include "game/game_handler.hpp"
#include "game/game_services.hpp"
#include "game/transport_manager.hpp"

#include <cmath>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstring>

namespace wowee {
namespace core {

namespace {
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
} // namespace

// --- Constructor / Destructor ---

EntitySpawner::EntitySpawner(rendering::Renderer* renderer,
                             pipeline::AssetManager* assetManager,
                             game::GameHandler* gameHandler,
                             pipeline::DBCLayout* dbcLayout,
                             game::GameServices* gameServices)
    : renderer_(renderer)
    , assetManager_(assetManager)
    , gameHandler_(gameHandler)
    , dbcLayout_(dbcLayout)
    , gameServices_(gameServices)
{
}

EntitySpawner::~EntitySpawner() = default;

// --- Lifecycle ---

void EntitySpawner::initialize() {
    buildCharSectionsCache();
    buildCreatureDisplayLookups();
    buildGameObjectDisplayLookups();
}

void EntitySpawner::update() {
    processPlayerSpawnQueue();
    processCreatureSpawnQueue();
    processAsyncNpcCompositeResults();
    processDeferredEquipmentQueue();
    processGameObjectSpawnQueue();
    processPendingTransportRegistrations();
    processPendingTransportDoodads();
    processPendingMount();
    processPendingRemotePlayerMounts();
    syncCreatureStealthVisuals();
}

void EntitySpawner::syncCreatureStealthVisuals() {
    if (!renderer_ || !gameHandler_) return;
    auto* characterRenderer = renderer_->getCharacterRenderer();
    if (!characterRenderer) return;

    // Stealth flags flip rarely, but this scan visits every spawned creature.
    // Doing that each frame with the mutex-locked getEntity() plus a
    // dynamic_pointer_cast per creature was a measurable main-thread cost in
    // crowded areas — a few sweeps per second is visually indistinguishable.
    if (++stealthSyncFrameCounter_ % 15 != 0) return;

    // EntitySpawner::update() runs on the main thread, so the unlocked
    // getEntities() reference is safe and avoids a mutex acquire per creature.
    const auto& entities = gameHandler_->getEntityManager().getEntities();

    // Undetected stealth is culled by the server. Units that are sent with the
    // CREEP visibility flag use the translucent detected-stealth presentation.
    constexpr float kDetectedStealthOpacity = 0.35f;
    for (const auto& [guid, instanceId] : creatureInstances_) {
        auto entIt = entities.find(guid);
        if (entIt == entities.end() || !entIt->second) continue;
        const game::Entity* entity = entIt->second.get();
        if (entity->getType() != game::ObjectType::UNIT &&
            entity->getType() != game::ObjectType::PLAYER) continue;
        const auto* unit = static_cast<const game::Unit*>(entity);

        const bool stealthed = unit->hasCreepVisibility();
        auto [it, inserted] = creatureWasStealthed_.try_emplace(guid, stealthed);
        if (!inserted && it->second == stealthed) continue;

        it->second = stealthed;
        characterRenderer->setInstanceOpacity(
            instanceId, stealthed ? kDetectedStealthOpacity : 1.0f);
    }
}

void EntitySpawner::shutdown() {
    clearAllQueues();
    // Clear all instances
    creatureInstances_.clear();
    creatureModelIds_.clear();
    creatureDisplayIds_.clear();
    requestedCreatureDisplayIds_.clear();
    creatureRenderPosCache_.clear();
    creatureWasMoving_.clear();
    creatureWasSwimming_.clear();
    creatureWasFlying_.clear();
    creatureWasWalking_.clear();
    creatureSwimmingState_.clear();
    creatureWalkingState_.clear();
    creatureFlyingState_.clear();
    creatureActiveEmotes_.clear();
    creatureWasStealthed_.clear();
    creatureWeaponsAttached_.clear();
    creatureWeaponAttachAttempts_.clear();
    playerInstances_.clear();
    onlinePlayerAppearance_.clear();
    remotePlayerMounts_.clear();
    pendingRemotePlayerMounts_.clear();
    gameObjectInstances_.clear();
}

void EntitySpawner::resetAllState() {
    // Wait for in-flight async loads before clearing state
    for (auto& load : asyncCreatureLoads_) {
        if (load.future.valid()) load.future.wait();
    }

    // Despawn all entities (renderer cleanup)
    despawnAllCreatures();
    despawnAllPlayers();
    despawnAllGameObjects();
    clearMountState();

    // Clear all queues and async loads
    clearAllQueues();

    // Clear all instance tracking
    creatureInstances_.clear();
    creatureModelIds_.clear();
    creatureDisplayIds_.clear();
    requestedCreatureDisplayIds_.clear();
    creatureRenderPosCache_.clear();
    playerInstances_.clear();
    onlinePlayerAppearance_.clear();
    remotePlayerMounts_.clear();
    pendingRemotePlayerMounts_.clear();
    gameObjectInstances_.clear();

    // Clear animation state maps
    creatureWasMoving_.clear();
    creatureWasSwimming_.clear();
    creatureWasFlying_.clear();
    creatureWasWalking_.clear();
    creatureSwimmingState_.clear();
    creatureWalkingState_.clear();
    creatureFlyingState_.clear();
    creatureActiveEmotes_.clear();
    // Carried over, a stale entry matching the creature's current stealth state
    // suppresses the opacity call that would apply it, so a stealthed NPC came
    // back fully opaque after a relog. shutdown() already cleared this.
    creatureWasStealthed_.clear();
    creatureWeaponsAttached_.clear();
    creatureWeaponAttachAttempts_.clear();
    modelIdIsWolfLike_.clear();

    // Clear display/spawn caches
    nonRenderableCreatureDisplayIds_.clear();
    displayIdModelCache_.clear();
    displayIdTexturesApplied_.clear();
    charSectionsCache_.clear();
    charSectionsCacheBuilt_ = false;

    // Clear GO display caches
    gameObjectDisplayIdModelCache_.clear();
    gameObjectDisplayIdWmoCache_.clear();
    gameObjectDisplayIdFailedCache_.clear();
    // Instance ids in here belong to a renderer that has just been cleared.
    gameObjectPendingAnimPolicy_.clear();
}

void EntitySpawner::rebuildLookups() {
    creatureLookupsBuilt_ = false;
    displayDataMap_.clear();
    humanoidExtraMap_.clear();
    creatureModelIds_.clear();
    creatureRenderPosCache_.clear();
    nonRenderableCreatureDisplayIds_.clear();
    initialize();
}

bool EntitySpawner::hasWorkPending() const {
    return !pendingCreatureSpawns_.empty() || !asyncCreatureLoads_.empty() ||
           !asyncNpcCompositeLoads_.empty() || !pendingPlayerSpawns_.empty() ||
           !asyncEquipmentLoads_.empty() || !deferredEquipmentQueue_.empty() ||
           !pendingGameObjectSpawns_.empty() || !asyncGameObjectLoads_.empty();
}

void EntitySpawner::clearMountState() {
    if (mountInstanceId_ != 0 && renderer_) {
        if (auto* charRenderer = renderer_->getCharacterRenderer()) {
            charRenderer->removeInstance(mountInstanceId_);
        }
    }
    mountInstanceId_ = 0;
    mountModelId_ = 0;
    pendingMountDisplayId_ = 0;
}

void EntitySpawner::setRemotePlayerMountDisplayId(uint64_t guid, uint32_t displayId) {
    if (guid == 0) return;
    pendingRemotePlayerMounts_[guid] = displayId;
}

void EntitySpawner::removeRemotePlayerMount(uint64_t guid) {
    auto it = remotePlayerMounts_.find(guid);
    if (it == remotePlayerMounts_.end()) return;
    if (renderer_) {
        if (auto* cr = renderer_->getCharacterRenderer()) {
            if (it->second.instanceId != 0) cr->removeInstance(it->second.instanceId);
            auto playerIt = playerInstances_.find(guid);
            if (playerIt != playerInstances_.end()) {
                cr->playAnimation(playerIt->second, rendering::anim::STAND, true);
            }
        }
    }
    remotePlayerMounts_.erase(it);
}

void EntitySpawner::queueTransportRegistration(uint64_t guid, uint32_t entry, uint32_t displayId,
                                                float x, float y, float z, float orientation) {
    pendingTransportRegistrations_.push_back({guid, entry, displayId, x, y, z, orientation});
}

void EntitySpawner::setTransportPendingMove(uint64_t guid, float x, float y, float z, float orientation) {
    pendingTransportMoves_[guid] = {x, y, z, orientation};
}

bool EntitySpawner::hasTransportRegistrationPending(uint64_t guid) const {
    return std::any_of(pendingTransportRegistrations_.begin(), pendingTransportRegistrations_.end(),
                       [guid](const PendingTransportRegistration& reg) { return reg.guid == guid; });
}

void EntitySpawner::updateTransportRegistration(uint64_t guid, uint32_t displayId,
                                                 float x, float y, float z, float orientation) {
    for (auto& reg : pendingTransportRegistrations_) {
        if (reg.guid == guid) {
            reg.displayId = displayId;
            reg.x = x; reg.y = y; reg.z = z; reg.orientation = orientation;
            return;
        }
    }
}

// --- Queue API ---

void EntitySpawner::queueCreatureSpawn(uint64_t guid, uint32_t displayId,
                                        float x, float y, float z, float orientation, float scale) {
    if (creatureInstances_.count(guid)) return;
    requestedCreatureDisplayIds_[guid] = displayId;
    if (pendingCreatureSpawnGuids_.count(guid)) {
        // Replace any not-yet-started request. An older async result can still
        // finish later; processAsyncCreatureResults rejects it against the map.
        pendingCreatureSpawns_.erase(
            std::remove_if(pendingCreatureSpawns_.begin(), pendingCreatureSpawns_.end(),
                           [guid](const PendingCreatureSpawn& spawn) {
                               return spawn.guid == guid;
                           }),
            pendingCreatureSpawns_.end());
    }
    pendingCreatureSpawns_.push_back({guid, displayId, x, y, z, orientation, scale});
    pendingCreatureSpawnGuids_.insert(guid);
}

void EntitySpawner::queuePlayerSpawn(uint64_t guid, uint8_t raceId, uint8_t genderId,
                                      uint32_t appearanceBytes, uint8_t facialFeatures,
                                      float x, float y, float z, float orientation) {
    if (playerInstances_.count(guid)) return;
    if (pendingPlayerSpawnGuids_.count(guid)) return;
    pendingPlayerSpawns_.push_back({guid, raceId, genderId, appearanceBytes, facialFeatures, x, y, z, orientation});
    pendingPlayerSpawnGuids_.insert(guid);
}

void EntitySpawner::queueGameObjectSpawn(uint64_t guid, uint32_t entry, uint32_t displayId,
                                          float x, float y, float z, float orientation, float scale) {
    pendingGameObjectSpawns_.push_back({guid, entry, displayId, x, y, z, orientation, scale});
}

void EntitySpawner::queuePlayerEquipment(uint64_t guid,
                                          const std::array<uint32_t, 19>& displayInfoIds,
                                          const std::array<uint8_t, 19>& inventoryTypes) {
    deferredEquipmentQueue_.push_back({guid, {displayInfoIds, inventoryTypes}});
}

// --- Immediate despawn wrappers ---

void EntitySpawner::clearAllQueues() {
    pendingCreatureSpawns_.clear();
    pendingCreatureSpawnGuids_.clear();
    requestedCreatureDisplayIds_.clear();
    creatureSpawnRetryDeadlines_.clear();
    creaturePermanentFailureGuids_.clear();
    deadCreatureGuids_.clear();
    pendingPlayerSpawns_.clear();
    pendingPlayerSpawnGuids_.clear();
    pendingOnlinePlayerEquipment_.clear();
    deferredEquipmentQueue_.clear();
    pendingGameObjectSpawns_.clear();
    pendingTransportRegistrations_.clear();
    pendingTransportMoves_.clear();
    pendingTransportDoodadBatches_.clear();
    asyncCreatureLoads_.clear();
    asyncCreatureDisplayLoads_.clear();
    asyncEquipmentLoads_.clear();
    asyncNpcCompositeLoads_.clear();
    asyncGameObjectLoads_.clear();
}

void EntitySpawner::despawnAllCreatures() {
    std::vector<uint64_t> guids;
    guids.reserve(creatureInstances_.size());
    for (const auto& [g, _] : creatureInstances_) guids.push_back(g);
    for (auto g : guids) despawnCreature(g);
}

void EntitySpawner::despawnAllPlayers() {
    std::vector<uint64_t> guids;
    guids.reserve(playerInstances_.size());
    for (const auto& [g, _] : playerInstances_) guids.push_back(g);
    for (auto g : guids) despawnPlayer(g);
}

void EntitySpawner::despawnAllGameObjects() {
    std::vector<uint64_t> guids;
    guids.reserve(gameObjectInstances_.size());
    for (const auto& [g, _] : gameObjectInstances_) guids.push_back(g);
    for (auto g : guids) despawnGameObject(g);
}

// --- Methods extracted from Application (with comments preserved) ---

bool EntitySpawner::tryAttachCreatureVirtualWeapons(uint64_t guid, uint32_t instanceId) {
    if (!renderer_ || !renderer_->getCharacterRenderer() || !assetManager_ || !gameHandler_) return false;
    auto* charRenderer = renderer_->getCharacterRenderer();
    if (!charRenderer) return false;

    auto entity = gameHandler_->getEntityManager().getEntity(guid);
    if (!entity || entity->getType() != game::ObjectType::UNIT) return false;
    auto unit = std::static_pointer_cast<game::Unit>(entity);
    if (!unit) return false;

    // Virtual weapons are only appropriate for humanoid-style displays.
    // Non-humanoids (wolves/boars/etc.) can expose non-zero virtual item fields
    // and otherwise end up with comedic floating weapons.
    uint32_t displayId = unit->getDisplayId();
    auto dIt = displayDataMap_.find(displayId);
    if (dIt == displayDataMap_.end()) return false;
    uint32_t extraDisplayId = dIt->second.extraDisplayId;
    if (extraDisplayId == 0 || humanoidExtraMap_.find(extraDisplayId) == humanoidExtraMap_.end()) {
        return false;
    }

    auto itemDisplayDbc = assetManager_->loadDBC("ItemDisplayInfo.dbc");
    if (!itemDisplayDbc) return false;
    // Item.dbc is not distributed to clients in Vanilla 1.12; on those expansions
    // item display IDs are resolved via the server-sent item cache instead.
    auto itemDbc = assetManager_->loadDBCOptional("Item.dbc");
    const auto* idiL = pipeline::getActiveDBCLayout()
        ? pipeline::getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;
    const auto* itemL = pipeline::getActiveDBCLayout()
        ? pipeline::getActiveDBCLayout()->getLayout("Item") : nullptr;

    auto resolveDisplayInfoId = [&](uint32_t rawId) -> uint32_t {
        if (rawId == 0) return 0;
        // Primary path: AzerothCore uses item entries in UNIT_VIRTUAL_ITEM_SLOT_ID.
        // Resolve strictly through Item.dbc entry -> DisplayID to avoid
        // accidental ItemDisplayInfo ID collisions (staff/hilt mismatches).
        if (itemDbc) {
            int32_t itemRec = itemDbc->findRecordById(rawId); // treat as item entry
            if (itemRec >= 0) {
                const uint32_t dispFieldPrimary = itemL ? (*itemL)["DisplayID"] : 5u;
                uint32_t displayIdA = itemDbc->getUInt32(static_cast<uint32_t>(itemRec), dispFieldPrimary);
                if (displayIdA != 0 && itemDisplayDbc->findRecordById(displayIdA) >= 0) {
                    return displayIdA;
                }
            }
        }
        // Fallback: Vanilla 1.12 does not distribute Item.dbc to clients.
        // Items arrive via SMSG_ITEM_QUERY_SINGLE_RESPONSE and are cached in
        // itemInfoCache_. Use the server-sent displayInfoId when available.
        if (!itemDbc && gameHandler_) {
            if (const auto* info = gameHandler_->getItemInfo(rawId)) {
                uint32_t displayIdB = info->displayInfoId;
                if (displayIdB != 0 && itemDisplayDbc->findRecordById(displayIdB) >= 0) {
                    return displayIdB;
                }
            }
        }
        return 0;
    };

    auto attachNpcWeaponDisplay = [&](uint32_t itemDisplayId, uint32_t attachmentId) -> bool {
        uint32_t resolvedDisplayId = resolveDisplayInfoId(itemDisplayId);
        if (resolvedDisplayId == 0) return false;
        int32_t recIdx = itemDisplayDbc->findRecordById(resolvedDisplayId);
        if (recIdx < 0) return false;

        const uint32_t modelFieldL = idiL ? (*idiL)["LeftModel"] : 1u;
        const uint32_t modelFieldR = idiL ? (*idiL)["RightModel"] : 2u;
        const uint32_t texFieldL = idiL ? (*idiL)["LeftModelTexture"] : 3u;
        const uint32_t texFieldR = idiL ? (*idiL)["RightModelTexture"] : 4u;
        // Prefer LeftModel (stock player equipment path uses LeftModel and avoids
        // the "hilt-only" variants seen when forcing RightModel).
        std::string modelName = itemDisplayDbc->getString(static_cast<uint32_t>(recIdx), modelFieldL);
        std::string textureName = itemDisplayDbc->getString(static_cast<uint32_t>(recIdx), texFieldL);
        if (modelName.empty()) {
            modelName = itemDisplayDbc->getString(static_cast<uint32_t>(recIdx), modelFieldR);
            textureName = itemDisplayDbc->getString(static_cast<uint32_t>(recIdx), texFieldR);
        }
        if (modelName.empty()) return false;

        std::string modelFile = modelName;
        size_t dotPos = modelFile.rfind('.');
        if (dotPos != std::string::npos) modelFile = modelFile.substr(0, dotPos);
        modelFile += ".m2";

        // Main-hand NPC weapon path: only use actual weapon models.
        std::string m2Path = "Item\\ObjectComponents\\Weapon\\" + modelFile;
        pipeline::M2Model weaponModel;
        if (!loadWeaponM2(m2Path, weaponModel)) return false;

        std::string texturePath;
        if (!textureName.empty()) {
            texturePath = "Item\\ObjectComponents\\Weapon\\" + textureName + ".blp";
            if (!assetManager_->fileExists(texturePath)) texturePath.clear();
        }

        uint32_t weaponModelId = nextWeaponModelId_++;
        return charRenderer->attachWeapon(instanceId, attachmentId, weaponModel, weaponModelId, texturePath);
    };

    auto hasResolvableWeaponModel = [&](uint32_t itemDisplayId) -> bool {
        uint32_t resolvedDisplayId = resolveDisplayInfoId(itemDisplayId);
        if (resolvedDisplayId == 0) return false;
        int32_t recIdx = itemDisplayDbc->findRecordById(resolvedDisplayId);
        if (recIdx < 0) return false;
        const uint32_t modelFieldL = idiL ? (*idiL)["LeftModel"] : 1u;
        const uint32_t modelFieldR = idiL ? (*idiL)["RightModel"] : 2u;
        std::string modelName = itemDisplayDbc->getString(static_cast<uint32_t>(recIdx), modelFieldL);
        if (modelName.empty()) {
            modelName = itemDisplayDbc->getString(static_cast<uint32_t>(recIdx), modelFieldR);
        }
        if (modelName.empty()) return false;
        std::string modelFile = modelName;
        size_t dotPos = modelFile.rfind('.');
        if (dotPos != std::string::npos) modelFile = modelFile.substr(0, dotPos);
        modelFile += ".m2";
        return assetManager_->fileExists("Item\\ObjectComponents\\Weapon\\" + modelFile);
    };

    bool attachedMain = false;
    bool hadWeaponCandidate = false;

    const uint16_t candidateBases[] = {56, 57, 58, 70, 148, 149, 150, 151, 152};
    for (uint16_t base : candidateBases) {
        uint32_t v0 = entity->getField(static_cast<uint16_t>(base + 0));
        if (v0 != 0) hadWeaponCandidate = true;
        if (!attachedMain && v0 != 0) attachedMain = attachNpcWeaponDisplay(v0, 1);
        if (attachedMain) break;
    }

    uint16_t unitEnd = game::fieldIndex(game::UF::UNIT_END);
    uint16_t scanLo = 60;
    uint16_t scanHi = (unitEnd != 0xFFFF) ? static_cast<uint16_t>(unitEnd + 96) : 320;
    std::map<uint16_t, uint32_t> candidateByIndex;
    for (const auto& [idx, val] : entity->getFields()) {
        if (idx < scanLo || idx > scanHi) continue;
        if (val == 0) continue;
        if (hasResolvableWeaponModel(val)) {
            candidateByIndex[idx] = val;
            hadWeaponCandidate = true;
        }
    }
    for (const auto& [idx, val] : candidateByIndex) {
        if (!attachedMain) attachedMain = attachNpcWeaponDisplay(val, 1);
        if (attachedMain) break;
    }

    // Force off-hand clear in NPC path to avoid incorrect shields/placeholder hilts.
    charRenderer->detachWeapon(instanceId, 2);
    // Success if main-hand attached when there was at least one candidate.
    return hadWeaponCandidate && attachedMain;
}

bool EntitySpawner::retryCreatureVirtualWeapons(uint64_t guid, uint32_t instanceId,
                                                uint8_t maxAttempts) {
    if (creatureWeaponsAttached_.count(guid)) return false;
    const auto attemptIt = creatureWeaponAttachAttempts_.find(guid);
    const uint8_t attempts = attemptIt != creatureWeaponAttachAttempts_.end()
        ? attemptIt->second : 0;
    if (attempts >= maxAttempts) return false;

    if (tryAttachCreatureVirtualWeapons(guid, instanceId)) {
        creatureWeaponsAttached_.insert(guid);
        creatureWeaponAttachAttempts_.erase(guid);
    } else {
        creatureWeaponAttachAttempts_[guid] = static_cast<uint8_t>(attempts + 1);
    }
    return true;
}


void EntitySpawner::buildCharSectionsCache() {
    if (charSectionsCacheBuilt_ || !assetManager_ || !assetManager_->isInitialized()) return;
    auto dbc = assetManager_->loadDBC("CharSections.dbc");
    if (!dbc) return;
    const auto* csL = pipeline::getActiveDBCLayout()
        ? pipeline::getActiveDBCLayout()->getLayout("CharSections") : nullptr;
    auto csF = pipeline::detectCharSectionsFields(dbc.get(), csL);
    for (uint32_t r = 0; r < dbc->getRecordCount(); r++) {
        uint32_t race = dbc->getUInt32(r, csF.raceId);
        uint32_t sex = dbc->getUInt32(r, csF.sexId);
        uint32_t section = dbc->getUInt32(r, csF.baseSection);
        uint32_t variation = dbc->getUInt32(r, csF.variationIndex);
        uint32_t color = dbc->getUInt32(r, csF.colorIndex);
        // We only cache sections 0 (skin), 1 (face), 3 (hair), 4 (underwear)
        if (section != 0 && section != 1 && section != 3 && section != 4) continue;
        for (int ti = 0; ti < 3; ti++) {
            std::string tex = dbc->getString(r, csF.texture1 + ti);
            if (tex.empty()) continue;
            // Key: race(8)|sex(4)|section(4)|variation(8)|color(8)|texIndex(2) packed into 64 bits
            uint64_t key = (static_cast<uint64_t>(race) << 26) |
                           (static_cast<uint64_t>(sex & 0xF) << 22) |
                           (static_cast<uint64_t>(section & 0xF) << 18) |
                           (static_cast<uint64_t>(variation & 0xFF) << 10) |
                           (static_cast<uint64_t>(color & 0xFF) << 2) |
                           static_cast<uint64_t>(ti);
            charSectionsCache_.emplace(key, tex);
        }
    }
    charSectionsCacheBuilt_ = true;
    LOG_INFO("CharSections cache built: ", charSectionsCache_.size(), " entries");
}

std::string EntitySpawner::lookupCharSection(uint8_t race, uint8_t sex, uint8_t section,
                                           uint8_t variation, uint8_t color, int texIndex) const {
    uint64_t key = (static_cast<uint64_t>(race) << 26) |
                   (static_cast<uint64_t>(sex & 0xF) << 22) |
                   (static_cast<uint64_t>(section & 0xF) << 18) |
                   (static_cast<uint64_t>(variation & 0xFF) << 10) |
                   (static_cast<uint64_t>(color & 0xFF) << 2) |
                   static_cast<uint64_t>(texIndex);
    auto it = charSectionsCache_.find(key);
    return (it != charSectionsCache_.end()) ? it->second : std::string();
}

void EntitySpawner::buildCreatureDisplayLookups() {
    if (creatureLookupsBuilt_ || !assetManager_ || !assetManager_->isInitialized()) return;

    LOG_INFO("Building creature display lookups from DBC files");

    // CreatureDisplayInfo.dbc structure (3.3.5a):
    // Col 0: displayId
    // Col 1: modelId
    // Col 3: extendedDisplayInfoID (link to CreatureDisplayInfoExtra.dbc)
    // Col 6: Skin1 (texture name)
    // Col 7: Skin2
    // Col 8: Skin3
    if (auto cdi = assetManager_->loadDBC("CreatureDisplayInfo.dbc"); cdi && cdi->isLoaded()) {
        const auto* cdiL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("CreatureDisplayInfo") : nullptr;
        for (uint32_t i = 0; i < cdi->getRecordCount(); i++) {
            CreatureDisplayData data;
            data.modelId = cdi->getUInt32(i, cdiL ? (*cdiL)["ModelID"] : 1);
            data.extraDisplayId = cdi->getUInt32(i, cdiL ? (*cdiL)["ExtraDisplayId"] : 3);
            data.skin1 = cdi->getString(i, cdiL ? (*cdiL)["Skin1"] : 6);
            data.skin2 = cdi->getString(i, cdiL ? (*cdiL)["Skin2"] : 7);
            data.skin3 = cdi->getString(i, cdiL ? (*cdiL)["Skin3"] : 8);
            displayDataMap_[cdi->getUInt32(i, cdiL ? (*cdiL)["ID"] : 0)] = data;
        }
        LOG_INFO("Loaded ", displayDataMap_.size(), " display→model mappings");
    }

    // CreatureDisplayInfoExtra.dbc structure (3.3.5a):
    // Col 0: ID
    // Col 1: DisplayRaceID
    // Col 2: DisplaySexID
    // Col 3: SkinID
    // Col 4: FaceID
    // Col 5: HairStyleID
    // Col 6: HairColorID
    // Col 7: FacialHairID
    // CreatureDisplayInfoExtra.dbc field layout depends on actual field count:
    //   19 fields: 10 equip slots (8-17), BakeName=18 (no Flags field)
    //   21 fields: 11 equip slots (8-18), Flags=19, BakeName=20
    if (auto cdie = assetManager_->loadDBC("CreatureDisplayInfoExtra.dbc"); cdie && cdie->isLoaded()) {
        const auto* cdieL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("CreatureDisplayInfoExtra") : nullptr;
        const uint32_t cdieEquip0 = cdieL ? (*cdieL)["EquipDisplay0"] : 8;
        // Detect actual field count to determine equip slot count and BakeName position
        const uint32_t dbcFieldCount = cdie->getFieldCount();
        int numEquipSlots;
        uint32_t bakeField;
        if (dbcFieldCount <= 19) {
            // 19 fields: 10 equip slots (8-17), BakeName at 18
            numEquipSlots = 10;
            bakeField = 18;
        } else {
            // 21 fields: 11 equip slots (8-18), Flags=19, BakeName=20
            numEquipSlots = 11;
            bakeField = cdieL ? (*cdieL)["BakeName"] : 20;
        }
        uint32_t withBakeName = 0;
        for (uint32_t i = 0; i < cdie->getRecordCount(); i++) {
            HumanoidDisplayExtra extra;
            extra.raceId = static_cast<uint8_t>(cdie->getUInt32(i, cdieL ? (*cdieL)["RaceID"] : 1));
            extra.sexId = static_cast<uint8_t>(cdie->getUInt32(i, cdieL ? (*cdieL)["SexID"] : 2));
            extra.skinId = static_cast<uint8_t>(cdie->getUInt32(i, cdieL ? (*cdieL)["SkinID"] : 3));
            extra.faceId = static_cast<uint8_t>(cdie->getUInt32(i, cdieL ? (*cdieL)["FaceID"] : 4));
            extra.hairStyleId = static_cast<uint8_t>(cdie->getUInt32(i, cdieL ? (*cdieL)["HairStyleID"] : 5));
            extra.hairColorId = static_cast<uint8_t>(cdie->getUInt32(i, cdieL ? (*cdieL)["HairColorID"] : 6));
            extra.facialHairId = static_cast<uint8_t>(cdie->getUInt32(i, cdieL ? (*cdieL)["FacialHairID"] : 7));
            for (int eq = 0; eq < numEquipSlots; eq++) {
                extra.equipDisplayId[eq] = cdie->getUInt32(i, cdieEquip0 + eq);
            }
            extra.bakeName = cdie->getString(i, bakeField);
            if (!extra.bakeName.empty()) withBakeName++;
            humanoidExtraMap_[cdie->getUInt32(i, cdieL ? (*cdieL)["ID"] : 0)] = extra;
        }
        LOG_DEBUG("Loaded ", humanoidExtraMap_.size(), " humanoid display extra entries (",
                 withBakeName, " with baked textures, ", numEquipSlots, " equip slots, ",
                 dbcFieldCount, " DBC fields, bakeField=", bakeField, ")");
    }

    // CreatureModelData.dbc: modelId (col 0) → modelPath (col 2, .mdx → .m2)
    if (auto cmd = assetManager_->loadDBC("CreatureModelData.dbc"); cmd && cmd->isLoaded()) {
        const auto* cmdL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("CreatureModelData") : nullptr;
        for (uint32_t i = 0; i < cmd->getRecordCount(); i++) {
            std::string mdx = cmd->getString(i, cmdL ? (*cmdL)["ModelPath"] : 2);
            if (mdx.empty()) continue;
            if (mdx.size() >= 4) {
                mdx = mdx.substr(0, mdx.size() - 4) + ".m2";
            }
            modelIdToPath_[cmd->getUInt32(i, cmdL ? (*cmdL)["ID"] : 0)] = mdx;
        }
        LOG_INFO("Loaded ", modelIdToPath_.size(), " model→path mappings");
    }

    // Resolve gryphon/wyvern display IDs by exact model path so taxi mounts have textures.
    auto toLower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto normalizePath = [&](const std::string& p) {
        std::string s = p;
        for (char& c : s) if (c == '/') c = '\\';
        return toLower(s);
    };
    auto resolveDisplayIdForExactPath = [&](const std::string& exactPath) -> uint32_t {
        const std::string target = normalizePath(exactPath);
        // Collect ALL model IDs that map to this path (multiple model IDs can
        // share the same .m2 file, e.g. modelId 147 and 792 both → Gryphon.m2)
        std::vector<uint32_t> modelIds;
        for (const auto& [mid, path] : modelIdToPath_) {
            if (normalizePath(path) == target) {
                modelIds.push_back(mid);
            }
        }
        if (modelIds.empty()) return 0;
        uint32_t bestDisplayId = 0;
        int bestScore = -1;
        for (const auto& [dispId, data] : displayDataMap_) {
            bool matches = false;
            for (uint32_t mid : modelIds) {
                if (data.modelId == mid) { matches = true; break; }
            }
            if (!matches) continue;
            int score = 0;
            if (!data.skin1.empty()) score += 3;
            if (!data.skin2.empty()) score += 2;
            if (!data.skin3.empty()) score += 1;
            if (score > bestScore) {
                bestScore = score;
                bestDisplayId = dispId;
            }
        }
        return bestDisplayId;
    };

    gryphonDisplayId_ = resolveDisplayIdForExactPath("Creature\\Gryphon\\Gryphon.m2");
    wyvernDisplayId_  = resolveDisplayIdForExactPath("Creature\\Wyvern\\Wyvern.m2");
    gameServices_->gryphonDisplayId = gryphonDisplayId_;
    gameServices_->wyvernDisplayId  = wyvernDisplayId_;
    LOG_INFO("Taxi mount displayIds: gryphon=", gryphonDisplayId_, " wyvern=", wyvernDisplayId_);

    // CharHairGeosets.dbc: maps (race, sex, hairStyleId) → skinSectionId for hair mesh
    // Col 0: ID, Col 1: RaceID, Col 2: SexID, Col 3: VariationID, Col 4: GeosetID, Col 5: Showscalp
    if (auto chg = assetManager_->loadDBC("CharHairGeosets.dbc"); chg && chg->isLoaded()) {
        const auto* chgL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("CharHairGeosets") : nullptr;
        for (uint32_t i = 0; i < chg->getRecordCount(); i++) {
            uint32_t raceId = chg->getUInt32(i, chgL ? (*chgL)["RaceID"] : 1);
            uint32_t sexId = chg->getUInt32(i, chgL ? (*chgL)["SexID"] : 2);
            uint32_t variation = chg->getUInt32(i, chgL ? (*chgL)["Variation"] : 3);
            uint32_t geosetId = chg->getUInt32(i, chgL ? (*chgL)["GeosetID"] : 4);
            // Showscalp/Bald means this style uses the default scalp instead
            // of the extra hair-cap mesh. Ignoring it makes the cap physically
            // poke through hair authored to expose the normal scalp.
            const bool useDefaultScalp = chg->getFieldCount() > 5 && chg->getUInt32(i, 5) != 0;
            uint32_t key = (raceId << 16) | (sexId << 8) | variation;
            hairGeosetMap_[key] = static_cast<uint16_t>(useDefaultScalp ? 1 : geosetId);
        }
        LOG_INFO("Loaded ", hairGeosetMap_.size(), " hair geoset mappings from CharHairGeosets.dbc");
    }

    // CharacterFacialHairStyles.dbc: maps (race, sex, facialHairId) → geoset IDs
    // No ID column: Col 0: RaceID, Col 1: SexID, Col 2: VariationID
    // Col 3: Geoset100, Col 4: Geoset300, Col 5: Geoset200
    if (auto cfh = assetManager_->loadDBC("CharacterFacialHairStyles.dbc"); cfh && cfh->isLoaded()) {
        const auto* cfhL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("CharacterFacialHairStyles") : nullptr;
        for (uint32_t i = 0; i < cfh->getRecordCount(); i++) {
            uint32_t raceId = cfh->getUInt32(i, cfhL ? (*cfhL)["RaceID"] : 0);
            uint32_t sexId = cfh->getUInt32(i, cfhL ? (*cfhL)["SexID"] : 1);
            uint32_t variation = cfh->getUInt32(i, cfhL ? (*cfhL)["Variation"] : 2);
            uint32_t key = (raceId << 16) | (sexId << 8) | variation;
            FacialHairGeosets fhg;
            // Columns 3-5 are not the geosets: they hold a constant per race in
            // every copy of this DBC that ships here, and reading them gave
            // values like 2010429269 that no model has. The variant numbers are
            // at 6-8, which is where a Draenei female's face tendrils live.
            fhg.geoset100 = static_cast<uint16_t>(cfh->getUInt32(i, cfhL ? (*cfhL)["Geoset100"] : 6));
            fhg.geoset300 = static_cast<uint16_t>(cfh->getUInt32(i, cfhL ? (*cfhL)["Geoset300"] : 7));
            fhg.geoset200 = static_cast<uint16_t>(cfh->getUInt32(i, cfhL ? (*cfhL)["Geoset200"] : 8));
            facialHairGeosetMap_[key] = fhg;
        }
        LOG_INFO("Loaded ", facialHairGeosetMap_.size(), " facial hair geoset mappings from CharacterFacialHairStyles.dbc");
    }

    creatureLookupsBuilt_ = true;
}

std::string EntitySpawner::getModelPathForDisplayId(uint32_t displayId) const {
    // WotLK 3.3.5a CreatureDisplayInfo tops out around ~32000; values far
    // beyond that are corrupted update-field data or packet parse errors.
    // Silently reject to avoid pointless DBC lookups and log spam.
    constexpr uint32_t kMaxReasonableDisplayId = 100000;
    if (displayId == 0 || displayId > kMaxReasonableDisplayId) {
        return "";
    }

    if (displayId == 30412) return "Creature\\Gryphon\\Gryphon.m2";
    if (displayId == 30413) return "Creature\\Wyvern\\Wyvern.m2";

    // WotLK servers can send display IDs that do not exist in older/local
    // CreatureDisplayInfo datasets. Keep those creatures visible by falling
    // back to a close base model instead of dropping spawn entirely.
    switch (displayId) {
        case 31048: // Diseased Young Wolf variants (AzerothCore WotLK)
        case 31049: // Diseased Wolf variants (AzerothCore WotLK)
            return "Creature\\Wolf\\Wolf.m2";
        default:
            break;
    }

    auto itData = displayDataMap_.find(displayId);
    if (itData == displayDataMap_.end()) {
        // Some sources (e.g., taxi nodes) may provide a modelId directly.
        auto itPath = modelIdToPath_.find(displayId);
        if (itPath != modelIdToPath_.end()) {
            return itPath->second;
        }
        if (displayId == 30412) return "Creature\\Gryphon\\Gryphon.m2";
        if (displayId == 30413) return "Creature\\Wyvern\\Wyvern.m2";
        if (warnedMissingDisplayDataIds_.insert(displayId).second) {
            LOG_WARNING("No display data for displayId ", displayId,
                        " (displayDataMap_ has ", displayDataMap_.size(), " entries)");
        }
        return "";
    }

    auto itPath = modelIdToPath_.find(itData->second.modelId);
    if (itPath == modelIdToPath_.end()) {
        if (warnedMissingModelPathIds_.insert(displayId).second) {
            LOG_WARNING("No model path for modelId ", itData->second.modelId,
                        " from displayId ", displayId,
                        " (modelIdToPath_ has ", modelIdToPath_.size(), " entries)");
        }
        return "";
    }

    return itPath->second;
}

audio::VoiceType EntitySpawner::detectVoiceTypeFromDisplayId(uint32_t displayId) const {
    // Look up display data
    auto itDisplay = displayDataMap_.find(displayId);
    if (itDisplay == displayDataMap_.end() || itDisplay->second.extraDisplayId == 0) {
        LOG_INFO("Voice detection: displayId ", displayId, " -> GENERIC (no display data)");
        return audio::VoiceType::GENERIC;  // Not a humanoid or no extra data
    }

    // Look up humanoid extra data (race/sex info)
    auto itExtra = humanoidExtraMap_.find(itDisplay->second.extraDisplayId);
    if (itExtra == humanoidExtraMap_.end()) {
        LOG_INFO("Voice detection: displayId ", displayId, " -> GENERIC (no humanoid extra data)");
        return audio::VoiceType::GENERIC;
    }

    uint8_t raceId = itExtra->second.raceId;
    uint8_t sexId = itExtra->second.sexId;

    const char* raceName = "Unknown";
    const char* sexName = (sexId == 0) ? "Male" : "Female";

    // Map (raceId, sexId) to VoiceType
    // Race IDs: 1=Human, 2=Orc, 3=Dwarf, 4=NightElf, 5=Undead, 6=Tauren, 7=Gnome, 8=Troll
    // Sex IDs: 0=Male, 1=Female
    audio::VoiceType result;
    switch (raceId) {
        case 1: raceName = "Human"; result = (sexId == 0) ? audio::VoiceType::HUMAN_MALE : audio::VoiceType::HUMAN_FEMALE; break;
        case 2: raceName = "Orc"; result = (sexId == 0) ? audio::VoiceType::ORC_MALE : audio::VoiceType::ORC_FEMALE; break;
        case 3: raceName = "Dwarf"; result = (sexId == 0) ? audio::VoiceType::DWARF_MALE : audio::VoiceType::DWARF_FEMALE; break;
        case 4: raceName = "NightElf"; result = (sexId == 0) ? audio::VoiceType::NIGHTELF_MALE : audio::VoiceType::NIGHTELF_FEMALE; break;
        case 5: raceName = "Undead"; result = (sexId == 0) ? audio::VoiceType::UNDEAD_MALE : audio::VoiceType::UNDEAD_FEMALE; break;
        case 6: raceName = "Tauren"; result = (sexId == 0) ? audio::VoiceType::TAUREN_MALE : audio::VoiceType::TAUREN_FEMALE; break;
        case 7: raceName = "Gnome"; result = (sexId == 0) ? audio::VoiceType::GNOME_MALE : audio::VoiceType::GNOME_FEMALE; break;
        case 8: raceName = "Troll"; result = (sexId == 0) ? audio::VoiceType::TROLL_MALE : audio::VoiceType::TROLL_FEMALE; break;
        case 10: raceName = "BloodElf"; result = (sexId == 0) ? audio::VoiceType::BLOODELF_MALE : audio::VoiceType::BLOODELF_FEMALE; break;
        case 11: raceName = "Draenei"; result = (sexId == 0) ? audio::VoiceType::DRAENEI_MALE : audio::VoiceType::DRAENEI_FEMALE; break;
        default: result = audio::VoiceType::GENERIC; break;
    }

    LOG_INFO("Voice detection: displayId ", displayId, " -> ", raceName, " ", sexName, " (race=", static_cast<int>(raceId), ", sex=", static_cast<int>(sexId), ")");
    return result;
}

void EntitySpawner::buildGameObjectDisplayLookups() {
    if (gameObjectLookupsBuilt_ || !assetManager_ || !assetManager_->isInitialized()) return;

    LOG_INFO("Building gameobject display lookups from DBC files");

    // GameObjectDisplayInfo.dbc structure (3.3.5a):
    // Col 0: ID (displayId)
    // Col 1: ModelName
    if (auto godi = assetManager_->loadDBC("GameObjectDisplayInfo.dbc"); godi && godi->isLoaded()) {
        const auto* godiL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("GameObjectDisplayInfo") : nullptr;
        for (uint32_t i = 0; i < godi->getRecordCount(); i++) {
            uint32_t displayId = godi->getUInt32(i, godiL ? (*godiL)["ID"] : 0);
            std::string modelName = godi->getString(i, godiL ? (*godiL)["ModelName"] : 1);
            if (modelName.empty()) continue;
            if (modelName.size() >= 4) {
                std::string ext = modelName.substr(modelName.size() - 4);
                for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (ext == ".mdx") {
                    modelName = modelName.substr(0, modelName.size() - 4) + ".m2";
                }
            }
            gameObjectDisplayIdToPath_[displayId] = modelName;
        }
        LOG_INFO("Loaded ", gameObjectDisplayIdToPath_.size(), " gameobject display mappings");
    } else {
        LOG_WARNING("GameObjectDisplayInfo.dbc failed to load — no GO display mappings available");
    }

    if (gameObjectDisplayIdToPath_.empty()) {
        LOG_WARNING("GO display mapping table is EMPTY — game objects will not render");
    }

    gameObjectLookupsBuilt_ = true;
}

std::string EntitySpawner::getGameObjectModelPathForDisplayId(uint32_t displayId) const {
    auto it = gameObjectDisplayIdToPath_.find(displayId);
    if (it == gameObjectDisplayIdToPath_.end()) return "";
    return it->second;
}


bool EntitySpawner::getRenderBoundsForGuid(uint64_t guid, glm::vec3& outCenter, float& outRadius) const {
    if (!renderer_) return false;

    // M2 game objects (mailboxes, chests, nodes, etc.) render via the M2
    // renderer, not the character renderer, so their bounds come from a
    // different instance table. Their true world-space visual sphere makes
    // cursor picking track the actual model instead of a flat fallback.
    //
    // WMO game objects (buildings) are intentionally not resolved here: a
    // bounding sphere from a building's AABB diagonal is enormous and would
    // swallow nearby objects in the picker. They keep the conservative
    // fallback sphere used by the click handlers.
    auto goIt = gameObjectInstances_.find(guid);
    if (goIt != gameObjectInstances_.end()) {
        const auto& go = goIt->second;
        if (go.isWmo) return false;
        auto* m2 = renderer_->getM2Renderer();
        return m2 && m2->getInstanceBounds(go.instanceId, outCenter, outRadius);
    }

    if (!renderer_->getCharacterRenderer()) return false;
    uint32_t instanceId = 0;

    if (gameHandler_ && guid == gameHandler_->getPlayerGuid()) {
        instanceId = renderer_->getCharacterInstanceId();
    }
    if (instanceId == 0) {
        auto pit = playerInstances_.find(guid);
        if (pit != playerInstances_.end()) instanceId = pit->second;
    }
    if (instanceId == 0) {
        auto it = creatureInstances_.find(guid);
        if (it != creatureInstances_.end()) instanceId = it->second;
    }
    if (instanceId == 0) return false;

    return renderer_->getCharacterRenderer()->getInstanceBounds(instanceId, outCenter, outRadius);
}

bool EntitySpawner::getRenderFootZForGuid(uint64_t guid, float& outFootZ) const {
    if (!renderer_ || !renderer_->getCharacterRenderer()) return false;
    uint32_t instanceId = 0;

    if (gameHandler_ && guid == gameHandler_->getPlayerGuid()) {
        instanceId = renderer_->getCharacterInstanceId();
    }
    if (instanceId == 0) {
        auto pit = playerInstances_.find(guid);
        if (pit != playerInstances_.end()) instanceId = pit->second;
    }
    if (instanceId == 0) {
        auto it = creatureInstances_.find(guid);
        if (it != creatureInstances_.end()) instanceId = it->second;
    }
    if (instanceId == 0) return false;

    return renderer_->getCharacterRenderer()->getInstanceFootZ(instanceId, outFootZ);
}

bool EntitySpawner::getRenderPositionForGuid(uint64_t guid, glm::vec3& outPos) const {
    if (!renderer_ || !renderer_->getCharacterRenderer()) return false;
    uint32_t instanceId = 0;

    if (gameHandler_ && guid == gameHandler_->getPlayerGuid()) {
        instanceId = renderer_->getCharacterInstanceId();
    }
    if (instanceId == 0) {
        auto pit = playerInstances_.find(guid);
        if (pit != playerInstances_.end()) instanceId = pit->second;
    }
    if (instanceId == 0) {
        auto it = creatureInstances_.find(guid);
        if (it != creatureInstances_.end()) instanceId = it->second;
    }
    if (instanceId == 0) return false;

    return renderer_->getCharacterRenderer()->getInstancePosition(instanceId, outPos);
}

EntitySpawner::CachedAttachmentModel
EntitySpawner::getOrLoadAttachmentModel(const std::vector<std::string>& candidatePaths,
                                        const std::string& texturePath) {
    // 1) Geometry: first candidate that yields a valid model wins, parsed at most once.
    std::shared_ptr<pipeline::M2Model> model;
    std::string resolvedPath;
    for (const auto& path : candidatePaths) {
        auto it = attachmentModelData_.find(path);
        if (it != attachmentModelData_.end()) {
            if (!it->second) continue;  // known missing — try the next candidate
            model = it->second;
            resolvedPath = path;
            break;
        }

        auto data = assetManager_->readFile(path);
        if (data.empty()) {
            attachmentModelData_[path] = nullptr;
            continue;
        }
        auto parsed = std::make_shared<pipeline::M2Model>(pipeline::M2Loader::load(data));
        if (parsed->name.empty()) parsed->name = path;
        // Skin is a sidecar file for WotLK M2s; vanilla embeds it.
        if (parsed->version >= 264) {
            std::string skinPath = path.substr(0, path.size() - 3) + "00.skin";
            auto skinData = assetManager_->readFile(skinPath);
            if (!skinData.empty()) pipeline::M2Loader::loadSkin(skinData, *parsed);
        }
        if (!parsed->isValid()) {
            attachmentModelData_[path] = nullptr;
            continue;
        }
        attachmentModelData_[path] = parsed;
        model = std::move(parsed);
        resolvedPath = path;
        break;
    }
    if (!model) return {};

    // 2) Model id is per (geometry, texture) — see attachmentModelIds_.
    const std::string key = resolvedPath + '|' + texturePath;
    auto idIt = attachmentModelIds_.find(key);
    if (idIt == attachmentModelIds_.end()) {
        idIt = attachmentModelIds_.emplace(key, nextCreatureModelId_++).first;
    }
    return CachedAttachmentModel{idIt->second, std::move(model)};
}

void EntitySpawner::spawnOnlineCreature(uint64_t guid, uint32_t displayId, float x, float y, float z, float orientation, float scale) {
    if (!renderer_ || !renderer_->getCharacterRenderer() || !assetManager_) return;

    // Skip if lookups not yet built (asset manager not ready)
    if (!creatureLookupsBuilt_) return;

    // Skip if already spawned
    if (creatureInstances_.count(guid)) return;
    if (nonRenderableCreatureDisplayIds_.count(displayId)) {
        creaturePermanentFailureGuids_.insert(guid);
        return;
    }

    // Get model path from displayId
    std::string m2Path = getModelPathForDisplayId(displayId);
    if (m2Path.empty()) {
        nonRenderableCreatureDisplayIds_.insert(displayId);
        creaturePermanentFailureGuids_.insert(guid);
        return;
    }
    {
        // Intentionally invisible helper creatures should not consume retry budget.
        std::string lowerPath = m2Path;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowerPath.find("invisiblestalker") != std::string::npos ||
            lowerPath.find("invisible_stalker") != std::string::npos) {
            nonRenderableCreatureDisplayIds_.insert(displayId);
            creaturePermanentFailureGuids_.insert(guid);
            return;
        }
    }

    auto* charRenderer = renderer_->getCharacterRenderer();

    // The model must already be in the cache: callers spawn only once the async load
    // has published it. Reading and parsing the M2 here instead would put file I/O on
    // the main thread mid-frame, so treat a miss as "not ready" and let the caller
    // re-queue the spawn through the async path.
    auto cacheIt = displayIdModelCache_.find(displayId);
    if (cacheIt == displayIdModelCache_.end()) {
        LOG_WARNING("spawnOnlineCreature: model not loaded yet for displayId=", displayId,
                    " — deferring to async load");
        return;
    }
    const uint32_t modelId = cacheIt->second;

    // Apply skin textures from CreatureDisplayInfo.dbc (only once per displayId model).
    // Track separately from model cache because async loading may upload the model
    // before textures are applied.
    auto itDisplayData = displayDataMap_.find(displayId);
    bool needsTextures = (displayIdTexturesApplied_.find(displayId) == displayIdTexturesApplied_.end());
    if (needsTextures && itDisplayData != displayDataMap_.end()) {
        auto texStart = std::chrono::steady_clock::now();
        displayIdTexturesApplied_.insert(displayId);
        const auto& dispData = itDisplayData->second;

        // Use pre-decoded textures from async creature load (if available)
        auto itPreDec = displayIdPredecodedTextures_.find(displayId);
        bool hasPreDec = (itPreDec != displayIdPredecodedTextures_.end());
        if (hasPreDec) {
            charRenderer->setPredecodedBLPCache(&itPreDec->second);
        }

        // Get model directory for texture path construction
        std::string modelDir;
        size_t lastSlash = m2Path.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            modelDir = m2Path.substr(0, lastSlash + 1);
        }

        LOG_DEBUG("DisplayId ", displayId, " skins: '", dispData.skin1, "', '", dispData.skin2, "', '", dispData.skin3,
                  "' extraDisplayId=", dispData.extraDisplayId);

        // Get model data from CharacterRenderer for texture iteration
        const auto* modelData = charRenderer->getModelData(modelId);
        if (!modelData) {
            LOG_WARNING("Model data not found for modelId ", modelId);
        }

        // Log texture types in the model
        if (modelData) {
        for (size_t ti = 0; ti < modelData->textures.size(); ti++) {
            LOG_DEBUG("  Model texture ", ti, ": type=", modelData->textures[ti].type, " filename='", modelData->textures[ti].filename, "'");
        }
        }

        // Check if this is a humanoid NPC with extra display info
        bool hasHumanoidTexture = false;
        if (dispData.extraDisplayId != 0) {
            auto itExtra = humanoidExtraMap_.find(dispData.extraDisplayId);
            if (itExtra != humanoidExtraMap_.end()) {
                const auto& extra = itExtra->second;
                LOG_DEBUG("  Found humanoid extra: raceId=", static_cast<int>(extra.raceId), " sexId=", static_cast<int>(extra.sexId),
                          " hairStyle=", static_cast<int>(extra.hairStyleId), " hairColor=", static_cast<int>(extra.hairColorId),
                          " bakeName='", extra.bakeName, "'");

                // Collect model texture slot info (type 1 = skin, type 6 = hair)
                std::vector<uint32_t> skinSlots, hairSlots;
                if (modelData) {
                    for (size_t ti = 0; ti < modelData->textures.size(); ti++) {
                        uint32_t texType = modelData->textures[ti].type;
                        if (texType == 1 || texType == 11 || texType == 12 || texType == 13)
                            skinSlots.push_back(static_cast<uint32_t>(ti));
                        if (texType == 6)
                            hairSlots.push_back(static_cast<uint32_t>(ti));
                    }
                }

                // Copy extra data for the async task (avoid dangling reference)
                HumanoidDisplayExtra extraCopy = extra;

                // Launch async task: ALL DBC lookups, path resolution, and BLP pre-decode
                // happen on a background thread. Only GPU texture upload runs on main thread
                // (in processAsyncNpcCompositeResults).
                auto* am = assetManager_;
                AsyncNpcCompositeLoad load;
                load.future = std::async(std::launch::async,
                    [am, extraCopy, skinSlots = std::move(skinSlots),
                     hairSlots = std::move(hairSlots), modelId, displayId]() mutable -> PreparedNpcComposite {
                        PreparedNpcComposite result;
                        DeferredNpcComposite& def = result.info;
                        def.modelId = modelId;
                        def.displayId = displayId;
                        def.skinTextureSlots = std::move(skinSlots);
                        def.hairTextureSlots = std::move(hairSlots);

                        std::vector<std::string> allPaths;  // paths to pre-decode

                        // --- Baked skin texture ---
                        if (!extraCopy.bakeName.empty()) {
                            def.bakedSkinPath = "Textures\\BakedNpcTextures\\" + extraCopy.bakeName;
                            def.hasBakedSkin = true;
                            allPaths.push_back(def.bakedSkinPath);
                        }

                        // --- CharSections fallback (skin/face/underwear) ---
                        if (!def.hasBakedSkin) {
                            auto csDbc = am->loadDBC("CharSections.dbc");
                            if (csDbc) {
                                const auto* csL = pipeline::getActiveDBCLayout()
                                    ? pipeline::getActiveDBCLayout()->getLayout("CharSections") : nullptr;
                                auto csF = pipeline::detectCharSectionsFields(csDbc.get(), csL);
                                uint32_t npcRace = static_cast<uint32_t>(extraCopy.raceId);
                                uint32_t npcSex = static_cast<uint32_t>(extraCopy.sexId);
                                uint32_t npcSkin = static_cast<uint32_t>(extraCopy.skinId);
                                uint32_t npcFace = static_cast<uint32_t>(extraCopy.faceId);
                                std::string npcFaceLower, npcFaceUpper;
                                std::vector<std::string> npcUnderwear;

                                for (uint32_t r = 0; r < csDbc->getRecordCount(); r++) {
                                    uint32_t rId = csDbc->getUInt32(r, csF.raceId);
                                    uint32_t sId = csDbc->getUInt32(r, csF.sexId);
                                    if (rId != npcRace || sId != npcSex) continue;

                                    uint32_t section = csDbc->getUInt32(r, csF.baseSection);
                                    uint32_t variation = csDbc->getUInt32(r, csF.variationIndex);
                                    uint32_t color = csDbc->getUInt32(r, csF.colorIndex);

                                    if (section == 0 && def.basePath.empty() && color == npcSkin) {
                                        def.basePath = csDbc->getString(r, csF.texture1);
                                    } else if (section == 1 && npcFaceLower.empty() &&
                                               variation == npcFace && color == npcSkin) {
                                        npcFaceLower = csDbc->getString(r, csF.texture1);
                                        npcFaceUpper = csDbc->getString(r, csF.texture2);
                                    } else if (section == 4 && npcUnderwear.empty() && color == npcSkin) {
                                        for (uint32_t f = csF.texture1; f <= csF.texture1 + 2; f++) {
                                            std::string tex = csDbc->getString(r, f);
                                            if (!tex.empty() && am->fileExists(tex))
                                                npcUnderwear.push_back(tex);
                                        }
                                    }
                                }

                                if (!def.basePath.empty()) {
                                    allPaths.push_back(def.basePath);
                                    if (!npcFaceLower.empty()) { def.overlayPaths.push_back(npcFaceLower); allPaths.push_back(npcFaceLower); }
                                    if (!npcFaceUpper.empty()) { def.overlayPaths.push_back(npcFaceUpper); allPaths.push_back(npcFaceUpper); }
                                    for (const auto& uw : npcUnderwear) { def.overlayPaths.push_back(uw); allPaths.push_back(uw); }
                                }
                            }
                        }

                        // --- Equipment region layers (ItemDisplayInfo DBC) ---
                        auto idiDbc = am->loadDBC("ItemDisplayInfo.dbc");
                        if (idiDbc) {
                            static constexpr const char* componentDirs[] = {
                                "ArmUpperTexture", "ArmLowerTexture", "HandTexture",
                                "TorsoUpperTexture", "TorsoLowerTexture",
                                "LegUpperTexture", "LegLowerTexture", "FootTexture",
                            };
                            const auto* idiL = pipeline::getActiveDBCLayout()
                                ? pipeline::getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;
                            uint32_t texRegionFields[8];
                            pipeline::getItemDisplayInfoTextureFields(*idiDbc, idiL, texRegionFields);
                            const bool npcIsFemale = (extraCopy.sexId == 1);
                            const bool npcHasArmArmor = (extraCopy.equipDisplayId[7] != 0 || extraCopy.equipDisplayId[8] != 0);

                            auto regionAllowedForNpcSlot = [](int eqSlot, int region) -> bool {
                                switch (eqSlot) {
                                    case 2: case 3: return region <= 4;
                                    case 4: return false;
                                    case 5: return region == 5 || region == 6;
                                    case 6: return region == 7;
                                    case 7: return false;
                                    case 8: return region == 2;
                                    case 9: return region == 3 || region == 4;
                                    default: return false;
                                }
                            };

                            for (int eqSlot = 0; eqSlot < 11; eqSlot++) {
                                uint32_t did = extraCopy.equipDisplayId[eqSlot];
                                if (did == 0) continue;
                                int32_t recIdx = idiDbc->findRecordById(did);
                                if (recIdx < 0) continue;

                                for (int region = 0; region < 8; region++) {
                                    if (!regionAllowedForNpcSlot(eqSlot, region)) continue;
                                    if (eqSlot == 2 && !npcHasArmArmor && !(region == 3 || region == 4)) continue;
                                    std::string texName = idiDbc->getString(
                                        static_cast<uint32_t>(recIdx), texRegionFields[region]);
                                    if (texName.empty()) continue;

                                    std::string base = "Item\\TextureComponents\\" +
                                        std::string(componentDirs[region]) + "\\" + texName;
                                    std::string genderPath = base + (npcIsFemale ? "_F.blp" : "_M.blp");
                                    std::string unisexPath = base + "_U.blp";
                                    std::string basePath = base + ".blp";
                                    std::string fullPath;
                                    if (am->fileExists(genderPath)) fullPath = genderPath;
                                    else if (am->fileExists(unisexPath)) fullPath = unisexPath;
                                    else if (am->fileExists(basePath)) fullPath = basePath;
                                    else continue;

                                    def.regionLayers.emplace_back(region, fullPath);
                                    allPaths.push_back(fullPath);
                                }
                            }
                        }

                        // Determine compositing mode
                        if (!def.basePath.empty()) {
                            bool needsComposite = !def.overlayPaths.empty() || !def.regionLayers.empty();
                            if (needsComposite && !def.skinTextureSlots.empty()) {
                                def.hasComposite = true;
                            } else if (!def.skinTextureSlots.empty()) {
                                def.hasSimpleSkin = true;
                            }
                        }

                        // --- Hair texture from CharSections (section 3) ---
                        {
                            auto csDbc = am->loadDBC("CharSections.dbc");
                            if (csDbc) {
                                const auto* csL = pipeline::getActiveDBCLayout()
                                    ? pipeline::getActiveDBCLayout()->getLayout("CharSections") : nullptr;
                                auto csF = pipeline::detectCharSectionsFields(csDbc.get(), csL);
                                uint32_t targetRace = static_cast<uint32_t>(extraCopy.raceId);
                                uint32_t targetSex = static_cast<uint32_t>(extraCopy.sexId);

                                for (uint32_t r = 0; r < csDbc->getRecordCount(); r++) {
                                    uint32_t raceId = csDbc->getUInt32(r, csF.raceId);
                                    uint32_t sexId = csDbc->getUInt32(r, csF.sexId);
                                    if (raceId != targetRace || sexId != targetSex) continue;
                                    uint32_t section = csDbc->getUInt32(r, csF.baseSection);
                                    if (section != 3) continue;
                                    uint32_t variation = csDbc->getUInt32(r, csF.variationIndex);
                                    uint32_t colorIdx = csDbc->getUInt32(r, csF.colorIndex);
                                    if (variation != static_cast<uint32_t>(extraCopy.hairStyleId)) continue;
                                    if (colorIdx != static_cast<uint32_t>(extraCopy.hairColorId)) continue;
                                    def.hairTexturePath = csDbc->getString(r, csF.texture1);
                                    break;
                                }

                                if (!def.hairTexturePath.empty()) {
                                    allPaths.push_back(def.hairTexturePath);
                                } else if (def.hasBakedSkin && !def.hairTextureSlots.empty()) {
                                    def.useBakedForHair = true;
                                    // bakedSkinPath already in allPaths
                                }
                            }
                        }

                        // --- Pre-decode all BLP textures on this background thread ---
                        for (const auto& path : allPaths) {
                            std::string key = path;
                            std::replace(key.begin(), key.end(), '/', '\\');
                            std::transform(key.begin(), key.end(), key.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (result.predecodedTextures.count(key)) continue;
                            auto blp = am->loadTexture(key);
                            if (blp.isValid()) {
                                result.predecodedTextures[key] = std::move(blp);
                            }
                        }

                        return result;
                    });
                asyncNpcCompositeLoads_.push_back(std::move(load));
                hasHumanoidTexture = true;  // skip non-humanoid skin block
            } else {
                LOG_WARNING("  extraDisplayId ", dispData.extraDisplayId, " not found in humanoidExtraMap");
            }
        }

        // Apply creature skin textures (for non-humanoid creatures)
        if (!hasHumanoidTexture && modelData) {
            auto resolveCreatureSkinPath = [&](const std::string& skinField) -> std::string {
                if (skinField.empty()) return "";

                std::string raw = skinField;
                std::replace(raw.begin(), raw.end(), '/', '\\');
                auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
                raw.erase(raw.begin(), std::find_if(raw.begin(), raw.end(), [&](unsigned char c) { return !isSpace(c); }));
                raw.erase(std::find_if(raw.rbegin(), raw.rend(), [&](unsigned char c) { return !isSpace(c); }).base(), raw.end());
                if (raw.empty()) return "";

                auto hasBlpExt = [](const std::string& p) {
                    if (p.size() < 4) return false;
                    std::string ext = p.substr(p.size() - 4);
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    return ext == ".blp";
                };
                auto addCandidate = [](std::vector<std::string>& out, const std::string& p) {
                    if (p.empty()) return;
                    if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
                };

                std::vector<std::string> candidates;
                const bool hasDir = (raw.find('\\') != std::string::npos || raw.find('/') != std::string::npos);
                const bool hasExt = hasBlpExt(raw);

                if (hasDir) {
                    addCandidate(candidates, raw);
                    if (!hasExt) addCandidate(candidates, raw + ".blp");
                } else {
                    addCandidate(candidates, modelDir + raw);
                    if (!hasExt) addCandidate(candidates, modelDir + raw + ".blp");
                    addCandidate(candidates, raw);
                    if (!hasExt) addCandidate(candidates, raw + ".blp");
                }

                for (const auto& c : candidates) {
                    if (assetManager_->fileExists(c)) return c;
                }
                return "";
            };

            for (size_t ti = 0; ti < modelData->textures.size(); ti++) {
                const auto& tex = modelData->textures[ti];
                std::string skinPath;

                // Creature skin types: 11 = skin1, 12 = skin2, 13 = skin3
                if (tex.type == 11 && !dispData.skin1.empty()) {
                    skinPath = resolveCreatureSkinPath(dispData.skin1);
                } else if (tex.type == 12 && !dispData.skin2.empty()) {
                    skinPath = resolveCreatureSkinPath(dispData.skin2);
                } else if (tex.type == 13 && !dispData.skin3.empty()) {
                    skinPath = resolveCreatureSkinPath(dispData.skin3);
                }

                if (!skinPath.empty()) {
                    rendering::VkTexture* skinTex = charRenderer->loadTexture(skinPath);
                    if (skinTex) {
                        charRenderer->setModelTexture(modelId, static_cast<uint32_t>(ti), skinTex);
                        LOG_DEBUG("Applied creature skin texture: ", skinPath, " to slot ", ti);
                    }
                } else if ((tex.type == 11 && !dispData.skin1.empty()) ||
                           (tex.type == 12 && !dispData.skin2.empty()) ||
                           (tex.type == 13 && !dispData.skin3.empty())) {
                    LOG_WARNING("Creature skin texture not found for displayId ", displayId,
                                " slot ", ti, " type ", tex.type,
                                " (skin fields: '", dispData.skin1, "', '",
                                dispData.skin2, "', '", dispData.skin3, "')");
                }
            }
        }

        // Clear pre-decoded cache after applying all display textures
        charRenderer->setPredecodedBLPCache(nullptr);
        displayIdPredecodedTextures_.erase(displayId);
        {
            auto texEnd = std::chrono::steady_clock::now();
            float texMs = std::chrono::duration<float, std::milli>(texEnd - texStart).count();
            if (texMs > 50.0f) {
                LOG_WARNING("spawnCreature texture setup took ", texMs, "ms displayId=", displayId,
                            " hasPreDec=", hasPreDec, " extra=", dispData.extraDisplayId);
            }
        }
    }

    // Use the entity's latest server-authoritative position rather than the stale spawn
    // position. Movement packets (SMSG_MONSTER_MOVE) can arrive while a creature is still
    // queued in pendingCreatureSpawns_ and get silently dropped. getLatestX/Y/Z returns
    // the movement destination if the entity is mid-move, which is always up-to-date
    // regardless of distance culling (unlike getX/Y/Z which requires updateMovement).
    if (gameHandler_) {
        if (auto entity = gameHandler_->getEntityManager().getEntity(guid)) {
            x = entity->getLatestX();
            y = entity->getLatestY();
            z = entity->getLatestZ();
            orientation = entity->getOrientation();
        }
    }

    // Convert canonical → render coordinates
    glm::vec3 renderPos = core::coords::canonicalToRender(glm::vec3(x, y, z));

    // Keep authoritative server Z for online creature spawns.
    // Terrain-based lifting can incorrectly move elevated NPCs (e.g. flight masters on
    // Stormwind ramparts) to bad heights relative to WMO geometry.

    // Convert canonical WoW orientation (0=north) -> render yaw (0=west)
    float renderYaw = orientation + glm::radians(90.0f);

    // Create instance (apply server-provided scale from OBJECT_FIELD_SCALE_X)
    uint32_t instanceId = charRenderer->createInstance(modelId, renderPos,
        glm::vec3(0.0f, 0.0f, renderYaw), scale);

    if (instanceId == 0) {
        LOG_WARNING("Failed to create creature instance for guid 0x", std::hex, guid, std::dec);
        return;
    }

    // Per-instance hair/skin texture overrides — runs for ALL NPCs (including cached models)
    // so that each NPC gets its own hair/skin color regardless of model sharing.
    // Uses pre-built CharSections cache (O(1) lookup instead of O(N) DBC scan).
    {
        if (!charSectionsCacheBuilt_) buildCharSectionsCache();
        auto itDD = displayDataMap_.find(displayId);
        if (itDD != displayDataMap_.end() && itDD->second.extraDisplayId != 0) {
            auto itExtra2 = humanoidExtraMap_.find(itDD->second.extraDisplayId);
            if (itExtra2 != humanoidExtraMap_.end()) {
                const auto& extra = itExtra2->second;
                const auto* md = charRenderer->getModelData(modelId);
                if (md) {
                        // Look up hair texture (section 3) via cache
                        rendering::VkTexture* whiteTex = charRenderer->loadTexture("");
                        std::string hairPath = lookupCharSection(
                            extra.raceId, extra.sexId, 3, extra.hairStyleId, extra.hairColorId, 0);
                        if (!hairPath.empty()) {
                            rendering::VkTexture* hairTex = charRenderer->loadTexture(hairPath);
                            if (hairTex && hairTex != whiteTex) {
                                for (size_t ti = 0; ti < md->textures.size(); ti++) {
                                    if (md->textures[ti].type == 6) {
                                        charRenderer->setTextureSlotOverride(instanceId, static_cast<uint16_t>(ti), hairTex);
                                    }
                                }
                            }
                        }

                        // Look up skin texture (section 0) for per-instance skin color.
                        // Skip when the NPC has a baked texture or composited equipment —
                        // those already encode armor over skin and must not be replaced.
                        bool hasEquipOrBake = !extra.bakeName.empty();
                        if (!hasEquipOrBake) {
                            for (int s = 0; s < 11 && !hasEquipOrBake; s++)
                                if (extra.equipDisplayId[s] != 0) hasEquipOrBake = true;
                        }
                        if (!hasEquipOrBake) {
                            std::string skinPath = lookupCharSection(
                                extra.raceId, extra.sexId, 0, 0, extra.skinId, 0);
                            if (!skinPath.empty()) {
                                rendering::VkTexture* skinTex = charRenderer->loadTexture(skinPath);
                                if (skinTex) {
                                    for (size_t ti = 0; ti < md->textures.size(); ti++) {
                                        uint32_t tt = md->textures[ti].type;
                                        if (tt == 1 || tt == 11) {
                                            charRenderer->setTextureSlotOverride(instanceId, static_cast<uint16_t>(ti), skinTex);
                                        }
                                    }
                                }
                            }
                        }
                }
            }
        }
    }

    // Optional humanoid NPC geoset mask. Disabled by default because forcing geosets
    // causes long-standing visual artifacts on some models (missing waist, phantom
    // bracers, flickering apron overlays). Prefer model defaults.
    static constexpr bool kEnableNpcSafeGeosetMask = false;
    if (kEnableNpcSafeGeosetMask &&
        itDisplayData != displayDataMap_.end() &&
        itDisplayData->second.extraDisplayId != 0) {
        auto itExtra = humanoidExtraMap_.find(itDisplayData->second.extraDisplayId);
        if (itExtra != humanoidExtraMap_.end()) {
            const auto& extra = itExtra->second;
            std::unordered_set<uint16_t> safeGeosets;
            std::unordered_set<uint16_t> modelGeosets;
            std::unordered_map<uint16_t, uint16_t> firstGeosetByGroup;
            if (const auto* md = charRenderer->getModelData(modelId)) {
                for (const auto& b : md->batches) {
                    const uint16_t sid = b.submeshId;
                    modelGeosets.insert(sid);
                    const uint16_t group = static_cast<uint16_t>(sid / 100);
                    auto it = firstGeosetByGroup.find(group);
                    if (it == firstGeosetByGroup.end() || sid < it->second) {
                        firstGeosetByGroup[group] = sid;
                    }
                }
            }
            auto addSafeGeoset = [&](uint16_t preferredId) {
                if (preferredId < 100 || modelGeosets.empty()) {
                    safeGeosets.insert(preferredId);
                    return;
                }
                if (modelGeosets.count(preferredId) > 0) {
                    safeGeosets.insert(preferredId);
                    return;
                }
                const uint16_t group = static_cast<uint16_t>(preferredId / 100);
                auto it = firstGeosetByGroup.find(group);
                if (it != firstGeosetByGroup.end()) {
                    safeGeosets.insert(it->second);
                }
            };
            uint16_t hairGeoset = 1;
            uint32_t hairKey = (static_cast<uint32_t>(extra.raceId) << 16) |
                               (static_cast<uint32_t>(extra.sexId) << 8) |
                               static_cast<uint32_t>(extra.hairStyleId);
            auto itHairGeo = hairGeosetMap_.find(hairKey);
            if (itHairGeo != hairGeosetMap_.end() && itHairGeo->second > 0) {
                hairGeoset = itHairGeo->second;
            }
            const uint16_t selectedHairScalp = (hairGeoset > 0 ? hairGeoset : 1);
            std::unordered_set<uint16_t> hairScalpGeosetsForRaceSex;
            for (const auto& [k, v] : hairGeosetMap_) {
                uint8_t race = static_cast<uint8_t>((k >> 16) & 0xFF);
                uint8_t sex = static_cast<uint8_t>((k >> 8) & 0xFF);
                if (race == extra.raceId && sex == extra.sexId && v > 0 && v < 100) {
                    hairScalpGeosetsForRaceSex.insert(v);
                }
            }
            // Group 0 contains both base body parts and race/sex hair scalp variants.
            // Keep all non-hair body submeshes, but only the selected hair scalp.
            for (uint16_t sid : modelGeosets) {
                if (sid >= 100) continue;
                if (hairScalpGeosetsForRaceSex.count(sid) > 0 && sid != selectedHairScalp) continue;
                safeGeosets.insert(sid);
            }
            safeGeosets.insert(selectedHairScalp);
            addSafeGeoset(static_cast<uint16_t>(100 + std::max<uint16_t>(hairGeoset, 1)));

            uint32_t facialKey = (static_cast<uint32_t>(extra.raceId) << 16) |
                                 (static_cast<uint32_t>(extra.sexId) << 8) |
                                 static_cast<uint32_t>(extra.facialHairId);
            auto itFacial = facialHairGeosetMap_.find(facialKey);
            if (itFacial != facialHairGeosetMap_.end()) {
                const auto& fhg = itFacial->second;
                addSafeGeoset(static_cast<uint16_t>(200 + fhg.geoset200));
                addSafeGeoset(static_cast<uint16_t>(300 + fhg.geoset300));
            } else {
                addSafeGeoset(201);
                addSafeGeoset(301);
            }

            // Force pants (1301) and avoid robe skirt variants unless we re-enable full slot-accurate geosets.
            addSafeGeoset(301);
            addSafeGeoset(kGeosetBareForearms);
            addSafeGeoset(402);
            addSafeGeoset(501);
            addSafeGeoset(701);
            addSafeGeoset(kGeosetBareSleeves);
            addSafeGeoset(901);
            addSafeGeoset(kGeosetDefaultTabard);
            addSafeGeoset(kGeosetBarePants);
            addSafeGeoset(kGeosetBareFeet);

            charRenderer->setActiveGeosets(instanceId, safeGeosets);
        }
    }

    // NOTE: Custom humanoid NPC geoset/equipment overrides are currently too
    // aggressive and can make NPCs invisible (targetable but not rendered).
    // Keep default model geosets for online creatures until this path is made
    // data-accurate per display model.
    static constexpr bool kEnableNpcHumanoidOverrides = false;

    // Set geosets for humanoid NPCs based on CreatureDisplayInfoExtra
    if (kEnableNpcHumanoidOverrides &&
        itDisplayData != displayDataMap_.end() &&
        itDisplayData->second.extraDisplayId != 0) {
        auto itExtra = humanoidExtraMap_.find(itDisplayData->second.extraDisplayId);
        if (itExtra != humanoidExtraMap_.end()) {
            const auto& extra = itExtra->second;
            std::unordered_set<uint16_t> activeGeosets;

            // Group 0: body base (id=0 always) + hair scalp mesh from CharHairGeosets.dbc
            activeGeosets.insert(0);  // Body base mesh

            // Hair: CharHairGeosets.dbc maps (race, sex, hairStyleId) → group 0 scalp submeshId
            uint32_t hairKey = (static_cast<uint32_t>(extra.raceId) << 16) |
                               (static_cast<uint32_t>(extra.sexId) << 8) |
                               static_cast<uint32_t>(extra.hairStyleId);
            auto itHairGeo = hairGeosetMap_.find(hairKey);
            uint16_t hairScalpId = (itHairGeo != hairGeosetMap_.end()) ? itHairGeo->second : 0;
            if (hairScalpId > 0) {
                activeGeosets.insert(hairScalpId);                        // Group 0 scalp/hair mesh
                activeGeosets.insert(static_cast<uint16_t>(100 + hairScalpId)); // Group 1 connector (if exists)
            } else {
                // Bald (geosetId=0): body base has a hole at the crown, so include
                // submeshId=1 (bald scalp cap with body skin texture) to cover it.
                activeGeosets.insert(1);    // Group 0 bald scalp mesh
                activeGeosets.insert(kGeosetDefaultConnector);  // Group 1 connector
            }
            uint16_t hairGeoset = (hairScalpId > 0) ? hairScalpId : 1;

            // Facial hair geosets from CharFacialHairStyles.dbc lookup
            uint32_t facialKey = (static_cast<uint32_t>(extra.raceId) << 16) |
                                 (static_cast<uint32_t>(extra.sexId) << 8) |
                                 static_cast<uint32_t>(extra.facialHairId);
            auto itFacial = facialHairGeosetMap_.find(facialKey);
            if (itFacial != facialHairGeosetMap_.end()) {
                const auto& fhg = itFacial->second;
                // DBC values are variation indices within each group; add group base
                activeGeosets.insert(static_cast<uint16_t>(100 + fhg.geoset100));
                activeGeosets.insert(static_cast<uint16_t>(300 + fhg.geoset300));
                activeGeosets.insert(static_cast<uint16_t>(200 + fhg.geoset200));
            } else {
                activeGeosets.insert(kGeosetDefaultConnector); // Default group 1: no extra
                activeGeosets.insert(201); // Default group 2: no facial hair
                activeGeosets.insert(301); // Default group 3: no facial hair
            }

            // Default equipment geosets (bare/no armor)
            // CharGeosets: group 4=gloves(forearm), 5=boots(shin), 8=sleeves, 12=tabard, 13=pants
            std::unordered_set<uint16_t> modelGeosets;
            std::unordered_map<uint16_t, uint16_t> firstByGroup;
            if (const auto* md = charRenderer->getModelData(modelId)) {
                for (const auto& b : md->batches) {
                    const uint16_t sid = b.submeshId;
                    modelGeosets.insert(sid);
                    const uint16_t group = static_cast<uint16_t>(sid / 100);
                    auto it = firstByGroup.find(group);
                    if (it == firstByGroup.end() || sid < it->second) {
                        firstByGroup[group] = sid;
                    }
                }
            }
            auto pickGeoset = [&](uint16_t preferred, uint16_t group) -> uint16_t {
                if (preferred != 0 && modelGeosets.count(preferred) > 0) return preferred;
                auto it = firstByGroup.find(group);
                if (it != firstByGroup.end()) return it->second;
                return preferred;
            };

            uint16_t geosetGloves = pickGeoset(kGeosetBareForearms, 4);
            uint16_t geosetBoots = pickGeoset(kGeosetBareShins, 5);
            uint16_t geosetSleeves = pickGeoset(kGeosetBareSleeves, 8);
            uint16_t geosetPants = pickGeoset(kGeosetBarePants, 13);
            uint16_t geosetCape = 0;       // Group 15 disabled unless cape is equipped
            uint16_t geosetTabard = pickGeoset(kGeosetDefaultTabard, 12);
            uint16_t geosetBelt = 0;       // Group 18 disabled unless belt is equipped
            rendering::VkTexture* npcCapeTextureId = nullptr;

            // Load equipment geosets from ItemDisplayInfo.dbc
            // DBC columns: 7=GeosetGroup[0], 8=GeosetGroup[1], 9=GeosetGroup[2]
            auto itemDisplayDbc = assetManager_->loadDBC("ItemDisplayInfo.dbc");
            const auto* idiL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;
            if (itemDisplayDbc) {
                // Equipment slots: 0=helm, 1=shoulder, 2=shirt, 3=chest, 4=belt, 5=legs, 6=feet, 7=wrist, 8=hands, 9=tabard, 10=cape
                const uint32_t fGG1 = idiL ? (*idiL)["GeosetGroup1"] : 7;

                auto readGeosetGroup = [&](int slot, const char* slotName) -> uint32_t {
                    uint32_t did = extra.equipDisplayId[slot];
                    if (did == 0) return 0;
                    int32_t idx = itemDisplayDbc->findRecordById(did);
                    if (idx < 0) {
                        LOG_DEBUG("NPC equip slot ", slotName, " displayId=", did, " NOT FOUND in ItemDisplayInfo.dbc");
                        return 0;
                    }
                    uint32_t gg = itemDisplayDbc->getUInt32(static_cast<uint32_t>(idx), fGG1);
                    LOG_DEBUG("NPC equip slot ", slotName, " displayId=", did, " GeosetGroup1=", gg);
                    return gg;
                };

                // Chest (slot 3) → group 8 (sleeves/wristbands)
                {
                    uint32_t gg = readGeosetGroup(3, "chest");
                    if (gg > 0) geosetSleeves = pickGeoset(static_cast<uint16_t>(kGeosetBareSleeves + gg), 8);
                }

                // Legs (slot 5) → group 13 (trousers)
                {
                    uint32_t gg = readGeosetGroup(5, "legs");
                    if (gg > 0) geosetPants = pickGeoset(static_cast<uint16_t>(kGeosetBarePants + gg), 13);
                }

                // Feet (slot 6) → group 5 (boots/shins)
                {
                    uint32_t gg = readGeosetGroup(6, "feet");
                    if (gg > 0) geosetBoots = pickGeoset(static_cast<uint16_t>(501 + gg), 5);
                }

                // Hands (slot 8) → group 4 (gloves/forearms)
                {
                    uint32_t gg = readGeosetGroup(8, "hands");
                    if (gg > 0) geosetGloves = pickGeoset(static_cast<uint16_t>(kGeosetBareForearms + gg), 4);
                }

                // Wrists (slot 7) → group 8 (sleeves, only if chest didn't set it)
                {
                    uint32_t gg = readGeosetGroup(7, "wrist");
                    if (gg > 0 && geosetSleeves == pickGeoset(kGeosetBareSleeves, 8))
                        geosetSleeves = pickGeoset(static_cast<uint16_t>(kGeosetBareSleeves + gg), 8);
                }

                // Belt (slot 4) → group 18 (buckle)
                {
                    uint32_t gg = readGeosetGroup(4, "belt");
                    if (gg > 0) geosetBelt = static_cast<uint16_t>(1801 + gg);
                }

                // Tabard (slot 9) → group 12 (tabard/robe mesh)
                {
                    uint32_t gg = readGeosetGroup(9, "tabard");
                    if (gg > 0) geosetTabard = pickGeoset(static_cast<uint16_t>(1200 + gg), 12);
                }

                // Cape (slot 10) → group 15
                if (extra.equipDisplayId[10] != 0) {
                    int32_t idx = itemDisplayDbc->findRecordById(extra.equipDisplayId[10]);
                    if (idx >= 0) {
                        geosetCape = kGeosetWithCape;
                        const bool npcIsFemale = (extra.sexId == 1);
                        const uint32_t leftTexField = idiL ? (*idiL)["LeftModelTexture"] : 3u;
                        std::vector<std::string> capeNames;
                        auto addName = [&](const std::string& n) {
                            if (!n.empty() && std::find(capeNames.begin(), capeNames.end(), n) == capeNames.end()) {
                                capeNames.push_back(n);
                            }
                        };
                        std::string leftName = itemDisplayDbc->getString(static_cast<uint32_t>(idx), leftTexField);
                        addName(leftName);

                        auto hasBlpExt = [](const std::string& p) {
                            if (p.size() < 4) return false;
                            std::string ext = p.substr(p.size() - 4);
                            std::transform(ext.begin(), ext.end(), ext.begin(),
                                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            return ext == ".blp";
                        };
                        std::vector<std::string> capeCandidates;
                        auto addCapeCandidate = [&](const std::string& p) {
                            if (p.empty()) return;
                            if (std::find(capeCandidates.begin(), capeCandidates.end(), p) == capeCandidates.end()) {
                                capeCandidates.push_back(p);
                            }
                        };
                        for (const auto& nameRaw : capeNames) {
                            std::string name = nameRaw;
                            std::replace(name.begin(), name.end(), '/', '\\');
                            const bool hasDir = (name.find('\\') != std::string::npos);
                            const bool hasExt = hasBlpExt(name);
                            if (hasDir) {
                                if (hasExt) addCapeCandidate(name);
                                else addCapeCandidate(name + ".blp");
                            } else {
                                std::string baseObj = "Item\\ObjectComponents\\Cape\\" + name;
                                std::string baseTex = "Item\\TextureComponents\\Cape\\" + name;
                                if (hasExt) {
                                    addCapeCandidate(baseObj);
                                    addCapeCandidate(baseTex);
                                } else {
                                    addCapeCandidate(baseObj + ".blp");
                                    addCapeCandidate(baseTex + ".blp");
                                }
                                addCapeCandidate(baseObj + (npcIsFemale ? "_F.blp" : "_M.blp"));
                                addCapeCandidate(baseObj + "_U.blp");
                                addCapeCandidate(baseTex + (npcIsFemale ? "_F.blp" : "_M.blp"));
                                addCapeCandidate(baseTex + "_U.blp");
                            }
                        }
                        const rendering::VkTexture* whiteTex = charRenderer->loadTexture("");
                        for (const auto& candidate : capeCandidates) {
                            rendering::VkTexture* tex = charRenderer->loadTexture(candidate);
                            if (tex && tex != whiteTex) {
                                npcCapeTextureId = tex;
                                break;
                            }
                        }
                    }
                }
            }

            // Apply equipment geosets
            activeGeosets.insert(geosetGloves);
            activeGeosets.insert(geosetBoots);
            activeGeosets.insert(geosetSleeves);
            activeGeosets.insert(geosetPants);
            if (geosetCape != 0) {
                activeGeosets.insert(geosetCape);
            }
            if (geosetTabard != 0) {
                activeGeosets.insert(geosetTabard);
            }
            if (geosetBelt != 0) {
                activeGeosets.insert(geosetBelt);
            }
            activeGeosets.insert(pickGeoset(kGeosetDefaultEars, 7));
            activeGeosets.insert(pickGeoset(kGeosetDefaultKneepads, 9));
            activeGeosets.insert(pickGeoset(kGeosetBareFeet, 20));
            // Keep all model-present torso variants active to avoid missing male
            // abdomen/waist sections when a single 5xx pick is wrong.
            for (uint16_t sid : modelGeosets) {
                if ((sid / 100) == 5) activeGeosets.insert(sid);
            }
            // Keep all model-present pelvis variants active to avoid missing waist/belt
            // sections on some humanoid males when a single 9xx variant is wrong.
            for (uint16_t sid : modelGeosets) {
                if ((sid / 100) == 9) activeGeosets.insert(sid);
            }

            // Hide hair under helmets: replace style-specific scalp with bald scalp
            if (extra.equipDisplayId[0] != 0 && hairGeoset > 1 &&
                core::helmHidesHair(*assetManager_, extra.equipDisplayId[0], extra.sexId)) {
                activeGeosets.erase(hairGeoset);                              // Remove style scalp
                activeGeosets.erase(static_cast<uint16_t>(100 + hairGeoset)); // Remove style group 1
                activeGeosets.insert(1);    // Bald scalp cap (group 0)
                activeGeosets.insert(kGeosetDefaultConnector);  // Default group 1 connector
            }

            charRenderer->setActiveGeosets(instanceId, activeGeosets);
            if (geosetCape != 0 && npcCapeTextureId) {
                charRenderer->setGroupTextureOverride(instanceId, 15, npcCapeTextureId);
                if (const auto* md = charRenderer->getModelData(modelId)) {
                    for (size_t ti = 0; ti < md->textures.size(); ti++) {
                        if (md->textures[ti].type == 2) {
                            charRenderer->setTextureSlotOverride(instanceId, static_cast<uint16_t>(ti), npcCapeTextureId);
                        }
                    }
                }
            }
            LOG_DEBUG("Set humanoid geosets: hair=", static_cast<int>(hairGeoset),
                      " sleeves=", geosetSleeves, " pants=", geosetPants,
                      " boots=", geosetBoots, " gloves=", geosetGloves);

            // NOTE: NPC helmet attachment with fallback logic to use bone 0 if attachment
            // point 11 is missing. This improves compatibility with models that don't have
            // attachment 11 explicitly defined.
            static constexpr bool kEnableNpcHelmetAttachmentsMainPath = true;
            // Load and attach helmet model if equipped
            if (kEnableNpcHelmetAttachmentsMainPath && extra.equipDisplayId[0] != 0) {
                const core::HelmVisual helm = core::resolveHelmVisual(
                    *assetManager_, extra.equipDisplayId[0], extra.raceId, extra.sexId);
                if (helm.valid()) {
                    std::vector<std::string> helmCandidates;
                    if (!helm.racialModelPath.empty()) helmCandidates.push_back(helm.racialModelPath);
                    helmCandidates.push_back(helm.baseModelPath);

                    auto loaded = getOrLoadAttachmentModel(helmCandidates, helm.texturePath);
                    if (loaded.modelId != 0) {
                        // Attachment 11 is the helm. This attached at 0 — the shield
                        // mount — which succeeded, so the fallback to 11 never ran and
                        // every NPC wore their helmet on the forearm.
                        const bool attached = charRenderer->attachWeapon(
                            instanceId, kAttachHelm, *loaded.model, loaded.modelId,
                            helm.texturePath);
                        if (attached) {
                            LOG_DEBUG("Attached helmet model: ", loaded.model->name,
                                      " tex: ", helm.texturePath);
                        }
                    }
                }
            }

            // NPC shoulder attachment: slot 1 = shoulder in the NPC equipment array.
            // Shoulders have TWO M2 models (left + right) at attachment points 5 and 6.
            if (extra.equipDisplayId[1] != 0) {
                int32_t shoulderIdx = itemDisplayDbc->findRecordById(extra.equipDisplayId[1]);
                if (shoulderIdx >= 0) {
                    const uint32_t leftModelField = idiL ? (*idiL)["LeftModel"] : 1u;
                    const uint32_t rightModelField = idiL ? (*idiL)["RightModel"] : 2u;
                    const uint32_t leftTexFieldS = idiL ? (*idiL)["LeftModelTexture"] : 3u;
                    const uint32_t rightTexFieldS = idiL ? (*idiL)["RightModelTexture"] : 4u;

                    static const std::unordered_map<uint8_t, std::string> shoulderRacePrefix = {
                        {1, "Hu"}, {2, "Or"}, {3, "Dw"}, {4, "Ni"}, {5, "Sc"},
                        {6, "Ta"}, {7, "Gn"}, {8, "Tr"}, {10, "Be"}, {11, "Dr"}
                    };
                    std::string genderSuffix = (extra.sexId == 0) ? "M" : "F";
                    std::string raceSuffix;
                    {
                        auto itRace = shoulderRacePrefix.find(extra.raceId);
                        if (itRace != shoulderRacePrefix.end()) {
                            raceSuffix = "_" + itRace->second + genderSuffix;
                        }
                    }

                    // Left shoulder (attachment point 5) using LeftModel
                    std::string leftModelName = itemDisplayDbc->getString(static_cast<uint32_t>(shoulderIdx), leftModelField);
                    if (!leftModelName.empty()) {
                        size_t dotPos = leftModelName.rfind('.');
                        if (dotPos != std::string::npos) leftModelName = leftModelName.substr(0, dotPos);

                        std::vector<std::string> leftCandidates;
                        if (!raceSuffix.empty()) {
                            leftCandidates.push_back("Item\\ObjectComponents\\Shoulder\\" + leftModelName + raceSuffix + ".m2");
                        }
                        leftCandidates.push_back("Item\\ObjectComponents\\Shoulder\\" + leftModelName + ".m2");

                        // Texture first: the cached model id is keyed by (geometry, texture).
                        std::string leftTexName = itemDisplayDbc->getString(static_cast<uint32_t>(shoulderIdx), leftTexFieldS);
                        std::string leftTexPath;
                        if (!leftTexName.empty()) {
                            if (!raceSuffix.empty()) {
                                std::string suffixedTex = "Item\\ObjectComponents\\Shoulder\\" + leftTexName + raceSuffix + ".blp";
                                if (assetManager_->fileExists(suffixedTex)) leftTexPath = suffixedTex;
                            }
                            if (leftTexPath.empty()) {
                                leftTexPath = "Item\\ObjectComponents\\Shoulder\\" + leftTexName + ".blp";
                            }
                        }

                        auto leftAtt = getOrLoadAttachmentModel(leftCandidates, leftTexPath);
                        if (leftAtt.modelId != 0) {
                            const auto& leftModel = *leftAtt.model;
                            {
                                bool attached = charRenderer->attachWeapon(instanceId, 5, leftModel, leftAtt.modelId, leftTexPath);
                                if (attached) {
                                    LOG_DEBUG("NPC attached left shoulder: ", leftModel.name, " tex: ", leftTexPath);
                                }
                            }
                        }
                    }

                    // Right shoulder (attachment point 6) using RightModel
                    std::string rightModelName = itemDisplayDbc->getString(static_cast<uint32_t>(shoulderIdx), rightModelField);
                    if (!rightModelName.empty()) {
                        size_t dotPos = rightModelName.rfind('.');
                        if (dotPos != std::string::npos) rightModelName = rightModelName.substr(0, dotPos);

                        std::vector<std::string> rightCandidates;
                        if (!raceSuffix.empty()) {
                            rightCandidates.push_back("Item\\ObjectComponents\\Shoulder\\" + rightModelName + raceSuffix + ".m2");
                        }
                        rightCandidates.push_back("Item\\ObjectComponents\\Shoulder\\" + rightModelName + ".m2");

                        // Texture first: the cached model id is keyed by (geometry, texture).
                        std::string rightTexName = itemDisplayDbc->getString(static_cast<uint32_t>(shoulderIdx), rightTexFieldS);
                        std::string rightTexPath;
                        if (!rightTexName.empty()) {
                            if (!raceSuffix.empty()) {
                                std::string suffixedTex = "Item\\ObjectComponents\\Shoulder\\" + rightTexName + raceSuffix + ".blp";
                                if (assetManager_->fileExists(suffixedTex)) rightTexPath = suffixedTex;
                            }
                            if (rightTexPath.empty()) {
                                rightTexPath = "Item\\ObjectComponents\\Shoulder\\" + rightTexName + ".blp";
                            }
                        }

                        auto rightAtt = getOrLoadAttachmentModel(rightCandidates, rightTexPath);
                        if (rightAtt.modelId != 0) {
                            const auto& rightModel = *rightAtt.model;
                            {
                                bool attached = charRenderer->attachWeapon(instanceId, 6, rightModel, rightAtt.modelId, rightTexPath);
                                if (attached) {
                                    LOG_DEBUG("NPC attached right shoulder: ", rightModel.name, " tex: ", rightTexPath);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // With full humanoid overrides disabled, some character-style NPC models still render
    // conflicting clothing geosets at once (global capes, robe skirts over trousers).
    // Normalize only clothing groups while leaving all other model batches untouched.
    if (const auto* md = charRenderer->getModelData(modelId)) {
        std::unordered_set<uint16_t> allGeosets;
        std::unordered_map<uint16_t, uint16_t> firstByGroup;
        bool hasGroup3 = false;  // glove/forearm variants
        bool hasGroup4 = false;  // glove/forearm variants (some models)
        bool hasGroup8 = false;  // sleeve/wrist variants
        bool hasGroup12 = false; // tabard variants
        bool hasGroup13 = false; // trousers/robe skirt variants
        bool hasGroup15 = false; // cloak variants
        for (const auto& b : md->batches) {
            const uint16_t sid = b.submeshId;
            const uint16_t group = static_cast<uint16_t>(sid / 100);
            allGeosets.insert(sid);
            auto itFirst = firstByGroup.find(group);
            if (itFirst == firstByGroup.end() || sid < itFirst->second) {
                firstByGroup[group] = sid;
            }
            if (group == 3) hasGroup3 = true;
            if (group == 4) hasGroup4 = true;
            if (group == 8) hasGroup8 = true;
            if (group == 12) hasGroup12 = true;
            if (group == 13) hasGroup13 = true;
            if (group == 15) hasGroup15 = true;
        }

        // These numeric submesh groups only mean clothing on player-character
        // models. Creature models reuse the same IDs for unrelated authored
        // geometry (elementals use them for their built-in wrist pieces), so a
        // group-number heuristic alone can manufacture a second floating set of
        // "bracers". CreatureDisplayInfoExtra is the authoritative indication
        // that this display uses humanoid equipment geosets.
        const bool hasHumanoidDisplayExtra =
            itDisplayData != displayDataMap_.end() &&
            itDisplayData->second.extraDisplayId != 0 &&
            humanoidExtraMap_.find(itDisplayData->second.extraDisplayId) != humanoidExtraMap_.end();
        if (hasHumanoidDisplayExtra &&
            (hasGroup3 || hasGroup4 || hasGroup8 || hasGroup12 || hasGroup13 || hasGroup15)) {
            bool hasRenderableCape = false;
            std::string capeTexturePath;  // first found cape texture for override
            bool hasEquippedTabard = false;
            bool hasHumanoidExtra = false;
            uint8_t extraRaceId = 0;
            uint8_t extraSexId = 0;
            uint16_t selectedHairScalp = 1;
            uint16_t selectedFacial100 = 100;
            uint16_t selectedFacial200 = 200;
            uint16_t selectedFacial300 = 300;
            uint32_t equipChestGG = 0, equipLegsGG = 0, equipFeetGG = 0;
            if (itDisplayData != displayDataMap_.end() &&
                itDisplayData->second.extraDisplayId != 0) {
                auto itExtra = humanoidExtraMap_.find(itDisplayData->second.extraDisplayId);
                if (itExtra != humanoidExtraMap_.end()) {
                    hasHumanoidExtra = true;
                    extraRaceId = itExtra->second.raceId;
                    extraSexId = itExtra->second.sexId;
                    hasEquippedTabard = (itExtra->second.equipDisplayId[9] != 0);
                    uint32_t hairKey = (static_cast<uint32_t>(extraRaceId) << 16) |
                                       (static_cast<uint32_t>(extraSexId) << 8) |
                                       static_cast<uint32_t>(itExtra->second.hairStyleId);
                    auto itHairGeo = hairGeosetMap_.find(hairKey);
                    if (itHairGeo != hairGeosetMap_.end() && itHairGeo->second > 0) {
                        selectedHairScalp = itHairGeo->second;
                    }
                    uint32_t facialKey = (static_cast<uint32_t>(extraRaceId) << 16) |
                                         (static_cast<uint32_t>(extraSexId) << 8) |
                                         static_cast<uint32_t>(itExtra->second.facialHairId);
                    auto itFacial = facialHairGeosetMap_.find(facialKey);
                    if (itFacial != facialHairGeosetMap_.end()) {
                        selectedFacial100 = static_cast<uint16_t>(100 + itFacial->second.geoset100);
                        selectedFacial200 = static_cast<uint16_t>(200 + itFacial->second.geoset200);
                        selectedFacial300 = static_cast<uint16_t>(300 + itFacial->second.geoset300);
                    }
                    auto itemDisplayDbc = assetManager_->loadDBC("ItemDisplayInfo.dbc");
                    const auto* idiL = pipeline::getActiveDBCLayout()
                        ? pipeline::getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;

                    uint32_t capeDisplayId = itExtra->second.equipDisplayId[10];
                    if (capeDisplayId != 0 && itemDisplayDbc) {
                            int32_t recIdx = itemDisplayDbc->findRecordById(capeDisplayId);
                            if (recIdx >= 0) {
                                const uint32_t leftTexField = idiL ? (*idiL)["LeftModelTexture"] : 3u;
                                const uint32_t rightTexField = idiL ? (*idiL)["RightModelTexture"] : 4u;
                                std::vector<std::string> capeNames;
                                auto addName = [&](const std::string& n) {
                                    if (!n.empty() &&
                                        std::find(capeNames.begin(), capeNames.end(), n) == capeNames.end()) {
                                        capeNames.push_back(n);
                                    }
                                };
                                addName(itemDisplayDbc->getString(static_cast<uint32_t>(recIdx), leftTexField));
                                addName(itemDisplayDbc->getString(static_cast<uint32_t>(recIdx), rightTexField));

                                auto hasBlpExt = [](const std::string& p) {
                                    if (p.size() < 4) return false;
                                    std::string ext = p.substr(p.size() - 4);
                                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                    return ext == ".blp";
                                };

                                const bool npcIsFemale = (itExtra->second.sexId == 1);
                                std::vector<std::string> candidates;
                                auto addCandidate = [&](const std::string& p) {
                                    if (p.empty()) return;
                                    if (std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
                                        candidates.push_back(p);
                                    }
                                };

                                for (const auto& raw : capeNames) {
                                    std::string name = raw;
                                    std::replace(name.begin(), name.end(), '/', '\\');
                                    const bool hasDir = (name.find('\\') != std::string::npos);
                                    const bool hasExt = hasBlpExt(name);
                                    if (hasDir) {
                                        if (hasExt) addCandidate(name);
                                        else addCandidate(name + ".blp");
                                    } else {
                                        std::string baseObj = "Item\\ObjectComponents\\Cape\\" + name;
                                        std::string baseTex = "Item\\TextureComponents\\Cape\\" + name;
                                        if (hasExt) {
                                            addCandidate(baseObj);
                                            addCandidate(baseTex);
                                        } else {
                                            addCandidate(baseObj + ".blp");
                                            addCandidate(baseTex + ".blp");
                                        }
                                        addCandidate(baseObj + (npcIsFemale ? "_F.blp" : "_M.blp"));
                                        addCandidate(baseObj + "_U.blp");
                                        addCandidate(baseTex + (npcIsFemale ? "_F.blp" : "_M.blp"));
                                        addCandidate(baseTex + "_U.blp");
                                    }
                                }

                                for (const auto& p : candidates) {
                                    if (assetManager_->fileExists(p)) {
                                        hasRenderableCape = true;
                                        capeTexturePath = p;
                                        break;
                                    }
                                }
                            }
                    }

                    // Read GeosetGroup1 from equipment to drive clothed mesh selection
                    if (itemDisplayDbc) {
                        const uint32_t fGG1 = idiL ? (*idiL)["GeosetGroup1"] : 7;
                        auto readGG = [&](uint32_t did) -> uint32_t {
                            if (did == 0) return 0;
                            int32_t idx = itemDisplayDbc->findRecordById(did);
                            return (idx >= 0) ? itemDisplayDbc->getUInt32(static_cast<uint32_t>(idx), fGG1) : 0;
                        };
                        equipChestGG = readGG(itExtra->second.equipDisplayId[3]);
                        if (equipChestGG == 0) equipChestGG = readGG(itExtra->second.equipDisplayId[2]); // shirt fallback
                        equipLegsGG = readGG(itExtra->second.equipDisplayId[5]);
                        equipFeetGG = readGG(itExtra->second.equipDisplayId[6]);
                    }
                }
            }

            std::unordered_set<uint16_t> normalizedGeosets;
            for (uint16_t sid : allGeosets) {
                const uint16_t group = static_cast<uint16_t>(sid / 100);
                if (group == 3 || group == 4 || group == 8 || group == 12 || group == 13 || group == 15) continue;
                // Group 17 = eye glow (DK/Night Elf "shining eyes" overlay), group 18 = related
                // glow geosets. NPCs are never DK/NE players opting into eye glow, so strip
                // these groups so creatures don't get unwanted glowing blue night-elf eyes.
                if (group == 17 || group == 18) continue;
                // Some humanoid models carry cloak cloth in group 16. Strip this too
                // when no cape is equipped to avoid "everyone has a cape".
                if (!hasRenderableCape && group == 16) continue;
                // Group 0 can contain multiple scalp/hair meshes. Keep only the selected
                // race/sex/style scalp to avoid overlapping broken hair.
                if (hasHumanoidExtra && sid < 100 && sid != 0 && sid != selectedHairScalp) {
                    continue;
                }
                // Group 1 is the first CharacterFacialHairStyles channel.
                if (hasHumanoidExtra && group == 1) {
                    uint16_t resolvedFacial100 = selectedFacial100;
                    if (allGeosets.count(resolvedFacial100) == 0)
                        resolvedFacial100 = allGeosets.count(101) > 0 ? 101 : firstByGroup[1];
                    if (sid != resolvedFacial100) continue;
                }
                // Group 2 facial variants: keep selected variant; fallback only if missing.
                if (hasHumanoidExtra && group == 2) {
                    uint16_t resolvedFacial200 = selectedFacial200;
                    if (allGeosets.count(resolvedFacial200) == 0) {
                        if (allGeosets.count(201) > 0) resolvedFacial200 = 201;
                        else if (allGeosets.count(200) > 0) resolvedFacial200 = 200;
                        else {
                            auto itFirst = firstByGroup.find(2);
                            resolvedFacial200 = (itFirst != firstByGroup.end()) ? itFirst->second : 0;
                        }
                    }
                    if (sid != resolvedFacial200) continue;
                }
                normalizedGeosets.insert(sid);
            }

            auto pickFromGroup = [&](uint16_t preferredSid, uint16_t group) -> uint16_t {
                if (allGeosets.count(preferredSid) > 0) return preferredSid;
                auto it = firstByGroup.find(group);
                if (it != firstByGroup.end()) return it->second;
                return 0;
            };

            // Intentionally do not add group 3 (glove/forearm accessory meshes).
            // Even "bare" variants can produce unwanted looped arm geometry on NPCs.

            if (hasGroup4) {
                uint16_t wantBoots = (equipFeetGG > 0) ? static_cast<uint16_t>(400 + equipFeetGG) : kGeosetBareForearms;
                uint16_t bootsSid = pickFromGroup(wantBoots, 4);
                if (bootsSid != 0) normalizedGeosets.insert(bootsSid);
            }

            // Add sleeve/wrist meshes when chest armor calls for them.
            if (hasGroup8 && equipChestGG > 0) {
                uint16_t wantSleeves = static_cast<uint16_t>(800 + equipChestGG);
                uint16_t sleeveSid = pickFromGroup(wantSleeves, 8);
                if (sleeveSid != 0) normalizedGeosets.insert(sleeveSid);
            }

            // Show tabard mesh only when CreatureDisplayInfoExtra equips one.
            if (hasGroup12 && hasEquippedTabard) {
                uint16_t wantTabard = kGeosetDefaultTabard;  // Default fallback

                // Try to read tabard geoset variant from ItemDisplayInfo.dbc (slot 9)
                if (hasHumanoidExtra && itDisplayData != displayDataMap_.end() &&
                    itDisplayData->second.extraDisplayId != 0) {
                    auto itExtra = humanoidExtraMap_.find(itDisplayData->second.extraDisplayId);
                    if (itExtra != humanoidExtraMap_.end()) {
                        uint32_t tabardDisplayId = itExtra->second.equipDisplayId[9];
                        if (tabardDisplayId != 0) {
                            auto itemDisplayDbc = assetManager_->loadDBC("ItemDisplayInfo.dbc");
                            const auto* idiL = pipeline::getActiveDBCLayout()
                                ? pipeline::getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;
                            if (itemDisplayDbc && idiL) {
                                int32_t tabardIdx = itemDisplayDbc->findRecordById(tabardDisplayId);
                                if (tabardIdx >= 0) {
                                    // Get geoset variant from ItemDisplayInfo GeosetGroup1 field
                                    const uint32_t ggField = (*idiL)["GeosetGroup1"];
                                    uint32_t tabardGG = itemDisplayDbc->getUInt32(static_cast<uint32_t>(tabardIdx), ggField);
                                    if (tabardGG > 0) {
                                        wantTabard = static_cast<uint16_t>(1200 + tabardGG);
                                    }
                                }
                            }
                        }
                    }
                }

                uint16_t tabardSid = pickFromGroup(wantTabard, 12);
                if (tabardSid != 0) normalizedGeosets.insert(tabardSid);
            }

            // Some mustache/goatee variants are authored in facial group 3xx.
            // Re-add selected facial 3xx plus low-index facial fallbacks.
            if (hasHumanoidExtra) {
                uint16_t facial300Sid = pickFromGroup(selectedFacial300, 3);
                if (facial300Sid != 0) normalizedGeosets.insert(facial300Sid);
                if (facial300Sid == 0) {
                    if (allGeosets.count(300) > 0) normalizedGeosets.insert(300);
                    else if (allGeosets.count(301) > 0) normalizedGeosets.insert(301);
                }
            }

            // Night Elf NPC eyes require the model's eye overlay. Continue to
            // strip it from other humanoids, but restore exactly one variant
            // for the race that actually uses it.
            if (hasHumanoidExtra && extraRaceId == 4) {
                uint16_t eyeGlowSid = pickFromGroup(1701, 17);
                if (eyeGlowSid != 0) normalizedGeosets.insert(eyeGlowSid);
            }

            // Prefer trousers geoset; use covered variant when legs armor exists.
            if (hasGroup13) {
                uint16_t wantPants = (equipLegsGG > 0) ? static_cast<uint16_t>(1300 + equipLegsGG) : kGeosetBarePants;
                uint16_t pantsSid = pickFromGroup(wantPants, 13);
                if (pantsSid != 0) normalizedGeosets.insert(pantsSid);
            }

            // Group 15: cloak mesh. Use "with cape" when equipped, otherwise
            // use "no cape" back panel to cover the single-sided torso.
            if (hasGroup15) {
                if (hasRenderableCape) {
                    uint16_t capeSid = pickFromGroup(kGeosetWithCape, 15);
                    if (capeSid != 0) normalizedGeosets.insert(capeSid);
                } else {
                    uint16_t noCape = pickFromGroup(kGeosetNoCape, 15);
                    if (noCape != 0) normalizedGeosets.insert(noCape);
                }
            }

            if (!normalizedGeosets.empty()) {
                charRenderer->setActiveGeosets(instanceId, normalizedGeosets);
            }

            // Apply cape texture override so the cloak mesh shows the actual cape
            // instead of the default body texture.
            if (hasRenderableCape && !capeTexturePath.empty()) {
                rendering::VkTexture* capeTex = charRenderer->loadTexture(capeTexturePath);
                const rendering::VkTexture* whiteTex = charRenderer->loadTexture("");
                if (capeTex && capeTex != whiteTex) {
                    charRenderer->setGroupTextureOverride(instanceId, 15, capeTex);
                    if (const auto* md2 = charRenderer->getModelData(modelId)) {
                        for (size_t ti = 0; ti < md2->textures.size(); ti++) {
                            if (md2->textures[ti].type == 2) {
                                charRenderer->setTextureSlotOverride(instanceId, static_cast<uint16_t>(ti), capeTex);
                            }
                        }
                    }
                }
            }
        }
    }

    // Try attaching NPC held weapons; if update fields are not ready yet,
    // IN_GAME retry loop will attempt again shortly.
    bool weaponsAttachedNow = tryAttachCreatureVirtualWeapons(guid, instanceId);

    // Spawn in the correct pose. If the server marked this creature dead before
    // the queued spawn was processed, start directly in death animation.
    if (deadCreatureGuids_.count(guid)) {
        charRenderer->playAnimation(instanceId, rendering::anim::DEATH, false);
    } else {
        // Check if this NPC has a persistent emote state (e.g. working, eating, dancing)
        uint32_t npcEmote = 0;
        if (gameHandler_) {
            auto entity = gameHandler_->getEntityManager().getEntity(guid);
            if (entity && entity->getType() == game::ObjectType::UNIT) {
                npcEmote = std::static_pointer_cast<game::Unit>(entity)->getNpcEmoteState();
            }
        }
        uint32_t npcEmoteAnim = npcEmote != 0
            ? rendering::AnimationController::getEmoteAnimByEmotesId(npcEmote)
            : 0;
        if (npcEmoteAnim == 0) {
            auto activeIt = creatureActiveEmotes_.find(guid);
            if (activeIt != creatureActiveEmotes_.end()) {
                npcEmoteAnim = activeIt->second;
            }
        }
        if (npcEmoteAnim != 0) {
            const uint32_t stateAnim =
                rendering::EmoteRegistry::instance().getStateVariant(npcEmoteAnim);
            if (stateAnim != 0 && charRenderer->hasAnimation(instanceId, stateAnim)) {
                npcEmoteAnim = stateAnim;
            }
        }
        if (npcEmoteAnim != 0 && charRenderer->hasAnimation(instanceId, npcEmoteAnim)) {
            creatureActiveEmotes_[guid] = npcEmoteAnim;
            charRenderer->playAnimation(instanceId, npcEmoteAnim, true);
        } else if (charRenderer->hasAnimation(instanceId, rendering::anim::BIRTH)) {
            // Play birth animation (one-shot) — will return to STAND after
            charRenderer->playAnimation(instanceId, rendering::anim::BIRTH, false);
        } else if (charRenderer->hasAnimation(instanceId, rendering::anim::SPAWN)) {
            charRenderer->playAnimation(instanceId, rendering::anim::SPAWN, false);
        } else {
            charRenderer->playAnimation(instanceId, rendering::anim::STAND, true);
        }
    }
    charRenderer->startFadeIn(instanceId, 0.5f);

    // Track instance
    creatureInstances_[guid] = instanceId;
    creatureModelIds_[guid] = modelId;
    creatureDisplayIds_[guid] = displayId;
    creatureRenderPosCache_[guid] = renderPos;
    if (weaponsAttachedNow) {
        creatureWeaponsAttached_.insert(guid);
        creatureWeaponAttachAttempts_.erase(guid);
    } else {
        creatureWeaponsAttached_.erase(guid);
        creatureWeaponAttachAttempts_[guid] = 1;
    }
    LOG_DEBUG("Spawned creature: guid=0x", std::hex, guid, std::dec,
             " displayId=", displayId, " at (", x, ", ", y, ", ", z, ")");
}

} // namespace core
} // namespace wowee
