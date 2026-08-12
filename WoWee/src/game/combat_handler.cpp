#include "game/combat_handler.hpp"
#include "game/game_handler.hpp"
#include "game/game_utils.hpp"
#include "game/packet_parsers.hpp"
#include "game/entity.hpp"
#include "game/update_field_table.hpp"
#include "game/opcode_table.hpp"
#include "rendering/renderer.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/combat_sound_manager.hpp"
#include "audio/activity_sound_manager.hpp"
#include "audio/npc_voice_manager.hpp"
#include "core/application.hpp"
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include "network/world_socket.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <random>

namespace wowee {
namespace game {

CombatHandler::CombatHandler(GameHandler& owner)
    : owner_(owner) {}

void CombatHandler::registerOpcodes(DispatchTable& table) {
    // ---- Combat clearing ----
    table[Opcode::SMSG_ATTACKSWING_DEADTARGET] = [this](network::Packet& /*packet*/) {
        autoAttacking_ = false;
        autoAttackTarget_ = 0;
    };
    table[Opcode::SMSG_THREAT_CLEAR] = [this](network::Packet& /*packet*/) {
        threatLists_.clear();
        if (owner_.addonEventCallbackRef()) owner_.addonEventCallbackRef()("UNIT_THREAT_LIST_UPDATE", {});
    };
    table[Opcode::SMSG_THREAT_REMOVE] = [this](network::Packet& packet) {
        if (!packet.hasRemaining(1)) return;
        uint64_t unitGuid   = packet.readPackedGuid();
        if (!packet.hasRemaining(1)) return;
        uint64_t victimGuid = packet.readPackedGuid();
        auto it = threatLists_.find(unitGuid);
        if (it != threatLists_.end()) {
            auto& list = it->second;
            list.erase(std::remove_if(list.begin(), list.end(),
                [victimGuid](const ThreatEntry& e){ return e.victimGuid == victimGuid; }),
                list.end());
            if (list.empty()) threatLists_.erase(it);
        }
    };
    table[Opcode::SMSG_CANCEL_COMBAT] = [this](network::Packet& /*packet*/) {
        autoAttacking_ = false;
        autoAttackTarget_ = 0;
        autoAttackRequested_ = false;
        autoAttackRetryPending_ = false;
        hostileAttackers_.clear();
    };

    // ---- Attack/combat delegates ----
    table[Opcode::SMSG_ATTACKSTART] = [this](network::Packet& packet) { handleAttackStart(packet); };
    table[Opcode::SMSG_ATTACKSTOP] = [this](network::Packet& packet) { handleAttackStop(packet); };
    table[Opcode::SMSG_ATTACKSWING_NOTINRANGE] = [this](network::Packet& /*packet*/) {
        autoAttackOutOfRange_ = true;
        if (autoAttackRangeWarnCooldown_ <= 0.0f) {
            owner_.addSystemChatMessage("Target is too far away.");
            autoAttackRangeWarnCooldown_ = 1.25f;
        }
    };
    table[Opcode::SMSG_ATTACKSWING_BADFACING] = [this](network::Packet& /*packet*/) {
        if (autoAttackRequested_ && autoAttackTarget_ != 0) {
            auto targetEntity = owner_.getEntityManager().getEntity(autoAttackTarget_);
            if (targetEntity) {
                float toTargetX = targetEntity->getX() - owner_.movementInfoRef().x;
                float toTargetY = targetEntity->getY() - owner_.movementInfoRef().y;
                if (std::abs(toTargetX) > 0.01f || std::abs(toTargetY) > 0.01f) {
                    // The server just told us we are not facing the target, so
                    // correcting only the packet would be undone by the next
                    // frame and it would tell us again.
                    owner_.faceCanonicalYaw(std::atan2(-toTargetY, toTargetX));
                }
            }
        }
    };
    table[Opcode::SMSG_ATTACKSWING_NOTSTANDING] = [this](network::Packet& /*packet*/) {
        autoAttackOutOfRange_ = false;
        autoAttackOutOfRangeTime_ = 0.0f;
        if (autoAttackRangeWarnCooldown_ <= 0.0f) {
            owner_.addSystemChatMessage("You need to stand up to fight.");
            autoAttackRangeWarnCooldown_ = 1.25f;
        }
    };
    table[Opcode::SMSG_ATTACKSWING_CANT_ATTACK] = [this](network::Packet& /*packet*/) {
        stopAutoAttack();
        if (autoAttackRangeWarnCooldown_ <= 0.0f) {
            owner_.addSystemChatMessage("You can't attack that.");
            autoAttackRangeWarnCooldown_ = 1.25f;
        }
    };
    table[Opcode::SMSG_ATTACKERSTATEUPDATE] = [this](network::Packet& packet) { handleAttackerStateUpdate(packet); };
    table[Opcode::SMSG_AI_REACTION] = [this](network::Packet& packet) {
        if (!packet.hasRemaining(12)) return;
        uint64_t guid = packet.readUInt64();
        uint32_t reaction = packet.readUInt32();
        if (reaction == 2 && owner_.npcAggroCallbackRef()) {
            auto entity = owner_.getEntityManager().getEntity(guid);
            if (entity)
                owner_.npcAggroCallbackRef()(guid, glm::vec3(entity->getX(), entity->getY(), entity->getZ()));
        }
    };
    table[Opcode::SMSG_SPELLNONMELEEDAMAGELOG] = [this](network::Packet& packet) { handleSpellDamageLog(packet); };
    table[Opcode::SMSG_SPELLHEALLOG] = [this](network::Packet& packet) { handleSpellHealLog(packet); };

    // ---- Environmental damage ----
    table[Opcode::SMSG_ENVIRONMENTAL_DAMAGE_LOG] = [this](network::Packet& packet) {
        // uint64 victimGuid + uint8 envDmgType + uint32 damage + uint32 absorbed + uint32 resisted
        // envDmgType: 0=Exhausted(fatigue), 1=Drowning, 2=Fall, 3=Lava, 4=Slime, 5=Fire
        if (!packet.hasRemaining(21)) { packet.skipAll(); return; }
        uint64_t victimGuid  = packet.readUInt64();
        uint8_t envType      = packet.readUInt8();
        uint32_t dmg         = packet.readUInt32();
        uint32_t envAbs      = packet.readUInt32();
        uint32_t envRes      = packet.readUInt32();
        if (victimGuid == owner_.getPlayerGuid()) {
            // Environmental damage: pass envType via powerType field for display differentiation
            if (dmg > 0)
                addCombatText(CombatTextEntry::ENVIRONMENTAL, static_cast<int32_t>(dmg), 0, false, envType, 0, victimGuid);
            if (envAbs > 0)
                addCombatText(CombatTextEntry::ABSORB, static_cast<int32_t>(envAbs), 0, false, 0, 0, victimGuid);
            if (envRes > 0)
                addCombatText(CombatTextEntry::RESIST, static_cast<int32_t>(envRes), 0, false, 0, 0, victimGuid);
            // Drowning damage → play DROWN one-shot on player
            if (envType == 1 && dmg > 0 && owner_.emoteAnimCallbackRef())
                owner_.emoteAnimCallbackRef()(victimGuid, 131, /*isState=*/false); // anim::DROWN
        }
        packet.skipAll();
    };

    // ---- Threat updates ----
    for (auto op : {Opcode::SMSG_HIGHEST_THREAT_UPDATE,
                    Opcode::SMSG_THREAT_UPDATE}) {
        table[op] = [this](network::Packet& packet) {
            // Both packets share the same format:
            // packed_guid (unit) + packed_guid (highest-threat target or target, unused here)
            // + uint32 count + count × (packed_guid victim + uint32 threat)
            if (!packet.hasRemaining(1)) return;
            uint64_t unitGuid = packet.readPackedGuid();
            if (!packet.hasRemaining(1)) return;
            (void)packet.readPackedGuid(); // highest-threat / current target
            if (!packet.hasRemaining(4)) return;
            uint32_t cnt = packet.readUInt32();
            if (cnt > 100) { packet.skipAll(); return; } // sanity
            std::vector<ThreatEntry> list;
            list.reserve(cnt);
            for (uint32_t i = 0; i < cnt; ++i) {
                if (!packet.hasRemaining(1)) return;
                ThreatEntry entry;
                entry.victimGuid = packet.readPackedGuid();
                if (!packet.hasRemaining(4)) return;
                entry.threat = packet.readUInt32();
                list.push_back(entry);
            }
            // Sort descending by threat so highest is first
            std::sort(list.begin(), list.end(),
                [](const ThreatEntry& a, const ThreatEntry& b){ return a.threat > b.threat; });
            threatLists_[unitGuid] = std::move(list);
            if (owner_.addonEventCallbackRef())
                owner_.addonEventCallbackRef()("UNIT_THREAT_LIST_UPDATE", {});
        };
    }

    // ---- Forced faction reactions ----
    table[Opcode::SMSG_SET_FORCED_REACTIONS] = [this](network::Packet& packet) { handleSetForcedReactions(packet); };

    // ---- Entity delta updates: health / power / combo / PvP / proc ----
    table[Opcode::SMSG_HEALTH_UPDATE] = [this](network::Packet& p) { handleHealthUpdate(p); };
    table[Opcode::SMSG_POWER_UPDATE] = [this](network::Packet& p) { handlePowerUpdate(p); };
    table[Opcode::SMSG_UPDATE_COMBO_POINTS] = [this](network::Packet& p) { handleUpdateComboPoints(p); };
    table[Opcode::SMSG_PVP_CREDIT] = [this](network::Packet& p) { handlePvpCredit(p); };
    table[Opcode::SMSG_PROCRESIST] = [this](network::Packet& p) { handleProcResist(p); };

    // SMSG_ENVIRONMENTALDAMAGELOG is an alias for SMSG_ENVIRONMENTAL_DAMAGE_LOG
    // (registered above at line 108 with envType forwarding). No separate handler needed.
    table[Opcode::SMSG_SPELLDAMAGESHIELD] = [this](network::Packet& p) { handleSpellDamageShield(p); };
    table[Opcode::SMSG_SPELLORDAMAGE_IMMUNE] = [this](network::Packet& p) { handleSpellOrDamageImmune(p); };
    table[Opcode::SMSG_RESISTLOG] = [this](network::Packet& p) { handleResistLog(p); };

    // ---- Pet feedback ----
    table[Opcode::SMSG_PET_TAME_FAILURE] = [this](network::Packet& p) { handlePetTameFailure(p); };
    table[Opcode::SMSG_PET_ACTION_FEEDBACK] = [this](network::Packet& p) { handlePetActionFeedback(p); };
    table[Opcode::SMSG_PET_CAST_FAILED] = [this](network::Packet& p) { handlePetCastFailed(p); };
    table[Opcode::SMSG_PET_BROKEN] = [this](network::Packet& p) { handlePetBroken(p); };
    table[Opcode::SMSG_PET_LEARNED_SPELL] = [this](network::Packet& p) { handlePetLearnedSpell(p); };
    table[Opcode::SMSG_PET_UNLEARNED_SPELL] = [this](network::Packet& p) { handlePetUnlearnedSpell(p); };
    table[Opcode::SMSG_PET_MODE] = [this](network::Packet& p) { handlePetMode(p); };

    // ---- Resurrect ----
    table[Opcode::SMSG_RESURRECT_FAILED] = [this](network::Packet& p) { handleResurrectFailed(p); };
}

// ============================================================
// Auto-attack
// ============================================================

void CombatHandler::startAutoAttack(uint64_t targetGuid) {
    // Can't attack yourself
    if (targetGuid == owner_.getPlayerGuid()) return;
    if (targetGuid == 0) return;

    // Client-side range gate to avoid starting "swing forever" loops when
    // target is already clearly out of range.
    if (auto target = owner_.getEntityManager().getEntity(targetGuid)) {
        float dx = owner_.movementInfoRef().x - target->getLatestX();
        float dy = owner_.movementInfoRef().y - target->getLatestY();
        float dz = owner_.movementInfoRef().z - target->getLatestZ();
        float dist3d = std::sqrt(dx * dx + dy * dy + dz * dz);
        // Use longer range limit when a ranged weapon is equipped
        const auto& rangedSlot = owner_.getInventory().getEquipSlot(game::EquipSlot::RANGED);
        bool hasRangedWeapon = !rangedSlot.empty() &&
            (rangedSlot.item.inventoryType == game::InvType::RANGED_BOW ||
             rangedSlot.item.inventoryType == game::InvType::RANGED_GUN ||
             rangedSlot.item.inventoryType == game::InvType::THROWN);
        float maxRange = hasRangedWeapon ? 40.0f : 8.0f;
        if (dist3d > maxRange) {
            if (autoAttackRangeWarnCooldown_ <= 0.0f) {
                owner_.addSystemChatMessage("Target is too far away.");
                autoAttackRangeWarnCooldown_ = 1.25f;
            }
            return;
        }
    }

    // Only dismount once this is a valid attack attempt. Doing it before the
    // target/range gate knocked the player off a mount even though no combat
    // request was sent.
    if (owner_.isMounted()) {
        owner_.dismount();
    }

    autoAttackRequested_ = true;
    autoAttackRetryPending_ = true;
    // Keep combat animation/state server-authoritative. We only flip autoAttacking
    // on SMSG_ATTACKSTART where attackerGuid == playerGuid.
    autoAttacking_ = false;
    autoAttackTarget_ = targetGuid;
    autoAttackOutOfRange_ = false;
    autoAttackOutOfRangeTime_ = 0.0f;
    autoAttackResendTimer_ = 0.0f;
    autoAttackFacingSyncTimer_ = 0.0f;
    if (owner_.getState() == WorldState::IN_WORLD && owner_.getSocket()) {
        auto packet = AttackSwingPacket::build(targetGuid);
        owner_.getSocket()->send(packet);
    }
    LOG_INFO("Starting auto-attack on 0x", std::hex, targetGuid, std::dec);
}

void CombatHandler::stopAutoAttack() {
    if (!autoAttacking_ && !autoAttackRequested_) return;
    autoAttackRequested_ = false;
    autoAttacking_ = false;
    autoAttackRetryPending_ = false;
    autoAttackTarget_ = 0;
    autoAttackOutOfRange_ = false;
    autoAttackOutOfRangeTime_ = 0.0f;
    autoAttackResendTimer_ = 0.0f;
    autoAttackFacingSyncTimer_ = 0.0f;
    if (owner_.getState() == WorldState::IN_WORLD && owner_.getSocket()) {
        auto packet = AttackStopPacket::build();
        owner_.getSocket()->send(packet);
    }
    LOG_INFO("Stopping auto-attack");
    if (owner_.addonEventCallbackRef())
        owner_.addonEventCallbackRef()("PLAYER_LEAVE_COMBAT", {});
}

// ============================================================
// Combat text
// ============================================================

void CombatHandler::addCombatText(CombatTextEntry::Type type, int32_t amount, uint32_t spellId, bool isPlayerSource, uint8_t powerType,
                                  uint64_t srcGuid, uint64_t dstGuid) {
    CombatTextEntry entry;
    entry.type = type;
    entry.amount = amount;
    entry.spellId = spellId;
    entry.age = 0.0f;
    entry.isPlayerSource = isPlayerSource;
    entry.powerType = powerType;
    entry.srcGuid = srcGuid;
    entry.dstGuid = dstGuid;
    // Random horizontal stagger so simultaneous hits don't stack vertically
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    entry.xSeed = dist(rng);
    combatText_.push_back(entry);

    // Persistent combat log — use explicit GUIDs if provided, else fall back to
    // player/current-target (the old behaviour for events without specific participants).
    CombatLogEntry log;
    log.type     = type;
    log.amount   = amount;
    log.spellId  = spellId;
    log.isPlayerSource = isPlayerSource;
    log.powerType = powerType;
    log.timestamp = std::time(nullptr);
    // If the caller provided an explicit destination GUID but left source GUID as 0,
    // preserve "unknown/no source" (e.g. environmental damage) instead of
    // backfilling from current target.
    uint64_t effectiveSrc = (srcGuid != 0) ? srcGuid
                          : ((dstGuid != 0) ? 0 : (isPlayerSource ? owner_.getPlayerGuid() : owner_.getTargetGuid()));
    uint64_t effectiveDst = (dstGuid != 0) ? dstGuid
                          : (isPlayerSource ? owner_.getTargetGuid() : owner_.getPlayerGuid());
    log.sourceName = owner_.lookupName(effectiveSrc);
    log.targetName = (effectiveDst != 0) ? owner_.lookupName(effectiveDst) : std::string{};
    if (combatLog_.size() >= MAX_COMBAT_LOG)
        combatLog_.pop_front();
    combatLog_.push_back(std::move(log));

    // Fire COMBAT_LOG_EVENT_UNFILTERED for Lua addons
    // Args: subevent, sourceGUID, sourceName, 0 (sourceFlags), destGUID, destName, 0 (destFlags), spellId, spellName, amount
    if (owner_.addonEventCallbackRef()) {
        static const char* kSubevents[] = {
            "SWING_DAMAGE", "SPELL_DAMAGE", "SPELL_HEAL", "SWING_MISSED", "SWING_MISSED",
            "SWING_MISSED", "SWING_MISSED", "SWING_MISSED", "SPELL_DAMAGE", "SPELL_HEAL",
            "SPELL_PERIODIC_DAMAGE", "SPELL_PERIODIC_HEAL", "ENVIRONMENTAL_DAMAGE",
            "SPELL_ENERGIZE", "SPELL_DRAIN", "PARTY_KILL", "SPELL_MISSED", "SPELL_ABSORBED",
            "SPELL_MISSED", "SPELL_MISSED", "SPELL_MISSED", "SPELL_AURA_APPLIED",
            "SPELL_DISPEL", "SPELL_STOLEN", "SPELL_INTERRUPT", "SPELL_INSTAKILL",
            "PARTY_KILL", "SWING_DAMAGE", "SWING_DAMAGE"
        };
        const char* subevent = (type < sizeof(kSubevents)/sizeof(kSubevents[0]))
            ? kSubevents[type] : "UNKNOWN";
        char srcBuf[32], dstBuf[32];
        snprintf(srcBuf, sizeof(srcBuf), "0x%016llX", (unsigned long long)effectiveSrc);
        snprintf(dstBuf, sizeof(dstBuf), "0x%016llX", (unsigned long long)effectiveDst);
        std::string spellName = (spellId != 0) ? owner_.getSpellName(spellId) : std::string{};
        std::string timestamp = std::to_string(static_cast<double>(std::time(nullptr)));
        owner_.addonEventCallbackRef()("COMBAT_LOG_EVENT_UNFILTERED", {
            timestamp, subevent,
            srcBuf, log.sourceName, "0",
            dstBuf, log.targetName, "0",
            std::to_string(spellId), spellName,
            std::to_string(amount)
        });
    }
}

bool CombatHandler::shouldLogSpellstealAura(uint64_t casterGuid, uint64_t victimGuid, uint32_t spellId) {
    if (spellId == 0) return false;

    const auto now = std::chrono::steady_clock::now();
    constexpr auto kRecentWindow = std::chrono::seconds(1);
    while (!recentSpellstealLogs_.empty() &&
           now - recentSpellstealLogs_.front().timestamp > kRecentWindow) {
        recentSpellstealLogs_.pop_front();
    }

    for (auto it = recentSpellstealLogs_.begin(); it != recentSpellstealLogs_.end(); ++it) {
        if (it->casterGuid == casterGuid &&
            it->victimGuid == victimGuid &&
            it->spellId == spellId) {
            recentSpellstealLogs_.erase(it);
            return false;
        }
    }

    if (recentSpellstealLogs_.size() >= MAX_RECENT_SPELLSTEAL_LOGS)
        recentSpellstealLogs_.pop_front();
    recentSpellstealLogs_.push_back({casterGuid, victimGuid, spellId, now});
    return true;
}

void CombatHandler::updateCombatText(float deltaTime) {
    for (auto& entry : combatText_) {
        entry.age += deltaTime;
    }
    combatText_.erase(
        std::remove_if(combatText_.begin(), combatText_.end(),
                       [](const CombatTextEntry& e) { return e.isExpired(); }),
        combatText_.end());
}

// ============================================================
// Packet handlers
// ============================================================

void CombatHandler::autoTargetAttacker(uint64_t attackerGuid) {
    if (attackerGuid == 0 || attackerGuid == owner_.getPlayerGuid()) return;
    if (owner_.getTargetGuid() != 0) return;
    if (!owner_.getEntityManager().hasEntity(attackerGuid)) return;
    owner_.setTarget(attackerGuid);
}

void CombatHandler::handleAttackStart(network::Packet& packet) {
    AttackStartData data;
    if (!AttackStartParser::parse(packet, data)) return;

    // CREEP is a client presentation flag, not an instruction to keep a unit
    // translucent after it has openly engaged. Some legacy realms do not send
    // a matching UNIT_FIELD_BYTES_1 update when an NPC breaks stealth, but the
    // stock client reveals both combatants at ATTACKSTART. Clear the cached
    // presentation state here so the normal creature visual sync restores full
    // opacity instead of leaving an invisible monster in melee.
    auto revealCombatant = [this](uint64_t guid) {
        auto entity = owner_.getEntityManager().getEntity(guid);
        if (!entity || !entity->isUnit()) return;
        auto unit = std::static_pointer_cast<Unit>(entity);
        if (!unit->hasCreepVisibility()) return;
        unit->clearCreepVisibility();
        if (guid == owner_.getPlayerGuid() && owner_.stealthStateCallbackRef()) {
            owner_.stealthStateCallbackRef()(false);
        }
    };
    revealCombatant(data.attackerGuid);
    revealCombatant(data.victimGuid);

    if (data.attackerGuid == owner_.getPlayerGuid()) {
        autoAttackRequested_ = true;
        autoAttacking_ = true;
        autoAttackRetryPending_ = false;
        autoAttackTarget_ = data.victimGuid;
        if (owner_.addonEventCallbackRef())
            owner_.addonEventCallbackRef()("PLAYER_ENTER_COMBAT", {});
    } else if (data.victimGuid == owner_.getPlayerGuid() && data.attackerGuid != 0) {
        hostileAttackers_.insert(data.attackerGuid);
        autoTargetAttacker(data.attackerGuid);

        // Play aggro sound when NPC attacks player
        if (owner_.npcAggroCallbackRef()) {
            auto entity = owner_.getEntityManager().getEntity(data.attackerGuid);
            if (entity && entity->getType() == ObjectType::UNIT) {
                glm::vec3 pos(entity->getX(), entity->getY(), entity->getZ());
                owner_.npcAggroCallbackRef()(data.attackerGuid, pos);
            }
        }
    }

    // Force both participants to face each other at combat start.
    // Uses atan2(-dy, dx): canonical orientation convention where the West/Y
    // component is negated (renderYaw = orientation + 90°, model-forward = render+X).
    auto attackerEnt = owner_.getEntityManager().getEntity(data.attackerGuid);
    auto victimEnt   = owner_.getEntityManager().getEntity(data.victimGuid);
    if (attackerEnt && victimEnt) {
        float dx = victimEnt->getX() - attackerEnt->getX();
        float dy = victimEnt->getY() - attackerEnt->getY();
        if (std::abs(dx) > 0.01f || std::abs(dy) > 0.01f) {
            attackerEnt->setOrientation(std::atan2(-dy,  dx));   // attacker → victim
            victimEnt->setOrientation  (std::atan2( dy, -dx));   // victim   → attacker
        }
    }
}

void CombatHandler::handleAttackStop(network::Packet& packet) {
    AttackStopData data;
    if (!AttackStopParser::parse(packet, data)) return;

    // Keep intent, but clear server-confirmed active state until ATTACKSTART resumes.
    if (data.attackerGuid == owner_.getPlayerGuid()) {
        autoAttacking_ = false;
        autoAttackRetryPending_ = autoAttackRequested_;
        autoAttackResendTimer_ = 0.0f;
        LOG_DEBUG("SMSG_ATTACKSTOP received (keeping auto-attack intent)");
    } else if (data.victimGuid == owner_.getPlayerGuid()) {
        hostileAttackers_.erase(data.attackerGuid);
    }
}

void CombatHandler::handleAttackerStateUpdate(network::Packet& packet) {
    AttackerStateUpdateData data;
    if (!owner_.getPacketParsers()->parseAttackerStateUpdate(packet, data)) return;

    bool isPlayerAttacker = (data.attackerGuid == owner_.getPlayerGuid());
    bool isPlayerTarget = (data.targetGuid == owner_.getPlayerGuid());
    if (!isPlayerAttacker && !isPlayerTarget) return;  // Not our combat

    if (isPlayerAttacker) {
        lastMeleeSwingMs_ = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        // Skip melee animation if a ranged shot was just triggered from
        // SMSG_SPELL_GO (Auto Shot / Shoot / Throw).  The ranged animation
        // is already playing; firing the melee callback here would override it.
        if (owner_.consumeSuppressMeleeSwingAnim()) {
            LOG_DEBUG("Suppressed melee swing anim — ranged shot already triggered");
        } else {
            if (owner_.meleeSwingCallbackRef()) owner_.meleeSwingCallbackRef()(0);
        }
    }
    if (!isPlayerAttacker && owner_.npcSwingCallbackRef()) {
        owner_.npcSwingCallbackRef()(data.attackerGuid);
    }
    if (!isPlayerAttacker) {
        auto entity = owner_.getEntityManager().getEntity(data.attackerGuid);
        auto* audioCoordinator = owner_.services().audioCoordinator;
        auto* voiceManager = audioCoordinator ? audioCoordinator->getNpcVoiceManager() : nullptr;
        if (entity && entity->getType() == ObjectType::UNIT && voiceManager) {
            auto unit = std::static_pointer_cast<Unit>(entity);
            const glm::vec3 canonical(unit->getLatestX(), unit->getLatestY(), unit->getLatestZ());
            voiceManager->playCombatAttack(
                data.attackerGuid, unit->getDisplayId(), core::coords::canonicalToRender(canonical));
        }
    }

    if (isPlayerTarget && data.attackerGuid != 0) {
        hostileAttackers_.insert(data.attackerGuid);
        autoTargetAttacker(data.attackerGuid);
    }

    // Play combat sounds via CombatSoundManager + character vocalizations
    if (auto* ac = owner_.services().audioCoordinator) {
        if (auto* csm = ac->getCombatSoundManager()) {
            auto weaponSize = audio::CombatSoundManager::WeaponSize::MEDIUM;
            if (data.isMiss()) {
                csm->playWeaponMiss(false);
            } else if (data.victimState == 1 || data.victimState == 2) {
                // Dodge/parry — swing whoosh but no impact
                csm->playWeaponSwing(weaponSize, false);
            } else {
                // Hit — swing + flesh impact
                csm->playWeaponSwing(weaponSize, data.isCrit());
                csm->playImpact(weaponSize, audio::CombatSoundManager::ImpactType::FLESH, data.isCrit());
            }
        }
        // Character vocalizations
        if (auto* asm_ = ac->getActivitySoundManager()) {
            if (isPlayerAttacker && !data.isMiss() && data.victimState != 1 && data.victimState != 2) {
                asm_->playAttackGrunt();
            }
            if (isPlayerTarget && !data.isMiss() && data.victimState != 1 && data.victimState != 2) {
                asm_->playWound(data.isCrit());
            }
        }
    }

    if (data.isMiss()) {
        addCombatText(CombatTextEntry::MISS, 0, 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
    } else if (data.victimState == 1) {
        addCombatText(CombatTextEntry::DODGE, 0, 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
    } else if (data.victimState == 2) {
        addCombatText(CombatTextEntry::PARRY, 0, 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
    } else if (data.victimState == 4) {
        // VICTIMSTATE_BLOCKS: show reduced damage and the blocked amount
        if (data.totalDamage > 0)
            addCombatText(CombatTextEntry::MELEE_DAMAGE, data.totalDamage, 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
        addCombatText(CombatTextEntry::BLOCK, static_cast<int32_t>(data.blocked), 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
    } else if (data.victimState == 5) {
        // VICTIMSTATE_EVADE: NPC evaded (out of combat zone).
        addCombatText(CombatTextEntry::EVADE, 0, 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
    } else if (data.victimState == 6) {
        // VICTIMSTATE_IS_IMMUNE: Target is immune to this attack.
        addCombatText(CombatTextEntry::IMMUNE, 0, 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
    } else if (data.victimState == 7) {
        // VICTIMSTATE_DEFLECT: Attack was deflected (e.g. shield slam reflect).
        addCombatText(CombatTextEntry::DEFLECT, 0, 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
    } else {
        CombatTextEntry::Type type;
        if (data.isCrit())
            type = CombatTextEntry::CRIT_DAMAGE;
        else if (data.isCrushing())
            type = CombatTextEntry::CRUSHING;
        else if (data.isGlancing())
            type = CombatTextEntry::GLANCING;
        else
            type = CombatTextEntry::MELEE_DAMAGE;
        addCombatText(type, data.totalDamage, 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
        // Show partial absorb/resist from sub-damage entries
        uint32_t totalAbsorbed = 0, totalResisted = 0;
        for (const auto& sub : data.subDamages) {
            totalAbsorbed += sub.absorbed;
            totalResisted += sub.resisted;
        }
        if (totalAbsorbed > 0)
            addCombatText(CombatTextEntry::ABSORB, static_cast<int32_t>(totalAbsorbed), 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
        if (totalResisted > 0)
            addCombatText(CombatTextEntry::RESIST, static_cast<int32_t>(totalResisted), 0, isPlayerAttacker, 0, data.attackerGuid, data.targetGuid);
    }

    // Fire hit reaction animation on the victim
    if (owner_.hitReactionCallbackRef() && !data.isMiss()) {
        using HR = GameHandler::HitReaction;
        HR reaction = HR::WOUND;
        if (data.victimState == 1) reaction = HR::DODGE;
        else if (data.victimState == 2) reaction = HR::PARRY;
        else if (data.victimState == 4) reaction = HR::BLOCK;
        else if (data.isCrit()) reaction = HR::CRIT_WOUND;
        owner_.hitReactionCallbackRef()(data.targetGuid, reaction);
    }

}

void CombatHandler::handleSpellDamageLog(network::Packet& packet) {
    SpellDamageLogData data;
    if (!owner_.getPacketParsers()->parseSpellDamageLog(packet, data)) return;

    bool isPlayerSource = (data.attackerGuid == owner_.getPlayerGuid());
    bool isPlayerTarget = (data.targetGuid == owner_.getPlayerGuid());
    if (!isPlayerSource && !isPlayerTarget) return;  // Not our combat

    if (isPlayerTarget && data.attackerGuid != 0) {
        hostileAttackers_.insert(data.attackerGuid);
        autoTargetAttacker(data.attackerGuid);
    }

    auto type = data.isCrit ? CombatTextEntry::CRIT_DAMAGE : CombatTextEntry::SPELL_DAMAGE;
    if (data.damage > 0)
        addCombatText(type, static_cast<int32_t>(data.damage), data.spellId, isPlayerSource, 0, data.attackerGuid, data.targetGuid);
    if (data.absorbed > 0)
        addCombatText(CombatTextEntry::ABSORB, static_cast<int32_t>(data.absorbed), data.spellId, isPlayerSource, 0, data.attackerGuid, data.targetGuid);
    if (data.resisted > 0)
        addCombatText(CombatTextEntry::RESIST, static_cast<int32_t>(data.resisted), data.spellId, isPlayerSource, 0, data.attackerGuid, data.targetGuid);
}

void CombatHandler::handleSpellHealLog(network::Packet& packet) {
    SpellHealLogData data;
    if (!owner_.getPacketParsers()->parseSpellHealLog(packet, data)) return;

    bool isPlayerSource = (data.casterGuid == owner_.getPlayerGuid());
    bool isPlayerTarget = (data.targetGuid == owner_.getPlayerGuid());
    if (!isPlayerSource && !isPlayerTarget) return;  // Not our combat

    auto type = data.isCrit ? CombatTextEntry::CRIT_HEAL : CombatTextEntry::HEAL;
    addCombatText(type, static_cast<int32_t>(data.heal), data.spellId, isPlayerSource, 0, data.casterGuid, data.targetGuid);
    if (data.absorbed > 0)
        addCombatText(CombatTextEntry::ABSORB, static_cast<int32_t>(data.absorbed), data.spellId, isPlayerSource, 0, data.casterGuid, data.targetGuid);
}

void CombatHandler::handleSetForcedReactions(network::Packet& packet) {
    if (!packet.hasRemaining(4)) return;
    uint32_t count = packet.readUInt32();
    if (count > 64) {
        LOG_WARNING("SMSG_SET_FORCED_REACTIONS: suspicious count ", count, ", ignoring");
        packet.skipAll();
        return;
    }
    forcedReactions_.clear();
    for (uint32_t i = 0; i < count; ++i) {
        if (!packet.hasRemaining(8)) break;
        uint32_t factionId = packet.readUInt32();
        uint32_t reaction  = packet.readUInt32();
        forcedReactions_[factionId] = static_cast<uint8_t>(reaction);
    }
    LOG_INFO("SMSG_SET_FORCED_REACTIONS: ", forcedReactions_.size(), " faction overrides");
}

// ============================================================
// Per-frame update
// ============================================================

void CombatHandler::updateAutoAttack(float deltaTime) {
    // Decrement range warn cooldown
    if (autoAttackRangeWarnCooldown_ > 0.0f) {
        autoAttackRangeWarnCooldown_ = std::max(0.0f, autoAttackRangeWarnCooldown_ - deltaTime);
    }

    // Leave combat if auto-attack target is too far away (leash range)
    // and keep melee intent tightly synced while stationary.
    if (autoAttackRequested_ && autoAttackTarget_ != 0) {
        auto targetEntity = owner_.getEntityManager().getEntity(autoAttackTarget_);
        if (targetEntity) {
            const float targetX = targetEntity->getLatestX();
            const float targetY = targetEntity->getLatestY();
            const float targetZ = targetEntity->getLatestZ();
            float dx = owner_.movementInfoRef().x - targetX;
            float dy = owner_.movementInfoRef().y - targetY;
            float dz = owner_.movementInfoRef().z - targetZ;
            float dist = std::sqrt(dx * dx + dy * dy);
            float dist3d = std::sqrt(dx * dx + dy * dy + dz * dz);
            const bool classicLike = isPreWotlk();
            if (dist > 40.0f) {
                stopAutoAttack();
                LOG_INFO("Left combat: target too far (", dist, " yards)");
            } else if (owner_.isInWorld()) {
                bool allowResync = true;
                const float meleeRange = classicLike ? 5.25f : 5.75f;
                if (dist3d > meleeRange) {
                    autoAttackOutOfRange_ = true;
                    autoAttackOutOfRangeTime_ += deltaTime;
                    if (autoAttackRangeWarnCooldown_ <= 0.0f) {
                        owner_.addSystemChatMessage("Target is too far away.");
                        owner_.addUIError("Target is too far away.");
                        autoAttackRangeWarnCooldown_ = 1.25f;
                    }
                    // Stop chasing stale swings when the target remains out of range.
                    if (autoAttackOutOfRangeTime_ > 2.0f && dist3d > 9.0f) {
                        stopAutoAttack();
                        owner_.addSystemChatMessage("Auto-attack stopped: target out of range.");
                        allowResync = false;
                    }
                } else {
                    autoAttackOutOfRange_ = false;
                    autoAttackOutOfRangeTime_ = 0.0f;
                }

                if (allowResync) {
                    autoAttackResendTimer_ += deltaTime;
                    autoAttackFacingSyncTimer_ += deltaTime;

                    // Classic/Turtle servers do not tolerate steady attack-start
                    // reissues well. Only retry once after local start or an
                    // explicit server-side attack stop while intent is still set.
                    const float resendInterval = classicLike ? 1.0f : 0.50f;
                    if (!autoAttacking_ && !autoAttackOutOfRange_ && autoAttackRetryPending_ &&
                        autoAttackResendTimer_ >= resendInterval) {
                        autoAttackResendTimer_ = 0.0f;
                        autoAttackRetryPending_ = false;
                        auto pkt = AttackSwingPacket::build(autoAttackTarget_);
                        owner_.getSocket()->send(pkt);
                    }

                    // Keep server-facing aligned while trying to acquire melee.
                    const float facingSyncInterval = classicLike ? 0.25f : 0.20f;
                    const bool allowPeriodicFacingSync = !classicLike || !autoAttacking_;
                    if (allowPeriodicFacingSync &&
                        autoAttackFacingSyncTimer_ >= facingSyncInterval) {
                        autoAttackFacingSyncTimer_ = 0.0f;
                        float toTargetX = targetX - owner_.movementInfoRef().x;
                        float toTargetY = targetY - owner_.movementInfoRef().y;
                        if (std::abs(toTargetX) > 0.01f || std::abs(toTargetY) > 0.01f) {
                            float desired = std::atan2(-toTargetY, toTargetX);
                            float diff = desired - owner_.movementInfoRef().orientation;
                            while (diff > static_cast<float>(M_PI)) diff -= 2.0f * static_cast<float>(M_PI);
                            while (diff < -static_cast<float>(M_PI)) diff += 2.0f * static_cast<float>(M_PI);
                            const float facingThreshold = classicLike ? 0.035f : 0.12f;
                            if (std::abs(diff) > facingThreshold) {
                                owner_.movementInfoRef().orientation = desired;
                                owner_.sendMovement(Opcode::MSG_MOVE_SET_FACING);
                            }
                        }
                    }
                }
            }
        }
    }

    // Keep active melee attackers visually facing the player as positions change.
    if (!hostileAttackers_.empty()) {
        for (uint64_t attackerGuid : hostileAttackers_) {
            auto attacker = owner_.getEntityManager().getEntity(attackerGuid);
            if (!attacker) continue;
            float dx = owner_.movementInfoRef().x - attacker->getX();
            float dy = owner_.movementInfoRef().y - attacker->getY();
            if (std::abs(dx) < 0.01f && std::abs(dy) < 0.01f) continue;
            attacker->setOrientation(std::atan2(-dy, dx));
        }
    }
}

// ============================================================
// State management
// ============================================================

void CombatHandler::resetAllCombatState() {
    hostileAttackers_.clear();
    combatText_.clear();
    autoAttacking_ = false;
    autoAttackRequested_ = false;
    autoAttackRetryPending_ = false;
    autoAttackTarget_ = 0;
    autoAttackOutOfRange_ = false;
    autoAttackOutOfRangeTime_ = 0.0f;
    autoAttackRangeWarnCooldown_ = 0.0f;
    autoAttackResendTimer_ = 0.0f;
    autoAttackFacingSyncTimer_ = 0.0f;
    lastMeleeSwingMs_ = 0;
}

void CombatHandler::removeHostileAttacker(uint64_t guid) {
    hostileAttackers_.erase(guid);
}

void CombatHandler::clearCombatText() {
    combatText_.clear();
}

void CombatHandler::removeCombatTextForGuid(uint64_t guid) {
    combatText_.erase(
        std::remove_if(combatText_.begin(), combatText_.end(),
            [guid](const CombatTextEntry& e) {
                return e.dstGuid == guid;
            }),
        combatText_.end());
}

// ============================================================
// Moved opcode handlers (from GameHandler::registerOpcodeHandlers)
// ============================================================

void CombatHandler::handleHealthUpdate(network::Packet& packet) {
    const bool huTbc = isActiveExpansion("tbc");
    if (!packet.hasRemaining(huTbc ? 8u : 2u) ) return;
    uint64_t guid = huTbc ? packet.readUInt64() : packet.readPackedGuid();
    if (!packet.hasRemaining(4)) return;
    uint32_t hp = packet.readUInt32();
    if (auto* unit = owner_.getUnitByGuid(guid)) unit->setHealth(hp);
    if (guid != 0) {
        auto unitId = owner_.guidToUnitId(guid);
        if (!unitId.empty()) owner_.fireAddonEvent("UNIT_HEALTH", {unitId});
    }
}

void CombatHandler::handlePowerUpdate(network::Packet& packet) {
    const bool puTbc = isActiveExpansion("tbc");
    if (!packet.hasRemaining(puTbc ? 8u : 2u) ) return;
    uint64_t guid = puTbc ? packet.readUInt64() : packet.readPackedGuid();
    if (!packet.hasRemaining(5)) return;
    uint8_t  powerType = packet.readUInt8();
    uint32_t value     = packet.readUInt32();
    if (auto* unit = owner_.getUnitByGuid(guid)) unit->setPowerByType(powerType, value);
    if (guid != 0) {
        auto unitId = owner_.guidToUnitId(guid);
        if (!unitId.empty()) {
            owner_.fireAddonEvent("UNIT_POWER", {unitId});
            if (guid == owner_.getPlayerGuid()) {
                owner_.fireAddonEvent("ACTIONBAR_UPDATE_USABLE", {});
                owner_.fireAddonEvent("SPELL_UPDATE_USABLE", {});
            }
        }
    }
}

void CombatHandler::handleUpdateComboPoints(network::Packet& packet) {
    const bool cpTbc = isActiveExpansion("tbc");
    if (!packet.hasRemaining(cpTbc ? 8u : 2u) ) return;
    uint64_t target = cpTbc ? packet.readUInt64() : packet.readPackedGuid();
    if (!packet.hasRemaining(1)) return;
    owner_.comboPointsRef() = packet.readUInt8();
    owner_.comboTargetRef() = target;
    LOG_DEBUG("SMSG_UPDATE_COMBO_POINTS: target=0x", std::hex, target,
              std::dec, " points=", static_cast<int>(owner_.comboPointsRef()));
    owner_.fireAddonEvent("PLAYER_COMBO_POINTS", {});
}

void CombatHandler::handlePvpCredit(network::Packet& packet) {
    if (packet.hasRemaining(16)) {
        uint32_t honor      = packet.readUInt32();
        uint64_t victimGuid = packet.readUInt64();
        uint32_t rank       = packet.readUInt32();
        LOG_INFO("SMSG_PVP_CREDIT: honor=", honor, " victim=0x", std::hex, victimGuid, std::dec, " rank=", rank);
        std::string msg = "You gain " + std::to_string(honor) + " honor points.";
        owner_.addSystemChatMessage(msg);
        if (honor > 0) addCombatText(CombatTextEntry::HONOR_GAIN, static_cast<int32_t>(honor), 0, true);
        if (owner_.pvpHonorCallbackRef()) owner_.pvpHonorCallbackRef()(honor, victimGuid, rank);
        owner_.fireAddonEvent("CHAT_MSG_COMBAT_HONOR_GAIN", {msg});
    }
}

void CombatHandler::handleProcResist(network::Packet& packet) {
    const bool prUsesFullGuid = isActiveExpansion("tbc");
    auto readPrGuid = [&]() -> uint64_t {
        if (prUsesFullGuid)
            return (packet.hasRemaining(8)) ? packet.readUInt64() : 0;
        return packet.readPackedGuid();
    };
    if (!packet.hasRemaining(prUsesFullGuid ? 8u : 1u)             || (!prUsesFullGuid && !packet.hasFullPackedGuid())) { packet.skipAll(); return; }
    uint64_t caster = readPrGuid();
    if (!packet.hasRemaining(prUsesFullGuid ? 8u : 1u)             || (!prUsesFullGuid && !packet.hasFullPackedGuid())) { packet.skipAll(); return; }
    uint64_t victim = readPrGuid();
    if (!packet.hasRemaining(4)) return;
    uint32_t spellId = packet.readUInt32();
    if (victim == owner_.getPlayerGuid())       addCombatText(CombatTextEntry::RESIST, 0, spellId, false, 0, caster, victim);
    else if (caster == owner_.getPlayerGuid())  addCombatText(CombatTextEntry::RESIST, 0, spellId, true,  0, caster, victim);
    packet.skipAll();
}

// ============================================================
// Environmental / reflect / immune / resist
// ============================================================


void CombatHandler::handleSpellDamageShield(network::Packet& packet) {
    // Classic: packed_guid victim + packed_guid caster + spellId(4) + damage(4) + schoolMask(4)
    // TBC:     uint64 victim + uint64 caster + spellId(4) + damage(4) + schoolMask(4)
    // WotLK:   packed_guid victim + packed_guid caster + spellId(4) + damage(4) + absorbed(4) + schoolMask(4)
    const bool shieldTbc = isActiveExpansion("tbc");
    const bool shieldWotlkLike = !isClassicLikeExpansion() && !shieldTbc;
    const auto shieldRem = [&]() { return packet.getRemainingSize(); };
    const size_t shieldMinSz = shieldTbc ? 24u : 2u;
    if (!packet.hasRemaining(shieldMinSz)) {
        packet.skipAll(); return;
    }
    if (!shieldTbc && (!packet.hasFullPackedGuid())) {
        packet.skipAll(); return;
    }
    uint64_t victimGuid = shieldTbc
        ? packet.readUInt64() : packet.readPackedGuid();
    if (!packet.hasRemaining(shieldTbc ? 8u : 1u)             || (!shieldTbc && !packet.hasFullPackedGuid())) {
        packet.skipAll(); return;
    }
    uint64_t casterGuid = shieldTbc
        ? packet.readUInt64() : packet.readPackedGuid();
    const size_t shieldTailSize = shieldWotlkLike ? 16u : 12u;
    if (shieldRem() < shieldTailSize) {
        packet.skipAll(); return;
    }
    uint32_t shieldSpellId = packet.readUInt32();
    uint32_t damage        = packet.readUInt32();
    if (shieldWotlkLike)
        /*uint32_t absorbed =*/ packet.readUInt32();
    /*uint32_t school =*/  packet.readUInt32();
    // Show combat text: damage shield reflect
    if (casterGuid == owner_.getPlayerGuid()) {
        // We have a damage shield that reflected damage
        addCombatText(CombatTextEntry::SPELL_DAMAGE, static_cast<int32_t>(damage), shieldSpellId, true, 0, casterGuid, victimGuid);
    } else if (victimGuid == owner_.getPlayerGuid()) {
        // A damage shield hit us (e.g. target's Thorns)
        addCombatText(CombatTextEntry::SPELL_DAMAGE, static_cast<int32_t>(damage), shieldSpellId, false, 0, casterGuid, victimGuid);
    }
}

void CombatHandler::handleSpellOrDamageImmune(network::Packet& packet) {
    // WotLK/Classic/Turtle: packed casterGuid + packed victimGuid + uint32 spellId + uint8 saveType
    // TBC:                  full uint64 casterGuid + full uint64 victimGuid + uint32 + uint8
    const bool immuneUsesFullGuid = isActiveExpansion("tbc");
    const size_t minSz = immuneUsesFullGuid ? 21u : 2u;
    if (!packet.hasRemaining(minSz)) {
        packet.skipAll(); return;
    }
    if (!immuneUsesFullGuid && !packet.hasFullPackedGuid()) {
        packet.skipAll(); return;
    }
    uint64_t casterGuid = immuneUsesFullGuid
        ? packet.readUInt64() : packet.readPackedGuid();
    if (!packet.hasRemaining(immuneUsesFullGuid ? 8u : 2u)             || (!immuneUsesFullGuid && !packet.hasFullPackedGuid())) {
        packet.skipAll(); return;
    }
    uint64_t victimGuid = immuneUsesFullGuid
        ? packet.readUInt64() : packet.readPackedGuid();
    if (!packet.hasRemaining(5)) return;
    uint32_t immuneSpellId = packet.readUInt32();
    /*uint8_t saveType =*/ packet.readUInt8();
    // Show IMMUNE text when the player is the caster (we hit an immune target)
    // or the victim (we are immune)
    if (casterGuid == owner_.getPlayerGuid() || victimGuid == owner_.getPlayerGuid()) {
        addCombatText(CombatTextEntry::IMMUNE, 0, immuneSpellId,
                      casterGuid == owner_.getPlayerGuid(), 0, casterGuid, victimGuid);
    }
}

void CombatHandler::handleResistLog(network::Packet& packet) {
    // WotLK/Classic/Turtle: uint32 hitInfo + packed_guid attacker + packed_guid victim + uint32 spellId
    //                      + float resistFactor + uint32 targetRes + uint32 resistedValue + ...
    // TBC:                 same layout but full uint64 GUIDs
    // Show RESIST combat text when player resists an incoming spell.
    const bool rlUsesFullGuid = isActiveExpansion("tbc");
    auto rl_rem = [&]() { return packet.getRemainingSize(); };
    if (rl_rem() < 4) { packet.skipAll(); return; }
    /*uint32_t hitInfo =*/ packet.readUInt32();
    if (rl_rem() < (rlUsesFullGuid ? 8u : 1u)
        || (!rlUsesFullGuid && !packet.hasFullPackedGuid())) {
        packet.skipAll(); return;
    }
    uint64_t attackerGuid = rlUsesFullGuid
        ? packet.readUInt64() : packet.readPackedGuid();
    if (rl_rem() < (rlUsesFullGuid ? 8u : 1u)
        || (!rlUsesFullGuid && !packet.hasFullPackedGuid())) {
        packet.skipAll(); return;
    }
    uint64_t victimGuid = rlUsesFullGuid
        ? packet.readUInt64() : packet.readPackedGuid();
    if (rl_rem() < 4) { packet.skipAll(); return; }
    uint32_t spellId = packet.readUInt32();
    // Resist payload includes:
    // float resistFactor + uint32 targetResistance + uint32 resistedValue.
    // Require the full payload so truncated packets cannot synthesize
    // zero-value resist events.
    if (rl_rem() < 12) { packet.skipAll(); return; }
    /*float resistFactor =*/ packet.readFloat();
    /*uint32_t targetRes =*/ packet.readUInt32();
    int32_t resistedAmount = static_cast<int32_t>(packet.readUInt32());
    // Show RESIST when the player is involved on either side.
    if (resistedAmount > 0 && victimGuid == owner_.getPlayerGuid()) {
        addCombatText(CombatTextEntry::RESIST, resistedAmount, spellId, false, 0, attackerGuid, victimGuid);
    } else if (resistedAmount > 0 && attackerGuid == owner_.getPlayerGuid()) {
        addCombatText(CombatTextEntry::RESIST, resistedAmount, spellId, true, 0, attackerGuid, victimGuid);
    }
    packet.skipAll();
}

// ============================================================
// Pet feedback
// ============================================================

void CombatHandler::handlePetTameFailure(network::Packet& packet) {
    static const char* reasons[] = {
        "Invalid creature", "Too many pets", "Already tamed",
        "Wrong faction", "Level too low", "Creature not tameable",
        "Can't control", "Can't command"
    };
    if (packet.hasRemaining(1)) {
        uint8_t reason = packet.readUInt8();
        const char* msg = (reason < 8) ? reasons[reason] : "Unknown reason";
        std::string s = std::string("Failed to tame: ") + msg;
        owner_.addUIError(s);
        owner_.addSystemChatMessage(s);
    }
}

void CombatHandler::handlePetActionFeedback(network::Packet& packet) {
    static const char* kPetFeedback[] = {
        nullptr,
        "Your pet is dead.", "Your pet has nothing to attack.",
        "Your pet cannot attack that target.", "That target is too far away.",
        "Your pet cannot find a path to the target.",
        "Your pet cannot attack an immune target.",
    };
    if (!packet.hasRemaining(1)) return;
    uint8_t msg = packet.readUInt8();
    if (msg > 0 && msg < 7 && kPetFeedback[msg]) owner_.addSystemChatMessage(kPetFeedback[msg]);
    packet.skipAll();
}

void CombatHandler::handlePetCastFailed(network::Packet& packet) {
    // WotLK: castCount(1) + spellId(4) + reason(1)
    // Classic/TBC: spellId(4) + reason(1) (no castCount)
    const bool hasCount = isActiveExpansion("wotlk");
    const size_t minSize = hasCount ? 6u : 5u;
    if (packet.hasRemaining(minSize)) {
        if (hasCount) /*uint8_t castCount =*/ packet.readUInt8();
        uint32_t spellId   = packet.readUInt32();
        uint8_t  reason    = (packet.hasRemaining(1))
                                 ? packet.readUInt8() : 0;
        LOG_DEBUG("SMSG_PET_CAST_FAILED: spell=", spellId,
                  " reason=", static_cast<int>(reason));
        if (reason != 0) {
            const char* reasonStr = getSpellCastResultString(reason);
            const std::string& sName = owner_.getSpellName(spellId);
            std::string errMsg;
            if (reasonStr && *reasonStr)
                errMsg = sName.empty() ? reasonStr : (sName + ": " + reasonStr);
            else
                errMsg = sName.empty() ? "Pet spell failed." : (sName + ": Pet spell failed.");
            owner_.addSystemChatMessage(errMsg);
        }
    }
    packet.skipAll();
}

void CombatHandler::handlePetBroken(network::Packet& packet) {
    // Pet bond broken (died or forcibly dismissed) — clear pet state
    owner_.petGuidRef() = 0;
    owner_.petSpellListRef().clear();
    owner_.petAutocastSpellsRef().clear();
    memset(owner_.petActionSlotsRef(), 0, sizeof(owner_.petActionSlotsRef()));
    owner_.addSystemChatMessage("Your pet has died.");
    LOG_INFO("SMSG_PET_BROKEN: pet bond broken");
    packet.skipAll();
}

void CombatHandler::handlePetLearnedSpell(network::Packet& packet) {
    if (packet.hasRemaining(4)) {
        uint32_t spellId = packet.readUInt32();
        owner_.petSpellListRef().push_back(spellId);
        const std::string& sname = owner_.getSpellName(spellId);
        owner_.addSystemChatMessage("Your pet has learned " + (sname.empty() ? "a new ability." : sname + "."));
        LOG_DEBUG("SMSG_PET_LEARNED_SPELL: spellId=", spellId);
        owner_.fireAddonEvent("PET_BAR_UPDATE", {});
    }
    packet.skipAll();
}

void CombatHandler::handlePetUnlearnedSpell(network::Packet& packet) {
    if (packet.hasRemaining(4)) {
        uint32_t spellId = packet.readUInt32();
        owner_.petSpellListRef().erase(
            std::remove(owner_.petSpellListRef().begin(), owner_.petSpellListRef().end(), spellId),
            owner_.petSpellListRef().end());
        owner_.petAutocastSpellsRef().erase(spellId);
        LOG_DEBUG("SMSG_PET_UNLEARNED_SPELL: spellId=", spellId);
    }
    packet.skipAll();
}

void CombatHandler::handlePetMode(network::Packet& packet) {
    // uint64 petGuid, uint32 mode
    // mode bits: low byte = command state, next byte = react state
    if (packet.hasRemaining(12)) {
        uint64_t modeGuid = packet.readUInt64();
        uint32_t mode     = packet.readUInt32();
        if (modeGuid == owner_.petGuidRef()) {
            owner_.petCommandRef() = static_cast<uint8_t>(mode & 0xFF);
            owner_.petReactRef()   = static_cast<uint8_t>((mode >> 8) & 0xFF);
            LOG_DEBUG("SMSG_PET_MODE: command=", static_cast<int>(owner_.petCommandRef()),
                      " react=", static_cast<int>(owner_.petReactRef()));
        }
    }
    packet.skipAll();
}

// ============================================================
// Resurrect
// ============================================================

void CombatHandler::handleResurrectFailed(network::Packet& packet) {
    if (packet.hasRemaining(4)) {
        uint32_t reason = packet.readUInt32();
        const char* msg = (reason == 1) ? "The target cannot be resurrected right now."
                        : (reason == 2) ? "Cannot resurrect in this area."
                        : "Resurrection failed.";
        owner_.addUIError(msg);
        owner_.addSystemChatMessage(msg);
    }
}

// ============================================================
// Targeting
// ============================================================

bool CombatHandler::isSelectableUnit(uint64_t /*guid*/) const {
    // Everything the player clicks is selectable; the server arbitrates whether a
    // corpse actually holds loot when we send CMSG_LOOT. We deliberately do NOT
    // gate dead creature corpses on UNIT_DYNFLAG_LOOTABLE: that bit is computed
    // per-viewer by the server and the client's cached copy goes stale across a
    // death/resurrect cycle (you kill something, die in the same fight, and on
    // return the corpse's loot bit was last evaluated while you were a ghost).
    // Gating on it left a genuinely lootable corpse permanently unclickable.
    // Empty, fully-looted corpses are already removed locally by the
    // lootableCleared -> despawnCreatureLocally path (see
    // EntityController::onValuesUpdateUnit), so they don't return as click targets.
    return true;
}

void CombatHandler::setTarget(uint64_t guid) {
    if (guid == owner_.getTargetGuid()) return;
    // Looted-out, unskinnable corpses cannot be selected.
    if (!isSelectableUnit(guid)) return;

    // Save previous target
    if (owner_.getTargetGuid() != 0) {
        owner_.lastTargetGuidRef() = owner_.getTargetGuid();
    }

    owner_.setTargetGuidRaw(guid);

    // Clear stale aura data from the previous target so the buff bar shows
    // an empty state until the server sends SMSG_AURA_UPDATE_ALL for the new target.
    if (owner_.getSpellHandler()) owner_.getSpellHandler()->clearTargetAuras();

    // Clear previous target's cast bar on target change
    // (the new target's cast state is naturally fetched from spellHandler_->unitCastStates_ by GUID)

    // Inform server of target selection
    if (owner_.isInWorld()) {
        auto packet = SetSelectionPacket::build(guid);
        owner_.getSocket()->send(packet);
    }

    if (guid != 0) {
        LOG_INFO("Target set: 0x", std::hex, guid, std::dec);
    }
    owner_.fireAddonEvent("PLAYER_TARGET_CHANGED", {});
}

void CombatHandler::clearTarget() {
    if (owner_.getTargetGuid() != 0) {
        LOG_INFO("Target cleared");
        // Zero the GUID before firing the event so callbacks/addons that query
        // the current target see null (consistent with setTarget which updates
        // targetGuid before the event).
        owner_.setTargetGuidRaw(0);
        owner_.fireAddonEvent("PLAYER_TARGET_CHANGED", {});
    } else {
        owner_.setTargetGuidRaw(0);
    }
    owner_.tabCycleIndexRef() = -1;
    owner_.tabCycleStaleRef() = true;
}

std::shared_ptr<Entity> CombatHandler::getTarget() const {
    if (owner_.getTargetGuid() == 0) return nullptr;
    return owner_.getEntityManager().getEntity(owner_.getTargetGuid());
}

void CombatHandler::setFocus(uint64_t guid) {
    owner_.focusGuidRef() = guid;
    owner_.fireAddonEvent("PLAYER_FOCUS_CHANGED", {});
    if (guid != 0) {
        auto entity = owner_.getEntityManager().getEntity(guid);
        if (entity) {
            std::string name;
            auto unit = std::dynamic_pointer_cast<Unit>(entity);
            if (unit && !unit->getName().empty()) {
                name = unit->getName();
            }
            if (name.empty()) name = owner_.lookupName(guid);
            if (name.empty()) name = "Unknown";
            owner_.addSystemChatMessage("Focus set: " + name);
            LOG_INFO("Focus set: 0x", std::hex, guid, std::dec);
        }
    }
}

void CombatHandler::clearFocus() {
    if (owner_.focusGuidRef() != 0) {
        owner_.addSystemChatMessage("Focus cleared.");
        LOG_INFO("Focus cleared");
    }
    owner_.focusGuidRef() = 0;
    owner_.fireAddonEvent("PLAYER_FOCUS_CHANGED", {});
}

std::shared_ptr<Entity> CombatHandler::getFocus() const {
    if (owner_.focusGuidRef() == 0) return nullptr;
    return owner_.getEntityManager().getEntity(owner_.focusGuidRef());
}

void CombatHandler::setMouseoverGuid(uint64_t guid) {
    if (owner_.mouseoverGuidRef() != guid) {
        owner_.mouseoverGuidRef() = guid;
        owner_.fireAddonEvent("UPDATE_MOUSEOVER_UNIT", {});
    }
}

void CombatHandler::targetLastTarget() {
    if (owner_.lastTargetGuidRef() == 0) {
        owner_.addSystemChatMessage("No previous target.");
        return;
    }

    // Swap current and last target
    uint64_t temp = owner_.getTargetGuid();
    setTarget(owner_.lastTargetGuidRef());
    owner_.lastTargetGuidRef() = temp;
}

void CombatHandler::targetEnemy(bool reverse) {
    constexpr float kRangeSq = 40.0f * 40.0f;

    // Player position for the range gate and nearest-first ordering
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    if (auto self = owner_.getEntityManager().getEntity(owner_.getPlayerGuid())) {
        px = self->getX(); py = self->getY(); pz = self->getZ();
    }

    // Living hostiles within range, nearest first. Corpses are never
    // /targetenemy candidates — that's what mouse looting is for.
    struct Cand { uint64_t guid; float distSq; };
    std::vector<Cand> cands;
    auto& entities = owner_.getEntityManager().getEntities();
    for (const auto& [guid, entity] : entities) {
        auto t = entity->getType();
        if (t != ObjectType::UNIT && t != ObjectType::PLAYER) continue;
        if (guid == owner_.getPlayerGuid()) continue;
        auto* unit = dynamic_cast<Unit*>(entity.get());
        if (!unit || unit->getHealth() == 0) continue;
        if (!unit->isHostile() && !isAggressiveTowardPlayer(guid)) continue;
        float dx = entity->getX() - px;
        float dy = entity->getY() - py;
        float dz = entity->getZ() - pz;
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > kRangeSq) continue;
        cands.push_back({guid, distSq});
    }
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.distSq < b.distSq; });

    std::vector<uint64_t> hostiles;
    hostiles.reserve(cands.size());
    for (const auto& c : cands) hostiles.push_back(c.guid);

    if (hostiles.empty()) {
        owner_.addSystemChatMessage("No enemies in range.");
        return;
    }

    // Find current target in list
    auto it = std::find(hostiles.begin(), hostiles.end(), owner_.getTargetGuid());

    if (it == hostiles.end()) {
        // Not currently targeting a hostile, target first one
        setTarget(reverse ? hostiles.back() : hostiles.front());
    } else {
        // Cycle to next/previous
        if (reverse) {
            if (it == hostiles.begin()) {
                setTarget(hostiles.back());
            } else {
                setTarget(*(--it));
            }
        } else {
            ++it;
            if (it == hostiles.end()) {
                setTarget(hostiles.front());
            } else {
                setTarget(*it);
            }
        }
    }
}

void CombatHandler::targetFriend(bool reverse) {
    constexpr float kRangeSq = 40.0f * 40.0f;

    float px = 0.0f, py = 0.0f, pz = 0.0f;
    if (auto self = owner_.getEntityManager().getEntity(owner_.getPlayerGuid())) {
        px = self->getX(); py = self->getY(); pz = self->getZ();
    }

    // Friendly players within range, nearest first. Dead friends stay
    // targetable — you need to select them to resurrect them.
    struct Cand { uint64_t guid; float distSq; };
    std::vector<Cand> cands;
    auto& entities = owner_.getEntityManager().getEntities();
    for (const auto& [guid, entity] : entities) {
        if (entity->getType() != ObjectType::PLAYER || guid == owner_.getPlayerGuid()) continue;
        float dx = entity->getX() - px;
        float dy = entity->getY() - py;
        float dz = entity->getZ() - pz;
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > kRangeSq) continue;
        cands.push_back({guid, distSq});
    }
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.distSq < b.distSq; });

    std::vector<uint64_t> friendlies;
    friendlies.reserve(cands.size());
    for (const auto& c : cands) friendlies.push_back(c.guid);

    if (friendlies.empty()) {
        owner_.addSystemChatMessage("No friendly targets in range.");
        return;
    }

    // Find current target in list
    auto it = std::find(friendlies.begin(), friendlies.end(), owner_.getTargetGuid());

    if (it == friendlies.end()) {
        // Not currently targeting a friend, target first one
        setTarget(reverse ? friendlies.back() : friendlies.front());
    } else {
        // Cycle to next/previous
        if (reverse) {
            if (it == friendlies.begin()) {
                setTarget(friendlies.back());
            } else {
                setTarget(*(--it));
            }
        } else {
            ++it;
            if (it == friendlies.end()) {
                setTarget(friendlies.front());
            } else {
                setTarget(*it);
            }
        }
    }
}

void CombatHandler::tabTarget(float playerX, float playerY, float playerZ) {
    // Maximum tab-target range in yards (matches the WoW client).
    constexpr float kTabRange   = 40.0f;
    constexpr float kTabRangeSq = kTabRange * kTabRange;
    // A pause this long between tab presses restarts the cycle from the
    // nearest enemy instead of continuing a stale rotation.
    constexpr uint64_t kCycleResetMs = 4000;

    const bool playerInCombat = owner_.isInCombat();

    // Helper: returns true if the entity is a hostile that can be tab-targeted.
    // Corpses are never tab targets while fighting; out of combat they only
    // qualify when they still hold loot.
    auto isValidTabTarget = [&](const std::shared_ptr<Entity>& e) -> bool {
        if (!e) return false;
        const uint64_t guid = e->getGuid();
        auto* unit = dynamic_cast<Unit*>(e.get());
        if (!unit) return false;
        if (unit->getHealth() == 0) {
            if (playerInCombat) return false;
            auto lootIt = owner_.localLootStateRef().find(guid);
            if (lootIt == owner_.localLootStateRef().end() || lootIt->second.data.items.empty()) {
                return false;
            }
            return true;
        }
        const bool hostileByFaction = unit->isHostile();
        const bool hostileByCombat = isAggressiveTowardPlayer(guid);
        if (!hostileByFaction && !hostileByCombat) return false;
        return true;
    };

    // Restart the cycle after a pause — the player has moved on; the next tab
    // should grab the nearest enemy, not resume an old rotation.
    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    if (nowMs - lastTabTargetMs_ > kCycleResetMs)
        owner_.tabCycleStaleRef() = true;
    lastTabTargetMs_ = nowMs;

    // Rebuild cycle list if stale (entity added/removed since last tab press).
    if (owner_.tabCycleStaleRef()) {
        owner_.tabCycleListRef().clear();
        owner_.tabCycleIndexRef() = -1;

        struct EntityDist { uint64_t guid; float distSq; bool dead; };
        std::vector<EntityDist> sortable;
        const auto& entities = owner_.getEntityManager().getEntities();
        sortable.reserve(entities.size());

        for (const auto& [guid, entity] : entities) {
            auto t = entity->getType();
            if (t != ObjectType::UNIT && t != ObjectType::PLAYER) continue;
            if (guid == owner_.getPlayerGuid()) continue;
            if (!isValidTabTarget(entity)) continue;
            float dx = entity->getX() - playerX;
            float dy = entity->getY() - playerY;
            float dz = entity->getZ() - playerZ;
            // Sort by squared distance — monotonic with distance, skips sqrt per entity.
            float distSq = dx*dx + dy*dy + dz*dz;
            if (distSq > kTabRangeSq) continue;
            auto* unit = static_cast<Unit*>(entity.get());
            sortable.push_back({guid, distSq, unit->getHealth() == 0});
        }

        // Live enemies cycle first (nearest to farthest); lootable corpses last.
        std::sort(sortable.begin(), sortable.end(),
                  [](const EntityDist& a, const EntityDist& b) {
                      if (a.dead != b.dead) return b.dead;
                      return a.distSq < b.distSq;
                  });

        owner_.tabCycleListRef().reserve(sortable.size());
        for (const auto& ed : sortable) {
            owner_.tabCycleListRef().push_back(ed.guid);
        }
        owner_.tabCycleStaleRef() = false;
    }

    if (owner_.tabCycleListRef().empty()) {
        clearTarget();
        return;
    }

    // If the current target was picked by other means (click, assist), resume
    // the cycle from its position so tab advances to the NEXT enemy instead of
    // jumping back to wherever the old rotation left off.
    {
        const uint64_t curTarget = owner_.getTargetGuid();
        if (curTarget != 0) {
            const auto& list = owner_.tabCycleListRef();
            for (size_t i = 0; i < list.size(); ++i) {
                if (list[i] == curTarget) {
                    owner_.tabCycleIndexRef() = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    // Advance through the cycle, skipping any entry that has since died or
    // turned friendly (e.g. NPC killed between two tab presses).
    int tries = static_cast<int>(owner_.tabCycleListRef().size());
    while (tries-- > 0) {
        owner_.tabCycleIndexRef() = (owner_.tabCycleIndexRef() + 1) % static_cast<int>(owner_.tabCycleListRef().size());
        uint64_t guid = owner_.tabCycleListRef()[owner_.tabCycleIndexRef()];
        auto entity = owner_.getEntityManager().getEntity(guid);
        if (isValidTabTarget(entity)) {
            setTarget(guid);
            return;
        }
    }

    // All cached entries are stale — clear target and force a fresh rebuild next time.
    owner_.tabCycleStaleRef() = true;
    clearTarget();
}

void CombatHandler::assistTarget() {
    if (owner_.getState() != WorldState::IN_WORLD) {
        LOG_WARNING("Cannot assist: not in world");
        return;
    }

    if (owner_.getTargetGuid() == 0) {
        owner_.addSystemChatMessage("You must target someone to assist.");
        return;
    }

    auto target = getTarget();
    if (!target) {
        owner_.addSystemChatMessage("Invalid target.");
        return;
    }

    // Get target name
    std::string targetName = "Target";
    if (target->getType() == ObjectType::PLAYER) {
        auto player = std::static_pointer_cast<Player>(target);
        if (!player->getName().empty()) {
            targetName = player->getName();
        }
    } else if (target->getType() == ObjectType::UNIT) {
        auto unit = std::static_pointer_cast<Unit>(target);
        targetName = unit->getName();
    }

    // Try to read target GUID from update fields (UNIT_FIELD_TARGET)
    uint64_t assistTargetGuid = 0;
    const auto& fields = target->getFields();
    auto it = fields.find(fieldIndex(UF::UNIT_FIELD_TARGET_LO));
    if (it != fields.end()) {
        assistTargetGuid = it->second;
        auto it2 = fields.find(fieldIndex(UF::UNIT_FIELD_TARGET_HI));
        if (it2 != fields.end()) {
            assistTargetGuid |= (static_cast<uint64_t>(it2->second) << 32);
        }
    }

    if (assistTargetGuid == 0) {
        owner_.addSystemChatMessage(targetName + " has no target.");
        LOG_INFO("Assist: ", targetName, " has no target");
        return;
    }

    // Set our target to their target
    setTarget(assistTargetGuid);
    LOG_INFO("Assisting ", targetName, ", now targeting GUID: 0x", std::hex, assistTargetGuid, std::dec);
}

// ============================================================
// PvP
// ============================================================

void CombatHandler::togglePvp() {
    if (!owner_.isInWorld()) {
        LOG_WARNING("Cannot toggle PvP: not in world or not connected");
        return;
    }

    auto packet = TogglePvpPacket::build();
    owner_.getSocket()->send(packet);
    auto entity = owner_.getEntityManager().getEntity(owner_.getPlayerGuid());
    bool currentlyPvp = false;
    if (entity) {
        // UNIT_FIELD_FLAGS (index 59), bit 0x1000 = UNIT_FLAG_PVP
        currentlyPvp = (entity->getField(59) & 0x00001000) != 0;
    }
    if (currentlyPvp) {
        owner_.addSystemChatMessage("PvP flag disabled.");
    } else {
        owner_.addSystemChatMessage("PvP flag enabled.");
    }
    LOG_INFO("Toggled PvP flag");
}

// ============================================================
// Death / Resurrection
// ============================================================

void CombatHandler::releaseSpirit() {
    if (owner_.getSocket() && owner_.getState() == WorldState::IN_WORLD) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (owner_.repopPendingRef() && now - static_cast<int64_t>(owner_.lastRepopRequestMsRef()) < 1000) {
            return;
        }
        auto packet = RepopRequestPacket::build();
        owner_.getSocket()->send(packet);
        owner_.selfResAvailableRef() = false;
        owner_.repopPendingRef() = true;
        owner_.lastRepopRequestMsRef() = static_cast<uint64_t>(now);
        LOG_INFO("Sent CMSG_REPOP_REQUEST (Release Spirit)");
        network::Packet cq(wireOpcode(Opcode::MSG_CORPSE_QUERY));
        owner_.getSocket()->send(cq);
    }
}

bool CombatHandler::canReclaimCorpse() const {
    if (!owner_.releasedSpiritRef() || owner_.corpseGuidRef() == 0 ||
        !owner_.corpsePositionValidRef()) return false;
    if (owner_.currentMapIdRef() != owner_.corpseMapIdRef()) return false;
    float dx = owner_.movementInfoRef().x - owner_.corpseYRef();
    float dy = owner_.movementInfoRef().y - owner_.corpseXRef();
    float dz = owner_.movementInfoRef().z - owner_.corpseZRef();
    return (dx*dx + dy*dy + dz*dz) <= (40.0f * 40.0f);
}

float CombatHandler::getCorpseReclaimDelaySec() const {
    if (owner_.corpseReclaimAvailableMsRef() == 0) return 0.0f;
    auto nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    if (nowMs >= owner_.corpseReclaimAvailableMsRef()) return 0.0f;
    return static_cast<float>(owner_.corpseReclaimAvailableMsRef() - nowMs) / 1000.0f;
}

void CombatHandler::reclaimCorpse() {
    if (!canReclaimCorpse() || !owner_.getSocket()) return;
    if (owner_.corpseGuidRef() == 0) {
        LOG_WARNING("reclaimCorpse: corpse GUID not yet known (corpse object not received); cannot reclaim");
        return;
    }
    auto packet = ReclaimCorpsePacket::build(owner_.corpseGuidRef());
    owner_.getSocket()->send(packet);
    LOG_INFO("Sent CMSG_RECLAIM_CORPSE for corpse guid=0x", std::hex, owner_.corpseGuidRef(), std::dec);
}

void CombatHandler::useSelfRes() {
    if (!owner_.selfResAvailableRef() || !owner_.getSocket()) return;
    network::Packet pkt(wireOpcode(Opcode::CMSG_SELF_RES));
    owner_.getSocket()->send(pkt);
    owner_.selfResAvailableRef() = false;
    LOG_INFO("Sent CMSG_SELF_RES (Reincarnation / Twisting Nether)");
}

void CombatHandler::activateSpiritHealer(uint64_t npcGuid) {
    if (!owner_.isInWorld()) return;
    owner_.pendingSpiritHealerGuidRef() = npcGuid;
    auto packet = SpiritHealerActivatePacket::build(npcGuid);
    owner_.getSocket()->send(packet);
    owner_.resurrectPendingRef() = true;
    LOG_INFO("Sent CMSG_SPIRIT_HEALER_ACTIVATE for 0x", std::hex, npcGuid, std::dec);
}

void CombatHandler::acceptResurrect() {
    if (owner_.getState() != WorldState::IN_WORLD || !owner_.getSocket() || !owner_.resurrectRequestPendingRef()) return;
    if (owner_.resurrectIsSpiritHealerRef()) {
        auto activate = SpiritHealerActivatePacket::build(owner_.resurrectCasterGuidRef());
        owner_.getSocket()->send(activate);
        LOG_INFO("Sent CMSG_SPIRIT_HEALER_ACTIVATE for 0x",
                 std::hex, owner_.resurrectCasterGuidRef(), std::dec);
    } else {
        auto resp = ResurrectResponsePacket::build(owner_.resurrectCasterGuidRef(), true);
        owner_.getSocket()->send(resp);
        LOG_INFO("Sent CMSG_RESURRECT_RESPONSE (accept) for 0x",
                 std::hex, owner_.resurrectCasterGuidRef(), std::dec);
    }
    owner_.resurrectRequestPendingRef() = false;
    owner_.resurrectPendingRef() = true;
}

void CombatHandler::declineResurrect() {
    if (owner_.getState() != WorldState::IN_WORLD || !owner_.getSocket() || !owner_.resurrectRequestPendingRef()) return;
    auto resp = ResurrectResponsePacket::build(owner_.resurrectCasterGuidRef(), false);
    owner_.getSocket()->send(resp);
    LOG_INFO("Sent CMSG_RESURRECT_RESPONSE (decline) for 0x",
             std::hex, owner_.resurrectCasterGuidRef(), std::dec);
    owner_.resurrectRequestPendingRef() = false;
}

// ============================================================
// XP
// ============================================================

uint32_t CombatHandler::killXp(uint32_t playerLevel, uint32_t victimLevel) {
    if (playerLevel == 0 || victimLevel == 0) return 0;

    int32_t grayLevel;
    if (playerLevel <= 5)        grayLevel = 0;
    else if (playerLevel <= 39)  grayLevel = static_cast<int32_t>(playerLevel) - 5 - static_cast<int32_t>(playerLevel) / 10;
    else if (playerLevel <= 59)  grayLevel = static_cast<int32_t>(playerLevel) - 1 - static_cast<int32_t>(playerLevel) / 5;
    else                         grayLevel = static_cast<int32_t>(playerLevel) - 9;

    if (static_cast<int32_t>(victimLevel) <= grayLevel) return 0;

    uint32_t baseXp = 45 + 5 * victimLevel;

    int32_t diff = static_cast<int32_t>(victimLevel) - static_cast<int32_t>(playerLevel);
    float multiplier = 1.0f + diff * 0.05f;
    if (multiplier < 0.1f) multiplier = 0.1f;
    if (multiplier > 2.0f) multiplier = 2.0f;

    return static_cast<uint32_t>(baseXp * multiplier);
}

void CombatHandler::handleXpGain(network::Packet& packet) {
    XpGainData data;
    if (!XpGainParser::parse(packet, data)) return;

    addCombatText(CombatTextEntry::XP_GAIN, static_cast<int32_t>(data.totalXp), 0, true);

    std::string msg;
    if (data.victimGuid != 0 && data.type == 0) {
        std::string victimName = owner_.lookupName(data.victimGuid);
        if (!victimName.empty())
            msg = victimName + " dies, you gain " + std::to_string(data.totalXp) + " experience.";
        else
            msg = "You gain " + std::to_string(data.totalXp) + " experience.";
    } else {
        msg = "You gain " + std::to_string(data.totalXp) + " experience.";
    }
    if (data.groupBonus > 0) {
        msg += " (+" + std::to_string(data.groupBonus) + " group bonus)";
    }
    owner_.addSystemChatMessage(msg);
    owner_.fireAddonEvent("CHAT_MSG_COMBAT_XP_GAIN", {msg, std::to_string(data.totalXp)});
}

} // namespace game
} // namespace wowee
