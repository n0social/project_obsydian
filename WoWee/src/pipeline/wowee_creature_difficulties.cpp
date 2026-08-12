#include "pipeline/wowee_creature_difficulties.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'D', 'F'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wcdf") {
        base += ".wcdf";
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

const WoweeCreatureDifficulty::Entry*
WoweeCreatureDifficulty::findById(uint32_t difficultyId) const {
    for (const auto& e : entries)
        if (e.difficultyId == difficultyId) return &e;
    return nullptr;
}

const WoweeCreatureDifficulty::Entry*
WoweeCreatureDifficulty::findByBaseCreature(
    uint32_t baseCreatureId) const {
    for (const auto& e : entries)
        if (e.baseCreatureId == baseCreatureId) return &e;
    return nullptr;
}

uint32_t WoweeCreatureDifficulty::resolveVariant(
    uint32_t difficultyId, uint8_t mode) const {
    const Entry* e = findById(difficultyId);
    if (!e) return 0;
    switch (mode) {
        case 0: return e->normal10Id;
        case 1: return e->normal25Id;
        case 2: return e->heroic10Id;
        case 3: return e->heroic25Id;
        default: return 0;
    }
}

const char* WoweeCreatureDifficulty::spawnGroupKindName(uint8_t k) {
    switch (k) {
        case Boss:      return "boss";
        case MiniBoss:  return "mini-boss";
        case RareElite: return "rare-elite";
        case Trash:     return "trash";
        case Add:       return "add";
        case WorldBoss: return "world-boss";
        default:        return "unknown";
    }
}

bool WoweeCreatureDifficultyLoader::save(
    const WoweeCreatureDifficulty& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.difficultyId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.baseCreatureId);
        writePOD(os, e.normal10Id);
        writePOD(os, e.normal25Id);
        writePOD(os, e.heroic10Id);
        writePOD(os, e.heroic25Id);
        writePOD(os, e.spawnGroupKind);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeCreatureDifficulty WoweeCreatureDifficultyLoader::load(
    const std::string& basePath) {
    WoweeCreatureDifficulty out;
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
        if (!readPOD(is, e.difficultyId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.baseCreatureId) ||
            !readPOD(is, e.normal10Id) ||
            !readPOD(is, e.normal25Id) ||
            !readPOD(is, e.heroic10Id) ||
            !readPOD(is, e.heroic25Id) ||
            !readPOD(is, e.spawnGroupKind) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeCreatureDifficultyLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeCreatureDifficulty WoweeCreatureDifficultyLoader::makeStarter(
    const std::string& catalogName) {
    using D = WoweeCreatureDifficulty;
    WoweeCreatureDifficulty c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t base,
                    uint32_t n10, uint32_t n25, uint32_t h10,
                    uint32_t h25, uint8_t kind,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        D::Entry e;
        e.difficultyId = id; e.name = name; e.description = desc;
        e.baseCreatureId = base;
        e.normal10Id = n10; e.normal25Id = n25;
        e.heroic10Id = h10; e.heroic25Id = h25;
        e.spawnGroupKind = kind;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    // 4 example bosses with the full 4-variant route.
    add(1, "ExampleBoss1",  8000, 8001, 8002, 8003, 8004,
        D::Boss,     240, 100, 100,
        "Encounter boss with all 4 difficulty variants set.");
    add(2, "ExampleBoss2",  8010, 8011, 8012, 8013, 8014,
        D::Boss,     240, 100, 100,
        "Encounter boss with all 4 difficulty variants set.");
    add(3, "ExampleSub",    8100, 8101, 8102, 8103, 8104,
        D::MiniBoss, 240, 180, 100,
        "Mini-boss with all 4 difficulty variants set.");
    add(4, "ExampleAdd",    8200, 8201, 8202, 8203, 8204,
        D::Add,      150, 150, 240,
        "Boss add with all 4 difficulty variants set.");
    return c;
}

WoweeCreatureDifficulty WoweeCreatureDifficultyLoader::makeWotlkRaid(
    const std::string& catalogName) {
    using D = WoweeCreatureDifficulty;
    WoweeCreatureDifficulty c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t base,
                    uint32_t n10, uint32_t n25, uint32_t h10,
                    uint32_t h25, const char* desc) {
        D::Entry e;
        e.difficultyId = id; e.name = name; e.description = desc;
        e.baseCreatureId = base;
        e.normal10Id = n10; e.normal25Id = n25;
        e.heroic10Id = h10; e.heroic25Id = h25;
        e.spawnGroupKind = D::Boss;
        e.iconColorRGBA = packRgba(220, 80, 100);  // raid red
        c.entries.push_back(e);
    };
    // Icecrown Citadel boss IDs (illustrative — real
    // numbers from AzerothCore's creature_template).
    add(100, "LordMarrowgar",    36612, 36612, 39120, 39121, 39122,
        "First ICC boss — 5 bone spike phases at 4 difficulties.");
    add(101, "LadyDeathwhisper", 36855, 36855, 38421, 38422, 38423,
        "Second ICC boss — 2-phase mind control encounter.");
    add(102, "DeathbringerSaurfang", 37813, 37813, 38762, 38763, 38764,
        "Plagueworks gatekeeper — Mark of the Fallen Champion.");
    add(103, "TheLichKing",      36597, 36597, 39166, 39167, 39168,
        "ICC final boss — Frostmourne defile encounter.");
    return c;
}

WoweeCreatureDifficulty WoweeCreatureDifficultyLoader::makeFiveMan(
    const std::string& catalogName) {
    using D = WoweeCreatureDifficulty;
    WoweeCreatureDifficulty c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t base,
                    uint32_t n10, uint32_t h10, const char* desc) {
        D::Entry e;
        e.difficultyId = id; e.name = name; e.description = desc;
        e.baseCreatureId = base;
        e.normal10Id = n10;
        // 25-man fields stay 0 — 5-man dungeons don't have
        // 25-man variants. The engine falls back to n10
        // when a 25-man variant is queried (resolveVariant
        // returns 0, caller substitutes baseCreatureId).
        e.heroic10Id = h10;
        e.spawnGroupKind = D::Boss;
        e.iconColorRGBA = packRgba(180, 200, 100);  // dungeon green
        c.entries.push_back(e);
    };
    // 5-man dungeon bosses — only Normal + Heroic variants.
    add(200, "DungeonBoss1",  31000, 31000, 31100,
        "5-man dungeon boss — Normal + Heroic only, no 25-man.");
    add(201, "DungeonBoss2",  31010, 31010, 31110,
        "5-man dungeon boss — Normal + Heroic only, no 25-man.");
    add(202, "DungeonBoss3",  31020, 31020, 31120,
        "5-man dungeon boss — Normal + Heroic only, no 25-man.");
    add(203, "DungeonFinal",  31030, 31030, 31130,
        "5-man dungeon final boss — Normal + Heroic only.");
    return c;
}

} // namespace pipeline
} // namespace wowee
