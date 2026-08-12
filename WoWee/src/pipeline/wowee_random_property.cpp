#include "pipeline/wowee_random_property.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'I', 'R', 'C'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wirc") {
        base += ".wirc";
    }
    return base;
}

} // namespace

const WoweeRandomProperty::Entry*
WoweeRandomProperty::findById(uint32_t poolId) const {
    for (const auto& e : entries)
        if (e.poolId == poolId) return &e;
    return nullptr;
}

std::vector<const WoweeRandomProperty::Entry*>
WoweeRandomProperty::findBySlot(uint8_t slotMask) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.allowedSlotsMask & slotMask)
            out.push_back(&e);
    }
    return out;
}

bool WoweeRandomPropertyLoader::save(
    const WoweeRandomProperty& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.poolId);
        writeStr(os, e.name);
        writePOD(os, e.scaleLevel);
        writePOD(os, e.allowedSlotsMask);
        writePOD(os, e.allowedClassesMask);
        writePOD(os, e.totalWeight);
        uint32_t enchantCount =
            static_cast<uint32_t>(e.enchants.size());
        writePOD(os, enchantCount);
        for (const auto& en : e.enchants) {
            writePOD(os, en.enchantId);
            writePOD(os, en.weight);
        }
    }
    return os.good();
}

WoweeRandomProperty WoweeRandomPropertyLoader::load(
    const std::string& basePath) {
    WoweeRandomProperty out;
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
        if (!readPOD(is, e.poolId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.scaleLevel) ||
            !readPOD(is, e.allowedSlotsMask) ||
            !readPOD(is, e.allowedClassesMask) ||
            !readPOD(is, e.totalWeight)) {
            out.entries.clear(); return out;
        }
        uint32_t enchantCount = 0;
        if (!readPOD(is, enchantCount)) {
            out.entries.clear(); return out;
        }
        // Sanity cap — vanilla pools never exceed 12
        // enchants; format cap 64.
        if (enchantCount > 64) {
            out.entries.clear(); return out;
        }
        e.enchants.resize(enchantCount);
        for (auto& en : e.enchants) {
            if (!readPOD(is, en.enchantId) ||
                !readPOD(is, en.weight)) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeRandomPropertyLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

// Helper: build entry with auto-summed totalWeight.
WoweeRandomProperty::Entry makePool(
    uint32_t poolId, const char* name,
    uint8_t scaleLevel, uint8_t slotsMask,
    uint16_t classesMask,
    std::vector<WoweeRandomProperty::EnchantEntry> enchants) {
    WoweeRandomProperty::Entry e;
    e.poolId = poolId; e.name = name;
    e.scaleLevel = scaleLevel;
    e.allowedSlotsMask = slotsMask;
    e.allowedClassesMask = classesMask;
    e.enchants = std::move(enchants);
    uint32_t sum = 0;
    for (const auto& en : e.enchants) sum += en.weight;
    e.totalWeight = sum;
    return e;
}

WoweeRandomProperty::EnchantEntry mkEn(uint32_t enchantId,
                                          uint32_t weight) {
    WoweeRandomProperty::EnchantEntry en;
    en.enchantId = enchantId;
    en.weight = weight;
    return en;
}

} // namespace

WoweeRandomProperty WoweeRandomPropertyLoader::makeOfTheBear(
    const std::string& catalogName) {
    using R = WoweeRandomProperty;
    WoweeRandomProperty c;
    c.name = catalogName;
    // "of the Bear" = +Sta. Plate slots (Helm/Chest/
    // Leg/Boot). Class mask: Warrior(1)+Paladin(2)+
    // DK(6) = 1<<1 | 1<<2 | 1<<6 = 0x46.
    // Enchants 1189..1192 are vanilla +3/+5/+7/+10
    // Stamina enchants. Weights tilt toward middle
    // tier (5 stamina being most common).
    c.entries.push_back(makePool(
        1, "of the Bear (low)", 20,
        R::Helm | R::Chest | R::Leg | R::Boot,
        0x46,
        {mkEn(1189, 30),  // +3 Sta
         mkEn(1190, 50),  // +5 Sta — most likely
         mkEn(1191, 15),  // +7 Sta
         mkEn(1192, 5)})); // +10 Sta — rare
    return c;
}

WoweeRandomProperty WoweeRandomPropertyLoader::makeOfTheEagle(
    const std::string& catalogName) {
    using R = WoweeRandomProperty;
    WoweeRandomProperty c;
    c.name = catalogName;
    // "of the Eagle" = +Int+Sta. Cloth slots.
    // Class mask: Mage(8)+Priest(5)+Warlock(9) =
    // 1<<8 | 1<<5 | 1<<9 = 0x320.
    // Enchant ids approximate (canonical IDs would
    // be looked up from Spell.dbc).
    c.entries.push_back(makePool(
        2, "of the Eagle", 30,
        R::Helm | R::Chest | R::Leg | R::Glove,
        0x320,
        {mkEn(1503, 25),  // +3 Int +3 Sta
         mkEn(1504, 35),  // +5 Int +5 Sta — most
                          //  common
         mkEn(1505, 25),  // +7 Int +7 Sta
         mkEn(1506, 10),  // +10 Int +10 Sta
         mkEn(1507, 5)})); // +12 Int +12 Sta
                            //  — rarest
    return c;
}

WoweeRandomProperty WoweeRandomPropertyLoader::makeOfTheTiger(
    const std::string& catalogName) {
    using R = WoweeRandomProperty;
    WoweeRandomProperty c;
    c.name = catalogName;
    // "of the Tiger" = +Str+Agi (rogue/druid AP
    // suffix). Leather slots. Class mask: Rogue(4)+
    // Druid(11)+Hunter(3) = 1<<4 | 1<<11 | 1<<3 =
    // 0x818.
    c.entries.push_back(makePool(
        3, "of the Tiger", 35,
        R::Helm | R::Chest | R::Leg | R::Glove | R::Boot,
        0x818,
        {mkEn(1605, 25),  // +3 Str +3 Agi
         mkEn(1606, 30),  // +5 Str +5 Agi — most
                          //  common
         mkEn(1607, 25),  // +7 Str +7 Agi
         mkEn(1608, 15),  // +10 Str +10 Agi
         mkEn(1609, 5)})); // +12 Str +12 Agi —
                            //  rarest
    return c;
}

} // namespace pipeline
} // namespace wowee
