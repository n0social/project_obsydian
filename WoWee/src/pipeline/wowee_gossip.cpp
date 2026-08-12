#include "pipeline/wowee_gossip.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'G', 'S', 'P'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wgsp") {
        base += ".wgsp";
    }
    return base;
}

} // namespace

const WoweeGossip::Entry* WoweeGossip::findById(uint32_t menuId) const {
    for (const auto& e : entries) {
        if (e.menuId == menuId) return &e;
    }
    return nullptr;
}

const char* WoweeGossip::optionKindName(uint8_t k) {
    switch (k) {
        case Close:        return "close";
        case Submenu:      return "submenu";
        case Vendor:       return "vendor";
        case Trainer:      return "trainer";
        case Quest:        return "quest";
        case Tabard:       return "tabard";
        case Banker:       return "banker";
        case Innkeeper:    return "innkeeper";
        case FlightMaster: return "flight";
        case TextOnly:     return "text";
        case Script:       return "script";
        case Battlemaster: return "battlemaster";
        case Auctioneer:   return "auctioneer";
        default:           return "unknown";
    }
}

bool WoweeGossipLoader::save(const WoweeGossip& cat,
                             const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.menuId);
        writeStr(os, e.titleText);
        uint8_t optCount = static_cast<uint8_t>(
            e.options.size() > 255 ? 255 : e.options.size());
        writePOD(os, optCount);
        uint8_t pad[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad), 3);
        for (uint8_t k = 0; k < optCount; ++k) {
            const auto& o = e.options[k];
            writePOD(os, o.optionId);
            writeStr(os, o.text);
            writePOD(os, o.kind);
            os.write(reinterpret_cast<const char*>(pad), 3);
            writePOD(os, o.actionTarget);
            writePOD(os, o.requiredFlags);
            writePOD(os, o.moneyCostCopper);
        }
    }
    return os.good();
}

WoweeGossip WoweeGossipLoader::load(const std::string& basePath) {
    WoweeGossip out;
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
        if (!readPOD(is, e.menuId)) { out.entries.clear(); return out; }
        if (!readStr(is, e.titleText)) { out.entries.clear(); return out; }
        uint8_t optCount = 0;
        if (!readPOD(is, optCount)) { out.entries.clear(); return out; }
        uint8_t pad[3];
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        e.options.resize(optCount);
        for (uint8_t k = 0; k < optCount; ++k) {
            auto& o = e.options[k];
            if (!readPOD(is, o.optionId)) {
                out.entries.clear(); return out;
            }
            if (!readStr(is, o.text)) {
                out.entries.clear(); return out;
            }
            if (!readPOD(is, o.kind)) {
                out.entries.clear(); return out;
            }
            is.read(reinterpret_cast<char*>(pad), 3);
            if (is.gcount() != 3) { out.entries.clear(); return out; }
            if (!readPOD(is, o.actionTarget) ||
                !readPOD(is, o.requiredFlags) ||
                !readPOD(is, o.moneyCostCopper)) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeGossipLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeGossip WoweeGossipLoader::makeStarter(const std::string& catalogName) {
    WoweeGossip c;
    c.name = catalogName;
    {
        WoweeGossip::Entry e;
        e.menuId = 1;
        e.titleText = "Greetings, traveler. How can I help?";
        e.options.push_back({1, "I want to browse your goods.",
                              WoweeGossip::Vendor, 0,
                              WoweeGossip::Closes, 0});
        e.options.push_back({2, "Train me.",
                              WoweeGossip::Trainer, 0,
                              WoweeGossip::Closes, 0});
        e.options.push_back({3, "Goodbye.",
                              WoweeGossip::Close, 0,
                              WoweeGossip::Closes, 0});
        c.entries.push_back(e);
    }
    return c;
}

WoweeGossip WoweeGossipLoader::makeInnkeeper(const std::string& catalogName) {
    WoweeGossip c;
    c.name = catalogName;
    {
        // menuId 4001 deliberately matches what WCRT.makeStarter
        // and WCRT.makeMerchants set as Bartleby's gossipId
        // (currently 0 — set this when the demo content stack
        // is updated to wire WCRT.gossipId = 4001).
        WoweeGossip::Entry e;
        e.menuId = 4001;
        e.titleText =
            "Welcome to the inn! What'll it be — a room, "
            "a meal, or directions?";
        e.options.push_back({1, "Make this inn my home.",
                              WoweeGossip::Innkeeper, 0,
                              WoweeGossip::Closes, 0});
        e.options.push_back({2, "Show me what you have for sale.",
                              WoweeGossip::Vendor, 4001,
                              WoweeGossip::Closes, 0});
        e.options.push_back({3, "I need to take a flight.",
                              WoweeGossip::FlightMaster, 0,
                              WoweeGossip::Closes, 0});
        e.options.push_back({4, "Tell me about the area.",
                              WoweeGossip::Submenu, 4002,
                              0, 0});
        e.options.push_back({5, "Goodbye.",
                              WoweeGossip::Close, 0,
                              WoweeGossip::Closes, 0});
        c.entries.push_back(e);
    }
    {
        // Submenu reached from the "tell me about the area" option.
        WoweeGossip::Entry e;
        e.menuId = 4002;
        e.titleText =
            "There's been bandit trouble of late. The Defias "
            "have a camp east of here. Mind your purse on the "
            "road.";
        e.options.push_back({1, "Back.",
                              WoweeGossip::Submenu, 4001,
                              0, 0});
        e.options.push_back({2, "Goodbye.",
                              WoweeGossip::Close, 0,
                              WoweeGossip::Closes, 0});
        c.entries.push_back(e);
    }
    return c;
}

WoweeGossip WoweeGossipLoader::makeQuestGiver(const std::string& catalogName) {
    WoweeGossip c;
    c.name = catalogName;
    {
        WoweeGossip::Entry e;
        e.menuId = 5000;
        e.titleText =
            "I have work for someone of your obvious talent.";
        // Quest options reference WQT.questId values from
        // makeStarter/makeChain.
        e.options.push_back({1, "Tell me about Bandit Trouble.",
                              WoweeGossip::Quest, 1,
                              0, 0});
        e.options.push_back({2, "What's this about a camp?",
                              WoweeGossip::Quest, 100,
                              0, 0});
        e.options.push_back({3, "I have business with the bank.",
                              WoweeGossip::Banker, 0,
                              WoweeGossip::Closes, 0});
        e.options.push_back({4, "Pay 10 gold to respec my talents.",
                              WoweeGossip::Script, 9001,
                              WoweeGossip::Coinpouch | WoweeGossip::Closes,
                              100000});  // 10g
        e.options.push_back({5, "Goodbye.",
                              WoweeGossip::Close, 0,
                              WoweeGossip::Closes, 0});
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
