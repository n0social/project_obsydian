#include "pipeline/wowee_server_broadcasts.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'C', 'B'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wscb") {
        base += ".wscb";
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

const WoweeServerBroadcasts::Entry*
WoweeServerBroadcasts::findById(uint32_t broadcastId) const {
    for (const auto& e : entries)
        if (e.broadcastId == broadcastId) return &e;
    return nullptr;
}

std::vector<const WoweeServerBroadcasts::Entry*>
WoweeServerBroadcasts::findFor(uint8_t playerFaction,
                                 uint8_t playerLevel) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (!(e.factionFilter & playerFaction)) continue;
        if (e.minLevel > 0 && playerLevel < e.minLevel) continue;
        if (e.maxLevel > 0 && playerLevel > e.maxLevel) continue;
        out.push_back(&e);
    }
    return out;
}

std::vector<const WoweeServerBroadcasts::Entry*>
WoweeServerBroadcasts::findByChannel(uint8_t channelKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.channelKind == channelKind) out.push_back(&e);
    return out;
}

bool WoweeServerBroadcastsLoader::save(const WoweeServerBroadcasts& cat,
                                        const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.broadcastId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.messageText);
        writePOD(os, e.intervalSeconds);
        writePOD(os, e.channelKind);
        writePOD(os, e.factionFilter);
        writePOD(os, e.minLevel);
        writePOD(os, e.maxLevel);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeServerBroadcasts WoweeServerBroadcastsLoader::load(
    const std::string& basePath) {
    WoweeServerBroadcasts out;
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
        if (!readPOD(is, e.broadcastId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) ||
            !readStr(is, e.description) ||
            !readStr(is, e.messageText)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.intervalSeconds) ||
            !readPOD(is, e.channelKind) ||
            !readPOD(is, e.factionFilter) ||
            !readPOD(is, e.minLevel) ||
            !readPOD(is, e.maxLevel) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeServerBroadcastsLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeServerBroadcasts WoweeServerBroadcastsLoader::makeMotd(
    const std::string& catalogName) {
    using S = WoweeServerBroadcasts;
    WoweeServerBroadcasts c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* msg,
                    const char* desc) {
        S::Entry e;
        e.broadcastId = id; e.name = name;
        e.description = desc;
        e.messageText = msg;
        e.intervalSeconds = 0;        // login-only
        e.channelKind = S::MOTD;
        e.factionFilter = S::Both;
        e.iconColorRGBA = packRgba(255, 220, 100);   // MOTD gold
        c.entries.push_back(e);
    };
    add(1, "WelcomeBanner",
        "Welcome to the server! Type /help for assistance.",
        "Top-of-MOTD welcome banner shown on every login.");
    add(2, "PatchNotesSummary",
        "Patch 3.3.5b: ICC 25H tuning + bugfixes. See "
        "/forum for full notes.",
        "One-line patch notes summary; updated each "
        "release. Replaces the hardcoded WoW 3.3.5a "
        "client-side default MOTD entry.");
    add(3, "DiscordInvite",
        "Join our community Discord: discord.gg/example "
        "for live support and groupfinder.",
        "Discord invite footer. Server-custom; not in "
        "stock WoW.");
    add(4, "ForumLink",
        "Bug reports + feature requests: forum.example.com",
        "Forum URL footer. Server-custom; not in stock "
        "WoW.");
    return c;
}

WoweeServerBroadcasts WoweeServerBroadcastsLoader::makeMaintenance(
    const std::string& catalogName) {
    using S = WoweeServerBroadcasts;
    WoweeServerBroadcasts c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* msg,
                    const char* desc) {
        S::Entry e;
        e.broadcastId = id; e.name = name;
        e.description = desc;
        e.messageText = msg;
        e.intervalSeconds = 0;        // one-shot, scheduled
                                       // externally
        e.channelKind = S::RaidWarning;
        e.factionFilter = S::Both;
        e.iconColorRGBA = packRgba(220, 60, 60);   // warning red
        c.entries.push_back(e);
    };
    add(100, "Restart15Min",
        "[SERVER] Restart in 15 minutes. Please complete "
        "your current activity and find a safe location.",
        "First maintenance warning — fired by the cron "
        "scheduler 15min before a planned restart.");
    add(101, "Restart5Min",
        "[SERVER] Restart in 5 minutes. World will save "
        "and shut down shortly.",
        "Second maintenance warning — fired 5min before "
        "restart.");
    add(102, "Restart1Min",
        "[SERVER] Restart in 60 SECONDS. Disconnect now "
        "to avoid character rollback.",
        "Final maintenance warning — fired 60s before "
        "restart. RaidWarning channel ensures the red "
        "banner appears center-screen.");
    return c;
}

WoweeServerBroadcasts WoweeServerBroadcastsLoader::makeHelpTips(
    const std::string& catalogName) {
    using S = WoweeServerBroadcasts;
    WoweeServerBroadcasts c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, const char* msg,
                    uint8_t minLvl, const char* desc) {
        S::Entry e;
        e.broadcastId = id; e.name = name;
        e.description = desc;
        e.messageText = msg;
        e.intervalSeconds = 600;        // 10-min cycle
        e.channelKind = S::HelpTip;
        e.factionFilter = S::Both;
        e.minLevel = minLvl;
        e.iconColorRGBA = packRgba(140, 200, 255);   // help blue
        c.entries.push_back(e);
    };
    add(200, "TalentTip",
        "[Tip] Your first talent point unlocks at level "
        "10. Press 'N' to open the talent tree.",
        10, "Help tip about talent tree, level-gated to "
        "10+ so it doesn't confuse new characters.");
    add(201, "MountTip",
        "[Tip] Your first mount becomes available at "
        "level 20. Visit the riding trainer in your "
        "capital city.",
        20, "Help tip about mount training, level-gated "
        "to 20+.");
    add(202, "AuctionTip",
        "[Tip] Selling unwanted gear at the Auction House "
        "is the fastest way to fund your next mount or "
        "training.",
        15, "Help tip about auction house economy.");
    add(203, "ProfessionTip",
        "[Tip] You can learn 2 primary professions and "
        "all 3 secondary skills (Cooking / First Aid / "
        "Fishing).",
        5, "Help tip about profession slots; fires for "
        "any character past tutorial.");
    add(204, "DungeonFinderTip",
        "[Tip] Press 'I' to open the Dungeon Finder. "
        "Queue as your role to get faster pops.",
        15, "Help tip about Dungeon Finder UI; level-"
        "gated to 15+ when LFG becomes available.");
    add(205, "HearthstoneTip",
        "[Tip] Right-click your Hearthstone to teleport "
        "to your bound inn. Speak to any innkeeper to "
        "rebind.",
        1, "Help tip about hearthstone mechanic; "
        "applies from level 1.");
    return c;
}

} // namespace pipeline
} // namespace wowee
