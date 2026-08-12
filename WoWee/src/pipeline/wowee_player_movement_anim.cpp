#include "pipeline/wowee_player_movement_anim.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'H', 'M'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wphm") {
        base += ".wphm";
    }
    return base;
}

} // namespace

const WoweePlayerMovementAnim::Entry*
WoweePlayerMovementAnim::findById(uint32_t mapId) const {
    for (const auto& e : entries)
        if (e.mapId == mapId) return &e;
    return nullptr;
}

const WoweePlayerMovementAnim::Entry*
WoweePlayerMovementAnim::find(uint8_t raceId,
                                uint8_t genderId,
                                uint8_t state) const {
    for (const auto& e : entries) {
        if (e.raceId == raceId &&
            e.genderId == genderId &&
            e.movementState == state) return &e;
    }
    return nullptr;
}

std::vector<const WoweePlayerMovementAnim::Entry*>
WoweePlayerMovementAnim::findByRaceGender(uint8_t raceId,
                                            uint8_t genderId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.raceId == raceId && e.genderId == genderId)
            out.push_back(&e);
    }
    return out;
}

bool WoweePlayerMovementAnimLoader::save(
    const WoweePlayerMovementAnim& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.mapId);
        writePOD(os, e.raceId);
        writePOD(os, e.genderId);
        writePOD(os, e.movementState);
        writePOD(os, e.pad0);
        writePOD(os, e.baseAnimId);
        writePOD(os, e.variantAnimId);
        writePOD(os, e.transitionMs);
        writePOD(os, e.pad1);
    }
    return os.good();
}

WoweePlayerMovementAnim WoweePlayerMovementAnimLoader::load(
    const std::string& basePath) {
    WoweePlayerMovementAnim out;
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
        if (!readPOD(is, e.mapId) ||
            !readPOD(is, e.raceId) ||
            !readPOD(is, e.genderId) ||
            !readPOD(is, e.movementState) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.baseAnimId) ||
            !readPOD(is, e.variantAnimId) ||
            !readPOD(is, e.transitionMs) ||
            !readPOD(is, e.pad1)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweePlayerMovementAnimLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

// Helper to build an 8-state machine for one
// (raceId, genderId) pair. Canonical M2 anim ids:
//   Stand=0, Death=1, Walk=4, Run=5, Swim=12,
//   Fly=68 (vanilla had no flying except taxi),
//   Sit=20, Mount=91.
struct StateRow {
    uint8_t state;
    uint32_t baseAnim;
    uint32_t variantAnim;
    uint16_t transitionMs;
};

WoweePlayerMovementAnim::Entry makeEntry(uint32_t mapId,
                                            uint8_t race,
                                            uint8_t gender,
                                            const StateRow& r) {
    WoweePlayerMovementAnim::Entry e;
    e.mapId = mapId;
    e.raceId = race;
    e.genderId = gender;
    e.movementState = r.state;
    e.baseAnimId = r.baseAnim;
    e.variantAnimId = r.variantAnim;
    e.transitionMs = r.transitionMs;
    return e;
}

void appendRaceGender(WoweePlayerMovementAnim& c,
                       uint32_t baseId, uint8_t race,
                       uint8_t gender,
                       const std::vector<StateRow>& rows) {
    using P = WoweePlayerMovementAnim;
    for (size_t i = 0; i < rows.size(); ++i) {
        c.entries.push_back(makeEntry(
            baseId + static_cast<uint32_t>(i),
            race, gender, rows[i]));
    }
    (void)P::StateIdle;
}

} // namespace

WoweePlayerMovementAnim
WoweePlayerMovementAnimLoader::makeHumanMovement(
    const std::string& catalogName) {
    using P = WoweePlayerMovementAnim;
    WoweePlayerMovementAnim c;
    c.name = catalogName;
    // Human Male: variantAnim on Walk = drunk-walk
    // sequence (anim id 39 in the canonical M2 table).
    // Other states have no variant.
    appendRaceGender(c, 1000, 1 /* Human */, 0 /* M */, {
        {P::StateIdle,  0,  0, 200},
        {P::StateWalk,  4, 39, 250},
        {P::StateRun,   5,  0, 200},
        {P::StateSwim, 12,  0, 350},
        {P::StateFly,  68,  0, 400},
        {P::StateSit,  20,  0, 300},
        {P::StateMount,91,  0, 400},
        {P::StateDeath, 1,  0, 100},
    });
    // Human Female: identical state shape but anim
    // base ids differ (M2 sex models have separate
    // anim tables) — using same numeric ids here as
    // placeholder; in production these would be the
    // female-model-specific anim indices.
    appendRaceGender(c, 1100, 1, 1, {
        {P::StateIdle,  0,  0, 200},
        {P::StateWalk,  4, 39, 250},
        {P::StateRun,   5,  0, 200},
        {P::StateSwim, 12,  0, 350},
        {P::StateFly,  68,  0, 400},
        {P::StateSit,  20,  0, 300},
        {P::StateMount,91,  0, 400},
        {P::StateDeath, 1,  0, 100},
    });
    return c;
}

WoweePlayerMovementAnim
WoweePlayerMovementAnimLoader::makeOrcMovement(
    const std::string& catalogName) {
    using P = WoweePlayerMovementAnim;
    WoweePlayerMovementAnim c;
    c.name = catalogName;
    // Orc Run uses a more aggressive variant (anim 17
    // = AttackRun) for war-stance flavor.
    appendRaceGender(c, 2000, 2 /* Orc */, 0, {
        {P::StateIdle,  0,  0, 200},
        {P::StateWalk,  4,  0, 250},
        {P::StateRun,   5, 17, 200},
        {P::StateSwim, 12,  0, 350},
        {P::StateFly,  68,  0, 400},
        {P::StateSit,  20,  0, 300},
        {P::StateMount,91,  0, 400},
        {P::StateDeath, 1,  0, 100},
    });
    appendRaceGender(c, 2100, 2, 1, {
        {P::StateIdle,  0,  0, 200},
        {P::StateWalk,  4,  0, 250},
        {P::StateRun,   5, 17, 200},
        {P::StateSwim, 12,  0, 350},
        {P::StateFly,  68,  0, 400},
        {P::StateSit,  20,  0, 300},
        {P::StateMount,91,  0, 400},
        {P::StateDeath, 1,  0, 100},
    });
    return c;
}

WoweePlayerMovementAnim
WoweePlayerMovementAnimLoader::makeUndeadMovement(
    const std::string& catalogName) {
    using P = WoweePlayerMovementAnim;
    WoweePlayerMovementAnim c;
    c.name = catalogName;
    // Undead Run uses a "shambling" variant anim (38)
    // as the wounded-low-health renderer override.
    // Walk uses a stiffer cadence variant (40).
    appendRaceGender(c, 5000, 5 /* Undead */, 0, {
        {P::StateIdle,  0,  0, 200},
        {P::StateWalk,  4, 40, 250},
        {P::StateRun,   5, 38, 200},
        {P::StateSwim, 12,  0, 400},  // slower blend
                                        //  — undead aren't
                                        //  graceful in
                                        //  water
        {P::StateFly,  68,  0, 400},
        {P::StateSit,  20,  0, 300},
        {P::StateMount,91,  0, 400},
        {P::StateDeath, 1,  0, 100},
    });
    appendRaceGender(c, 5100, 5, 1, {
        {P::StateIdle,  0,  0, 200},
        {P::StateWalk,  4, 40, 250},
        {P::StateRun,   5, 38, 200},
        {P::StateSwim, 12,  0, 400},
        {P::StateFly,  68,  0, 400},
        {P::StateSit,  20,  0, 300},
        {P::StateMount,91,  0, 400},
        {P::StateDeath, 1,  0, 100},
    });
    return c;
}

} // namespace pipeline
} // namespace wowee
