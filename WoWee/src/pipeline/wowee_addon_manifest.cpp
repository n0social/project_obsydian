#include "pipeline/wowee_addon_manifest.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'O', 'D'};
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

void writeU32Vec(std::ofstream& os,
                  const std::vector<uint32_t>& v) {
    uint32_t n = static_cast<uint32_t>(v.size());
    writePOD(os, n);
    if (n > 0) {
        os.write(reinterpret_cast<const char*>(v.data()),
                 static_cast<std::streamsize>(n * sizeof(uint32_t)));
    }
}

bool readU32Vec(std::ifstream& is, std::vector<uint32_t>& v) {
    uint32_t n = 0;
    if (!readPOD(is, n)) return false;
    // Hard cap on per-addon dependency count — prevents
    // a corrupted input from allocating GBs.
    if (n > 4096) return false;
    v.resize(n);
    if (n > 0) {
        is.read(reinterpret_cast<char*>(v.data()),
                static_cast<std::streamsize>(n * sizeof(uint32_t)));
        if (is.gcount() !=
            static_cast<std::streamsize>(n * sizeof(uint32_t))) {
            v.clear();
            return false;
        }
    }
    return true;
}

std::string normalizePath(std::string base) {
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wmod") {
        base += ".wmod";
    }
    return base;
}

} // namespace

const WoweeAddonManifest::Entry*
WoweeAddonManifest::findById(uint32_t addonId) const {
    for (const auto& e : entries)
        if (e.addonId == addonId) return &e;
    return nullptr;
}

const WoweeAddonManifest::Entry*
WoweeAddonManifest::findByName(const std::string& nm) const {
    for (const auto& e : entries)
        if (e.name == nm) return &e;
    return nullptr;
}

std::vector<const WoweeAddonManifest::Entry*>
WoweeAddonManifest::findDependents(uint32_t addonId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        for (uint32_t d : e.dependencies) {
            if (d == addonId) { out.push_back(&e); break; }
        }
    }
    return out;
}

bool WoweeAddonManifestLoader::save(
    const WoweeAddonManifest& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.addonId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.version);
        writeStr(os, e.author);
        writePOD(os, e.minClientBuild);
        writePOD(os, e.requiresSavedVariables);
        writePOD(os, e.loadOnDemand);
        writePOD(os, e.pad0);
        writeU32Vec(os, e.dependencies);
        writeU32Vec(os, e.optionalDependencies);
    }
    return os.good();
}

WoweeAddonManifest WoweeAddonManifestLoader::load(
    const std::string& basePath) {
    WoweeAddonManifest out;
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
        if (!readPOD(is, e.addonId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) ||
            !readStr(is, e.description) ||
            !readStr(is, e.version) ||
            !readStr(is, e.author)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.minClientBuild) ||
            !readPOD(is, e.requiresSavedVariables) ||
            !readPOD(is, e.loadOnDemand) ||
            !readPOD(is, e.pad0)) {
            out.entries.clear(); return out;
        }
        if (!readU32Vec(is, e.dependencies) ||
            !readU32Vec(is, e.optionalDependencies)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeAddonManifestLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeAddonManifest WoweeAddonManifestLoader::makeStandardAddons(
    const std::string& catalogName) {
    using A = WoweeAddonManifest;
    WoweeAddonManifest c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* version, const char* author,
                    uint8_t needsSV, uint8_t lod,
                    std::vector<uint32_t> deps,
                    std::vector<uint32_t> optDeps,
                    const char* desc) {
        A::Entry e;
        e.addonId = id; e.name = name;
        e.version = version; e.author = author;
        e.description = desc;
        e.minClientBuild = 5875;            // 1.12 vanilla
                                              // build floor
        e.requiresSavedVariables = needsSV;
        e.loadOnDemand = lod;
        e.dependencies = std::move(deps);
        e.optionalDependencies = std::move(optDeps);
        c.entries.push_back(e);
    };
    // Recount: standalone DPS meter — no deps,
    // persists session combat history.
    add(1, "Recount", "2.0.4", "Cryect/Elsia", 1, 0,
        {}, {},
        "Damage meter — tracks DPS/HPS/threat per "
        "encounter. Saves recent combat sessions to "
        "SavedVariables.");
    // Atlas: standalone instance map browser, no deps,
    // no persistence.
    add(2, "Atlas", "1.10.2", "DanGilbert", 0, 0,
        {}, {},
        "Instance map browser — shows boss + loot "
        "locations for vanilla dungeons / raids. "
        "Static data, no SavedVariables.");
    // Auctioneer: optionally depends on Atlas for
    // map-link buttons in scan history (graceful
    // degradation if Atlas absent).
    add(3, "Auctioneer", "5.21.5497", "Norganna", 1, 0,
        {}, {2},
        "Auction house scanner + market analysis. "
        "Optionally uses Atlas for map links in scan "
        "history (degrades gracefully if absent).");
    // Questie: standalone quest helper, persists quest
    // log + completed-quest cache.
    add(4, "Questie", "4.4.1", "Questie-Team", 1, 0,
        {}, {},
        "Quest helper — overlay markers + objective "
        "tracking. Persists per-character completed "
        "quest list to SavedVariables.");
    return c;
}

WoweeAddonManifest WoweeAddonManifestLoader::makeUIReplacement(
    const std::string& catalogName) {
    using A = WoweeAddonManifest;
    WoweeAddonManifest c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* version, const char* author,
                    std::vector<uint32_t> deps,
                    const char* desc) {
        A::Entry e;
        e.addonId = id; e.name = name;
        e.version = version; e.author = author;
        e.description = desc;
        e.minClientBuild = 5875;
        e.requiresSavedVariables = 1;       // UI mods
                                              // always need
                                              // settings
                                              // persistence
        e.loadOnDemand = 0;
        e.dependencies = std::move(deps);
        c.entries.push_back(e);
    };
    // Bartender4: action-bar replacement, root of the
    // UI-replacement dep chain.
    add(10, "Bartender4", "4.5.5", "Nevcairiel",
        {},
        "Action-bar replacement — supports 10 movable "
        "bars with per-bar visibility states. Standalone "
        "root of the UI-replacement dep chain.");
    // ElvUI: full UI replacement — depends on
    // Bartender4 for action-bar layer (real ElvUI
    // ships its own bar mod, but for this preset we
    // model the dep chain).
    add(11, "ElvUI", "1.21", "TukUI-Team",
        {10},
        "Full UI replacement — unitframes / nameplates "
        "/ chat / minimap. Depends on Bartender4 for "
        "the action-bar layer (preset models a chain).");
    // SuperOrders: ElvUI extension for raid frames —
    // requires ElvUI.
    add(12, "SuperOrders", "0.9.3", "RaidLeader",
        {11},
        "ElvUI raid-frame extension — adds clickcast "
        "+ smartheal. Requires ElvUI as parent.");
    return c;
}

WoweeAddonManifest WoweeAddonManifestLoader::makeUtility(
    const std::string& catalogName) {
    using A = WoweeAddonManifest;
    WoweeAddonManifest c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* version, const char* author,
                    uint8_t lod,
                    const char* desc) {
        A::Entry e;
        e.addonId = id; e.name = name;
        e.version = version; e.author = author;
        e.description = desc;
        e.minClientBuild = 5875;
        e.requiresSavedVariables = 0;
        e.loadOnDemand = lod;
        c.entries.push_back(e);
    };
    add(20, "XPerl", "3.7.5", "ZenTabi/XPerl-Team", 0,
        "Unit-frame replacement — drop-in UI mod, no "
        "deps, no persistence. Default-load.");
    add(21, "Decursive", "2.7.7", "Archarodim", 0,
        "Auto-decurse mouseover — keybind helper for "
        "removing harmful auras. Default-load.");
    // GearVendor is loadOnDemand: only loads when the
    // user opens the gear-comparison popup.
    add(22, "GearVendor", "1.0.2", "GearLab", 1,
        "Item upgrade comparison popup — loadOnDemand: "
        "skipped at login, loaded only when popup "
        "opens. Saves favorite-item list.");
    return c;
}

} // namespace pipeline
} // namespace wowee
