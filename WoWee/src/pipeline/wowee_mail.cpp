#include "pipeline/wowee_mail.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'M', 'A', 'L'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wmal") {
        base += ".wmal";
    }
    return base;
}

} // namespace

const WoweeMail::Entry* WoweeMail::findById(uint32_t templateId) const {
    for (const auto& e : entries) if (e.templateId == templateId) return &e;
    return nullptr;
}

const char* WoweeMail::categoryName(uint8_t c) {
    switch (c) {
        case QuestReward:        return "quest";
        case Auction:            return "auction";
        case GmCorrespondence:   return "gm";
        case AchievementReward:  return "achievement";
        case EventMailing:       return "event";
        case Raffle:             return "raffle";
        case ScriptDelivery:     return "script";
        case ReturnedMail:       return "returned";
        default:                 return "unknown";
    }
}

bool WoweeMailLoader::save(const WoweeMail& cat,
                           const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.templateId);
        writePOD(os, e.senderNpcId);
        writeStr(os, e.subject);
        writeStr(os, e.body);
        writeStr(os, e.senderName);
        writePOD(os, e.moneyCopperAttached);
        uint8_t attCount = static_cast<uint8_t>(
            e.attachments.size() > 255 ? 255 : e.attachments.size());
        writePOD(os, attCount);
        writePOD(os, e.categoryId);
        writePOD(os, e.cod);
        writePOD(os, e.returnable);
        writePOD(os, e.expiryDays);
        uint8_t pad2[2] = {0, 0};
        os.write(reinterpret_cast<const char*>(pad2), 2);
        for (uint8_t k = 0; k < attCount; ++k) {
            const auto& a = e.attachments[k];
            writePOD(os, a.itemId);
            writePOD(os, a.quantity);
            os.write(reinterpret_cast<const char*>(pad2), 2);
        }
    }
    return os.good();
}

WoweeMail WoweeMailLoader::load(const std::string& basePath) {
    WoweeMail out;
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
        if (!readPOD(is, e.templateId) ||
            !readPOD(is, e.senderNpcId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.subject) || !readStr(is, e.body) ||
            !readStr(is, e.senderName)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.moneyCopperAttached)) {
            out.entries.clear(); return out;
        }
        uint8_t attCount = 0;
        if (!readPOD(is, attCount) ||
            !readPOD(is, e.categoryId) ||
            !readPOD(is, e.cod) ||
            !readPOD(is, e.returnable) ||
            !readPOD(is, e.expiryDays)) {
            out.entries.clear(); return out;
        }
        uint8_t pad2[2];
        is.read(reinterpret_cast<char*>(pad2), 2);
        if (is.gcount() != 2) { out.entries.clear(); return out; }
        e.attachments.resize(attCount);
        for (uint8_t k = 0; k < attCount; ++k) {
            auto& a = e.attachments[k];
            if (!readPOD(is, a.itemId) ||
                !readPOD(is, a.quantity)) {
                out.entries.clear(); return out;
            }
            is.read(reinterpret_cast<char*>(pad2), 2);
            if (is.gcount() != 2) { out.entries.clear(); return out; }
        }
    }
    return out;
}

bool WoweeMailLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeMail WoweeMailLoader::makeStarter(const std::string& catalogName) {
    WoweeMail c;
    c.name = catalogName;
    {
        WoweeMail::Entry e;
        e.templateId = 1;
        e.subject = "Quest Reward Overflow";
        e.body = "Your bag was full so we mailed your reward. Enjoy!";
        e.senderName = "Postmaster";
        e.categoryId = WoweeMail::QuestReward;
        e.attachments.push_back({3, 5});       // 5 healing potions (WIT 3)
        c.entries.push_back(e);
    }
    {
        WoweeMail::Entry e;
        e.templateId = 2;
        e.subject = "Auction Won";
        e.body = "Congratulations, you won the auction. Your item is attached.";
        e.senderName = "Auction House";
        e.categoryId = WoweeMail::Auction;
        e.attachments.push_back({1001, 1});    // apprentice sword
        c.entries.push_back(e);
    }
    {
        WoweeMail::Entry e;
        e.templateId = 3;
        e.subject = "A small gift";
        e.body =
            "We're sorry for the recent server downtime. "
            "Please accept this token of our appreciation.";
        e.senderName = "GameMaster";
        e.categoryId = WoweeMail::GmCorrespondence;
        e.moneyCopperAttached = 100000;        // 10g
        c.entries.push_back(e);
    }
    return c;
}

WoweeMail WoweeMailLoader::makeHoliday(const std::string& catalogName) {
    WoweeMail c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* subject, const char* body,
                    const char* sender, uint32_t itemId, uint16_t qty) {
        WoweeMail::Entry e;
        e.templateId = id; e.subject = subject; e.body = body;
        e.senderName = sender;
        e.categoryId = WoweeMail::EventMailing;
        e.attachments.push_back({itemId, qty});
        c.entries.push_back(e);
    };
    // The itemIds (200-203) shadow WTKN.makeSeasonal token IDs
    // so a holiday event arrives with its matching currency
    // sample as a free starter pack.
    add(100, "Hallow's End Sample Pack",
        "Hallow's End is upon us. Here are some Tricky Treats to start.",
        "Headless Horseman", 200, 25);
    add(101, "Brewfest Sampler",
        "The kegs are open. Have a few tokens on us.",
        "Brewmaster Drohn", 201, 10);
    add(102, "Lunar Blessing",
        "May the new year bring you fortune. A coin to begin.",
        "Elder Skygleam", 202, 5);
    add(103, "Winter's Veil Gift",
        "Greatfather Winter sends his regards. Enjoy this gift.",
        "Greatfather Winter", 203, 1);
    return c;
}

WoweeMail WoweeMailLoader::makeAuction(const std::string& catalogName) {
    WoweeMail c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* subject, const char* body,
                    uint8_t cod = 0) {
        WoweeMail::Entry e;
        e.templateId = id; e.subject = subject; e.body = body;
        e.senderName = "Auction House";
        e.categoryId = WoweeMail::Auction;
        e.cod = cod;
        e.returnable = 1;
        c.entries.push_back(e);
    };
    add(200, "Auction outbid",
        "You have been outbid on your recent auction.");
    add(201, "Auction won",
        "Congratulations on winning your auction.");
    add(202, "Auction sold",
        "Your auction has sold. Payment is attached.");
    add(203, "Auction expired",
        "Your auction expired without selling. Item returned.");
    add(204, "Auction cancelled",
        "Your auction was cancelled. Listing fee refunded.");
    return c;
}

} // namespace pipeline
} // namespace wowee
