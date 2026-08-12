#include "pipeline/wowee_item_flags.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'I', 'F', 'S'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wifs") {
        base += ".wifs";
    }
    return base;
}

uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
            static_cast<uint32_t>(r);
}

} // namespace

const WoweeItemFlags::Entry*
WoweeItemFlags::findById(uint32_t flagId) const {
    for (const auto& e : entries)
        if (e.flagId == flagId) return &e;
    return nullptr;
}

const WoweeItemFlags::Entry*
WoweeItemFlags::findByBit(uint32_t bitMask) const {
    for (const auto& e : entries)
        if (e.bitMask == bitMask) return &e;
    return nullptr;
}

std::vector<const WoweeItemFlags::Entry*>
WoweeItemFlags::decode(uint32_t flagsValue) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.bitMask == 0) continue;
        if ((flagsValue & e.bitMask) == e.bitMask) {
            out.push_back(&e);
        }
    }
    return out;
}

const char* WoweeItemFlags::flagKindName(uint8_t k) {
    switch (k) {
        case Quality: return "quality";
        case Drop:    return "drop";
        case Trade:   return "trade";
        case Magic:   return "magic";
        case Account: return "account";
        case Server:  return "server";
        case Misc:    return "misc";
        default:      return "unknown";
    }
}

bool WoweeItemFlagsLoader::save(const WoweeItemFlags& cat,
                                 const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.flagId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.bitMask);
        writePOD(os, e.flagKind);
        writePOD(os, e.isPositive);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeItemFlags WoweeItemFlagsLoader::load(const std::string& basePath) {
    WoweeItemFlags out;
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
        if (!readPOD(is, e.flagId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.bitMask) ||
            !readPOD(is, e.flagKind) ||
            !readPOD(is, e.isPositive) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeItemFlagsLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeItemFlags WoweeItemFlagsLoader::makeStandard(
    const std::string& catalogName) {
    using F = WoweeItemFlags;
    WoweeItemFlags c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t bit,
                    uint8_t kind, uint8_t positive, const char* desc) {
        F::Entry e;
        e.flagId = id; e.name = name; e.description = desc;
        e.bitMask = bit;
        e.flagKind = kind;
        e.isPositive = positive;
        e.iconColorRGBA = positive ? packRgba(100, 220, 100)
                                    : packRgba(220, 100, 100);
        c.entries.push_back(e);
    };
    // Canonical Item.dbc flag bits — values match the
    // standard 3.3.5a Item.dbc Flags constants.
    add(1, "NoLoot",         0x00000001u, F::Drop,    0,
        "Item never appears in random loot tables.");
    add(2, "Conjured",       0x00000002u, F::Magic,   1,
        "Item is mage-conjured (vanishes on death/logout).");
    add(3, "Lootable",       0x00000004u, F::Drop,    1,
        "Item shows the 'right-click to open' container icon.");
    add(4, "Wrapped",        0x00000008u, F::Magic,   1,
        "Item is gift-wrapped (opens to reveal contents).");
    add(5, "Heroic",         0x00000010u, F::Quality, 1,
        "Heroic-mode raid drop (heroic tooltip / quality border).");
    add(6, "Deprecated",     0x00000020u, F::Misc,    0,
        "Item is deprecated by the engine; should not drop.");
    add(7, "NoUserDestroy",  0x00000040u, F::Trade,   0,
        "Cannot be destroyed via right-click context menu.");
    add(8, "NoEquipCooldown",0x00000080u, F::Trade,   1,
        "Equipping does not trigger the on-equip use cooldown.");
    return c;
}

WoweeItemFlags WoweeItemFlagsLoader::makeBinding(
    const std::string& catalogName) {
    using F = WoweeItemFlags;
    WoweeItemFlags c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t bit,
                    uint8_t positive, const char* desc) {
        F::Entry e;
        e.flagId = id; e.name = name; e.description = desc;
        e.bitMask = bit;
        e.flagKind = F::Trade;
        e.isPositive = positive;
        e.iconColorRGBA = packRgba(180, 180, 240);   // binding blue
        c.entries.push_back(e);
    };
    // Binding flags — restrictive, isPositive=0 since they
    // limit trading.
    add(100, "BindOnPickup",  0x00000100u, 0,
        "Soulbound on pickup — cannot be traded.");
    add(101, "BindOnEquip",   0x00000200u, 0,
        "Soulbound on equip — tradeable in inventory only.");
    add(102, "BindOnUse",     0x00000400u, 0,
        "Soulbound on use — tradeable until first use.");
    add(103, "BindToAccount", 0x00000800u, 0,
        "Account-bound — tradeable across own characters only.");
    add(104, "Soulbound",     0x00001000u, 0,
        "Already soulbound (combined state, after pickup/equip/use).");
    return c;
}

WoweeItemFlags WoweeItemFlagsLoader::makeServer(
    const std::string& catalogName) {
    using F = WoweeItemFlags;
    WoweeItemFlags c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t bit,
                    const char* desc) {
        F::Entry e;
        e.flagId = id; e.name = name; e.description = desc;
        e.bitMask = bit;
        e.flagKind = F::Server;
        e.isPositive = 1;
        e.iconColorRGBA = packRgba(240, 100, 240);   // custom magenta
        c.entries.push_back(e);
    };
    // Server-custom flags in the upper bits (typically
    // unused by Blizzard so safe for server overlays).
    add(200, "Donator",        0x01000000u,
        "Server-custom: donator-only item, special tooltip.");
    add(201, "EventReward",    0x02000000u,
        "Server-custom: event-only drop (Hallow's End / Brewfest).");
    add(202, "Anniversary",    0x04000000u,
        "Server-custom: server anniversary commemorative item.");
    add(203, "Honored",        0x08000000u,
        "Server-custom: bonus stats for honored players.");
    add(204, "Heroic25man",    0x10000000u,
        "Server-custom: dropped from 25-Heroic raids only.");
    return c;
}

} // namespace pipeline
} // namespace wowee
