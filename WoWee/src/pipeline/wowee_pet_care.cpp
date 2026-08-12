#include "pipeline/wowee_pet_care.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'P', 'C', 'R'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wpcr") {
        base += ".wpcr";
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

const WoweePetCare::Entry*
WoweePetCare::findById(uint32_t actionId) const {
    for (const auto& e : entries)
        if (e.actionId == actionId) return &e;
    return nullptr;
}

std::vector<const WoweePetCare::Entry*>
WoweePetCare::findByClass(uint32_t classBit) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.classFilter & classBit) out.push_back(&e);
    return out;
}

std::vector<const WoweePetCare::Entry*>
WoweePetCare::findByKind(uint8_t actionKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.actionKind == actionKind) out.push_back(&e);
    return out;
}

bool WoweePetCareLoader::save(const WoweePetCare& cat,
                                const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.actionId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.spellId);
        writePOD(os, e.classFilter);
        writePOD(os, e.actionKind);
        writePOD(os, e.happinessRestore);
        writePOD(os, e.requiresPet);
        writePOD(os, e.requiresStableNPC);
        writePOD(os, e.costCopper);
        writePOD(os, e.reagentItemId);
        writePOD(os, e.castTimeMs);
        writePOD(os, e.cooldownSec);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweePetCare WoweePetCareLoader::load(const std::string& basePath) {
    WoweePetCare out;
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
        if (!readPOD(is, e.actionId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.spellId) ||
            !readPOD(is, e.classFilter) ||
            !readPOD(is, e.actionKind) ||
            !readPOD(is, e.happinessRestore) ||
            !readPOD(is, e.requiresPet) ||
            !readPOD(is, e.requiresStableNPC) ||
            !readPOD(is, e.costCopper) ||
            !readPOD(is, e.reagentItemId) ||
            !readPOD(is, e.castTimeMs) ||
            !readPOD(is, e.cooldownSec) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweePetCareLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweePetCare WoweePetCareLoader::makeHunterCare(
    const std::string& catalogName) {
    using P = WoweePetCare;
    WoweePetCare c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t spellId, uint8_t kind,
                    int8_t happiness, uint8_t needsPet,
                    uint32_t cost, uint32_t reagent,
                    uint32_t castMs, const char* desc) {
        P::Entry e;
        e.actionId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.classFilter = 4;        // Hunter
        e.actionKind = kind;
        e.happinessRestore = happiness;
        e.requiresPet = needsPet;
        e.requiresStableNPC = 0;
        e.costCopper = cost;
        e.reagentItemId = reagent;
        e.castTimeMs = castMs;
        // Tame Beast canonically has a 15s internal
        // cooldown to prevent macro-spam; other actions
        // are off-GCD or no cooldown.
        e.cooldownSec = (kind == P::Tame) ? 15 : 0;
        e.iconColorRGBA = packRgba(170, 210, 100);   // hunter green
        c.entries.push_back(e);
    };
    add(1, "RevivePet", 982, P::Revive,
        0, 0, 0, 0, 1500,
        "Revive a dead pet. 1.5s cast, no cost. Pet "
        "must have died within the last 60 seconds.");
    add(2, "MendPet", 136, P::Mend,
        +10, 1, 0, 0, 0,
        "Channel a healing-over-time on the pet. 25s "
        "duration, instant cast, +10 happiness over "
        "duration. Cannot move while channeling.");
    add(3, "FeedPet", 6991, P::Feed,
        +10, 1, 0, 4536, 1500,
        "Feed the pet a food item to restore happiness. "
        "Reagent (Spongy Morel itemId 4536 default; the "
        "client picks an appropriate food per-pet "
        "diet at cast time).");
    add(4, "DismissPet", 2641, P::Dismiss,
        0, 1, 0, 0, 0,
        "Dismiss the active pet (instant). Pet returns "
        "to the world but is invisible until re-summoned. "
        "No cost, no cooldown.");
    add(5, "TameBeast", 1515, P::Tame,
        0, 0, 0, 0, 20000,
        "Tame a wild beast. 20s channel — beast attacks "
        "during channel; succeed only if hunter survives. "
        "No reagent, no cost.");
    return c;
}

WoweePetCare WoweePetCareLoader::makeStableActions(
    const std::string& catalogName) {
    using P = WoweePetCare;
    WoweePetCare c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t spellId, uint8_t kind,
                    uint32_t cost, const char* desc) {
        P::Entry e;
        e.actionId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.classFilter = 4;        // Hunter only
        e.actionKind = kind;
        e.happinessRestore = 0;
        e.requiresPet = (kind == P::Stable ||
                          kind == P::Untrain ||
                          kind == P::Rename ||
                          kind == P::Abandon) ? 1 : 0;
        e.requiresStableNPC = 1;
        e.costCopper = cost;
        e.reagentItemId = 0;
        e.castTimeMs = 0;
        e.cooldownSec = 0;
        e.iconColorRGBA = packRgba(140, 100, 60);   // stable brown
        c.entries.push_back(e);
    };
    // costCopper: 50 = 50 copper = 50s
    add(100, "StableSlotPurchase", 0, P::Stable,
        500000,
        "Purchase a stable slot from the stable master. "
        "500g (500000 copper). Up to 5 slots available "
        "as the hunter's stable grows.");
    add(101, "UntrainPet", 0, P::Untrain,
        10000,
        "Reset all pet talent points. Cost ramps with "
        "each untrain (1g first, +1g each subsequent — "
        "10000 copper = 1g shown as the base entry; "
        "client computes ramp at runtime).");
    add(102, "RenamePet", 0, P::Rename,
        0,
        "Rename the active pet. Free, instant. Available "
        "from any stable master.");
    add(103, "AbandonPet", 2641, P::Abandon,
        0,
        "Permanently release the active pet (back to "
        "the wild). Free, instant — but PERMANENT. The "
        "pet is gone forever; cannot be re-tamed without "
        "finding the same beast in the world. UI "
        "confirmation prompt highly recommended.");
    return c;
}

WoweePetCare WoweePetCareLoader::makeWarlockMinions(
    const std::string& catalogName) {
    using P = WoweePetCare;
    WoweePetCare c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t spellId, uint32_t reagent,
                    uint32_t castMs, const char* desc) {
        P::Entry e;
        e.actionId = id; e.name = name; e.description = desc;
        e.spellId = spellId;
        e.classFilter = 256;        // Warlock
        e.actionKind = P::Summon;
        e.happinessRestore = 0;
        e.requiresPet = 0;
        e.requiresStableNPC = 0;
        e.costCopper = 0;
        e.reagentItemId = reagent;
        e.castTimeMs = castMs;
        e.cooldownSec = 10;        // shared 10s warlock
                                    // summon cooldown
        e.iconColorRGBA = packRgba(140, 30, 140);   // warlock purple
        c.entries.push_back(e);
    };
    // Reagent: Soul Shard (itemId 6265).
    add(200, "SummonImp",        688,  6265, 6500,
        "Summon Imp — 6.5s cast, 1 Soul Shard. Imp is "
        "the leveling-default minion (ranged caster, "
        "Firebolt + Phase Shift).");
    add(201, "SummonVoidwalker", 697,  6265, 10000,
        "Summon Voidwalker — 10s cast, 1 Soul Shard. "
        "Tank minion (Sacrifice + Suffering taunt).");
    add(202, "SummonSuccubus",   712,  6265, 10000,
        "Summon Succubus — 10s cast, 1 Soul Shard. "
        "DPS minion (Lash of Pain + Seduce CC).");
    add(203, "SummonFelhunter",  691,  6265, 10000,
        "Summon Felhunter — 10s cast, 1 Soul Shard. "
        "Anti-magic minion (Spell Lock interrupt + "
        "Devour Magic dispel).");
    return c;
}

} // namespace pipeline
} // namespace wowee
