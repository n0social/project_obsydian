#include "pipeline/wowee_npc_services.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'B', 'K', 'D'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wbkd") {
        base += ".wbkd";
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

const WoweeNPCService::Entry*
WoweeNPCService::findById(uint32_t serviceId) const {
    for (const auto& e : entries)
        if (e.serviceId == serviceId) return &e;
    return nullptr;
}

std::vector<const WoweeNPCService::Entry*>
WoweeNPCService::findByKind(uint8_t kind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.serviceKind == kind) out.push_back(&e);
    }
    return out;
}

const char* WoweeNPCService::serviceKindName(uint8_t k) {
    switch (k) {
        case Banker:        return "banker";
        case Mailbox:       return "mailbox";
        case Auctioneer:    return "auctioneer";
        case StableMaster:  return "stable-master";
        case FlightMaster:  return "flight-master";
        case Trainer:       return "trainer";
        case Innkeeper:     return "innkeeper";
        case Battlemaster:  return "battlemaster";
        case GuildBanker:   return "guild-banker";
        case ReagentVendor: return "reagent-vendor";
        case TabardVendor:  return "tabard-vendor";
        case Misc:          return "misc";
        default:            return "unknown";
    }
}

bool WoweeNPCServiceLoader::save(const WoweeNPCService& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.serviceId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.serviceKind);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.pad2);
        writePOD(os, e.requiresGold);
        writePOD(os, e.factionRequiredId);
        writePOD(os, e.gossipTextId);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeNPCService WoweeNPCServiceLoader::load(const std::string& basePath) {
    WoweeNPCService out;
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
        if (!readPOD(is, e.serviceId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.serviceKind) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.pad2) ||
            !readPOD(is, e.requiresGold) ||
            !readPOD(is, e.factionRequiredId) ||
            !readPOD(is, e.gossipTextId) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeNPCServiceLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeNPCService WoweeNPCServiceLoader::makeCity(
    const std::string& catalogName) {
    using N = WoweeNPCService;
    WoweeNPCService c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t cop, uint32_t faction, uint32_t gossip,
                    uint8_t r, uint8_t g, uint8_t b,
                    const char* desc) {
        N::Entry e;
        e.serviceId = id; e.name = name; e.description = desc;
        e.serviceKind = kind;
        e.requiresGold = cop;
        e.factionRequiredId = faction;
        e.gossipTextId = gossip;
        e.iconColorRGBA = packRgba(r, g, b);
        c.entries.push_back(e);
    };
    add(1, "CityBanker",      N::Banker,       0, 0, 1000,
        220, 220, 100, "City banker — opens 28-slot inventory bank.");
    add(2, "CityMailbox",     N::Mailbox,      0, 0, 0,
        180, 180, 240, "City mailbox — send/receive mail (no NPC).");
    add(3, "CityInnkeeper",   N::Innkeeper,    0, 0, 1500,
        240, 200, 100, "City innkeeper — set hearthstone bind, "
        "rest XP buff.");
    add(4, "CityAuctioneer",  N::Auctioneer,   0, 0, 1200,
        180, 220, 180, "City auctioneer — opens AH (5%% deposit, "
        "5%% sale cut).");
    add(5, "CityFlightMaster",N::FlightMaster, 0, 0, 1100,
        140, 200, 240, "City flight master — taxi node selection.");
    return c;
}

WoweeNPCService WoweeNPCServiceLoader::makeBattle(
    const std::string& catalogName) {
    using N = WoweeNPCService;
    WoweeNPCService c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* desc) {
        N::Entry e;
        e.serviceId = id; e.name = name; e.description = desc;
        e.serviceKind = N::Battlemaster;
        // Battlemasters don't charge gold, but require
        // faction-aligned battleground access.
        e.iconColorRGBA = packRgba(220, 80, 80);   // pvp red
        c.entries.push_back(e);
    };
    add(100, "BattlemasterAV",
        "Alterac Valley battlemaster — 40v40 BG queue.");
    add(101, "BattlemasterWSG",
        "Warsong Gulch battlemaster — 10v10 capture-flag BG queue.");
    add(102, "BattlemasterAB",
        "Arathi Basin battlemaster — 15v15 control-point BG queue.");
    return c;
}

WoweeNPCService WoweeNPCServiceLoader::makeProfession(
    const std::string& catalogName) {
    using N = WoweeNPCService;
    WoweeNPCService c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t kind,
                    uint32_t cop, const char* desc) {
        N::Entry e;
        e.serviceId = id; e.name = name; e.description = desc;
        e.serviceKind = kind;
        e.requiresGold = cop;
        e.iconColorRGBA = packRgba(180, 140, 80);   // crafting brown
        c.entries.push_back(e);
    };
    add(200, "BlacksmithTrainer", N::Trainer,        0,
        "Blacksmithing trainer — teaches recipes and rank-ups.");
    add(201, "TailoringTrainer",  N::Trainer,        0,
        "Tailoring trainer — teaches cloth crafting recipes.");
    add(202, "ReagentVendor",     N::ReagentVendor,  0,
        "Reagent vendor — sells profession reagents in stacks.");
    add(203, "StableMaster",      N::StableMaster,  100,
        "Stable master — costs 1 silver to swap pets in/out "
        "of stable.");
    return c;
}

} // namespace pipeline
} // namespace wowee
