#include "pipeline/wowee_runes.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'R', 'U', 'N'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wrun") {
        base += ".wrun";
    }
    return base;
}

} // namespace

const WoweeRuneCost::Entry*
WoweeRuneCost::findById(uint32_t runeCostId) const {
    for (const auto& e : entries)
        if (e.runeCostId == runeCostId) return &e;
    return nullptr;
}

const char* WoweeRuneCost::spellTreeBranchName(uint8_t b) {
    switch (b) {
        case BloodTree:  return "blood";
        case FrostTree:  return "frost";
        case UnholyTree: return "unholy";
        case Generic:    return "generic";
        default:         return "unknown";
    }
}

bool WoweeRuneCostLoader::save(const WoweeRuneCost& cat,
                                const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.runeCostId);
        writePOD(os, e.spellId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.bloodCost);
        writePOD(os, e.frostCost);
        writePOD(os, e.unholyCost);
        writePOD(os, e.anyDeathConvertCost);
        writePOD(os, e.runicPowerCost);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        writePOD(os, e.spellTreeBranch);
        uint8_t pad3[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad3), 3);
    }
    return os.good();
}

WoweeRuneCost WoweeRuneCostLoader::load(const std::string& basePath) {
    WoweeRuneCost out;
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
        if (!readPOD(is, e.runeCostId) ||
            !readPOD(is, e.spellId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.bloodCost) ||
            !readPOD(is, e.frostCost) ||
            !readPOD(is, e.unholyCost) ||
            !readPOD(is, e.anyDeathConvertCost) ||
            !readPOD(is, e.runicPowerCost)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        if (!readPOD(is, e.spellTreeBranch)) {
            out.entries.clear(); return out;
        }
        uint8_t pad3[3];
        is.read(reinterpret_cast<char*>(pad3), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
    }
    return out;
}

bool WoweeRuneCostLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeRuneCost WoweeRuneCostLoader::makeStarter(
    const std::string& catalogName) {
    WoweeRuneCost c;
    c.name = catalogName;
    {
        WoweeRuneCost::Entry e;
        e.runeCostId = 1; e.name = "DeathStrikeRuneCost";
        e.description = "Death Strike — costs 1 Frost + 1 Unholy, "
                         "generates 20 RP, heals based on damage taken.";
        e.spellId = 49998;       // canonical Death Strike spellId
        e.frostCost = 1; e.unholyCost = 1;
        e.runicPowerCost = -20;  // generator
        e.spellTreeBranch = WoweeRuneCost::Generic;
        c.entries.push_back(e);
    }
    {
        WoweeRuneCost::Entry e;
        e.runeCostId = 2; e.name = "FrostStrikeRuneCost";
        e.description = "Frost Strike — pure runic power spender, "
                         "no rune cost.";
        e.spellId = 49143;       // canonical Frost Strike
        e.runicPowerCost = 40;   // spender
        e.spellTreeBranch = WoweeRuneCost::FrostTree;
        c.entries.push_back(e);
    }
    {
        WoweeRuneCost::Entry e;
        e.runeCostId = 3; e.name = "HeartStrikeRuneCost";
        e.description = "Heart Strike — costs 1 Blood, generates "
                         "10 RP, blood-tree filler.";
        e.spellId = 55050;       // canonical Heart Strike
        e.bloodCost = 1;
        e.runicPowerCost = -10;  // generator
        e.spellTreeBranch = WoweeRuneCost::BloodTree;
        c.entries.push_back(e);
    }
    return c;
}

WoweeRuneCost WoweeRuneCostLoader::makeBlood(
    const std::string& catalogName) {
    WoweeRuneCost c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t spellId,
                    uint8_t blood, uint8_t frost, uint8_t unholy,
                    int16_t rp, const char* desc) {
        WoweeRuneCost::Entry e;
        e.runeCostId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.bloodCost = blood;
        e.frostCost = frost;
        e.unholyCost = unholy;
        e.runicPowerCost = rp;
        e.spellTreeBranch = WoweeRuneCost::BloodTree;
        c.entries.push_back(e);
    };
    add(100, "HeartStrike",    55050,   1, 0, 0, -10,
        "Blood-tree filler — 1 Blood, generates 10 RP.");
    add(101, "DeathAndDecay",  43265,   1, 1, 1, -15,
        "AoE blood ability — 1 of each + 15 RP gain.");
    add(102, "VampiricBlood",  55233,   0, 0, 0,  20,
        "Tanking cooldown — pure 20 RP spender.");
    add(103, "RuneTap",        48982,   1, 0, 0,   0,
        "Self-heal — 1 Blood, no RP gain or cost.");
    return c;
}

WoweeRuneCost WoweeRuneCostLoader::makeFrost(
    const std::string& catalogName) {
    WoweeRuneCost c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t spellId,
                    uint8_t blood, uint8_t frost, uint8_t unholy,
                    int16_t rp, const char* desc) {
        WoweeRuneCost::Entry e;
        e.runeCostId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.bloodCost = blood;
        e.frostCost = frost;
        e.unholyCost = unholy;
        e.runicPowerCost = rp;
        e.spellTreeBranch = WoweeRuneCost::FrostTree;
        c.entries.push_back(e);
    };
    add(200, "FrostStrike",   49143,  0, 0, 0,  40,
        "Pure RP spender — 40 RP for big single-target hit.");
    add(201, "HowlingBlast",  49184,  0, 1, 0, -10,
        "AoE frost — 1 Frost, generates 10 RP.");
    add(202, "Obliterate",    49020,  0, 1, 1, -15,
        "Frost finisher — 1 Frost + 1 Unholy, generates 15 RP.");
    add(203, "IcyTouch",      45477,  0, 1, 0, -10,
        "Frost ranged opener — 1 Frost, generates 10 RP, "
        "applies Frost Fever DoT.");
    return c;
}

} // namespace pipeline
} // namespace wowee
