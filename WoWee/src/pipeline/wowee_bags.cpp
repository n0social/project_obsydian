#include "pipeline/wowee_bags.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'N', 'K'};
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
    if (n > (1u << 20)) return false;
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wbnk") {
        base += ".wbnk";
    }
    return base;
}

} // namespace

const WoweeBagSlot::Entry*
WoweeBagSlot::findById(uint32_t bagSlotId) const {
    for (const auto& e : entries)
        if (e.bagSlotId == bagSlotId) return &e;
    return nullptr;
}

const char* WoweeBagSlot::bagKindName(uint8_t k) {
    switch (k) {
        case Inventory: return "inventory";
        case Bank:      return "bank";
        case Keyring:   return "keyring";
        case Quiver:    return "quiver";
        case SoulShard: return "soul-shard";
        case Stable:    return "stable";
        case Reagent:   return "reagent";
        case Wallet:    return "wallet";
        default:        return "unknown";
    }
}

bool WoweeBagSlotLoader::save(const WoweeBagSlot& cat,
                               const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.bagSlotId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.bagKind);
        writePOD(os, e.containerSize);
        writePOD(os, e.displayOrder);
        writePOD(os, e.isUnlocked);
        writePOD(os, e.fixedBagItemId);
        writePOD(os, e.unlockCostCopper);
        writePOD(os, e.acceptsBagSubclassMask);
    }
    return os.good();
}

WoweeBagSlot WoweeBagSlotLoader::load(const std::string& basePath) {
    WoweeBagSlot out;
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
        if (!readPOD(is, e.bagSlotId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.bagKind) ||
            !readPOD(is, e.containerSize) ||
            !readPOD(is, e.displayOrder) ||
            !readPOD(is, e.isUnlocked) ||
            !readPOD(is, e.fixedBagItemId) ||
            !readPOD(is, e.unlockCostCopper) ||
            !readPOD(is, e.acceptsBagSubclassMask)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeBagSlotLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeBagSlot WoweeBagSlotLoader::makeStarter(
    const std::string& catalogName) {
    WoweeBagSlot c;
    c.name = catalogName;
    {
        // Main backpack — 16-slot fixed, item id 0 = built-in.
        WoweeBagSlot::Entry e;
        e.bagSlotId = 1;
        e.name = "MainBackpack";
        e.description = "Built-in 16-slot starter backpack — "
                         "always present, never empty.";
        e.bagKind = WoweeBagSlot::Inventory;
        e.containerSize = 16;
        e.displayOrder = 0;
        e.isUnlocked = 1;
        e.acceptsBagSubclassMask = 0;   // no equippable bag here
        c.entries.push_back(e);
    }
    auto addBagSlot = [&](uint32_t id, uint8_t order) {
        WoweeBagSlot::Entry e;
        e.bagSlotId = id;
        e.name = std::string("BagSlot") + std::to_string(order);
        e.description = std::string("Player-equippable bag slot ") +
                         std::to_string(order) +
                         " — accepts any generic container.";
        e.bagKind = WoweeBagSlot::Inventory;
        e.containerSize = 0;             // size determined by equipped bag
        e.displayOrder = order;
        e.isUnlocked = 1;
        e.acceptsBagSubclassMask =
            WoweeBagSlot::kAcceptsAnyContainer |
            WoweeBagSlot::kAcceptsHerb |
            WoweeBagSlot::kAcceptsEnchanting;
        c.entries.push_back(e);
    };
    addBagSlot(2, 1);
    addBagSlot(3, 2);
    addBagSlot(4, 3);
    addBagSlot(5, 4);
    return c;
}

WoweeBagSlot WoweeBagSlotLoader::makeBank(
    const std::string& catalogName) {
    WoweeBagSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint8_t order, uint8_t unlocked,
                    uint32_t cost) {
        WoweeBagSlot::Entry e;
        e.bagSlotId = id;
        e.name = std::string("BankBag") + std::to_string(order);
        e.description = std::string("Bank bag slot ") +
                         std::to_string(order) +
                         (unlocked
                            ? " — free, unlocked at character creation."
                            : " — requires gold purchase to unlock.");
        e.bagKind = WoweeBagSlot::Bank;
        e.containerSize = 0;
        e.displayOrder = order;
        e.isUnlocked = unlocked;
        e.unlockCostCopper = cost;
        e.acceptsBagSubclassMask =
            WoweeBagSlot::kAcceptsAnyContainer |
            WoweeBagSlot::kAcceptsHerb |
            WoweeBagSlot::kAcceptsEnchanting |
            WoweeBagSlot::kAcceptsEngineer |
            WoweeBagSlot::kAcceptsGem |
            WoweeBagSlot::kAcceptsMining |
            WoweeBagSlot::kAcceptsLeather |
            WoweeBagSlot::kAcceptsInscription;
        c.entries.push_back(e);
    };
    // Bank bag costs match canonical WoW bank bag costs:
    // slots 0+1 are free, then 10s, 1g, 10g, 25g, 50g, 100g.
    // 1g = 10000c; 1s = 100c.
    add(100, 0, 1, 0);
    add(101, 1, 1, 0);
    add(102, 2, 0,    1000);     // 10 silver
    add(103, 3, 0,   10000);     //  1 gold
    add(104, 4, 0,  100000);     // 10 gold
    add(105, 5, 0,  250000);     // 25 gold
    add(106, 6, 0,  500000);     // 50 gold
    add(107, 7, 0, 1000000);     // 100 gold
    return c;
}

WoweeBagSlot WoweeBagSlotLoader::makeSpecial(
    const std::string& catalogName) {
    WoweeBagSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t size, uint32_t mask, const char* desc) {
        WoweeBagSlot::Entry e;
        e.bagSlotId = id; e.name = name; e.description = desc;
        e.bagKind = kind;
        e.containerSize = size;
        e.acceptsBagSubclassMask = mask;
        c.entries.push_back(e);
    };
    add(200, "Keyring",        WoweeBagSlot::Keyring,   32, 0,
        "Fixed 32-slot keyring — accepts only key-class items "
        "(no equippable bag).");
    add(201, "SoulShardBag",   WoweeBagSlot::SoulShard, 0,
        WoweeBagSlot::kAcceptsSoulShard,
        "Warlock-only soul shard bag slot — accepts only "
        "Soul Shard Bag containers.");
    add(202, "ArrowQuiver",    WoweeBagSlot::Quiver,    0,
        WoweeBagSlot::kAcceptsQuiver | WoweeBagSlot::kAcceptsAmmoPouch,
        "Hunter-only ranged ammo slot — accepts quivers and "
        "ammo pouches (boost ranged attack speed).");
    add(203, "HuntersStable",  WoweeBagSlot::Stable,    5, 0,
        "5 hunter pet stable slots — only hunters can use this.");
    return c;
}

} // namespace pipeline
} // namespace wowee
