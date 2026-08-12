#include "pipeline/wowee_word_filters.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'W', 'F', 'L'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wwfl") {
        base += ".wwfl";
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

const WoweeWordFilters::Entry*
WoweeWordFilters::findById(uint32_t filterId) const {
    for (const auto& e : entries)
        if (e.filterId == filterId) return &e;
    return nullptr;
}

std::vector<const WoweeWordFilters::Entry*>
WoweeWordFilters::findByKind(uint8_t filterKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.filterKind == filterKind) out.push_back(&e);
    return out;
}

bool WoweeWordFiltersLoader::save(const WoweeWordFilters& cat,
                                    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.filterId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.pattern);
        writeStr(os, e.replacement);
        writePOD(os, e.filterKind);
        writePOD(os, e.severity);
        writePOD(os, e.caseSensitive);
        writePOD(os, e.pad0);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeWordFilters WoweeWordFiltersLoader::load(
    const std::string& basePath) {
    WoweeWordFilters out;
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
        if (!readPOD(is, e.filterId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.pattern) ||
            !readStr(is, e.replacement)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.filterKind) ||
            !readPOD(is, e.severity) ||
            !readPOD(is, e.caseSensitive) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeWordFiltersLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeWordFilters WoweeWordFiltersLoader::makeSpamRMT(
    const std::string& catalogName) {
    using F = WoweeWordFilters;
    WoweeWordFilters c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* pattern, const char* repl,
                    uint8_t severity, uint8_t caseSens,
                    const char* desc) {
        F::Entry e;
        e.filterId = id; e.name = name; e.description = desc;
        e.pattern = pattern;
        e.replacement = repl;
        e.filterKind = F::GoldSeller;
        e.severity = severity;
        e.caseSensitive = caseSens;
        e.iconColorRGBA = packRgba(220, 200, 80);   // RMT yellow
        c.entries.push_back(e);
    };
    // RMT-pattern detection. All examples are PG —
    // generic gold-seller phrases without profanity.
    add(1, "WtsGold",
        "wts gold", "***",
        F::Drop, 0,
        "'wts gold' (Want To Sell) RMT solicitation. "
        "Drop the message; warn server moderators.");
    add(2, "WtbGold",
        "wtb gold", "***",
        F::Drop, 0,
        "'wtb gold' (Want To Buy) RMT solicitation.");
    add(3, "GoldTypoSubstitution",
        "g0ld", "gold",
        F::Replace, 0,
        "Common typo-substitution to bypass exact-string "
        "filters: 'g0ld' (zero instead of o). Replace "
        "with 'gold' so the message gets normalized then "
        "re-checked by other filters.");
    add(4, "BulkGoldOffer",
        "1000g for", "***",
        F::Drop, 0,
        "Common gold-seller offer pattern: '1000g for "
        "$X' or '1000g for cheap'. Match the prefix.");
    add(5, "FreeGold",
        "free gold", "***",
        F::Mute, 0,
        "'free gold' adverts — almost always RMT or "
        "phishing. Mute sender for 60s + drop message.");
    return c;
}

WoweeWordFilters WoweeWordFiltersLoader::makeAllCaps(
    const std::string& catalogName) {
    using F = WoweeWordFilters;
    WoweeWordFilters c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* pattern, const char* repl,
                    uint8_t severity, uint8_t caseSens,
                    const char* desc) {
        F::Entry e;
        e.filterId = id; e.name = name; e.description = desc;
        e.pattern = pattern;
        e.replacement = repl;
        e.filterKind = F::AllCaps;
        e.severity = severity;
        e.caseSensitive = caseSens;
        e.iconColorRGBA = packRgba(220, 80, 100);   // shout red
        c.entries.push_back(e);
    };
    add(100, "AllCapsWord",
        "ANYBODY",
        "anybody",
        F::Replace, 1,
        "Single common all-caps word — replace with "
        "lowercase. Case-sensitive match (caseSens=1) so "
        "'Anybody' isn't affected.");
    add(101, "AllCapsExclamation",
        "!!!",
        "!",
        F::Replace, 0,
        "Triple-exclamation overuse. Collapse to single "
        "'!' so emphasis stays but spam-style "
        "punctuation is normalized.");
    add(102, "DollarSpam",
        "$$$",
        "***",
        F::Replace, 0,
        "Money-emphasis spam ('$$$ FOR YOU!!!' style). "
        "Replace with redaction marks.");
    return c;
}

WoweeWordFilters WoweeWordFiltersLoader::makeURLDetect(
    const std::string& catalogName) {
    using F = WoweeWordFilters;
    WoweeWordFilters c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    const char* pattern, const char* repl,
                    uint8_t severity, uint8_t caseSens,
                    const char* desc) {
        F::Entry e;
        e.filterId = id; e.name = name; e.description = desc;
        e.pattern = pattern;
        e.replacement = repl;
        e.filterKind = F::URL;
        e.severity = severity;
        e.caseSensitive = caseSens;
        e.iconColorRGBA = packRgba(140, 200, 255);   // URL blue
        c.entries.push_back(e);
    };
    add(200, "HttpUrl",
        "http://", "[link]",
        F::Replace, 0,
        "HTTP URL — replace with [link] placeholder. "
        "Server admins can decide per-channel whether "
        "to permit links via WCHN config.");
    add(201, "HttpsUrl",
        "https://", "[link]",
        F::Replace, 0,
        "HTTPS URL — same handling as HTTP.");
    add(202, "WwwShortUrl",
        "www.", "[link]",
        F::Replace, 0,
        "Bare www.example URL — common shortening when "
        "the http:// prefix is omitted. Catch-all.");
    return c;
}

} // namespace pipeline
} // namespace wowee
