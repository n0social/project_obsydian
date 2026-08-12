#include "pipeline/wowee_item_materials.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'A', 'T'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wmat") {
        base += ".wmat";
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

const WoweeItemMaterial::Entry*
WoweeItemMaterial::findById(uint32_t materialId) const {
    for (const auto& e : entries)
        if (e.materialId == materialId) return &e;
    return nullptr;
}

const char* WoweeItemMaterial::materialKindName(uint8_t k) {
    switch (k) {
        case Cloth:    return "cloth";
        case Leather:  return "leather";
        case Mail:     return "mail";
        case Plate:    return "plate";
        case Wood:     return "wood";
        case Stone:    return "stone";
        case Metal:    return "metal";
        case Liquid:   return "liquid";
        case Organic:  return "organic";
        case Crystal:  return "crystal";
        case Ethereal: return "ethereal";
        case Hide:     return "hide";
        default:       return "unknown";
    }
}

const char* WoweeItemMaterial::weightCategoryName(uint8_t w) {
    switch (w) {
        case Light:  return "light";
        case Medium: return "medium";
        case Heavy:  return "heavy";
        default:     return "unknown";
    }
}

bool WoweeItemMaterialLoader::save(const WoweeItemMaterial& cat,
                                    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.materialId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.materialKind);
        writePOD(os, e.weightCategory);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.foleySoundId);
        writePOD(os, e.impactSoundId);
        writePOD(os, e.materialFlags);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeItemMaterial WoweeItemMaterialLoader::load(
    const std::string& basePath) {
    WoweeItemMaterial out;
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
        if (!readPOD(is, e.materialId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.materialKind) ||
            !readPOD(is, e.weightCategory) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.foleySoundId) ||
            !readPOD(is, e.impactSoundId) ||
            !readPOD(is, e.materialFlags) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeItemMaterialLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeItemMaterial WoweeItemMaterialLoader::makeArmor(
    const std::string& catalogName) {
    using M = WoweeItemMaterial;
    WoweeItemMaterial c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t weight, uint32_t foley, uint32_t impact,
                    uint32_t flags, uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        M::Entry e;
        e.materialId = id; e.name = name; e.description = desc;
        e.materialKind = kind;
        e.weightCategory = weight;
        e.foleySoundId = foley;
        e.impactSoundId = impact;
        e.materialFlags = flags;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    // foley/impact sound ids reference WSND entries; using
    // 1xx range for foley + 2xx for impact as illustrative
    // anchors that engine projects override.
    add(1, "Cloth",   M::Cloth,   M::Light,  101, 201,
        M::IsFlammable,
        220, 220, 200, "Cloth — light, flammable, no impact sound.");
    add(2, "Leather", M::Leather, M::Light,  102, 202, 0,
        160, 100,  60,
        "Leather — light, supple, dull thud on impact.");
    add(3, "Mail",    M::Mail,    M::Medium, 103, 203,
        M::IsConductive,
        180, 180, 200, "Mail — medium, metallic ring, conducts lightning.");
    add(4, "Plate",   M::Plate,   M::Heavy,  104, 204,
        M::IsConductive,
        220, 220, 230,
        "Plate — heavy, loud clang, conducts lightning.");
    add(5, "Hide",    M::Hide,    M::Medium, 105, 205,
        M::IsFlammable,
        140,  90,  50,
        "Hide — raw furred hide, medium weight, flammable.");
    return c;
}

WoweeItemMaterial WoweeItemMaterialLoader::makeWeapon(
    const std::string& catalogName) {
    using M = WoweeItemMaterial;
    WoweeItemMaterial c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint8_t weight, uint32_t foley, uint32_t impact,
                    uint32_t flags, const char* desc) {
        M::Entry e;
        e.materialId = id; e.name = name; e.description = desc;
        e.materialKind = kind;
        e.weightCategory = weight;
        e.foleySoundId = foley;
        e.impactSoundId = impact;
        e.materialFlags = flags;
        e.iconColorRGBA = packRgba(180, 180, 200);   // weapon-steel grey
        c.entries.push_back(e);
    };
    add(100, "Wood",            M::Wood,  M::Light,  110, 210,
        M::IsBreakable | M::IsFlammable,
        "Wood — staves and bows. Breakable + flammable.");
    add(101, "Steel",           M::Metal, M::Medium, 111, 211,
        M::IsConductive,
        "Steel — vendor-buy weapons. Conducts lightning.");
    add(102, "Mithril",         M::Metal, M::Medium, 112, 212,
        M::IsConductive,
        "Mithril — mid-tier weapons (40-50). Lighter than steel.");
    add(103, "Adamantite",      M::Metal, M::Medium, 113, 213,
        M::IsConductive,
        "Adamantite — endgame raw material (TBC-era). Tough metal.");
    add(104, "EnchantedSteel",  M::Metal, M::Medium, 114, 214,
        M::IsConductive | M::IsMagical,
        "Enchanted steel — magical raid weapons. Glows + conducts.");
    return c;
}

WoweeItemMaterial WoweeItemMaterialLoader::makeMagical(
    const std::string& catalogName) {
    using M = WoweeItemMaterial;
    WoweeItemMaterial c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t flags, uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        M::Entry e;
        e.materialId = id; e.name = name; e.description = desc;
        e.materialKind = kind;
        e.weightCategory = M::Light;     // magical things are weightless
        e.materialFlags = flags;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(200, "Crystal",     M::Crystal,
        M::IsMagical | M::IsBreakable,
        180, 220, 240,
        "Crystal — magical, breakable, refracts light.");
    add(201, "Ethereal",    M::Ethereal,
        M::IsMagical,
        200, 200, 240,
        "Ethereal — ghostly weightless material.");
    add(202, "CursedBone",  M::Organic,
        M::IsCursed,
        100,  60,  60,
        "Cursed bone — applies a debuff to wearer.");
    add(203, "HolyForged",  M::Metal,
        M::IsMagical | M::IsHolyCharged,
        240, 240, 200,
        "Holy-forged steel — damages undead on contact.");
    return c;
}

} // namespace pipeline
} // namespace wowee
