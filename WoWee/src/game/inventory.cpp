#include "game/inventory.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <unordered_map>

namespace wowee {
namespace game {

static const ItemSlot EMPTY_SLOT{};

Inventory::Inventory() = default;

const ItemSlot& Inventory::getBackpackSlot(int index) const {
    if (index < 0 || index >= BACKPACK_SLOTS) return EMPTY_SLOT;
    return backpack[index];
}

bool Inventory::setBackpackSlot(int index, const ItemDef& item) {
    if (index < 0 || index >= BACKPACK_SLOTS) return false;
    backpack[index].item = item;
    return true;
}

bool Inventory::clearBackpackSlot(int index) {
    if (index < 0 || index >= BACKPACK_SLOTS) return false;
    backpack[index].item = ItemDef{};
    return true;
}

const ItemSlot& Inventory::getEquipSlot(EquipSlot slot) const {
    int idx = static_cast<int>(slot);
    if (idx < 0 || idx >= NUM_EQUIP_SLOTS) return EMPTY_SLOT;
    return equipment[idx];
}

bool Inventory::setEquipSlot(EquipSlot slot, const ItemDef& item) {
    int idx = static_cast<int>(slot);
    if (idx < 0 || idx >= NUM_EQUIP_SLOTS) return false;
    equipment[idx].item = item;
    return true;
}

bool Inventory::clearEquipSlot(EquipSlot slot) {
    int idx = static_cast<int>(slot);
    if (idx < 0 || idx >= NUM_EQUIP_SLOTS) return false;
    equipment[idx].item = ItemDef{};
    return true;
}

const ItemSlot& Inventory::getKeyringSlot(int index) const {
    if (index < 0 || index >= KEYRING_SLOTS) return EMPTY_SLOT;
    return keyring_[index];
}

bool Inventory::setKeyringSlot(int index, const ItemDef& item) {
    if (index < 0 || index >= KEYRING_SLOTS) return false;
    keyring_[index].item = item;
    return true;
}

bool Inventory::clearKeyringSlot(int index) {
    if (index < 0 || index >= KEYRING_SLOTS) return false;
    keyring_[index].item = ItemDef{};
    return true;
}

int Inventory::getBagSize(int bagIndex) const {
    if (bagIndex < 0 || bagIndex >= NUM_BAG_SLOTS) return 0;
    return bags[bagIndex].size;
}

void Inventory::setBagSize(int bagIndex, int size) {
    if (bagIndex < 0 || bagIndex >= NUM_BAG_SLOTS) return;
    bags[bagIndex].size = std::min(size, MAX_BAG_SIZE);
}

bool Inventory::isBagSpecial(int bagIndex) const {
    if (bagIndex < 0 || bagIndex >= NUM_BAG_SLOTS) return false;
    return bags[bagIndex].special;
}

void Inventory::setBagSpecial(int bagIndex, bool special) {
    if (bagIndex < 0 || bagIndex >= NUM_BAG_SLOTS) return;
    bags[bagIndex].special = special;
}

const ItemSlot& Inventory::getBagSlot(int bagIndex, int slotIndex) const {
    if (bagIndex < 0 || bagIndex >= NUM_BAG_SLOTS) return EMPTY_SLOT;
    if (slotIndex < 0 || slotIndex >= bags[bagIndex].size) return EMPTY_SLOT;
    return bags[bagIndex].slots[slotIndex];
}

bool Inventory::setBagSlot(int bagIndex, int slotIndex, const ItemDef& item) {
    if (bagIndex < 0 || bagIndex >= NUM_BAG_SLOTS) return false;
    if (slotIndex < 0 || slotIndex >= bags[bagIndex].size) return false;
    bags[bagIndex].slots[slotIndex].item = item;
    return true;
}

bool Inventory::clearBagSlot(int bagIndex, int slotIndex) {
    if (bagIndex < 0 || bagIndex >= NUM_BAG_SLOTS) return false;
    if (slotIndex < 0 || slotIndex >= bags[bagIndex].size) return false;
    bags[bagIndex].slots[slotIndex].item = ItemDef{};
    return true;
}

const ItemSlot& Inventory::getBankSlot(int index) const {
    if (index < 0 || index >= BANK_SLOTS) return EMPTY_SLOT;
    return bankSlots_[index];
}

bool Inventory::setBankSlot(int index, const ItemDef& item) {
    if (index < 0 || index >= BANK_SLOTS) return false;
    bankSlots_[index].item = item;
    return true;
}

bool Inventory::clearBankSlot(int index) {
    if (index < 0 || index >= BANK_SLOTS) return false;
    bankSlots_[index].item = ItemDef{};
    return true;
}

const ItemSlot& Inventory::getBankBagSlot(int bagIndex, int slotIndex) const {
    if (bagIndex < 0 || bagIndex >= BANK_BAG_SLOTS) return EMPTY_SLOT;
    if (slotIndex < 0 || slotIndex >= bankBags_[bagIndex].size) return EMPTY_SLOT;
    return bankBags_[bagIndex].slots[slotIndex];
}

bool Inventory::setBankBagSlot(int bagIndex, int slotIndex, const ItemDef& item) {
    if (bagIndex < 0 || bagIndex >= BANK_BAG_SLOTS) return false;
    if (slotIndex < 0 || slotIndex >= bankBags_[bagIndex].size) return false;
    bankBags_[bagIndex].slots[slotIndex].item = item;
    return true;
}

bool Inventory::clearBankBagSlot(int bagIndex, int slotIndex) {
    if (bagIndex < 0 || bagIndex >= BANK_BAG_SLOTS) return false;
    if (slotIndex < 0 || slotIndex >= bankBags_[bagIndex].size) return false;
    bankBags_[bagIndex].slots[slotIndex].item = ItemDef{};
    return true;
}

int Inventory::getBankBagSize(int bagIndex) const {
    if (bagIndex < 0 || bagIndex >= BANK_BAG_SLOTS) return 0;
    return bankBags_[bagIndex].size;
}

void Inventory::setBankBagSize(int bagIndex, int size) {
    if (bagIndex < 0 || bagIndex >= BANK_BAG_SLOTS) return;
    bankBags_[bagIndex].size = std::min(size, MAX_BAG_SIZE);
}

const ItemSlot& Inventory::getBankBagItem(int bagIndex) const {
    static const ItemSlot EMPTY_SLOT;
    if (bagIndex < 0 || bagIndex >= BANK_BAG_SLOTS) return EMPTY_SLOT;
    return bankBags_[bagIndex].bagItem;
}

void Inventory::setBankBagItem(int bagIndex, const ItemDef& item) {
    if (bagIndex < 0 || bagIndex >= BANK_BAG_SLOTS) return;
    bankBags_[bagIndex].bagItem.item = item;
}

void Inventory::swapBagContents(int bagA, int bagB) {
    if (bagA < 0 || bagA >= NUM_BAG_SLOTS || bagB < 0 || bagB >= NUM_BAG_SLOTS) return;
    if (bagA == bagB) return;
    std::swap(bags[bagA], bags[bagB]);
}

int Inventory::findFreeBackpackSlot() const {
    for (int i = 0; i < BACKPACK_SLOTS; i++) {
        if (backpack[i].empty()) return i;
    }
    return -1;
}

bool Inventory::addItem(const ItemDef& item) {
    // Try stacking first
    if (item.maxStack > 1) {
        for (int i = 0; i < BACKPACK_SLOTS; i++) {
            if (!backpack[i].empty() &&
                backpack[i].item.itemId == item.itemId &&
                backpack[i].item.stackCount < backpack[i].item.maxStack) {
                uint32_t space = backpack[i].item.maxStack - backpack[i].item.stackCount;
                uint32_t toAdd = std::min(space, item.stackCount);
                backpack[i].item.stackCount += toAdd;
                if (toAdd >= item.stackCount) return true;
                // Remaining needs a new slot — fall through
            }
        }
    }

    int slot = findFreeBackpackSlot();
    if (slot < 0) return false;
    backpack[slot].item = item;
    return true;
}

namespace {

/// One candidate slot for merging, addressed the way CMSG_SWAP_ITEM wants it.
struct MergeEntry {
    uint8_t bag;
    uint8_t slot;
    ItemSlot* ref;
};

/// Pour later partial stacks into earlier ones until each item has at most one
/// partial left. Emits a swap per pour, which is what the server acts on.
std::vector<Inventory::SwapOp> mergeEntries(std::vector<MergeEntry>& entries) {
    std::unordered_map<uint32_t, std::vector<size_t>> byItem;
    for (size_t i = 0; i < entries.size(); ++i) {
        const ItemDef& item = entries[i].ref->item;
        if (entries[i].ref->empty()) continue;
        // A stack of one is not a stack, and a full one has nowhere to go.
        if (item.maxStack <= 1 || item.stackCount >= item.maxStack) continue;
        byItem[item.itemId].push_back(i);
    }

    std::vector<Inventory::SwapOp> ops;
    for (auto& [itemId, indices] : byItem) {
        (void)itemId;
        if (indices.size() < 2) continue;
        size_t lo = 0;
        size_t hi = indices.size() - 1;
        while (lo < hi) {
            ItemSlot& dst = *entries[indices[lo]].ref;
            ItemSlot& src = *entries[indices[hi]].ref;

            const uint32_t space = dst.item.maxStack > dst.item.stackCount
                                       ? dst.item.maxStack - dst.item.stackCount : 0;
            if (space == 0) { ++lo; continue; }
            if (src.empty() || src.item.stackCount == 0) { --hi; continue; }

            const uint32_t moved = std::min(space, src.item.stackCount);
            dst.item.stackCount += moved;
            src.item.stackCount -= moved;
            ops.push_back({entries[indices[hi]].bag, entries[indices[hi]].slot,
                           entries[indices[lo]].bag, entries[indices[lo]].slot});

            if (src.item.stackCount == 0) { src.item = ItemDef{}; --hi; }
            if (dst.item.stackCount >= dst.item.maxStack) ++lo;
        }
    }
    return ops;
}

} // namespace

std::vector<Inventory::SwapOp> Inventory::mergePartialStacks() {
    std::vector<MergeEntry> entries;
    entries.reserve(BACKPACK_SLOTS + NUM_BAG_SLOTS * MAX_BAG_SIZE);

    for (int i = 0; i < BACKPACK_SLOTS; ++i) {
        entries.push_back({0xFF, static_cast<uint8_t>(NUM_EQUIP_SLOTS + i), &backpack[i]});
    }
    for (int b = 0; b < NUM_BAG_SLOTS; ++b) {
        if (bags[b].special) continue;  // must match sortBags(): quivers stay as they are
        for (int s = 0; s < bags[b].size; ++s) {
            entries.push_back({static_cast<uint8_t>(FIRST_BAG_EQUIP_SLOT + b),
                               static_cast<uint8_t>(s), &bags[b].slots[s]});
        }
    }
    return mergeEntries(entries);
}

std::vector<Inventory::SwapOp> Inventory::mergeBankPartialStacks(int mainSlotCount) {
    if (mainSlotCount < 0) mainSlotCount = 0;
    if (mainSlotCount > BANK_SLOTS) mainSlotCount = BANK_SLOTS;

    std::vector<MergeEntry> entries;
    entries.reserve(BANK_SLOTS + BANK_BAG_SLOTS * MAX_BAG_SIZE);

    for (int i = 0; i < mainSlotCount; ++i) {
        entries.push_back({0xFF, static_cast<uint8_t>(BANK_SLOT_START + i), &bankSlots_[i]});
    }
    for (int b = 0; b < BANK_BAG_SLOTS; ++b) {
        if (bankBags_[b].special) continue;
        for (int s = 0; s < bankBags_[b].size; ++s) {
            entries.push_back({static_cast<uint8_t>(BANK_BAG_CONTAINER_START + b),
                               static_cast<uint8_t>(s), &bankBags_[b].slots[s]});
        }
    }
    return mergeEntries(entries);
}

void Inventory::sortBags() {
    // Collect all items from backpack and equip bags into a flat list.
    std::vector<ItemDef> items;
    items.reserve(BACKPACK_SLOTS + NUM_BAG_SLOTS * MAX_BAG_SIZE);

    for (int i = 0; i < BACKPACK_SLOTS; ++i) {
        if (!backpack[i].empty())
            items.push_back(backpack[i].item);
    }
    for (int b = 0; b < NUM_BAG_SLOTS; ++b) {
        if (bags[b].special) continue;  // Quivers etc. keep their contents in place
        for (int s = 0; s < bags[b].size; ++s) {
            if (!bags[b].slots[s].empty())
                items.push_back(bags[b].slots[s].item);
        }
    }

    // Sort: quality descending → itemId ascending → stackCount descending.
    std::stable_sort(items.begin(), items.end(), [](const ItemDef& a, const ItemDef& b) {
        if (a.quality != b.quality)
            return static_cast<int>(a.quality) > static_cast<int>(b.quality);
        if (a.itemId != b.itemId)
            return a.itemId < b.itemId;
        return a.stackCount > b.stackCount;
    });

    // Write sorted items back, filling backpack first then equip bags.
    int idx = 0;
    int n = static_cast<int>(items.size());

    for (int i = 0; i < BACKPACK_SLOTS; ++i)
        backpack[i].item = (idx < n) ? items[idx++] : ItemDef{};

    for (int b = 0; b < NUM_BAG_SLOTS; ++b) {
        if (bags[b].special) continue;
        for (int s = 0; s < bags[b].size; ++s)
            bags[b].slots[s].item = (idx < n) ? items[idx++] : ItemDef{};
    }
}

std::vector<Inventory::SwapOp> Inventory::computeSortSwaps() const {
    // Build a flat list of (bag, slot, item) entries matching the same traversal
    // order as sortBags(): backpack first, then equip bags in order.
    struct Entry {
        uint8_t bag;   // WoW bag address: 0xFF=backpack, 19+i=equip bag i
        uint8_t slot;  // WoW slot address: 23+i for backpack, slotIndex for bags
        uint32_t itemId;
        ItemQuality quality;
        uint32_t stackCount;
    };

    std::vector<Entry> entries;
    entries.reserve(BACKPACK_SLOTS + NUM_BAG_SLOTS * MAX_BAG_SIZE);

    for (int i = 0; i < BACKPACK_SLOTS; ++i) {
        entries.push_back({0xFF, static_cast<uint8_t>(NUM_EQUIP_SLOTS + i),
                           backpack[i].item.itemId, backpack[i].item.quality,
                           backpack[i].item.stackCount});
    }
    for (int b = 0; b < NUM_BAG_SLOTS; ++b) {
        if (bags[b].special) continue;  // must match sortBags(): never touch restricted bags
        for (int s = 0; s < bags[b].size; ++s) {
            entries.push_back({static_cast<uint8_t>(FIRST_BAG_EQUIP_SLOT + b), static_cast<uint8_t>(s),
                               bags[b].slots[s].item.itemId, bags[b].slots[s].item.quality,
                               bags[b].slots[s].item.stackCount});
        }
    }

    // Build a sorted index array using the same comparator as sortBags().
    int n = static_cast<int>(entries.size());
    std::vector<int> sortedIdx(n);
    for (int i = 0; i < n; ++i) sortedIdx[i] = i;

    // Separate non-empty items and empty slots, then sort the non-empty items.
    // Items are sorted by quality desc -> itemId asc -> stackCount desc.
    // Empty slots go to the end.
    std::stable_sort(sortedIdx.begin(), sortedIdx.end(), [&](int a, int b) {
        bool aEmpty = (entries[a].itemId == 0);
        bool bEmpty = (entries[b].itemId == 0);
        if (aEmpty != bEmpty) return bEmpty; // non-empty before empty
        if (aEmpty) return false; // both empty: preserve order
        // Both non-empty: same comparator as sortBags()
        if (entries[a].quality != entries[b].quality)
            return static_cast<int>(entries[a].quality) > static_cast<int>(entries[b].quality);
        if (entries[a].itemId != entries[b].itemId)
            return entries[a].itemId < entries[b].itemId;
        return entries[a].stackCount > entries[b].stackCount;
    });

    // sortedIdx[targetPos] = sourcePos means the item currently at sourcePos
    // needs to end up at targetPos.  We use selection-sort-style swaps to
    // permute current positions into sorted order, tracking where items move.

    // posOf[i] = current position of the item that was originally at index i
    std::vector<int> posOf(n);
    for (int i = 0; i < n; ++i) posOf[i] = i;

    // invPos[p] = which original item index is currently sitting at position p
    std::vector<int> invPos(n);
    for (int i = 0; i < n; ++i) invPos[i] = i;

    std::vector<SwapOp> swaps;

    for (int target = 0; target < n; ++target) {
        int need = sortedIdx[target]; // original index that should be at 'target'
        int cur = invPos[target];     // original index currently at 'target'
        if (cur == need) continue;    // already in place

        // Skip swaps between two empty slots
        if (entries[cur].itemId == 0 && entries[need].itemId == 0) continue;

        int srcPos = posOf[need]; // current position of the item we need

        // Emit a swap between position srcPos and position target
        swaps.push_back({entries[srcPos].bag, entries[srcPos].slot,
                         entries[target].bag, entries[target].slot});

        // Update tracking arrays
        posOf[cur] = srcPos;
        posOf[need] = target;
        invPos[srcPos] = cur;
        invPos[target] = need;
    }

    return swaps;
}

void Inventory::sortBank(int mainSlotCount) {
    if (mainSlotCount < 0) mainSlotCount = 0;
    if (mainSlotCount > BANK_SLOTS) mainSlotCount = BANK_SLOTS;

    // Collect all items from the main bank and every (non-special) bank bag.
    std::vector<ItemDef> items;
    items.reserve(BANK_SLOTS + BANK_BAG_SLOTS * MAX_BAG_SIZE);

    for (int i = 0; i < mainSlotCount; ++i) {
        if (!bankSlots_[i].empty())
            items.push_back(bankSlots_[i].item);
    }
    for (int b = 0; b < BANK_BAG_SLOTS; ++b) {
        if (bankBags_[b].special) continue;  // Special containers keep their contents in place
        for (int s = 0; s < bankBags_[b].size; ++s) {
            if (!bankBags_[b].slots[s].empty())
                items.push_back(bankBags_[b].slots[s].item);
        }
    }

    // Same ordering as sortBags(): quality desc → itemId asc → stackCount desc.
    std::stable_sort(items.begin(), items.end(), [](const ItemDef& a, const ItemDef& b) {
        if (a.quality != b.quality)
            return static_cast<int>(a.quality) > static_cast<int>(b.quality);
        if (a.itemId != b.itemId)
            return a.itemId < b.itemId;
        return a.stackCount > b.stackCount;
    });

    // Write sorted items back, filling main bank first then bank bags.
    int idx = 0;
    int n = static_cast<int>(items.size());

    for (int i = 0; i < mainSlotCount; ++i)
        bankSlots_[i].item = (idx < n) ? items[idx++] : ItemDef{};

    for (int b = 0; b < BANK_BAG_SLOTS; ++b) {
        if (bankBags_[b].special) continue;
        for (int s = 0; s < bankBags_[b].size; ++s)
            bankBags_[b].slots[s].item = (idx < n) ? items[idx++] : ItemDef{};
    }
}

std::vector<Inventory::SwapOp> Inventory::computeBankSortSwaps(int mainSlotCount) const {
    if (mainSlotCount < 0) mainSlotCount = 0;
    if (mainSlotCount > BANK_SLOTS) mainSlotCount = BANK_SLOTS;

    // Build a flat list matching the same traversal order as sortBank():
    // main bank slots first, then bank bag contents in order.
    struct Entry {
        uint8_t bag;   // WoW bag address: 0xFF=main bank, BANK_BAG_CONTAINER_START+b=bank bag b
        uint8_t slot;  // WoW slot address: BANK_SLOT_START+i for main bank, slotIndex for bags
        uint32_t itemId;
        ItemQuality quality;
        uint32_t stackCount;
    };

    std::vector<Entry> entries;
    entries.reserve(BANK_SLOTS + BANK_BAG_SLOTS * MAX_BAG_SIZE);

    for (int i = 0; i < mainSlotCount; ++i) {
        entries.push_back({0xFF, static_cast<uint8_t>(BANK_SLOT_START + i),
                           bankSlots_[i].item.itemId, bankSlots_[i].item.quality,
                           bankSlots_[i].item.stackCount});
    }
    for (int b = 0; b < BANK_BAG_SLOTS; ++b) {
        if (bankBags_[b].special) continue;  // must match sortBank(): never touch restricted bags
        for (int s = 0; s < bankBags_[b].size; ++s) {
            entries.push_back({static_cast<uint8_t>(BANK_BAG_CONTAINER_START + b), static_cast<uint8_t>(s),
                               bankBags_[b].slots[s].item.itemId, bankBags_[b].slots[s].item.quality,
                               bankBags_[b].slots[s].item.stackCount});
        }
    }

    int n = static_cast<int>(entries.size());
    std::vector<int> sortedIdx(n);
    for (int i = 0; i < n; ++i) sortedIdx[i] = i;

    // Non-empty items sort by quality desc → itemId asc → stackCount desc; empties go to the end.
    std::stable_sort(sortedIdx.begin(), sortedIdx.end(), [&](int a, int b) {
        bool aEmpty = (entries[a].itemId == 0);
        bool bEmpty = (entries[b].itemId == 0);
        if (aEmpty != bEmpty) return bEmpty; // non-empty before empty
        if (aEmpty) return false;            // both empty: preserve order
        if (entries[a].quality != entries[b].quality)
            return static_cast<int>(entries[a].quality) > static_cast<int>(entries[b].quality);
        if (entries[a].itemId != entries[b].itemId)
            return entries[a].itemId < entries[b].itemId;
        return entries[a].stackCount > entries[b].stackCount;
    });

    // Selection-sort-style permutation into sorted order, tracking where items move.
    std::vector<int> posOf(n);
    for (int i = 0; i < n; ++i) posOf[i] = i;
    std::vector<int> invPos(n);
    for (int i = 0; i < n; ++i) invPos[i] = i;

    std::vector<SwapOp> swaps;

    for (int target = 0; target < n; ++target) {
        int need = sortedIdx[target]; // original index that should be at 'target'
        int cur = invPos[target];     // original index currently at 'target'
        if (cur == need) continue;    // already in place

        // Skip swaps between two empty slots
        if (entries[cur].itemId == 0 && entries[need].itemId == 0) continue;

        int srcPos = posOf[need]; // current position of the item we need

        swaps.push_back({entries[srcPos].bag, entries[srcPos].slot,
                         entries[target].bag, entries[target].slot});

        posOf[cur] = srcPos;
        posOf[need] = target;
        invPos[srcPos] = cur;
        invPos[target] = need;
    }

    return swaps;
}

// Sort one bank bag in place. sortBank() pools every item into the main slots,
// which destroys a bag being used as a deliberate category — a bag of herbs
// stays a bag of herbs, just in order.
void Inventory::sortBankBag(int bagIndex) {
    if (bagIndex < 0 || bagIndex >= BANK_BAG_SLOTS) return;
    BagData& bag = bankBags_[bagIndex];
    if (bag.special) return;  // Restricted containers keep their contents in place

    std::vector<ItemDef> items;
    items.reserve(static_cast<size_t>(bag.size));
    for (int s = 0; s < bag.size; ++s) {
        if (!bag.slots[s].empty()) items.push_back(bag.slots[s].item);
    }

    // Same ordering as sortBags(): quality desc → itemId asc → stackCount desc.
    std::stable_sort(items.begin(), items.end(), [](const ItemDef& a, const ItemDef& b) {
        if (a.quality != b.quality)
            return static_cast<int>(a.quality) > static_cast<int>(b.quality);
        if (a.itemId != b.itemId)
            return a.itemId < b.itemId;
        return a.stackCount > b.stackCount;
    });

    int idx = 0;
    const int n = static_cast<int>(items.size());
    for (int s = 0; s < bag.size; ++s)
        bag.slots[s].item = (idx < n) ? items[idx++] : ItemDef{};
}

std::vector<Inventory::SwapOp> Inventory::computeBankBagSortSwaps(int bagIndex) const {
    std::vector<SwapOp> swaps;
    if (bagIndex < 0 || bagIndex >= BANK_BAG_SLOTS) return swaps;
    const BagData& bag = bankBags_[bagIndex];
    if (bag.special) return swaps;  // must match sortBankBag(): never touch restricted bags

    struct Entry {
        uint8_t slot;
        uint32_t itemId;
        ItemQuality quality;
        uint32_t stackCount;
    };

    std::vector<Entry> entries;
    entries.reserve(static_cast<size_t>(bag.size));
    for (int s = 0; s < bag.size; ++s) {
        entries.push_back({static_cast<uint8_t>(s), bag.slots[s].item.itemId,
                           bag.slots[s].item.quality, bag.slots[s].item.stackCount});
    }

    const int n = static_cast<int>(entries.size());
    std::vector<int> sortedIdx(n);
    for (int i = 0; i < n; ++i) sortedIdx[i] = i;

    // Non-empty items sort by quality desc → itemId asc → stackCount desc; empties go to the end.
    std::stable_sort(sortedIdx.begin(), sortedIdx.end(), [&](int a, int b) {
        const bool aEmpty = (entries[a].itemId == 0);
        const bool bEmpty = (entries[b].itemId == 0);
        if (aEmpty != bEmpty) return bEmpty;
        if (aEmpty) return false;
        if (entries[a].quality != entries[b].quality)
            return static_cast<int>(entries[a].quality) > static_cast<int>(entries[b].quality);
        if (entries[a].itemId != entries[b].itemId)
            return entries[a].itemId < entries[b].itemId;
        return entries[a].stackCount > entries[b].stackCount;
    });

    const uint8_t bagAddr = static_cast<uint8_t>(BANK_BAG_CONTAINER_START + bagIndex);

    std::vector<int> posOf(n);
    for (int i = 0; i < n; ++i) posOf[i] = i;
    std::vector<int> invPos(n);
    for (int i = 0; i < n; ++i) invPos[i] = i;

    for (int target = 0; target < n; ++target) {
        const int need = sortedIdx[target];
        const int cur = invPos[target];
        if (cur == need) continue;
        if (entries[cur].itemId == 0 && entries[need].itemId == 0) continue;

        const int srcPos = posOf[need];
        swaps.push_back({bagAddr, entries[srcPos].slot, bagAddr, entries[target].slot});

        posOf[cur] = srcPos;
        posOf[need] = target;
        invPos[srcPos] = cur;
        invPos[target] = need;
    }

    return swaps;
}

void Inventory::populateTestItems() {
    // Equipment
    {
        ItemDef sword;
        sword.itemId = 25;
        sword.name = "Worn Shortsword";
        sword.quality = ItemQuality::COMMON;
        sword.inventoryType = 21; // Main Hand
        sword.strength = 1;
        sword.displayInfoId = 1542;    // Sword_1H_Short_A_02.m2
        sword.subclassName = "Sword";
        setEquipSlot(EquipSlot::MAIN_HAND, sword);
    }
    {
        ItemDef shield;
        shield.itemId = 2129;
        shield.name = "Large Round Shield";
        shield.quality = ItemQuality::COMMON;
        shield.inventoryType = 14; // Off Hand (Shield)
        shield.armor = 18;
        shield.stamina = 1;
        shield.displayInfoId = 18662;  // Shield_Round_A_01.m2
        shield.subclassName = "Shield";
        setEquipSlot(EquipSlot::OFF_HAND, shield);
    }
    // Shirt/pants/boots in backpack (character model renders bare geosets)
    {
        ItemDef shirt;
        shirt.itemId = 38;
        shirt.name = "Recruit's Shirt";
        shirt.quality = ItemQuality::COMMON;
        shirt.inventoryType = 4; // Shirt
        shirt.displayInfoId = 2163;
        addItem(shirt);
    }
    {
        ItemDef pants;
        pants.itemId = 39;
        pants.name = "Recruit's Pants";
        pants.quality = ItemQuality::COMMON;
        pants.inventoryType = 7; // Legs
        pants.armor = 4;
        pants.displayInfoId = 1883;
        addItem(pants);
    }
    {
        ItemDef boots;
        boots.itemId = 40;
        boots.name = "Recruit's Boots";
        boots.quality = ItemQuality::COMMON;
        boots.inventoryType = 8; // Feet
        boots.armor = 3;
        boots.displayInfoId = 2166;
        addItem(boots);
    }

    // Backpack items
    {
        ItemDef potion;
        potion.itemId = 118;
        potion.name = "Minor Healing Potion";
        potion.quality = ItemQuality::COMMON;
        potion.stackCount = 3;
        potion.maxStack = 5;
        addItem(potion);
    }
    {
        ItemDef hearthstone;
        hearthstone.itemId = 6948;
        hearthstone.name = "Hearthstone";
        hearthstone.quality = ItemQuality::COMMON;
        addItem(hearthstone);
    }
    {
        ItemDef leather;
        leather.itemId = 2318;
        leather.name = "Light Leather";
        leather.quality = ItemQuality::COMMON;
        leather.stackCount = 5;
        leather.maxStack = 20;
        addItem(leather);
    }
    {
        ItemDef cloth;
        cloth.itemId = 2589;
        cloth.name = "Linen Cloth";
        cloth.quality = ItemQuality::COMMON;
        cloth.stackCount = 8;
        cloth.maxStack = 20;
        addItem(cloth);
    }
    {
        ItemDef quest;
        quest.itemId = 50000;
        quest.name = "Kobold Candle";
        quest.quality = ItemQuality::COMMON;
        quest.stackCount = 4;
        quest.maxStack = 10;
        addItem(quest);
    }
    {
        ItemDef ring;
        ring.itemId = 11287;
        ring.name = "Verdant Ring";
        ring.quality = ItemQuality::UNCOMMON;
        ring.inventoryType = 11; // Ring
        ring.stamina = 3;
        ring.spirit = 2;
        addItem(ring);
    }
    {
        ItemDef cloak;
        cloak.itemId = 2570;
        cloak.name = "Linen Cloak";
        cloak.quality = ItemQuality::UNCOMMON;
        cloak.inventoryType = 16; // Back
        cloak.armor = 10;
        cloak.agility = 1;
        cloak.displayInfoId = 15055;
        addItem(cloak);
    }
    {
        ItemDef rareAxe;
        rareAxe.itemId = 15268;
        rareAxe.name = "Stoneslayer";
        rareAxe.quality = ItemQuality::RARE;
        rareAxe.inventoryType = 17; // Two-Hand
        rareAxe.strength = 8;
        rareAxe.stamina = 7;
        rareAxe.subclassName = "Axe";
        rareAxe.displayInfoId = 782;    // Axe_2H_Battle_B_01.m2
        addItem(rareAxe);
    }

    LOG_INFO("Inventory: populated test items (2 equipped, 11 backpack)");
}

const char* getQualityName(ItemQuality quality) {
    switch (quality) {
        case ItemQuality::POOR:      return "Poor";
        case ItemQuality::COMMON:    return "Common";
        case ItemQuality::UNCOMMON:  return "Uncommon";
        case ItemQuality::RARE:      return "Rare";
        case ItemQuality::EPIC:      return "Epic";
        case ItemQuality::LEGENDARY: return "Legendary";
        case ItemQuality::ARTIFACT:  return "Artifact";
        case ItemQuality::HEIRLOOM:  return "Heirloom";
        default:                     return "Unknown";
    }
}

const char* getEquipSlotName(EquipSlot slot) {
    switch (slot) {
        case EquipSlot::HEAD:       return "Head";
        case EquipSlot::NECK:       return "Neck";
        case EquipSlot::SHOULDERS:  return "Shoulders";
        case EquipSlot::SHIRT:      return "Shirt";
        case EquipSlot::CHEST:      return "Chest";
        case EquipSlot::WAIST:      return "Waist";
        case EquipSlot::LEGS:       return "Legs";
        case EquipSlot::FEET:       return "Feet";
        case EquipSlot::WRISTS:     return "Wrists";
        case EquipSlot::HANDS:      return "Hands";
        case EquipSlot::RING1:      return "Ring 1";
        case EquipSlot::RING2:      return "Ring 2";
        case EquipSlot::TRINKET1:   return "Trinket 1";
        case EquipSlot::TRINKET2:   return "Trinket 2";
        case EquipSlot::BACK:       return "Back";
        case EquipSlot::MAIN_HAND:  return "Main Hand";
        case EquipSlot::OFF_HAND:   return "Off Hand";
        case EquipSlot::RANGED:     return "Ranged";
        case EquipSlot::TABARD:     return "Tabard";
        case EquipSlot::BAG1:       return "Bag 1";
        case EquipSlot::BAG2:       return "Bag 2";
        case EquipSlot::BAG3:       return "Bag 3";
        case EquipSlot::BAG4:       return "Bag 4";
        default:                    return "Unknown";
    }
}

} // namespace game
} // namespace wowee
