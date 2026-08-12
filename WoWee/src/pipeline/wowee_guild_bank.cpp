#include "pipeline/wowee_guild_bank.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'B', 'K'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wgbk") {
        base += ".wgbk";
    }
    return base;
}

} // namespace

const WoweeGuildBank::Entry*
WoweeGuildBank::findById(uint32_t tabId) const {
    for (const auto& e : entries)
        if (e.tabId == tabId) return &e;
    return nullptr;
}

std::vector<const WoweeGuildBank::Entry*>
WoweeGuildBank::findByGuild(uint32_t guildId) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.guildId == guildId) out.push_back(&e);
    return out;
}

bool WoweeGuildBankLoader::save(const WoweeGuildBank& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.tabId);
        writePOD(os, e.guildId);
        writeStr(os, e.tabName);
        writePOD(os, e.iconIndex);
        writePOD(os, e.depositOnly);
        writePOD(os, e.pad0);
        writePOD(os, e.slotCount);
        for (uint32_t r = 0;
             r < WoweeGuildBank::kRankCount; ++r) {
            writePOD(os, e.perRankWithdrawalLimit[r]);
        }
    }
    return os.good();
}

WoweeGuildBank WoweeGuildBankLoader::load(
    const std::string& basePath) {
    WoweeGuildBank out;
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
        if (!readPOD(is, e.tabId) ||
            !readPOD(is, e.guildId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.tabName)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.iconIndex) ||
            !readPOD(is, e.depositOnly) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.slotCount)) {
            out.entries.clear(); return out;
        }
        for (uint32_t r = 0;
             r < WoweeGuildBank::kRankCount; ++r) {
            if (!readPOD(is, e.perRankWithdrawalLimit[r])) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeGuildBankLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

WoweeGuildBank::Entry makeTab(uint32_t tabId, uint32_t guildId,
                                const char* tabName,
                                uint32_t iconIndex,
                                uint8_t depositOnly,
                                uint16_t slotCount,
                                std::initializer_list<uint32_t> limits) {
    WoweeGuildBank::Entry e;
    e.tabId = tabId; e.guildId = guildId;
    e.tabName = tabName;
    e.iconIndex = iconIndex;
    e.depositOnly = depositOnly;
    e.slotCount = slotCount;
    uint32_t r = 0;
    for (uint32_t v : limits) {
        if (r >= WoweeGuildBank::kRankCount) break;
        e.perRankWithdrawalLimit[r++] = v;
    }
    return e;
}

} // namespace

WoweeGuildBank WoweeGuildBankLoader::makeStandardBank(
    const std::string& catalogName) {
    using G = WoweeGuildBank;
    WoweeGuildBank c;
    c.name = catalogName;
    // Per-rank limits: rank 0=GM, rank 1=Officer,
    // rank 2..7=members. GM always Unlimited; lower
    // ranks get progressively tighter caps.
    // General: open-access tab.
    c.entries.push_back(makeTab(
        1, 1, "General", 1392, 0, 98,
        {G::kUnlimited, 50, 30, 20, 15, 10, 5, 0}));
    // Materials: cloth/herb/leather pool — modest
    // caps for ranks 1-4, none below.
    c.entries.push_back(makeTab(
        2, 1, "Materials", 5765, 0, 98,
        {G::kUnlimited, 100, 50, 25, 10, 0, 0, 0}));
    // Consumables: pots/scrolls/elixirs — generous
    // cap for raiders (rank 1-3), nothing for casuals.
    c.entries.push_back(makeTab(
        3, 1, "Consumables", 5764, 0, 98,
        {G::kUnlimited, 75, 50, 25, 0, 0, 0, 0}));
    // Officer: ranks 0-2 only. Used for raid drops
    // pre-distribution.
    c.entries.push_back(makeTab(
        4, 1, "Officer", 7079, 0, 56,
        {G::kUnlimited, 20, 10, 0, 0, 0, 0, 0}));
    return c;
}

WoweeGuildBank WoweeGuildBankLoader::makeRaidGuild(
    const std::string& catalogName) {
    using G = WoweeGuildBank;
    WoweeGuildBank c;
    c.name = catalogName;
    // Tier set tabs: high slot count, withdrawal
    // strictly officer-only for the rare items.
    c.entries.push_back(makeTab(
        10, 2, "Tier1_BWL", 18811, 0, 98,
        {G::kUnlimited, 4, 2, 0, 0, 0, 0, 0}));
    c.entries.push_back(makeTab(
        11, 2, "Tier2_AQ40", 21221, 0, 98,
        {G::kUnlimited, 4, 2, 0, 0, 0, 0, 0}));
    c.entries.push_back(makeTab(
        12, 2, "Tier3_Naxx", 22349, 0, 98,
        {G::kUnlimited, 4, 2, 0, 0, 0, 0, 0}));
    // Consumables: any raider can pull.
    c.entries.push_back(makeTab(
        13, 2, "Consumables", 5764, 0, 98,
        {G::kUnlimited, 50, 50, 50, 25, 10, 0, 0}));
    // Officer: drops awaiting distribution.
    c.entries.push_back(makeTab(
        14, 2, "Officer", 7079, 0, 56,
        {G::kUnlimited, 25, 10, 0, 0, 0, 0, 0}));
    return c;
}

WoweeGuildBank WoweeGuildBankLoader::makeSmallGuild(
    const std::string& catalogName) {
    using G = WoweeGuildBank;
    WoweeGuildBank c;
    c.name = catalogName;
    // Small guild: tight controls — most ranks
    // capped at 5 slots/day for the General tab,
    // Officer tab is GM+officer only.
    c.entries.push_back(makeTab(
        20, 3, "General", 1392, 0, 28,
        {G::kUnlimited, 10, 5, 5, 5, 5, 5, 0}));
    c.entries.push_back(makeTab(
        21, 3, "Officer", 7079, 0, 14,
        {G::kUnlimited, 5, 0, 0, 0, 0, 0, 0}));
    return c;
}

} // namespace pipeline
} // namespace wowee
