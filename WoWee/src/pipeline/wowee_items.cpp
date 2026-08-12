#include "pipeline/wowee_items.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'I', 'T', 'M'};
constexpr uint32_t kVersion = 1;

template <typename T>
void writePOD(std::ofstream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool readPOD(std::ifstream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return is.gcount() == static_cast<std::streamsize>(sizeof(T));
}

void writeStr(std::ofstream& os, const std::string& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    writePOD(os, n);
    if (n > 0) os.write(s.data(), n);
}

bool readStr(std::ifstream& is, std::string& s) {
    uint32_t n = 0;
    if (!readPOD(is, n)) return false;
    if (n > (1u << 20)) return false;  // 1 MiB sanity cap
    s.resize(n);
    if (n > 0) {
        is.read(s.data(), n);
        if (is.gcount() != static_cast<std::streamsize>(n)) {
            s.clear();
            return false;
        }
    }
    return true;
}

std::string normalizePath(std::string base) {
    if (base.size() < 4 || base.substr(base.size() - 4) != ".wit") {
        base += ".wit";
    }
    return base;
}

} // namespace

const WoweeItem::Entry* WoweeItem::findById(uint32_t itemId) const {
    for (const auto& e : entries) {
        if (e.itemId == itemId) return &e;
    }
    return nullptr;
}

const char* WoweeItem::qualityName(uint8_t q) {
    switch (q) {
        case Poor:      return "poor";
        case Common:    return "common";
        case Uncommon:  return "uncommon";
        case Rare:      return "rare";
        case Epic:      return "epic";
        case Legendary: return "legendary";
        case Artifact:  return "artifact";
        case Heirloom:  return "heirloom";
        default:        return "unknown";
    }
}

const char* WoweeItem::classNameOf(uint8_t c) {
    switch (c) {
        case Consumable: return "consumable";
        case Container:  return "container";
        case Weapon:     return "weapon";
        case Gem:        return "gem";
        case Armor:      return "armor";
        case Reagent:    return "reagent";
        case Projectile: return "projectile";
        case TradeGoods: return "trade-goods";
        case Recipe:     return "recipe";
        case Quiver:     return "quiver";
        case Quest:      return "quest";
        case Key:        return "key";
        case Misc:       return "misc";
        default:         return "unknown";
    }
}

const char* WoweeItem::slotName(uint8_t s) {
    switch (s) {
        case NonEquip:  return "-";
        case Head:      return "head";
        case Neck:      return "neck";
        case Shoulders: return "shoulders";
        case Body:      return "shirt";
        case Chest:     return "chest";
        case Waist:     return "waist";
        case Legs:      return "legs";
        case Feet:      return "feet";
        case Wrists:    return "wrists";
        case Hands:     return "hands";
        case Finger:    return "finger";
        case Trinket:   return "trinket";
        case Weapon1H:  return "weapon-1h";
        case Shield:    return "shield";
        case Ranged:    return "ranged";
        case Cloak:     return "cloak";
        case Weapon2H:  return "weapon-2h";
        default:        return "?";
    }
}

const char* WoweeItem::statName(uint8_t t) {
    switch (t) {
        case StatNone:      return "-";
        case StatHealth:    return "health";
        case StatMana:      return "mana";
        case StatAgility:   return "agility";
        case StatStrength:  return "strength";
        case StatIntellect: return "intellect";
        case StatSpirit:    return "spirit";
        case StatStamina:   return "stamina";
        case StatDefense:   return "defense";
        default:            return "stat?";
    }
}

bool WoweeItemLoader::save(const WoweeItem& cat,
                           const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.itemId);
        writePOD(os, e.displayId);
        writePOD(os, e.quality);
        writePOD(os, e.itemClass);
        writePOD(os, e.itemSubClass);
        writePOD(os, e.inventoryType);
        writePOD(os, e.flags);
        writePOD(os, e.requiredLevel);
        writePOD(os, e.itemLevel);
        writePOD(os, e.sellPriceCopper);
        writePOD(os, e.buyPriceCopper);
        writePOD(os, e.maxStack);
        writePOD(os, e.durability);
        writePOD(os, e.damageMin);
        writePOD(os, e.damageMax);
        writePOD(os, e.attackSpeedMs);
        // statCount is uint8; cap to 255 to avoid silent truncation
        // when a hand-edit JSON has more.
        uint8_t statCount = static_cast<uint8_t>(
            e.stats.size() > 255 ? 255 : e.stats.size());
        writePOD(os, statCount);
        uint8_t pad[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad), 3);
        for (uint8_t k = 0; k < statCount; ++k) {
            const auto& s = e.stats[k];
            writePOD(os, s.type);
            uint8_t spad = 0;
            writePOD(os, spad);
            writePOD(os, s.value);
        }
        writeStr(os, e.name);
        writeStr(os, e.description);
    }
    return os.good();
}

WoweeItem WoweeItemLoader::load(const std::string& basePath) {
    WoweeItem out;
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    if (!is) return out;
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return out;
    uint32_t version = 0;
    if (!readPOD(is, version) || version != kVersion) return out;
    if (!readStr(is, out.name)) return out;
    uint32_t entryCount = 0;
    if (!readPOD(is, entryCount)) return out;
    if (entryCount > (1u << 20)) return out;
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readPOD(is, e.itemId) ||
            !readPOD(is, e.displayId) ||
            !readPOD(is, e.quality) ||
            !readPOD(is, e.itemClass) ||
            !readPOD(is, e.itemSubClass) ||
            !readPOD(is, e.inventoryType) ||
            !readPOD(is, e.flags) ||
            !readPOD(is, e.requiredLevel) ||
            !readPOD(is, e.itemLevel) ||
            !readPOD(is, e.sellPriceCopper) ||
            !readPOD(is, e.buyPriceCopper) ||
            !readPOD(is, e.maxStack) ||
            !readPOD(is, e.durability) ||
            !readPOD(is, e.damageMin) ||
            !readPOD(is, e.damageMax) ||
            !readPOD(is, e.attackSpeedMs)) {
            out.entries.clear();
            return out;
        }
        uint8_t statCount = 0;
        if (!readPOD(is, statCount)) {
            out.entries.clear(); return out;
        }
        uint8_t pad[3];
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        e.stats.resize(statCount);
        for (uint8_t k = 0; k < statCount; ++k) {
            if (!readPOD(is, e.stats[k].type)) {
                out.entries.clear(); return out;
            }
            uint8_t spad = 0;
            if (!readPOD(is, spad)) {
                out.entries.clear(); return out;
            }
            if (!readPOD(is, e.stats[k].value)) {
                out.entries.clear(); return out;
            }
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear();
            return out;
        }
    }
    return out;
}

bool WoweeItemLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeItem WoweeItemLoader::makeStarter(const std::string& catalogName) {
    WoweeItem c;
    c.name = catalogName;
    {
        WoweeItem::Entry e;
        e.itemId = 1; e.displayId = 100;
        e.quality = WoweeItem::Common; e.itemClass = WoweeItem::Weapon;
        e.itemSubClass = 0;             // sword 1H
        e.inventoryType = WoweeItem::Weapon1H;
        e.requiredLevel = 1; e.itemLevel = 5;
        e.sellPriceCopper = 50; e.buyPriceCopper = 200;
        e.maxStack = 1; e.durability = 50;
        e.damageMin = 4; e.damageMax = 9; e.attackSpeedMs = 1800;
        e.stats.push_back({WoweeItem::StatStrength, 1});
        e.name = "Worn Shortsword";
        e.description = "A simple training sword.";
        c.entries.push_back(e);
    }
    {
        WoweeItem::Entry e;
        e.itemId = 2; e.displayId = 200;
        e.quality = WoweeItem::Common; e.itemClass = WoweeItem::Armor;
        e.itemSubClass = 1;             // cloth chest
        e.inventoryType = WoweeItem::Chest;
        e.requiredLevel = 1; e.itemLevel = 5;
        e.sellPriceCopper = 30; e.buyPriceCopper = 150;
        e.maxStack = 1; e.durability = 40;
        e.stats.push_back({WoweeItem::StatStamina, 1});
        e.name = "Linen Vest";
        e.description = "Plain linen.";
        c.entries.push_back(e);
    }
    {
        WoweeItem::Entry e;
        e.itemId = 3; e.displayId = 300;
        e.quality = WoweeItem::Common;
        e.itemClass = WoweeItem::Consumable;
        e.itemSubClass = 0;             // potion
        e.inventoryType = WoweeItem::NonEquip;
        e.requiredLevel = 1; e.itemLevel = 1;
        e.sellPriceCopper = 5; e.buyPriceCopper = 25;
        e.maxStack = 20; e.durability = 0;
        e.name = "Minor Healing Potion";
        e.description = "Restores a small amount of health.";
        c.entries.push_back(e);
    }
    {
        WoweeItem::Entry e;
        e.itemId = 4; e.displayId = 400;
        e.quality = WoweeItem::Common;
        e.itemClass = WoweeItem::Quest;
        e.itemSubClass = 0;
        e.inventoryType = WoweeItem::NonEquip;
        e.flags = WoweeItem::QuestItem | WoweeItem::Unique |
                   WoweeItem::NoSellable;
        e.maxStack = 1; e.durability = 0;
        e.name = "Tattered Letter";
        e.description = "Deliver to the captain in the next town.";
        c.entries.push_back(e);
    }
    return c;
}

WoweeItem WoweeItemLoader::makeWeapons(const std::string& catalogName) {
    WoweeItem c;
    c.name = catalogName;
    auto addWeapon = [&](uint32_t id, uint32_t disp, uint8_t qual,
                         uint8_t slot, uint16_t lvlReq, uint16_t ilvl,
                         uint32_t dmgMin, uint32_t dmgMax, uint32_t speedMs,
                         const char* name) {
        WoweeItem::Entry e;
        e.itemId = id; e.displayId = disp;
        e.quality = qual; e.itemClass = WoweeItem::Weapon;
        e.itemSubClass = (slot == WoweeItem::Weapon2H ? 1 : 0);
        e.inventoryType = slot;
        e.requiredLevel = lvlReq; e.itemLevel = ilvl;
        e.sellPriceCopper = ilvl * 10u;
        e.buyPriceCopper = ilvl * 100u;
        e.maxStack = 1;
        e.durability = static_cast<uint16_t>(40 + ilvl);
        e.damageMin = dmgMin; e.damageMax = dmgMax;
        e.attackSpeedMs = speedMs;
        e.stats.push_back({WoweeItem::StatStrength,
                            static_cast<int16_t>(1 + ilvl / 5)});
        e.name = name;
        c.entries.push_back(e);
    };
    addWeapon(1001, 1100, WoweeItem::Common,    WoweeItem::Weapon1H,  1,  5,  4,  9, 1800, "Apprentice Sword");
    addWeapon(1002, 1101, WoweeItem::Uncommon,  WoweeItem::Weapon1H, 10, 15, 12, 22, 1700, "Journeyman Blade");
    addWeapon(1003, 1102, WoweeItem::Rare,      WoweeItem::Weapon1H, 20, 30, 28, 48, 1600, "Steelthorn Edge");
    addWeapon(1004, 1103, WoweeItem::Epic,      WoweeItem::Weapon2H, 35, 50, 70, 110, 3200, "Bloodforged Greatsword");
    addWeapon(1005, 1104, WoweeItem::Legendary, WoweeItem::Weapon2H, 50, 70, 130, 195, 3000, "Doombringer");
    return c;
}

WoweeItem WoweeItemLoader::makeArmor(const std::string& catalogName) {
    WoweeItem c;
    c.name = catalogName;
    auto addArmor = [&](uint32_t id, uint32_t disp, uint8_t slot,
                        uint16_t ilvl, int16_t stam, int16_t str_,
                        const char* name) {
        WoweeItem::Entry e;
        e.itemId = id; e.displayId = disp;
        e.quality = WoweeItem::Uncommon;
        e.itemClass = WoweeItem::Armor;
        e.itemSubClass = 3;       // mail
        e.inventoryType = slot;
        e.requiredLevel = 20; e.itemLevel = ilvl;
        e.sellPriceCopper = ilvl * 8u;
        e.buyPriceCopper = ilvl * 80u;
        e.maxStack = 1;
        e.durability = static_cast<uint16_t>(60 + ilvl);
        e.flags = WoweeItem::BindOnEquip;
        if (stam) e.stats.push_back({WoweeItem::StatStamina, stam});
        if (str_) e.stats.push_back({WoweeItem::StatStrength, str_});
        e.stats.push_back({WoweeItem::StatDefense,
                            static_cast<int16_t>(ilvl / 5)});
        e.name = name;
        c.entries.push_back(e);
    };
    addArmor(2001, 2100, WoweeItem::Head,  25, 6, 4, "Iron Helm");
    addArmor(2002, 2101, WoweeItem::Chest, 25, 9, 6, "Iron Chestguard");
    addArmor(2003, 2102, WoweeItem::Legs,  25, 7, 5, "Iron Legguards");
    addArmor(2004, 2103, WoweeItem::Feet,  25, 4, 3, "Iron Boots");
    addArmor(2005, 2104, WoweeItem::Hands, 25, 4, 3, "Iron Gauntlets");
    addArmor(2006, 2105, WoweeItem::Cloak, 25, 5, 0, "Traveler's Cloak");
    return c;
}

} // namespace pipeline
} // namespace wowee
