#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <vector>

namespace wowee {
namespace game {

enum class ItemQuality : uint8_t {
    POOR = 0,       // Grey
    COMMON = 1,     // White
    UNCOMMON = 2,   // Green
    RARE = 3,       // Blue
    EPIC = 4,       // Purple
    LEGENDARY = 5,  // Orange
    ARTIFACT = 6,   // Yellow (unused in 3.3.5a but valid quality value)
    HEIRLOOM = 7,   // Yellow/gold (WotLK bind-on-account heirlooms)
};

enum class EquipSlot : uint8_t {
    HEAD = 0, NECK, SHOULDERS, SHIRT, CHEST,
    WAIST, LEGS, FEET, WRISTS, HANDS,
    RING1, RING2, TRINKET1, TRINKET2,
    BACK, MAIN_HAND, OFF_HAND, RANGED, TABARD,
    BAG1, BAG2, BAG3, BAG4,
    NUM_SLOTS  // = 23
};

// WoW InventoryType field values (from ItemDisplayInfo / Item.dbc / CMSG_ITEM_QUERY)
// Used in ItemDef::inventoryType and equipment update packets.
namespace InvType {
    constexpr uint8_t NON_EQUIP     = 0;   // Not equippable / unarmed
    constexpr uint8_t HEAD          = 1;
    constexpr uint8_t NECK          = 2;
    constexpr uint8_t SHOULDERS     = 3;
    constexpr uint8_t SHIRT         = 4;
    constexpr uint8_t CHEST         = 5;   // Chest armor
    constexpr uint8_t WAIST         = 6;
    constexpr uint8_t LEGS          = 7;
    constexpr uint8_t FEET          = 8;
    constexpr uint8_t WRISTS        = 9;
    constexpr uint8_t HANDS         = 10;
    constexpr uint8_t FINGER        = 11;  // Ring
    constexpr uint8_t TRINKET       = 12;
    constexpr uint8_t ONE_HAND      = 13;  // One-handed weapon (sword, mace, dagger, fist)
    constexpr uint8_t SHIELD        = 14;
    constexpr uint8_t RANGED_BOW    = 15;  // Bow
    constexpr uint8_t BACK          = 16;  // Cloak
    constexpr uint8_t TWO_HAND      = 17;  // Two-handed weapon (also polearm/staff by inventoryType alone)
    constexpr uint8_t BAG           = 18;
    constexpr uint8_t TABARD        = 19;
    constexpr uint8_t ROBE          = 20;  // Chest (robe variant)
    constexpr uint8_t MAIN_HAND     = 21;  // Main-hand only weapon
    constexpr uint8_t OFF_HAND      = 22;  // Off-hand (held-in-off-hand items, not weapons)
    constexpr uint8_t HOLDABLE      = 23;  // Off-hand holdable (books, orbs)
    constexpr uint8_t AMMO          = 24;
    constexpr uint8_t THROWN        = 25;
    constexpr uint8_t RANGED_GUN    = 26;  // Gun / Crossbow / Wand
} // namespace InvType

struct ItemDef {
    uint32_t itemId = 0;
    std::string name;
    std::string subclassName;  // "Sword", "Mace", "Shield", etc.
    ItemQuality quality = ItemQuality::COMMON;
    uint8_t inventoryType = 0;
    uint32_t stackCount = 1;
    uint32_t maxStack = 1;
    uint32_t bagSlots = 0;
    float damageMin = 0.0f;
    float damageMax = 0.0f;
    uint32_t delayMs = 0;
    // Stats
    int32_t armor = 0;
    int32_t stamina = 0;
    int32_t strength = 0;
    int32_t agility = 0;
    int32_t intellect = 0;
    int32_t spirit = 0;
    uint32_t displayInfoId = 0;
    uint32_t sellPrice = 0;
    uint32_t curDurability = 0;
    uint32_t maxDurability = 0;
    uint32_t itemLevel = 0;
    uint32_t requiredLevel = 0;
    uint32_t bindType = 0;      // 0=none, 1=BoP, 2=BoE, 3=BoU, 4=BoQ
    // Per-instance bound state from ITEM_FIELD_FLAGS bit 0x1 (ITEM_FLAG_SOULBOUND).
    // A BoE item is bindType==2 but only prompts on equip while this is false; once the
    // server sets the soulbound bit (on equip), it is already bound and must not prompt.
    bool soulbound = false;
    // Per-instance ITEM_FIELD_RANDOM_PROPERTIES_ID: >0 → ItemRandomProperties.dbc (prefix),
    // <0 → ItemRandomSuffix.dbc (e.g. "of the Bear"). 0 means no random property rolled.
    int32_t randomPropertyId = 0;
    std::string description;    // Flavor/lore text shown in tooltip (italic yellow)
    uint32_t pageTextId = 0;     // Non-zero: item opens readable page text
    // Generic stat pairs for non-primary stats (hit, crit, haste, AP, SP, etc.)
    struct ExtraStat { uint32_t statType = 0; int32_t statValue = 0; };
    std::vector<ExtraStat> extraStats;
    uint32_t startQuestId = 0;  // Non-zero: item begins a quest
    // Exact server object identity for this displayed inventory slot. Item IDs
    // are not unique and must never be used to guess destructive operations.
    uint64_t guid = 0;
};

struct ItemSlot {
    ItemDef item;
    bool empty() const { return item.itemId == 0; }
};

class Inventory {
public:
    static constexpr int BACKPACK_SLOTS = 16;
    static constexpr int KEYRING_SLOTS = 32;
    // WoW slot layout: 0-22 are equipment (head, neck, ... tabard, mainhand, offhand, ranged, ammo).
    // Backpack inventory starts at slot 23 in bag 0xFF, so packet slot = NUM_EQUIP_SLOTS + backpackIndex.
    static constexpr int NUM_EQUIP_SLOTS = 23;
    // Bag containers occupy equipment slots 19-22 (bag1, bag2, bag3, bag4).
    // Packet bag byte = FIRST_BAG_EQUIP_SLOT + bagIndex.
    static constexpr int FIRST_BAG_EQUIP_SLOT = 19;
    static constexpr int NUM_BAG_SLOTS = 4;
    static constexpr int MAX_BAG_SIZE = 36;
    static constexpr int BANK_SLOTS = 28;
    static constexpr int BANK_BAG_SLOTS = 7;

    Inventory();

    // Backpack
    const ItemSlot& getBackpackSlot(int index) const;
    bool setBackpackSlot(int index, const ItemDef& item);
    bool clearBackpackSlot(int index);
    int getBackpackSize() const { return BACKPACK_SLOTS; }

    // Equipment
    const ItemSlot& getEquipSlot(EquipSlot slot) const;
    bool setEquipSlot(EquipSlot slot, const ItemDef& item);
    bool clearEquipSlot(EquipSlot slot);

    // Keyring
    const ItemSlot& getKeyringSlot(int index) const;
    bool setKeyringSlot(int index, const ItemDef& item);
    bool clearKeyringSlot(int index);
    int getKeyringSize() const { return KEYRING_SLOTS; }

    // Extra bags
    int getBagSize(int bagIndex) const;
    void setBagSize(int bagIndex, int size);
    // Special containers (quivers, ammo pouches, profession bags) only accept
    // their own item type: sorting skips them and the UI marks their slots.
    bool isBagSpecial(int bagIndex) const;
    void setBagSpecial(int bagIndex, bool special);
    const ItemSlot& getBagSlot(int bagIndex, int slotIndex) const;
    bool setBagSlot(int bagIndex, int slotIndex, const ItemDef& item);
    bool clearBagSlot(int bagIndex, int slotIndex);

    // Bank slots (28 main + 7 bank bags)
    const ItemSlot& getBankSlot(int index) const;
    bool setBankSlot(int index, const ItemDef& item);
    bool clearBankSlot(int index);

    const ItemSlot& getBankBagSlot(int bagIndex, int slotIndex) const;
    bool setBankBagSlot(int bagIndex, int slotIndex, const ItemDef& item);
    bool clearBankBagSlot(int bagIndex, int slotIndex);
    int getBankBagSize(int bagIndex) const;
    void setBankBagSize(int bagIndex, int size);
    const ItemSlot& getBankBagItem(int bagIndex) const;
    void setBankBagItem(int bagIndex, const ItemDef& item);

    uint8_t getPurchasedBankBagSlots() const { return purchasedBankBagSlots_; }
    void setPurchasedBankBagSlots(uint8_t count) { purchasedBankBagSlots_ = count; }

    // Swap two bag slots (equip items + contents)
    void swapBagContents(int bagA, int bagB);

    // Utility
    int findFreeBackpackSlot() const;
    bool addItem(const ItemDef& item);

    // Sort all bag slots (backpack + equip bags) by quality desc → itemId asc → stackCount desc.
    // Purely client-side: reorders the local inventory struct without server interaction.
    void sortBags();

    // Sort the bank the same way (main bank slots + bank bag contents). mainSlotCount is the
    // number of usable main bank slots for the active expansion (24 Classic, 28 TBC/WotLK) so
    // sorting never spills items into slots the server doesn't have.
    void sortBank(int mainSlotCount);

    // Sort one bank bag's contents in place, leaving the rest of the bank alone.
    // Sorting the whole bank pools everything into the main slots, which is the
    // wrong tool when a bag is being kept as a deliberate category.
    void sortBankBag(int bagIndex);

    // A single swap operation using WoW bag/slot addressing (for CMSG_SWAP_ITEM).
    struct SwapOp {
        uint8_t srcBag;
        uint8_t srcSlot;
        uint8_t dstBag;
        uint8_t dstSlot;
    };

    // Pour partial stacks of the same item together, so two half stacks become
    // one. Dropping a stack onto another of the same item is a swap as far as the
    // wire is concerned — the server merges what fits and leaves the rest behind —
    // so the returned ops go through the same queue as a sort.
    //
    // Unlike the sort, this both plans and applies: keeping the plan and the local
    // preview in one pass means the two cannot disagree.
    std::vector<SwapOp> mergePartialStacks();
    std::vector<SwapOp> mergeBankPartialStacks(int mainSlotCount);

    // Compute the CMSG_SWAP_ITEM operations needed to reach sorted order.
    // Does NOT modify the inventory — caller is responsible for sending packets.
    std::vector<SwapOp> computeSortSwaps() const;
    std::vector<SwapOp> computeBankSortSwaps(int mainSlotCount) const;
    std::vector<SwapOp> computeBankBagSortSwaps(int bagIndex) const;

    // WoW bag/slot addressing for bank storage (used by sort + drag-drop):
    // main bank slots live in bag 0xFF at slot BANK_SLOT_START + index; each bank bag's
    // contents live in bag BANK_BAG_CONTAINER_START + bagIndex.
    static constexpr uint8_t BANK_SLOT_START = 39;
    static constexpr uint8_t BANK_BAG_CONTAINER_START = 67;

    // Test data
    void populateTestItems();

private:
    std::array<ItemSlot, BACKPACK_SLOTS> backpack{};
    std::array<ItemSlot, KEYRING_SLOTS> keyring_{};
    std::array<ItemSlot, NUM_EQUIP_SLOTS> equipment{};

    struct BagData {
        int size = 0;
        bool special = false;  // Quiver/ammo pouch/profession bag — restricted contents
        ItemSlot bagItem;  // The bag item itself (for icon/name/tooltip)
        std::array<ItemSlot, MAX_BAG_SIZE> slots{};
    };
    std::array<BagData, NUM_BAG_SLOTS> bags{};

    // Bank
    std::array<ItemSlot, BANK_SLOTS> bankSlots_{};
    std::array<BagData, BANK_BAG_SLOTS> bankBags_{};
    uint8_t purchasedBankBagSlots_ = 0;
};

const char* getQualityName(ItemQuality quality);
const char* getEquipSlotName(EquipSlot slot);

} // namespace game
} // namespace wowee
