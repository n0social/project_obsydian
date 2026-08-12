#include "pipeline/wowee_gems.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'E', 'M'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wgem") {
        base += ".wgem";
    }
    return base;
}

} // namespace

const WoweeGem::GemEntry* WoweeGem::findGem(uint32_t gemId) const {
    for (const auto& g : gems) if (g.gemId == gemId) return &g;
    return nullptr;
}

const WoweeGem::EnchantEntry* WoweeGem::findEnchant(uint32_t enchantId) const {
    for (const auto& e : enchantments) if (e.enchantId == enchantId) return &e;
    return nullptr;
}

const char* WoweeGem::colorName(uint8_t c) {
    switch (c) {
        case Meta:      return "meta";
        case Red:       return "red";
        case Yellow:    return "yellow";
        case Blue:      return "blue";
        case Purple:    return "purple";
        case Green:     return "green";
        case Orange:    return "orange";
        case Prismatic: return "prismatic";
        default:        return "unknown";
    }
}

const char* WoweeGem::enchantSlotName(uint8_t s) {
    switch (s) {
        case Permanent:   return "permanent";
        case Temporary:   return "temporary";
        case SocketColor: return "socket";
        case Ring:        return "ring";
        case Cloak:       return "cloak";
        default:          return "unknown";
    }
}

bool WoweeGemLoader::save(const WoweeGem& cat,
                          const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t gemCount = static_cast<uint32_t>(cat.gems.size());
    writePOD(os, gemCount);
    for (const auto& g : cat.gems) {
        writePOD(os, g.gemId);
        writePOD(os, g.itemIdToInsert);
        writeStr(os, g.name);
        writePOD(os, g.color);
        writePOD(os, g.statType);
        writePOD(os, g.requiredItemQuality);
        uint8_t pad = 0;
        writePOD(os, pad);
        writePOD(os, g.statValue);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, g.spellId);
    }
    uint32_t enchCount = static_cast<uint32_t>(cat.enchantments.size());
    writePOD(os, enchCount);
    for (const auto& e : cat.enchantments) {
        writePOD(os, e.enchantId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.iconPath);
        writePOD(os, e.enchantSlot);
        writePOD(os, e.statType);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.statValue);
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.spellId);
        writePOD(os, e.durationSeconds);
        writePOD(os, e.chargeCount);
        os.write(reinterpret_cast<const char*>(pad2), 2);
    }
    return os.good();
}

WoweeGem WoweeGemLoader::load(const std::string& basePath) {
    WoweeGem out;
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    if (!is) return out;
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return out;
    uint32_t version = 0;
    if (!readPOD(is, version) || version != kVersion) return out;
    if (!readStr(is, out.name)) return out;
    uint32_t gemCount = 0;
    if (!readPOD(is, gemCount)) return out;
    if (gemCount > (1u << 20)) return out;
    out.gems.resize(gemCount);
    for (auto& g : out.gems) {
        if (!readPOD(is, g.gemId) ||
            !readPOD(is, g.itemIdToInsert)) {
            out.gems.clear(); return out;
        }
        if (!readStr(is, g.name)) {
            out.gems.clear(); return out;
        }
        if (!readPOD(is, g.color) ||
            !readPOD(is, g.statType) ||
            !readPOD(is, g.requiredItemQuality)) {
            out.gems.clear(); return out;
        }
        uint8_t pad = 0;
        if (!readPOD(is, pad)) {
            out.gems.clear(); return out;
        }
        if (!readPOD(is, g.statValue)) {
            out.gems.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.gems.clear(); return out; }
        if (!readPOD(is, g.spellId)) {
            out.gems.clear(); return out;
        }
    }
    uint32_t enchCount = 0;
    if (!readPOD(is, enchCount)) {
        out.gems.clear(); return out;
    }
    if (enchCount > (1u << 20)) {
        out.gems.clear(); return out;
    }
    out.enchantments.resize(enchCount);
    for (auto& e : out.enchantments) {
        if (!readPOD(is, e.enchantId)) {
            out.gems.clear(); out.enchantments.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.iconPath)) {
            out.gems.clear(); out.enchantments.clear(); return out;
        }
        if (!readPOD(is, e.enchantSlot) ||
            !readPOD(is, e.statType)) {
            out.gems.clear(); out.enchantments.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) {
            out.gems.clear(); out.enchantments.clear(); return out;
        }
        if (!readPOD(is, e.statValue)) {
            out.gems.clear(); out.enchantments.clear(); return out;
        }
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) {
            out.gems.clear(); out.enchantments.clear(); return out;
        }
        if (!readPOD(is, e.spellId) ||
            !readPOD(is, e.durationSeconds) ||
            !readPOD(is, e.chargeCount)) {
            out.gems.clear(); out.enchantments.clear(); return out;
        }
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) {
            out.gems.clear(); out.enchantments.clear(); return out;
        }
    }
    return out;
}

bool WoweeGemLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeGem WoweeGemLoader::makeStarter(const std::string& catalogName) {
    WoweeGem c;
    c.name = catalogName;
    {
        WoweeGem::GemEntry g;
        g.gemId = 1; g.itemIdToInsert = 23436;
        g.name = "Bold Living Ruby";
        g.color = WoweeGem::Red;
        // statType=4 matches WIT::StatStrength.
        g.statType = 4; g.statValue = 8;
        c.gems.push_back(g);
    }
    {
        WoweeGem::GemEntry g;
        g.gemId = 2; g.itemIdToInsert = 23437;
        g.name = "Brilliant Dawnstone";
        g.color = WoweeGem::Yellow;
        // statType=5 matches WIT::StatIntellect.
        g.statType = 5; g.statValue = 8;
        c.gems.push_back(g);
    }
    {
        WoweeGem::GemEntry g;
        g.gemId = 3; g.itemIdToInsert = 23438;
        g.name = "Solid Star of Elune";
        g.color = WoweeGem::Blue;
        // statType=7 matches WIT::StatStamina.
        g.statType = 7; g.statValue = 12;
        c.gems.push_back(g);
    }
    {
        WoweeGem::EnchantEntry e;
        e.enchantId = 1; e.name = "Crusader";
        e.description = "Chance on hit to heal you and increase strength.";
        e.enchantSlot = WoweeGem::Permanent;
        e.spellId = 20007;            // canonical enchant proc
        c.enchantments.push_back(e);
    }
    {
        WoweeGem::EnchantEntry e;
        e.enchantId = 2; e.name = "Greater Stats";
        e.description = "+8 to all stats.";
        e.enchantSlot = WoweeGem::Permanent;
        e.statType = 7; e.statValue = 8;     // stamina
        c.enchantments.push_back(e);
    }
    return c;
}

WoweeGem WoweeGemLoader::makeGemSet(const std::string& catalogName) {
    WoweeGem c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t color,
                    uint8_t stat, int16_t value) {
        WoweeGem::GemEntry g;
        g.gemId = id; g.name = name;
        g.color = color; g.statType = stat; g.statValue = value;
        c.gems.push_back(g);
    };
    // Primary colors.
    add(101, "Bold Crimson Spinel",     WoweeGem::Red,    4, 14);  // STR
    add(102, "Brilliant Lionseye",      WoweeGem::Yellow, 5, 14);  // INT
    add(103, "Solid Empyrean Sapphire", WoweeGem::Blue,   7, 21);  // STA
    // Secondary colors (combinations).
    add(201, "Sovereign Shadowsong Amethyst", WoweeGem::Purple, 4, 7);
    add(202, "Forceful Earthsiege Diamond",   WoweeGem::Green,  5, 7);
    add(203, "Inscribed Pyrestone",           WoweeGem::Orange, 4, 7);
    return c;
}

WoweeGem WoweeGemLoader::makeEnchants(const std::string& catalogName) {
    WoweeGem c;
    c.name = catalogName;
    // Lambda parameters in order: id, name, slot, stat type,
    // stat value, durSec, charges, spellId, description.
    // Proc-only enchants pass spellId = a placeholder in the
    // 28000-29000 range (outside WSPL preset spellIds; the
    // runtime resolves these to real proc spells when WSPL is
    // extended with enchant procs).
    auto add = [&](uint32_t id, const char* name, uint8_t slot,
                    uint8_t stat, int16_t value, uint32_t durSec,
                    uint16_t charges, uint32_t spellId,
                    const char* desc) {
        WoweeGem::EnchantEntry e;
        e.enchantId = id; e.name = name; e.description = desc;
        e.enchantSlot = slot; e.statType = stat; e.statValue = value;
        e.durationSeconds = durSec; e.chargeCount = charges;
        e.spellId = spellId;
        c.enchantments.push_back(e);
    };
    add(300, "Mongoose",       WoweeGem::Permanent, 3, 0,    0,  0, 28100,
        "Chance on hit: +120 agility for 15 seconds.");
    add(301, "Deadly Poison",  WoweeGem::Temporary, 0, 0,  3600, 60, 28200,
        "Each hit applies a poison stack.");
    add(302, "Greater Stats Ring", WoweeGem::Ring, 7, 4, 0, 0,  0,
        "+4 to all stats.");
    add(303, "Major Agility Cloak", WoweeGem::Cloak, 3, 16, 0, 0,  0,
        "+16 agility.");
    add(304, "Berserking",     WoweeGem::Permanent, 0, 0, 0, 0, 28300,
        "Chance on hit: temporarily increase haste.");
    return c;
}

} // namespace pipeline
} // namespace wowee
