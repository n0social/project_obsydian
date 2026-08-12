#include "pipeline/wowee_chat_links.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'N', 'K'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wlnk") {
        base += ".wlnk";
    }
    return base;
}

} // namespace

const WoweeChatLinks::Entry*
WoweeChatLinks::findById(uint32_t linkId) const {
    for (const auto& e : entries)
        if (e.linkId == linkId) return &e;
    return nullptr;
}

const WoweeChatLinks::Entry*
WoweeChatLinks::findByKind(uint8_t linkKind) const {
    for (const auto& e : entries)
        if (e.linkKind == linkKind) return &e;
    return nullptr;
}

bool WoweeChatLinksLoader::save(const WoweeChatLinks& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.linkId);
        writeStr(os, e.name);
        writePOD(os, e.linkKind);
        writePOD(os, e.requireServerLookup);
        writePOD(os, e.pad0);
        writePOD(os, e.colorRGBA);
        writeStr(os, e.linkTemplate);
        writeStr(os, e.tooltipTemplate);
        writeStr(os, e.iconRule);
    }
    return os.good();
}

WoweeChatLinks WoweeChatLinksLoader::load(
    const std::string& basePath) {
    WoweeChatLinks out;
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
        if (!readPOD(is, e.linkId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.linkKind) ||
            !readPOD(is, e.requireServerLookup) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.colorRGBA)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.linkTemplate) ||
            !readStr(is, e.tooltipTemplate) ||
            !readStr(is, e.iconRule)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeChatLinksLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

WoweeChatLinks::Entry makeLink(
    uint32_t linkId, const char* name,
    uint8_t linkKind, uint8_t serverLookup,
    uint32_t colorRGBA,
    const char* linkTemplate,
    const char* tooltipTemplate,
    const char* iconRule) {
    WoweeChatLinks::Entry e;
    e.linkId = linkId; e.name = name;
    e.linkKind = linkKind;
    e.requireServerLookup = serverLookup;
    e.colorRGBA = colorRGBA;
    e.linkTemplate = linkTemplate;
    e.tooltipTemplate = tooltipTemplate;
    e.iconRule = iconRule;
    return e;
}

} // namespace

WoweeChatLinks WoweeChatLinksLoader::makeStandardLinks(
    const std::string& catalogName) {
    using L = WoweeChatLinks;
    WoweeChatLinks c;
    c.name = catalogName;
    // Item link: classic 4-rune-slot template
    // |cffFFFFFF|Hitem:itemId:enchant:gem1:gem2|h
    // [Name]|h|r. Quality color is white default;
    // quality variants override (see makeColor
    // Variants).
    c.entries.push_back(makeLink(
        1, "Item Hyperlink (Common)",
        L::Item, 1 /* server lookup for item data */,
        0xFFFFFFFFu /* white */,
        "|cffffffff|Hitem:%d:%d:%d:%d|h[%s]|h|r",
        "%s",
        "inv"));
    // Quest link: |cff808080|Hquest:questId:level|h
    // [Name]|h|r. Gray color for completable quests.
    c.entries.push_back(makeLink(
        2, "Quest Hyperlink",
        L::Quest, 0,
        0xFFFFFF00u /* yellow — quest color */,
        "|cffffff00|Hquest:%d:%d|h[%s]|h|r",
        "Level %d quest",
        "questmark"));
    // Spell link.
    c.entries.push_back(makeLink(
        3, "Spell Hyperlink",
        L::Spell, 0,
        0xFFFFFFFFu /* white */,
        "|cffffffff|Hspell:%d|h[%s]|h|r",
        "%s",
        "spell"));
    // Achievement link.
    c.entries.push_back(makeLink(
        4, "Achievement Hyperlink",
        L::Achievement, 1 /* server lookup for
                            completion state */,
        0xFFFFFF00u /* yellow */,
        "|cffffff00|Hachievement:%d:%s:%d:%d:%d:%d:%d:%d:%d|h[%s]|h|r",
        "%s (%d points)",
        "achievement"));
    return c;
}

WoweeChatLinks WoweeChatLinksLoader::makeTalentTrade(
    const std::string& catalogName) {
    using L = WoweeChatLinks;
    WoweeChatLinks c;
    c.name = catalogName;
    // Talent link: green color (passive enhancements).
    c.entries.push_back(makeLink(
        10, "Talent Hyperlink",
        L::Talent, 0,
        0xFF00FF00u /* green */,
        "|cff00ff00|Htalent:%d:%d|h[%s]|h|r",
        "%s (rank %d)",
        "talent"));
    // Trade-skill recipe: orange color (rare-quality
    // recipe).
    c.entries.push_back(makeLink(
        11, "Trade Recipe Hyperlink",
        L::Trade, 1 /* server lookup for ingredients
                       list */,
        0xFFFFA500u /* orange */,
        "|cffffa500|Htrade:%d:%d:%d:%s|h[%s]|h|r",
        "%s — requires %d %s skill",
        "trade"));
    return c;
}

WoweeChatLinks WoweeChatLinksLoader::makeColorVariants(
    const std::string& catalogName) {
    using L = WoweeChatLinks;
    WoweeChatLinks c;
    c.name = catalogName;
    // Three Item-kind variants distinguished by
    // quality color. The chat composer picks which
    // variant by item quality at link time.
    c.entries.push_back(makeLink(
        20, "Item Common (gray)",
        L::Item, 1, 0xFF9D9D9D /* gray quality */,
        "|cff9d9d9d|Hitem:%d:%d:%d:%d|h[%s]|h|r",
        "%s",
        "inv"));
    c.entries.push_back(makeLink(
        21, "Item Epic (purple)",
        L::Item, 1, 0xFFA335EE /* purple */,
        "|cffa335ee|Hitem:%d:%d:%d:%d|h[%s]|h|r",
        "%s (Epic)",
        "inv"));
    c.entries.push_back(makeLink(
        22, "Item Legendary (orange)",
        L::Item, 1, 0xFFFF8000 /* orange */,
        "|cffff8000|Hitem:%d:%d:%d:%d|h[%s]|h|r",
        "%s (Legendary)",
        "inv"));
    return c;
}

} // namespace pipeline
} // namespace wowee
