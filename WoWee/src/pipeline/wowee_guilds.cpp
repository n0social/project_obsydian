#include "pipeline/wowee_guilds.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'L', 'D'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wgld") {
        base += ".wgld";
    }
    return base;
}

} // namespace

const WoweeGuild::Entry* WoweeGuild::findById(uint32_t guildId) const {
    for (const auto& e : entries) if (e.guildId == guildId) return &e;
    return nullptr;
}

const char* WoweeGuild::factionName(uint8_t f) {
    switch (f) {
        case Alliance: return "alliance";
        case Horde:    return "horde";
        default:       return "unknown";
    }
}

bool WoweeGuildLoader::save(const WoweeGuild& cat,
                            const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.guildId);
        writeStr(os, e.name);
        writeStr(os, e.leaderName);
        writeStr(os, e.motd);
        writeStr(os, e.info);
        writePOD(os, e.creationDate);
        writePOD(os, e.experience);
        writePOD(os, e.level);
        writePOD(os, e.factionId);
        uint8_t pad1 = 0;
        writePOD(os, pad1);
        writePOD(os, e.bankCopper);
        writePOD(os, e.emblem);

        uint8_t rankCount = static_cast<uint8_t>(
            e.ranks.size() > 255 ? 255 : e.ranks.size());
        writePOD(os, rankCount);
        for (uint8_t k = 0; k < rankCount; ++k) {
            const auto& r = e.ranks[k];
            writePOD(os, r.rankIndex);
            uint8_t pad3[3] = {0, 0, 0};
            os.write(reinterpret_cast<const char*>(pad3), 3);
            writeStr(os, r.name);
            writePOD(os, r.permissionsMask);
            writePOD(os, r.moneyPerDayCopper);
        }
        uint16_t memCount = static_cast<uint16_t>(
            e.members.size() > 0xFFFF ? 0xFFFF : e.members.size());
        writePOD(os, memCount);
        for (uint16_t k = 0; k < memCount; ++k) {
            const auto& m = e.members[k];
            writeStr(os, m.characterName);
            writePOD(os, m.rankIndex);
            uint8_t pad7[7] = {0};
            os.write(reinterpret_cast<const char*>(pad7), 7);
            writePOD(os, m.joinedDate);
            writeStr(os, m.publicNote);
            writeStr(os, m.officerNote);
        }
        uint8_t tabCount = static_cast<uint8_t>(
            e.bankTabs.size() > 255 ? 255 : e.bankTabs.size());
        writePOD(os, tabCount);
        for (uint8_t k = 0; k < tabCount; ++k) {
            const auto& t = e.bankTabs[k];
            writePOD(os, t.tabIndex);
            uint8_t pad3b[3] = {0, 0, 0};
            os.write(reinterpret_cast<const char*>(pad3b), 3);
            writeStr(os, t.name);
            writeStr(os, t.iconPath);
            writePOD(os, t.depositPermissionMask);
            writePOD(os, t.withdrawPermissionMask);
            writePOD(os, t.viewPermissionMask);
        }
        uint8_t perkCount = static_cast<uint8_t>(
            e.perks.size() > 255 ? 255 : e.perks.size());
        writePOD(os, perkCount);
        for (uint8_t k = 0; k < perkCount; ++k) {
            const auto& p = e.perks[k];
            writePOD(os, p.perkId);
            writeStr(os, p.name);
            writePOD(os, p.spellId);
            writePOD(os, p.requiredGuildLevel);
            uint8_t pad2c[2] = {0, 0};
            os.write(reinterpret_cast<const char*>(pad2c), 2);
        }
    }
    return os.good();
}

WoweeGuild WoweeGuildLoader::load(const std::string& basePath) {
    WoweeGuild out;
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
    auto fail = [&]() {
        out.entries.clear();
        return out;
    };
    for (auto& e : out.entries) {
        if (!readPOD(is, e.guildId)) return fail();
        if (!readStr(is, e.name) || !readStr(is, e.leaderName) ||
            !readStr(is, e.motd) || !readStr(is, e.info)) return fail();
        if (!readPOD(is, e.creationDate) ||
            !readPOD(is, e.experience) ||
            !readPOD(is, e.level) ||
            !readPOD(is, e.factionId)) return fail();
        uint8_t pad1 = 0;
        if (!readPOD(is, pad1)) return fail();
        if (!readPOD(is, e.bankCopper) || !readPOD(is, e.emblem)) return fail();

        uint8_t rankCount = 0;
        if (!readPOD(is, rankCount)) return fail();
        e.ranks.resize(rankCount);
        for (uint8_t k = 0; k < rankCount; ++k) {
            auto& r = e.ranks[k];
            if (!readPOD(is, r.rankIndex)) return fail();
            uint8_t pad3[3];
            is.read(reinterpret_cast<char*>(pad3), 3);
            if (is.gcount() != 3) return fail();
            if (!readStr(is, r.name)) return fail();
            if (!readPOD(is, r.permissionsMask) ||
                !readPOD(is, r.moneyPerDayCopper)) return fail();
        }
        uint16_t memCount = 0;
        if (!readPOD(is, memCount)) return fail();
        e.members.resize(memCount);
        for (uint16_t k = 0; k < memCount; ++k) {
            auto& m = e.members[k];
            if (!readStr(is, m.characterName)) return fail();
            if (!readPOD(is, m.rankIndex)) return fail();
            uint8_t pad7[7];
            is.read(reinterpret_cast<char*>(pad7), 7);
            if (is.gcount() != 7) return fail();
            if (!readPOD(is, m.joinedDate)) return fail();
            if (!readStr(is, m.publicNote) || !readStr(is, m.officerNote)) return fail();
        }
        uint8_t tabCount = 0;
        if (!readPOD(is, tabCount)) return fail();
        e.bankTabs.resize(tabCount);
        for (uint8_t k = 0; k < tabCount; ++k) {
            auto& t = e.bankTabs[k];
            if (!readPOD(is, t.tabIndex)) return fail();
            uint8_t pad3b[3];
            is.read(reinterpret_cast<char*>(pad3b), 3);
            if (is.gcount() != 3) return fail();
            if (!readStr(is, t.name) || !readStr(is, t.iconPath)) return fail();
            if (!readPOD(is, t.depositPermissionMask) ||
                !readPOD(is, t.withdrawPermissionMask) ||
                !readPOD(is, t.viewPermissionMask)) return fail();
        }
        uint8_t perkCount = 0;
        if (!readPOD(is, perkCount)) return fail();
        e.perks.resize(perkCount);
        for (uint8_t k = 0; k < perkCount; ++k) {
            auto& p = e.perks[k];
            if (!readPOD(is, p.perkId)) return fail();
            if (!readStr(is, p.name)) return fail();
            if (!readPOD(is, p.spellId) ||
                !readPOD(is, p.requiredGuildLevel)) return fail();
            uint8_t pad2c[2];
            is.read(reinterpret_cast<char*>(pad2c), 2);
            if (is.gcount() != 2) return fail();
        }
    }
    return out;
}

bool WoweeGuildLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

// Default 5-rank ladder used by both starter + faction-pair
// presets. Permissions widen toward the GM end of the ladder.
void addDefaultRanks(WoweeGuild::Entry& e) {
    using G = WoweeGuild;
    e.ranks.push_back({0, "Guild Master",
                        0xFFFFFFFFu, 1000000});   // 100g/day
    e.ranks.push_back({1, "Officer",
                        G::PermGuildChat | G::PermOfficerChat |
                        G::PermInvite | G::PermRemove |
                        G::PermPromote | G::PermDemote |
                        G::PermSetMotd | G::PermViewBank |
                        G::PermDeposit | G::PermWithdraw,
                        500000});   // 50g/day
    e.ranks.push_back({2, "Veteran",
                        G::PermGuildChat | G::PermInvite |
                        G::PermViewBank | G::PermDeposit |
                        G::PermWithdraw,
                        100000});   // 10g/day
    e.ranks.push_back({3, "Member",
                        G::PermGuildChat | G::PermViewBank |
                        G::PermDeposit,
                        10000});    // 1g/day
    e.ranks.push_back({4, "Initiate",
                        G::PermGuildChat,
                        0});
}

} // namespace

WoweeGuild WoweeGuildLoader::makeStarter(const std::string& catalogName) {
    WoweeGuild c;
    c.name = catalogName;
    {
        WoweeGuild::Entry e;
        e.guildId = 1; e.name = "Sentinels of Dawn";
        e.leaderName = "Bartleby";
        e.motd = "Welcome adventurer! Read the info tab.";
        e.info = "Casual leveling guild. All are welcome.";
        e.factionId = WoweeGuild::Alliance;
        e.level = 1;
        addDefaultRanks(e);
        e.members.push_back({"Bartleby",   0, 0,
                              "Founder", "Owns the inn"});
        e.members.push_back({"Hank Steelarm", 1, 0,
                              "Smith", "Friendly officer"});
        e.members.push_back({"Sera Goldroot", 3, 0,
                              "Alchemist", ""});
        c.entries.push_back(e);
    }
    return c;
}

WoweeGuild WoweeGuildLoader::makeFull(const std::string& catalogName) {
    WoweeGuild c;
    c.name = catalogName;
    {
        WoweeGuild::Entry e;
        e.guildId = 100; e.name = "Defenders of Stormwind";
        e.leaderName = "Lord Tideborne";
        e.motd = "Raid week starts Tuesday at 8 PM server.";
        e.info = "Heroic raiding guild. Apply on the website.";
        e.factionId = WoweeGuild::Alliance;
        e.level = 25;
        e.experience = 1500000;
        e.bankCopper = 50000000;     // 5000g in bank
        e.emblem = 0x12345678;
        // 6 ranks: GM + Officer + 2 Council tiers + Member + Initiate.
        addDefaultRanks(e);
        e.ranks.push_back({5, "Recruit",
                            WoweeGuild::PermGuildChat,
                            0});
        for (int k = 0; k < 8; ++k) {
            WoweeGuild::Member m;
            m.characterName = "Officer" + std::to_string(k);
            m.rankIndex = static_cast<uint8_t>(k % 6);
            m.joinedDate = 1700000000 + k * 86400;
            e.members.push_back(m);
        }
        // 4 bank tabs with progressively more restrictive
        // withdraw permissions (officers only on tabs 3 + 4).
        for (int k = 0; k < 4; ++k) {
            WoweeGuild::BankTab t;
            t.tabIndex = static_cast<uint8_t>(k);
            t.name = "Tab " + std::to_string(k + 1);
            // Bit per rank (rank 0 = bit 0 etc).
            t.depositPermissionMask  = 0x3F;       // ranks 0-5
            t.viewPermissionMask     = 0x3F;
            t.withdrawPermissionMask = (k < 2) ? 0x3F : 0x03;  // tabs 3+4 = GM/Officer only
            e.bankTabs.push_back(t);
        }
        // 3 perks referencing WSPL spell IDs from makeMage / generic.
        e.perks.push_back({1, "Fast Track",     78,    1});  // Heroic Strike (placeholder)
        e.perks.push_back({2, "Cash Flow",      6673,  10}); // Battle Shout (placeholder)
        e.perks.push_back({3, "Reinforce",      6343,  20}); // Thunder Clap (placeholder)
        c.entries.push_back(e);
    }
    return c;
}

WoweeGuild WoweeGuildLoader::makeFactionPair(const std::string& catalogName) {
    WoweeGuild c;
    c.name = catalogName;
    {
        WoweeGuild::Entry e;
        e.guildId = 200; e.name = "Light's Vanguard";
        e.leaderName = "Lothar Crownguard";
        e.factionId = WoweeGuild::Alliance;
        addDefaultRanks(e);
        e.members.push_back({"Lothar Crownguard", 0, 0, "GM", ""});
        c.entries.push_back(e);
    }
    {
        WoweeGuild::Entry e;
        e.guildId = 201; e.name = "Bloodfang Warband";
        e.leaderName = "Garrok Bloodfang";
        e.factionId = WoweeGuild::Horde;
        addDefaultRanks(e);
        e.members.push_back({"Garrok Bloodfang", 0, 0, "Chieftain", ""});
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
