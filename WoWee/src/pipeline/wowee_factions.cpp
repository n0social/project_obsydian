#include "pipeline/wowee_factions.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'F', 'A', 'C'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wfac") {
        base += ".wfac";
    }
    return base;
}

} // namespace

const WoweeFaction::Entry* WoweeFaction::findById(uint32_t factionId) const {
    for (const auto& e : entries) {
        if (e.factionId == factionId) return &e;
    }
    return nullptr;
}

bool WoweeFaction::isHostile(uint32_t aFactionId, uint32_t bFactionId) const {
    const Entry* a = findById(aFactionId);
    if (!a) return false;
    for (uint32_t e : a->enemies) {
        if (e == bFactionId) return true;
    }
    return false;
}

bool WoweeFactionLoader::save(const WoweeFaction& cat,
                              const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.factionId);
        writePOD(os, e.parentFactionId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.reputationFlags);
        writePOD(os, e.baseReputation);
        writePOD(os, e.thresholdHostile);
        writePOD(os, e.thresholdUnfriendly);
        writePOD(os, e.thresholdNeutral);
        writePOD(os, e.thresholdFriendly);
        writePOD(os, e.thresholdHonored);
        writePOD(os, e.thresholdRevered);
        writePOD(os, e.thresholdExalted);
        uint8_t enCount = static_cast<uint8_t>(
            e.enemies.size() > 255 ? 255 : e.enemies.size());
        writePOD(os, enCount);
        uint8_t pad[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad), 3);
        for (uint8_t k = 0; k < enCount; ++k) writePOD(os, e.enemies[k]);
        uint8_t frCount = static_cast<uint8_t>(
            e.friends.size() > 255 ? 255 : e.friends.size());
        writePOD(os, frCount);
        os.write(reinterpret_cast<const char*>(pad), 3);
        for (uint8_t k = 0; k < frCount; ++k) writePOD(os, e.friends[k]);
    }
    return os.good();
}

WoweeFaction WoweeFactionLoader::load(const std::string& basePath) {
    WoweeFaction out;
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
        if (!readPOD(is, e.factionId) ||
            !readPOD(is, e.parentFactionId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.reputationFlags) ||
            !readPOD(is, e.baseReputation) ||
            !readPOD(is, e.thresholdHostile) ||
            !readPOD(is, e.thresholdUnfriendly) ||
            !readPOD(is, e.thresholdNeutral) ||
            !readPOD(is, e.thresholdFriendly) ||
            !readPOD(is, e.thresholdHonored) ||
            !readPOD(is, e.thresholdRevered) ||
            !readPOD(is, e.thresholdExalted)) {
            out.entries.clear(); return out;
        }
        uint8_t enCount = 0;
        if (!readPOD(is, enCount)) { out.entries.clear(); return out; }
        uint8_t pad[3];
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        e.enemies.resize(enCount);
        for (uint8_t k = 0; k < enCount; ++k) {
            if (!readPOD(is, e.enemies[k])) {
                out.entries.clear(); return out;
            }
        }
        uint8_t frCount = 0;
        if (!readPOD(is, frCount)) { out.entries.clear(); return out; }
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        e.friends.resize(frCount);
        for (uint8_t k = 0; k < frCount; ++k) {
            if (!readPOD(is, e.friends[k])) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeFactionLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeFaction WoweeFactionLoader::makeStarter(const std::string& catalogName) {
    WoweeFaction c;
    c.name = catalogName;
    {
        // factionId 35 is canonical "friendly to all" used by
        // WCRT.makeStarter and WCRT.makeMerchants.
        WoweeFaction::Entry e;
        e.factionId = 35; e.name = "Friendly";
        e.description = "Friendly to all players. Standard NPCs.";
        e.enemies.push_back(14);
        c.entries.push_back(e);
    }
    {
        // factionId 14 is canonical "hostile to all" used by
        // WCRT.makeBandit.
        WoweeFaction::Entry e;
        e.factionId = 14; e.name = "Hostile";
        e.description = "Hostile on sight. Bandits / monsters.";
        e.reputationFlags = WoweeFaction::AtWarDefault;
        e.enemies.push_back(35);
        e.enemies.push_back(1);
        c.entries.push_back(e);
    }
    {
        WoweeFaction::Entry e;
        e.factionId = 1; e.name = "Player Faction";
        e.description = "The player's own faction; never hostile to self.";
        e.enemies.push_back(14);
        c.entries.push_back(e);
    }
    return c;
}

WoweeFaction WoweeFactionLoader::makeAlliance(const std::string& catalogName) {
    WoweeFaction c;
    c.name = catalogName;
    auto add = [&](uint32_t id, uint32_t parent, const char* name,
                   const char* desc, std::vector<uint32_t> enemies,
                   std::vector<uint32_t> friends_) {
        WoweeFaction::Entry e;
        e.factionId = id; e.parentFactionId = parent;
        e.name = name; e.description = desc;
        e.reputationFlags = WoweeFaction::VisibleOnTab;
        e.enemies = std::move(enemies);
        e.friends = std::move(friends_);
        c.entries.push_back(e);
    };
    // Header (parent grouping in the player reputation panel).
    {
        WoweeFaction::Entry e;
        e.factionId = 1000; e.name = "Alliance";
        e.description = "The Alliance, united.";
        e.reputationFlags = WoweeFaction::IsHeader;
        c.entries.push_back(e);
    }
    add(72, 1000, "Stormwind",
        "City of Stormwind. Capital of the human kingdom.",
        {349},                  // enemy: Defias
        {69, 54});              // friends: Darnassus, Ironforge
    add(69, 1000, "Darnassus",
        "Night elf city in the world tree.",
        {349}, {72, 54});
    add(54, 1000, "Ironforge",
        "Dwarven mountain city.",
        {349}, {72, 69});
    add(349, 0, "Defias Brotherhood",
        "Outlaw conspiracy in Westfall.",
        {72, 69, 54}, {});
    return c;
}

WoweeFaction WoweeFactionLoader::makeWildlife(const std::string& catalogName) {
    WoweeFaction c;
    c.name = catalogName;
    auto addBeast = [&](uint32_t id, const char* name) {
        WoweeFaction::Entry e;
        e.factionId = id; e.name = name;
        e.reputationFlags = WoweeFaction::Hidden;
        // Beasts are hostile to player factions (1) but not
        // to each other — the wildlife of a zone fights the
        // player but won't pull adjacent packs.
        e.enemies.push_back(1);
        c.entries.push_back(e);
    };
    addBeast(2001, "Wolves");
    addBeast(2002, "Bears");
    addBeast(2003, "Spiders");
    {
        WoweeFaction::Entry e;
        e.factionId = 2010; e.name = "Kobolds";
        e.description = "Cave-dwelling humanoids; mob in groups.";
        e.reputationFlags = WoweeFaction::AtWarDefault;
        e.enemies.push_back(1);
        // Kobolds and wolves coexist (both feral hostile, but
        // different ecology niches).
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
