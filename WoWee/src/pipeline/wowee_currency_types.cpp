#include "pipeline/wowee_currency_types.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'T', 'R'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wctr") {
        base += ".wctr";
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

const WoweeCurrencyType::Entry*
WoweeCurrencyType::findById(uint32_t currencyId) const {
    for (const auto& e : entries)
        if (e.currencyId == currencyId) return &e;
    return nullptr;
}

uint32_t WoweeCurrencyType::earnableNow(uint32_t currencyId,
                                         uint32_t currentTotal,
                                         uint32_t earnedThisWeek) const {
    const Entry* e = findById(currencyId);
    if (!e) return 0;
    uint32_t remainAbs = (e->maxQuantity == 0)
        ? UINT32_MAX
        : (e->maxQuantity > currentTotal
            ? e->maxQuantity - currentTotal : 0);
    uint32_t remainWeek = (e->maxQuantityWeekly == 0)
        ? UINT32_MAX
        : (e->maxQuantityWeekly > earnedThisWeek
            ? e->maxQuantityWeekly - earnedThisWeek : 0);
    return std::min(remainAbs, remainWeek);
}

const char* WoweeCurrencyType::currencyKindName(uint8_t k) {
    switch (k) {
        case PvPHonor:     return "pvp-honor";
        case PvERaid:      return "pve-raid";
        case FactionToken: return "faction-token";
        case EventToken:   return "event-token";
        case Crafting:     return "crafting";
        case Misc:         return "misc";
        default:           return "unknown";
    }
}

bool WoweeCurrencyTypeLoader::save(const WoweeCurrencyType& cat,
                                    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.currencyId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.itemId);
        writePOD(os, e.maxQuantity);
        writePOD(os, e.maxQuantityWeekly);
        writePOD(os, e.categoryId);
        writePOD(os, e.currencyKind);
        writePOD(os, e.isAccountWide);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writeStr(os, e.iconPath);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeCurrencyType WoweeCurrencyTypeLoader::load(
    const std::string& basePath) {
    WoweeCurrencyType out;
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
        if (!readPOD(is, e.currencyId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.itemId) ||
            !readPOD(is, e.maxQuantity) ||
            !readPOD(is, e.maxQuantityWeekly) ||
            !readPOD(is, e.categoryId) ||
            !readPOD(is, e.currencyKind) ||
            !readPOD(is, e.isAccountWide) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.iconPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeCurrencyTypeLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeCurrencyType WoweeCurrencyTypeLoader::makePvP(
    const std::string& catalogName) {
    using C = WoweeCurrencyType;
    WoweeCurrencyType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t item,
                    uint32_t maxQ, uint32_t maxWeekly,
                    uint8_t kind, uint8_t accountWide,
                    const char* icon, const char* desc) {
        C::Entry e;
        e.currencyId = id; e.name = name; e.description = desc;
        e.itemId = item;
        e.maxQuantity = maxQ;
        e.maxQuantityWeekly = maxWeekly;
        e.categoryId = 1;       // category 1 = PvP
        e.currencyKind = kind;
        e.isAccountWide = accountWide;
        e.iconPath = icon;
        e.iconColorRGBA = packRgba(220, 200, 100);   // pvp gold
        c.entries.push_back(e);
    };
    // 3.3.5a Honor Points cap is 75000; Arena Points are
    // weekly only (5000); Cata-style Conquest Points 1650
    // weekly cap.
    add(43308, "HonorPoints",   0,      75000, 0,
        C::PvPHonor, 0, "Interface\\Icons\\PVPCurrency-Honor-Alliance",
        "Honor Points — earned in PvP, 75k absolute cap.");
    add(390,   "ArenaPoints",  29024,   0,    5000,
        C::PvPHonor, 0, "Interface\\Icons\\PVPCurrency-Conquest-Alliance",
        "Arena Points — weekly 5k earn cap, no absolute cap.");
    add(390000,"ConquestPoints",0,      0,    1650,
        C::PvPHonor, 0, "Interface\\Icons\\PVPCurrency-Conquest-Alliance",
        "Conquest Points (Cata-style) — 1650 weekly cap.");
    add(241,   "ChampionsSeal",  44990, 0,    0,
        C::PvPHonor, 0, "Interface\\Icons\\Achievement_PVP_A_18",
        "Champion's Seal — Argent Tournament token, no cap.");
    return c;
}

WoweeCurrencyType WoweeCurrencyTypeLoader::makePvE(
    const std::string& catalogName) {
    using C = WoweeCurrencyType;
    WoweeCurrencyType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t item,
                    uint32_t maxQ, uint32_t maxWeekly,
                    const char* icon, const char* desc) {
        C::Entry e;
        e.currencyId = id; e.name = name; e.description = desc;
        e.itemId = item;
        e.maxQuantity = maxQ;
        e.maxQuantityWeekly = maxWeekly;
        e.categoryId = 2;       // category 2 = PvE
        e.currencyKind = C::PvERaid;
        e.iconPath = icon;
        e.iconColorRGBA = packRgba(180, 100, 240);   // raid epic purple
        c.entries.push_back(e);
    };
    add(395,   "JusticePoints", 0, 4000, 0,
        "Interface\\Icons\\Achievement_GuildPerk_Honorable",
        "Justice Points (Cata-style) — 4k absolute cap.");
    add(396,   "ValorPoints",   0, 0, 1000,
        "Interface\\Icons\\Achievement_Reputation_07",
        "Valor Points — 1k weekly cap, no absolute cap.");
    add(341,   "EmblemOfFrost", 49426, 0, 0,
        "Interface\\Icons\\INV_Misc_Token_HonorBound",
        "Emblem of Frost — no caps, ICC-tier raid currency.");
    add(442,   "TrophyOfCrusade", 47241, 0, 0,
        "Interface\\Icons\\INV_Misc_Token_Argentdawn",
        "Trophy of the Crusade — no cap, T9 token.");
    return c;
}

WoweeCurrencyType WoweeCurrencyTypeLoader::makeFactionTokens(
    const std::string& catalogName) {
    using C = WoweeCurrencyType;
    WoweeCurrencyType c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t item,
                    uint32_t factionId, const char* icon,
                    const char* desc) {
        C::Entry e;
        e.currencyId = id; e.name = name; e.description = desc;
        e.itemId = item;
        e.categoryId = factionId;     // categoryId references WFAC
        e.currencyKind = C::FactionToken;
        e.iconPath = icon;
        e.iconColorRGBA = packRgba(100, 200, 100);   // faction green
        c.entries.push_back(e);
    };
    // Faction tokens — gated by reputation, not by a cap.
    // categoryId references the WFAC.factionId.
    add(1000, "SpearFragmentOfHodir", 41511, 1119,
        "Interface\\Icons\\INV_Spear_05",
        "Spear-fragment of Hodir — Sons of Hodir rep token.");
    add(1001, "MarkOfTheCenarion",    20809, 609,
        "Interface\\Icons\\INV_Misc_Cape_18",
        "Mark of the Cenarion — Cenarion Circle rep token.");
    add(1002, "ArgentDawnValorToken", 12846, 529,
        "Interface\\Icons\\INV_Jewelry_Talisman_05",
        "Argent Dawn Valor Token — Argent Dawn rep token.");
    add(1003, "WintergraspMark",      43589, 1156,
        "Interface\\Icons\\Achievement_Zone_Wintergrasp_01",
        "Wintergrasp Mark of Honor — Wintergrasp rep token.");
    return c;
}

} // namespace pipeline
} // namespace wowee
