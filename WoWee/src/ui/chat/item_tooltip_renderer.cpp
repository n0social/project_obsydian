// ItemTooltipRenderer — renders full WoW-style item tooltips via ImGui.
// Extracted from ChatMarkupRenderer::renderItemTooltip (Phase 6.7).
#include "ui/chat/item_tooltip_renderer.hpp"
#include "ui/ui_colors.hpp"
#include "ui/inventory_screen.hpp"
#include "game/game_handler.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/dbc_layout.hpp"
#include <imgui.h>
#include <cstring>
#include <unordered_map>
#include <cstdio>

namespace wowee { namespace ui {

void ItemTooltipRenderer::render(
    uint32_t itemEntry,
    game::GameHandler& gameHandler,
    InventoryScreen& inventoryScreen,
    pipeline::AssetManager* assetMgr)
{
    const auto* info = gameHandler.getItemInfo(itemEntry);
    if (!info || !info->valid) return;

    auto findComparableEquipped = [&](uint8_t inventoryType) -> const game::ItemSlot* {
        using ES = game::EquipSlot;
        const auto& inv = gameHandler.getInventory();
        auto slotPtr = [&](ES slot) -> const game::ItemSlot* {
            const auto& s = inv.getEquipSlot(slot);
            return s.empty() ? nullptr : &s;
        };
        switch (inventoryType) {
            case 1: return slotPtr(ES::HEAD);
            case 2: return slotPtr(ES::NECK);
            case 3: return slotPtr(ES::SHOULDERS);
            case 4: return slotPtr(ES::SHIRT);
            case 5:
            case 20: return slotPtr(ES::CHEST);
            case 6: return slotPtr(ES::WAIST);
            case 7: return slotPtr(ES::LEGS);
            case 8: return slotPtr(ES::FEET);
            case 9: return slotPtr(ES::WRISTS);
            case 10: return slotPtr(ES::HANDS);
            case 11: {
                if (auto* s = slotPtr(ES::RING1)) return s;
                return slotPtr(ES::RING2);
            }
            case 12: {
                if (auto* s = slotPtr(ES::TRINKET1)) return s;
                return slotPtr(ES::TRINKET2);
            }
            case 13:
                if (auto* s = slotPtr(ES::MAIN_HAND)) return s;
                return slotPtr(ES::OFF_HAND);
            case 14:
            case 22:
            case 23: return slotPtr(ES::OFF_HAND);
            case 15:
            case 25:
            case 26: return slotPtr(ES::RANGED);
            case 16: return slotPtr(ES::BACK);
            case 17:
            case 21: return slotPtr(ES::MAIN_HAND);
            case 18:
                for (int i = 0; i < game::Inventory::NUM_BAG_SLOTS; ++i) {
                    auto slot = static_cast<ES>(static_cast<int>(ES::BAG1) + i);
                    if (auto* s = slotPtr(slot)) return s;
                }
                return nullptr;
            case 19: return slotPtr(ES::TABARD);
            default: return nullptr;
        }
    };

    auto isWeaponInventoryType = [](uint32_t invType) {
        switch (invType) {
            case 13: case 15: case 17: case 21: case 25: case 26: return true;
            default: return false;
        }
    };

    auto appendBonus = [](std::string& out, int32_t val, const char* shortName) {
        if (val <= 0) return;
        if (!out.empty()) out += "  ";
        out += "+" + std::to_string(val) + " ";
        out += shortName;
    };

    ImGui::BeginTooltip();
    // Quality color for name
    auto qColor = ui::getQualityColor(static_cast<game::ItemQuality>(info->quality));
    ImGui::TextColored(qColor, "%s", info->name.c_str());

    // Heroic indicator (green, matches WoW tooltip style)
    constexpr uint32_t kFlagHeroic         = 0x8;
    constexpr uint32_t kFlagUniqueEquipped = 0x1000000;
    if (info->itemFlags & kFlagHeroic)
        ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "Heroic");

    // Bind type (appears right under name in WoW)
    switch (info->bindType) {
        case 1: ImGui::TextDisabled("Binds when picked up");   break;
        case 2: ImGui::TextDisabled("Binds when equipped");    break;
        case 3: ImGui::TextDisabled("Binds when used");        break;
        case 4: ImGui::TextDisabled("Quest Item");             break;
    }
    // Unique / Unique-Equipped
    if (info->maxCount == 1)
        ImGui::TextColored(ui::colors::kTooltipGold, "Unique");
    else if (info->itemFlags & kFlagUniqueEquipped)
        ImGui::TextColored(ui::colors::kTooltipGold, "Unique-Equipped");

    // Slot type
    if (info->inventoryType > 0) {
        const char* slotName = ui::getInventorySlotName(info->inventoryType);
        if (slotName[0]) {
            if (!info->subclassName.empty())
                ImGui::TextColored(ui::colors::kLightGray, "%s  %s", slotName, info->subclassName.c_str());
            else
                ImGui::TextColored(ui::colors::kLightGray, "%s", slotName);
        }
    }

    const bool isWeapon = isWeaponInventoryType(info->inventoryType);

    // Item level (after slot/subclass)
    if (info->itemLevel > 0)
        ImGui::TextDisabled("Item Level %u", info->itemLevel);

    if (isWeapon && info->damageMax > 0.0f && info->delayMs > 0) {
        float speed = static_cast<float>(info->delayMs) / 1000.0f;
        float dps = ((info->damageMin + info->damageMax) * 0.5f) / speed;
        char dmgBuf[64], spdBuf[32];
        std::snprintf(dmgBuf, sizeof(dmgBuf), "%d - %d Damage",
                      static_cast<int>(info->damageMin), static_cast<int>(info->damageMax));
        std::snprintf(spdBuf, sizeof(spdBuf), "Speed %.2f", speed);
        float spdW = ImGui::CalcTextSize(spdBuf).x;
        ImGui::Text("%s", dmgBuf);
        ImGui::SameLine(ImGui::GetWindowWidth() - spdW - 16.0f);
        ImGui::Text("%s", spdBuf);
        ImGui::TextDisabled("(%.1f damage per second)", dps);
    }
    ImVec4 green(0.0f, 1.0f, 0.0f, 1.0f);
    std::string bonusLine;
    appendBonus(bonusLine, info->strength, "Str");
    appendBonus(bonusLine, info->agility, "Agi");
    appendBonus(bonusLine, info->stamina, "Sta");
    appendBonus(bonusLine, info->intellect, "Int");
    appendBonus(bonusLine, info->spirit, "Spi");
    if (!bonusLine.empty()) {
        ImGui::TextColored(green, "%s", bonusLine.c_str());
    }
    if (info->armor > 0) {
        ImGui::Text("%d Armor", info->armor);
    }
    // Elemental resistances
    {
        const int32_t resVals[6] = {
            info->holyRes, info->fireRes, info->natureRes,
            info->frostRes, info->shadowRes, info->arcaneRes
        };
        static constexpr const char* resLabels[6] = {
            "Holy Resistance", "Fire Resistance", "Nature Resistance",
            "Frost Resistance", "Shadow Resistance", "Arcane Resistance"
        };
        for (int ri = 0; ri < 6; ++ri)
            if (resVals[ri] > 0) ImGui::Text("+%d %s", resVals[ri], resLabels[ri]);
    }
    // Extra stats (hit/crit/haste/sp/ap/expertise/resilience/etc.)
    if (!info->extraStats.empty()) {
        auto statName = [](uint32_t t) -> const char* {
            switch (t) {
                case 12: return "Defense Rating";
                case 13: return "Dodge Rating";
                case 14: return "Parry Rating";
                case 15: return "Block Rating";
                case 16: case 17: case 18: case 31: return "Hit Rating";
                case 19: case 20: case 21: case 32: return "Critical Strike Rating";
                case 28: case 29: case 30: case 35: return "Haste Rating";
                case 34: return "Resilience Rating";
                case 36: return "Expertise Rating";
                case 37: return "Attack Power";
                case 38: return "Ranged Attack Power";
                case 45: return "Spell Power";
                case 46: return "Healing Power";
                case 47: return "Spell Damage";
                case 49: return "Mana per 5 sec.";
                case 43: return "Spell Penetration";
                case 44: return "Block Value";
                default: return nullptr;
            }
        };
        for (const auto& es : info->extraStats) {
            const char* nm = statName(es.statType);
            if (nm && es.statValue > 0)
                ImGui::TextColored(green, "+%d %s", es.statValue, nm);
        }
    }
    // Gem sockets
    {
        const auto& kSocketTypes = ui::kSocketTypes;
        bool hasSocket = false;
        for (int s = 0; s < 3; ++s) {
            if (info->socketColor[s] == 0) continue;
            if (!hasSocket) { ImGui::Spacing(); hasSocket = true; }
            for (const auto& st : kSocketTypes) {
                if (info->socketColor[s] & st.mask) {
                    ImGui::TextColored(st.col, "%s", st.label);
                    break;
                }
            }
        }
        if (hasSocket && info->socketBonus != 0) {
            static std::unordered_map<uint32_t, std::string> s_enchantNames;
            static bool s_enchantNamesLoaded = false;
            if (!s_enchantNamesLoaded && assetMgr) {
                s_enchantNamesLoaded = true;
                auto dbc = assetMgr->loadDBC("SpellItemEnchantment.dbc");
                if (dbc && dbc->isLoaded()) {
                    const auto* lay = pipeline::getActiveDBCLayout()
                        ? pipeline::getActiveDBCLayout()->getLayout("SpellItemEnchantment") : nullptr;
                    uint32_t nameField = pipeline::detectEnchantmentNameField(dbc.get(), lay);
                    uint32_t fc = dbc->getFieldCount();
                    for (uint32_t r = 0; r < dbc->getRecordCount(); ++r) {
                        uint32_t eid = dbc->getUInt32(r, 0);
                        if (eid == 0 || nameField >= fc) continue;
                        std::string ename = dbc->getString(r, nameField);
                        if (!ename.empty()) s_enchantNames[eid] = std::move(ename);
                    }
                }
            }
            auto enchIt = s_enchantNames.find(info->socketBonus);
            if (enchIt != s_enchantNames.end())
                ImGui::TextColored(colors::kSocketGreen, "Socket Bonus: %s", enchIt->second.c_str());
            else
                ImGui::TextColored(colors::kSocketGreen, "Socket Bonus: (id %u)", info->socketBonus);
        }
    }
    // Item set membership
    if (info->itemSetId != 0) {
        struct SetEntry {
            std::string name;
            std::array<uint32_t, 10> itemIds{};
            std::array<uint32_t, 10> spellIds{};
            std::array<uint32_t, 10> thresholds{};
        };
        static std::unordered_map<uint32_t, SetEntry> s_setData;
        static bool s_setDataLoaded = false;
        if (!s_setDataLoaded && assetMgr) {
            s_setDataLoaded = true;
            auto dbc = assetMgr->loadDBC("ItemSet.dbc");
            if (dbc && dbc->isLoaded()) {
                const auto* layout = pipeline::getActiveDBCLayout()
                    ? pipeline::getActiveDBCLayout()->getLayout("ItemSet") : nullptr;
                auto lf = [&](const char* k, uint32_t def) -> uint32_t {
                    return layout ? (*layout)[k] : def;
                };
                uint32_t idF = lf("ID", 0), nameF = lf("Name", 1);
                const auto& itemKeys = ui::kItemSetItemKeys;
                const auto& spellKeys = ui::kItemSetSpellKeys;
                const auto& thrKeys = ui::kItemSetThresholdKeys;
                for (uint32_t r = 0; r < dbc->getRecordCount(); ++r) {
                    uint32_t id = dbc->getUInt32(r, idF);
                    if (!id) continue;
                    SetEntry e;
                    e.name = dbc->getString(r, nameF);
                    for (int i = 0; i < 10; ++i) {
                        e.itemIds[i]    = dbc->getUInt32(r, layout ? (*layout)[itemKeys[i]]  : uint32_t(18 + i));
                        e.spellIds[i]   = dbc->getUInt32(r, layout ? (*layout)[spellKeys[i]] : uint32_t(28 + i));
                        e.thresholds[i] = dbc->getUInt32(r, layout ? (*layout)[thrKeys[i]]   : uint32_t(38 + i));
                    }
                    s_setData[id] = std::move(e);
                }
            }
        }
        ImGui::Spacing();
        const auto& inv = gameHandler.getInventory();
        auto setIt = s_setData.find(info->itemSetId);
        if (setIt != s_setData.end()) {
            const SetEntry& se = setIt->second;
            int equipped = 0, total = 0;
            for (int i = 0; i < 10; ++i) {
                if (se.itemIds[i] == 0) continue;
                ++total;
                for (int sl = 0; sl < game::Inventory::NUM_EQUIP_SLOTS; sl++) {
                    const auto& eq = inv.getEquipSlot(static_cast<game::EquipSlot>(sl));
                    if (!eq.empty() && eq.item.itemId == se.itemIds[i]) { ++equipped; break; }
                }
            }
            if (total > 0)
                ImGui::TextColored(ui::colors::kTooltipGold,
                    "%s (%d/%d)", se.name.empty() ? "Set" : se.name.c_str(), equipped, total);
            else if (!se.name.empty())
                ImGui::TextColored(ui::colors::kTooltipGold, "%s", se.name.c_str());
            for (int i = 0; i < 10; ++i) {
                if (se.spellIds[i] == 0 || se.thresholds[i] == 0) continue;
                const std::string& bname = gameHandler.getSpellName(se.spellIds[i]);
                bool active = (equipped >= static_cast<int>(se.thresholds[i]));
                ImVec4 col = active ? colors::kActiveGreen : colors::kInactiveGray;
                if (!bname.empty())
                    ImGui::TextColored(col, "(%u) %s", se.thresholds[i], bname.c_str());
                else
                    ImGui::TextColored(col, "(%u) Set Bonus", se.thresholds[i]);
            }
        } else {
            ImGui::TextColored(ui::colors::kTooltipGold, "Set (id %u)", info->itemSetId);
        }
    }
    // Item spell effects (Use / Equip / Chance on Hit / Teaches)
    for (const auto& sp : info->spells) {
        if (sp.spellId == 0) continue;
        const char* triggerLabel = nullptr;
        switch (sp.spellTrigger) {
            case 0: triggerLabel = "Use";          break;
            case 1: triggerLabel = "Equip";        break;
            case 2: triggerLabel = "Chance on Hit"; break;
            case 5: triggerLabel = "Teaches";      break;
        }
        if (!triggerLabel) continue;
        const std::string& spDesc = gameHandler.getSpellDescription(sp.spellId);
        std::string spText = !spDesc.empty()
            ? gameHandler.formatSpellDescription(sp.spellId, spDesc)
            : gameHandler.getSpellName(sp.spellId);
        if (!spText.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 300.0f);
            ImGui::TextColored(colors::kCyan,
                               "%s: %s", triggerLabel, spText.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    // Required level
    if (info->requiredLevel > 1)
        ImGui::TextDisabled("Requires Level %u", info->requiredLevel);
    // Required skill
    if (info->requiredSkill != 0 && info->requiredSkillRank > 0) {
        static std::unordered_map<uint32_t, std::string> s_skillNames;
        static bool s_skillNamesLoaded = false;
        if (!s_skillNamesLoaded && assetMgr) {
            s_skillNamesLoaded = true;
            auto dbc = assetMgr->loadDBC("SkillLine.dbc");
            if (dbc && dbc->isLoaded()) {
                const auto* layout = pipeline::getActiveDBCLayout()
                    ? pipeline::getActiveDBCLayout()->getLayout("SkillLine") : nullptr;
                uint32_t idF   = layout ? (*layout)["ID"]   : 0u;
                uint32_t nameF = layout ? (*layout)["Name"] : 2u;
                for (uint32_t r = 0; r < dbc->getRecordCount(); ++r) {
                    uint32_t sid = dbc->getUInt32(r, idF);
                    if (!sid) continue;
                    std::string sname = dbc->getString(r, nameF);
                    if (!sname.empty()) s_skillNames[sid] = std::move(sname);
                }
            }
        }
        uint32_t playerSkillVal = 0;
        const auto& skills = gameHandler.getPlayerSkills();
        auto skPit = skills.find(info->requiredSkill);
        if (skPit != skills.end()) playerSkillVal = skPit->second.effectiveValue();
        bool meetsSkill = (playerSkillVal == 0 || playerSkillVal >= info->requiredSkillRank);
        ImVec4 skColor = meetsSkill ? ImVec4(1.0f, 1.0f, 1.0f, 0.75f) : colors::kPaleRed;
        auto skIt = s_skillNames.find(info->requiredSkill);
        if (skIt != s_skillNames.end())
            ImGui::TextColored(skColor, "Requires %s (%u)", skIt->second.c_str(), info->requiredSkillRank);
        else
            ImGui::TextColored(skColor, "Requires Skill %u (%u)", info->requiredSkill, info->requiredSkillRank);
    }
    // Required reputation
    if (info->requiredReputationFaction != 0 && info->requiredReputationRank > 0) {
        static std::unordered_map<uint32_t, std::string> s_factionNames;
        static bool s_factionNamesLoaded = false;
        if (!s_factionNamesLoaded && assetMgr) {
            s_factionNamesLoaded = true;
            auto dbc = assetMgr->loadDBC("Faction.dbc");
            if (dbc && dbc->isLoaded()) {
                const auto* layout = pipeline::getActiveDBCLayout()
                    ? pipeline::getActiveDBCLayout()->getLayout("Faction") : nullptr;
                uint32_t idF   = layout ? (*layout)["ID"]   : 0u;
                uint32_t nameF = layout ? (*layout)["Name"] : 20u;
                for (uint32_t r = 0; r < dbc->getRecordCount(); ++r) {
                    uint32_t fid = dbc->getUInt32(r, idF);
                    if (!fid) continue;
                    std::string fname = dbc->getString(r, nameF);
                    if (!fname.empty()) s_factionNames[fid] = std::move(fname);
                }
            }
        }
        static constexpr const char* kRepRankNames[] = {
            "Hated", "Hostile", "Unfriendly", "Neutral",
            "Friendly", "Honored", "Revered", "Exalted"
        };
        const char* rankName = (info->requiredReputationRank < 8)
            ? kRepRankNames[info->requiredReputationRank] : "Unknown";
        auto fIt = s_factionNames.find(info->requiredReputationFaction);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.75f), "Requires %s with %s",
            rankName,
            fIt != s_factionNames.end() ? fIt->second.c_str() : "Unknown Faction");
    }
    // Class restriction
    if (info->allowableClass != 0) {
        const auto& kClasses = ui::kClassMasks;
        int matchCount = 0;
        for (const auto& kc : kClasses)
            if (info->allowableClass & kc.mask) ++matchCount;
        if (matchCount > 0 && matchCount < 10) {
            char classBuf[128] = "Classes: ";
            bool first = true;
            for (const auto& kc : kClasses) {
                if (!(info->allowableClass & kc.mask)) continue;
                if (!first) strncat(classBuf, ", ", sizeof(classBuf) - strlen(classBuf) - 1);
                strncat(classBuf, kc.name, sizeof(classBuf) - strlen(classBuf) - 1);
                first = false;
            }
            uint8_t pc = gameHandler.getPlayerClass();
            uint32_t pmask = (pc > 0 && pc <= 10) ? (1u << (pc - 1)) : 0u;
            bool playerAllowed = (pmask == 0 || (info->allowableClass & pmask));
            ImVec4 clColor = playerAllowed ? ImVec4(1.0f, 1.0f, 1.0f, 0.75f) : colors::kPaleRed;
            ImGui::TextColored(clColor, "%s", classBuf);
        }
    }
    // Race restriction
    if (info->allowableRace != 0) {
        const auto& kRaces = ui::kRaceMasks;
        constexpr uint32_t kAllPlayable = 1|2|4|8|16|32|64|128|512|1024;
        if ((info->allowableRace & kAllPlayable) != kAllPlayable) {
            int matchCount = 0;
            for (const auto& kr : kRaces)
                if (info->allowableRace & kr.mask) ++matchCount;
            if (matchCount > 0) {
                char raceBuf[160] = "Races: ";
                bool first = true;
                for (const auto& kr : kRaces) {
                    if (!(info->allowableRace & kr.mask)) continue;
                    if (!first) strncat(raceBuf, ", ", sizeof(raceBuf) - strlen(raceBuf) - 1);
                    strncat(raceBuf, kr.name, sizeof(raceBuf) - strlen(raceBuf) - 1);
                    first = false;
                }
                uint8_t pr = gameHandler.getPlayerRace();
                uint32_t pmask = (pr > 0 && pr <= 11) ? (1u << (pr - 1)) : 0u;
                bool playerAllowed = (pmask == 0 || (info->allowableRace & pmask));
                ImVec4 rColor = playerAllowed ? ImVec4(1.0f, 1.0f, 1.0f, 0.75f) : colors::kPaleRed;
                ImGui::TextColored(rColor, "%s", raceBuf);
            }
        }
    }
    // Flavor text
    if (!info->description.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(300.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.0f, 0.85f), "\"%s\"", info->description.c_str());
        ImGui::PopTextWrapPos();
    }
    if (info->sellPrice > 0) {
        ImGui::TextDisabled("Sell:"); ImGui::SameLine(0, 4);
        renderCoinsFromCopper(info->sellPrice);
    }

    if (ImGui::GetIO().KeyShift && info->inventoryType > 0) {
        if (const auto* eq = findComparableEquipped(static_cast<uint8_t>(info->inventoryType))) {
            ImGui::Separator();
            ImGui::TextDisabled("Equipped:");
            VkDescriptorSet eqIcon = inventoryScreen.getItemIcon(eq->item.displayInfoId);
            if (eqIcon) {
                ImGui::Image((ImTextureID)(uintptr_t)eqIcon, ImVec2(18.0f, 18.0f));
                ImGui::SameLine();
            }
            ImGui::TextColored(InventoryScreen::getQualityColor(eq->item.quality), "%s", eq->item.name.c_str());
            if (isWeaponInventoryType(eq->item.inventoryType) &&
                eq->item.damageMax > 0.0f && eq->item.delayMs > 0) {
                float speed = static_cast<float>(eq->item.delayMs) / 1000.0f;
                float dps = ((eq->item.damageMin + eq->item.damageMax) * 0.5f) / speed;
                char eqDmg[64], eqSpd[32];
                std::snprintf(eqDmg, sizeof(eqDmg), "%d - %d Damage",
                              static_cast<int>(eq->item.damageMin), static_cast<int>(eq->item.damageMax));
                std::snprintf(eqSpd, sizeof(eqSpd), "Speed %.2f", speed);
                float eqSpdW = ImGui::CalcTextSize(eqSpd).x;
                ImGui::Text("%s", eqDmg);
                ImGui::SameLine(ImGui::GetWindowWidth() - eqSpdW - 16.0f);
                ImGui::Text("%s", eqSpd);
                ImGui::TextDisabled("(%.1f damage per second)", dps);
            }
            if (eq->item.armor > 0) {
                ImGui::Text("%d Armor", eq->item.armor);
            }
            std::string eqBonusLine;
            appendBonus(eqBonusLine, eq->item.strength, "Str");
            appendBonus(eqBonusLine, eq->item.agility, "Agi");
            appendBonus(eqBonusLine, eq->item.stamina, "Sta");
            appendBonus(eqBonusLine, eq->item.intellect, "Int");
            appendBonus(eqBonusLine, eq->item.spirit, "Spi");
            if (!eqBonusLine.empty()) {
                ImGui::TextColored(green, "%s", eqBonusLine.c_str());
            }
            // Extra stats for the equipped item
            for (const auto& es : eq->item.extraStats) {
                const char* nm = nullptr;
                switch (es.statType) {
                    case 12: nm = "Defense Rating"; break;
                    case 13: nm = "Dodge Rating"; break;
                    case 14: nm = "Parry Rating"; break;
                    case 16: case 17: case 18: case 31: nm = "Hit Rating"; break;
                    case 19: case 20: case 21: case 32: nm = "Critical Strike Rating"; break;
                    case 28: case 29: case 30: case 35: nm = "Haste Rating"; break;
                    case 34: nm = "Resilience Rating"; break;
                    case 36: nm = "Expertise Rating"; break;
                    case 37: nm = "Attack Power"; break;
                    case 38: nm = "Ranged Attack Power"; break;
                    case 45: nm = "Spell Power"; break;
                    case 46: nm = "Healing Power"; break;
                    case 49: nm = "Mana per 5 sec."; break;
                    default: break;
                }
                if (nm && es.statValue > 0)
                    ImGui::TextColored(green, "+%d %s", es.statValue, nm);
            }
        }
    }
    ImGui::EndTooltip();
}

} // namespace ui
} // namespace wowee
