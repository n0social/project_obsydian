#include "pipeline/wowee_spell_cooldowns.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'C', 'D'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wscd") {
        base += ".wscd";
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

const WoweeSpellCooldown::Entry*
WoweeSpellCooldown::findById(uint32_t bucketId) const {
    for (const auto& e : entries)
        if (e.bucketId == bucketId) return &e;
    return nullptr;
}

const char* WoweeSpellCooldown::bucketKindName(uint8_t k) {
    switch (k) {
        case Spell:  return "spell";
        case Item:   return "item";
        case Class:  return "class";
        case Global: return "global";
        case Misc:   return "misc";
        default:     return "unknown";
    }
}

bool WoweeSpellCooldownLoader::save(const WoweeSpellCooldown& cat,
                                     const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.bucketId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.bucketKind);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
        writePOD(os, e.cooldownMs);
        writePOD(os, e.categoryFlags);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeSpellCooldown WoweeSpellCooldownLoader::load(
    const std::string& basePath) {
    WoweeSpellCooldown out;
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
        if (!readPOD(is, e.bucketId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.bucketKind)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.cooldownMs) ||
            !readPOD(is, e.categoryFlags) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSpellCooldownLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeSpellCooldown WoweeSpellCooldownLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellCooldown c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t cdMs, uint32_t flags,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        WoweeSpellCooldown::Entry e;
        e.bucketId = id; e.name = name; e.description = desc;
        e.bucketKind = kind;
        e.cooldownMs = cdMs;
        e.categoryFlags = flags;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "GlobalCooldown",   WoweeSpellCooldown::Global,
        1500,
        WoweeSpellCooldown::AffectedByHaste |
        WoweeSpellCooldown::OnGCDStart,
        220, 220, 220, "Global cooldown — 1.5s, hasted, applies to "
        "every combat spell cast.");
    add(2, "ShortItemCD",      WoweeSpellCooldown::Item,
        5000,  0,
        180, 240, 180, "Short item cooldown — 5s (low-tier consumables).");
    add(3, "MediumItemCD",     WoweeSpellCooldown::Item,
        30000, 0,
        180, 240, 100, "Medium item cooldown — 30s (mid-tier "
        "consumables / wands).");
    add(4, "LongItemCD",       WoweeSpellCooldown::Item,
        60000, WoweeSpellCooldown::SharedWithItems,
        240, 220, 100, "Long item cooldown — 60s, shared between "
        "healing/mana potions.");
    return c;
}

WoweeSpellCooldown WoweeSpellCooldownLoader::makeClass(
    const std::string& catalogName) {
    WoweeSpellCooldown c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t cdMs,
                    uint32_t flags, const char* desc) {
        WoweeSpellCooldown::Entry e;
        e.bucketId = id; e.name = name; e.description = desc;
        e.bucketKind = WoweeSpellCooldown::Class;
        e.cooldownMs = cdMs;
        e.categoryFlags = flags;
        e.iconColorRGBA = packRgba(100, 200, 240);   // mage blue
        c.entries.push_back(e);
    };
    add(100, "PolymorphFamily",  0,
        0, "Mage Polymorph variants (Sheep / Pig / Turtle / Cat) — "
        "0ms cooldown but exclusive: only one variant active per "
        "target.");
    add(101, "AlterTime",        90000,
        WoweeSpellCooldown::AffectedByHaste,
        "Alter Time — 90s, hasted by spell haste.");
    add(102, "Counterspell",     24000,
        0, "Counterspell — 24s, fixed cooldown.");
    add(103, "Blink",            15000,
        0, "Blink — 15s, fixed cooldown.");
    add(104, "IceBlock",        300000,
        WoweeSpellCooldown::IgnoresCooldownReduction,
        "Ice Block — 5min, not affected by Cold Snap or CDR.");
    return c;
}

WoweeSpellCooldown WoweeSpellCooldownLoader::makeItems(
    const std::string& catalogName) {
    WoweeSpellCooldown c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t cdMs,
                    uint32_t flags, const char* desc) {
        WoweeSpellCooldown::Entry e;
        e.bucketId = id; e.name = name; e.description = desc;
        e.bucketKind = WoweeSpellCooldown::Item;
        e.cooldownMs = cdMs;
        e.categoryFlags = flags;
        e.iconColorRGBA = packRgba(240, 200, 100);   // gold for items
        c.entries.push_back(e);
    };
    add(200, "HealingPotion",      60000,
        WoweeSpellCooldown::SharedWithItems,
        "Healing potion family — 60s shared with mana potions.");
    add(201, "ManaPotion",         60000,
        WoweeSpellCooldown::SharedWithItems,
        "Mana potion family — 60s shared with healing potions.");
    add(202, "ManaJade",            1500,
        WoweeSpellCooldown::OnGCDStart,
        "Mana Jade / oil flasks — GCD-only, no item cooldown.");
    add(203, "EngineerTrinket",    60000, 0,
        "Engineer trinket — 60s standalone bucket.");
    add(204, "HearthstoneFamily", 3600000, 0,
        "Hearthstone — 60min, exclusive across alt-bind variants.");
    return c;
}

} // namespace pipeline
} // namespace wowee
