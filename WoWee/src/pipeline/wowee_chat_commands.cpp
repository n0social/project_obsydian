#include "pipeline/wowee_chat_commands.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'C', 'M', 'D'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wcmd") {
        base += ".wcmd";
    }
    return base;
}

} // namespace

const WoweeChatCommands::Entry*
WoweeChatCommands::findById(uint32_t cmdId) const {
    for (const auto& e : entries)
        if (e.cmdId == cmdId) return &e;
    return nullptr;
}

const WoweeChatCommands::Entry*
WoweeChatCommands::findByCommand(const std::string& cmd) const {
    for (const auto& e : entries) {
        if (e.command == cmd) return &e;
        for (const auto& a : e.aliases) {
            if (a == cmd) return &e;
        }
    }
    return nullptr;
}

std::vector<const WoweeChatCommands::Entry*>
WoweeChatCommands::findByMinSecurity(uint8_t playerSec) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries) {
        if (e.minSecurityLevel <= playerSec)
            out.push_back(&e);
    }
    return out;
}

bool WoweeChatCommandsLoader::save(const WoweeChatCommands& cat,
                                     const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.cmdId);
        writeStr(os, e.command);
        writePOD(os, e.minSecurityLevel);
        writePOD(os, e.category);
        writePOD(os, e.isHidden);
        writePOD(os, e.pad0);
        writePOD(os, e.throttleMs);
        writeStr(os, e.argSchema);
        writeStr(os, e.helpText);
        uint32_t aliasCount =
            static_cast<uint32_t>(e.aliases.size());
        writePOD(os, aliasCount);
        for (const auto& a : e.aliases) {
            writeStr(os, a);
        }
    }
    return os.good();
}

WoweeChatCommands WoweeChatCommandsLoader::load(
    const std::string& basePath) {
    WoweeChatCommands out;
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
        if (!readPOD(is, e.cmdId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.command)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.minSecurityLevel) ||
            !readPOD(is, e.category) ||
            !readPOD(is, e.isHidden) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.throttleMs)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.argSchema) ||
            !readStr(is, e.helpText)) {
            out.entries.clear(); return out;
        }
        uint32_t aliasCount = 0;
        if (!readPOD(is, aliasCount)) {
            out.entries.clear(); return out;
        }
        // Sanity cap — no command should have more
        // than 32 aliases.
        if (aliasCount > 32) {
            out.entries.clear(); return out;
        }
        e.aliases.resize(aliasCount);
        for (auto& a : e.aliases) {
            if (!readStr(is, a)) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeChatCommandsLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

namespace {

WoweeChatCommands::Entry makeCmd(
    uint32_t cmdId, const char* command,
    uint8_t minSec, uint8_t category, uint8_t isHidden,
    uint32_t throttleMs,
    const char* argSchema, const char* helpText,
    std::vector<std::string> aliases) {
    WoweeChatCommands::Entry e;
    e.cmdId = cmdId; e.command = command;
    e.minSecurityLevel = minSec;
    e.category = category;
    e.isHidden = isHidden;
    e.throttleMs = throttleMs;
    e.argSchema = argSchema;
    e.helpText = helpText;
    e.aliases = std::move(aliases);
    return e;
}

} // namespace

WoweeChatCommands WoweeChatCommandsLoader::makeBasicCommands(
    const std::string& catalogName) {
    using W = WoweeChatCommands;
    WoweeChatCommands c;
    c.name = catalogName;
    // Standard player-facing info commands. All
    // Player security level. No throttle (used
    // freely). Aliases provided where vanilla
    // historically supported them.
    c.entries.push_back(makeCmd(
        1, "who", W::Player, W::Info, 0, 0,
        "[name|class|race|zone]",
        "Show players matching the filter "
        "(or all if no filter). Capped at 49 "
        "results in vanilla.",
        {"w"}));
    c.entries.push_back(makeCmd(
        2, "played", W::Player, W::Info, 0, 0,
        "(no args)",
        "Print total /played time for this "
        "character + total time at current level.",
        {}));
    c.entries.push_back(makeCmd(
        3, "time", W::Player, W::Info, 0, 0,
        "(no args)",
        "Print current server time + local "
        "time + uptime.",
        {}));
    c.entries.push_back(makeCmd(
        4, "ginfo", W::Player, W::Info, 0, 0,
        "(no args)",
        "Print guild info: name, MOTD, member "
        "count online/total, current GM.",
        {"guildinfo"}));
    return c;
}

WoweeChatCommands
WoweeChatCommandsLoader::makeMovementCommands(
    const std::string& catalogName) {
    using W = WoweeChatCommands;
    WoweeChatCommands c;
    c.name = catalogName;
    // Emote-style movement commands. Player level,
    // Movement category, no throttle. Each has at
    // least one alias for typing speed.
    c.entries.push_back(makeCmd(
        10, "sit", W::Player, W::Movement, 0, 0,
        "(no args)",
        "Sit down. Regenerates health/mana 33% "
        "faster while sitting.",
        {"sitdown"}));
    c.entries.push_back(makeCmd(
        11, "stand", W::Player, W::Movement, 0, 0,
        "(no args)",
        "Stand up.",
        {"standup", "su"}));
    c.entries.push_back(makeCmd(
        12, "sleep", W::Player, W::Movement, 0, 0,
        "(no args)",
        "Lie down to sleep. Cosmetic only.",
        {"laydown"}));
    return c;
}

WoweeChatCommands WoweeChatCommandsLoader::makeAdminCommands(
    const std::string& catalogName) {
    using W = WoweeChatCommands;
    WoweeChatCommands c;
    c.name = catalogName;
    // GameMaster security with rate-limiting to
    // prevent admin-spam abuse / accidental
    // floods.
    c.entries.push_back(makeCmd(
        20, "announce", W::GameMaster, W::AdminCmd, 0,
        5000 /* 5s throttle */,
        "<message>",
        "Broadcast a server-wide announcement to "
        "all online players. 5s per-GM throttle "
        "to prevent spam.",
        {"broadcast"}));
    c.entries.push_back(makeCmd(
        21, "kick", W::GameMaster, W::AdminCmd, 0,
        2000 /* 2s throttle */,
        "<charname> [reason]",
        "Force a player offline. Reason is sent "
        "as a system message before disconnect. "
        "2s throttle.",
        {}));
    c.entries.push_back(makeCmd(
        22, "ban", W::GameMaster, W::AdminCmd, 0,
        10000 /* 10s throttle — bans are heavy */,
        "<charname> <durationHours> <reason>",
        "Ban an account for the specified duration. "
        "Use durationHours=0 for permanent ban. "
        "Reason is logged. 10s throttle.",
        {}));
    return c;
}

} // namespace pipeline
} // namespace wowee
