#include "pipeline/wowee_anniversary_events.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'A', 'N', 'V'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wanv") {
        base += ".wanv";
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

const WoweeAnniversaryEvents::Entry*
WoweeAnniversaryEvents::findById(uint32_t eventId) const {
    for (const auto& e : entries)
        if (e.eventId == eventId) return &e;
    return nullptr;
}

std::vector<const WoweeAnniversaryEvents::Entry*>
WoweeAnniversaryEvents::findByKind(uint8_t eventKind) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries)
        if (e.eventKind == eventKind) out.push_back(&e);
    return out;
}

bool WoweeAnniversaryEventsLoader::save(
    const WoweeAnniversaryEvents& cat,
    const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.eventId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.eventKind);
        writePOD(os, e.recurrenceKind);
        writePOD(os, e.startMonth);
        writePOD(os, e.startDay);
        writePOD(os, e.durationDays);
        writePOD(os, e.pad0);
        writePOD(os, e.pad1);
        writePOD(os, e.payloadSpellId);
        writePOD(os, e.payloadItemId);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeAnniversaryEvents WoweeAnniversaryEventsLoader::load(
    const std::string& basePath) {
    WoweeAnniversaryEvents out;
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
        if (!readPOD(is, e.eventId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.eventKind) ||
            !readPOD(is, e.recurrenceKind) ||
            !readPOD(is, e.startMonth) ||
            !readPOD(is, e.startDay) ||
            !readPOD(is, e.durationDays) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.pad1) ||
            !readPOD(is, e.payloadSpellId) ||
            !readPOD(is, e.payloadItemId) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeAnniversaryEventsLoader::exists(
    const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeAnniversaryEvents
WoweeAnniversaryEventsLoader::makeStandardHolidays(
    const std::string& catalogName) {
    using A = WoweeAnniversaryEvents;
    WoweeAnniversaryEvents c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t month, uint8_t day,
                    uint16_t days, uint32_t spellId,
                    uint32_t itemId, const char* desc) {
        A::Entry e;
        e.eventId = id; e.name = name; e.description = desc;
        e.eventKind = A::Holiday;
        e.recurrenceKind = A::Yearly;
        e.startMonth = month;
        e.startDay = day;
        e.durationDays = days;
        e.payloadSpellId = spellId;
        e.payloadItemId = itemId;
        e.iconColorRGBA = packRgba(220, 200, 80);   // holiday gold
        c.entries.push_back(e);
    };
    add(1, "HallowsEnd",        10, 18, 14, 24710, 33226,
        "Hallow's End — Oct 18 to Nov 1 yearly. "
        "14-day window with cosmetic costume buff "
        "(spell 24710 Trick or Treat) and gift basket "
        "item (33226).");
    add(2, "WintersVeil",       12, 16, 17, 26157, 21525,
        "Feast of Winter Veil — Dec 16 to Jan 1 yearly. "
        "17-day window with snowfall environmental buff "
        "(spell 26157) and Smokywood Pastures gift "
        "(21525).");
    add(3, "LunarFestival",      1, 22, 21, 8898, 21100,
        "Lunar Festival — late Jan to mid Feb yearly. "
        "21-day window with Coin of Ancestry quest "
        "currency (item 21100). Buff: Spirit of Yu'lon "
        "8898.");
    add(4, "ChildrensWeek",      5,  1,  7, 0, 23007,
        "Children's Week — May 1-7 yearly. 7-day "
        "window with orphan companion-pet quest reward "
        "(item 23007 Whiskers the Rat). No buff payload.");
    add(5, "Brewfest",           9, 20, 17, 42500, 33927,
        "Brewfest — Sep 20 to Oct 6 yearly. 17-day "
        "window with Brewfest Buff (spell 42500) and "
        "Brewfest gift box (33927).");
    return c;
}

WoweeAnniversaryEvents
WoweeAnniversaryEventsLoader::makeBonusEvents(
    const std::string& catalogName) {
    using A = WoweeAnniversaryEvents;
    WoweeAnniversaryEvents c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t kind, uint8_t weekday,
                    uint16_t days, uint32_t spellId,
                    const char* desc) {
        A::Entry e;
        e.eventId = id; e.name = name; e.description = desc;
        e.eventKind = kind;
        e.recurrenceKind = A::Weekly;
        e.startMonth = 0;            // ignored for Weekly
        e.startDay = weekday;        // 0=Sunday..6=Saturday
        e.durationDays = days;
        e.payloadSpellId = spellId;
        e.payloadItemId = 0;
        e.iconColorRGBA = packRgba(140, 200, 255);   // bonus blue
        c.entries.push_back(e);
    };
    add(100, "DoubleXPWeekend",   A::DoubleXP, 5, 3, 0,
        "Double XP Weekend — Friday through Sunday "
        "(weekday 5, 3-day window). Server XP rate "
        "doubled via WCFG override; no per-character "
        "spell buff needed.");
    add(101, "DoubleHonorWeekend", A::DoubleHonor, 5, 3, 0,
        "Double Honor Weekend — Friday through Sunday. "
        "PvP honor accrual doubled.");
    add(102, "PetBattleWeekend",   A::PetBattleWeekend,
        6, 2, 0,
        "Pet Battle Weekend — Saturday-Sunday only "
        "(weekday 6, 2-day window). +50%% pet XP from "
        "battles. Anachronistic for WotLK (Pet Battles "
        "came in MoP) but useful template for custom "
        "servers.");
    add(103, "BattlegroundBonus",  A::BattlegroundBonus,
        2, 1, 0,
        "Battleground Bonus Day — Tuesday only "
        "(weekday 2). Random BG queue grants +100%% "
        "tokens for that day. Tuesday chosen as a "
        "weekday-traffic boost to balance the weekend "
        "events.");
    return c;
}

WoweeAnniversaryEvents
WoweeAnniversaryEventsLoader::makeAnniversary(
    const std::string& catalogName) {
    using A = WoweeAnniversaryEvents;
    WoweeAnniversaryEvents c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint8_t month, uint8_t day,
                    uint16_t days, uint32_t spellId,
                    uint32_t itemId, const char* desc) {
        A::Entry e;
        e.eventId = id; e.name = name; e.description = desc;
        e.eventKind = A::Anniversary;
        e.recurrenceKind = A::Yearly;
        e.startMonth = month;
        e.startDay = day;
        e.durationDays = days;
        e.payloadSpellId = spellId;
        e.payloadItemId = itemId;
        e.iconColorRGBA = packRgba(180, 60, 60);   // anniversary red
        c.entries.push_back(e);
    };
    add(200, "WoWLaunchAnniversary",   11, 23, 14, 71601, 49700,
        "World of Warcraft launch anniversary — Nov 23 "
        "yearly (US launch 2004). 14-day celebration "
        "window with Anniversary Buff (spell 71601 "
        "Bloody Anniversary) and gift item 49700.");
    add(201, "TBCLaunchAnniversary",    1, 16, 7,  71601, 49701,
        "The Burning Crusade launch anniversary — "
        "Jan 16 yearly (2007). 7-day window. Same "
        "Anniversary Buff spell, distinct gift item.");
    add(202, "WotLKLaunchAnniversary", 11, 13, 7,  71601, 49702,
        "Wrath of the Lich King launch anniversary — "
        "Nov 13 yearly (2008). 7-day window. Overlaps "
        "with WoW Launch Anniversary by 10 days — both "
        "events run concurrently for combined celebration.");
    return c;
}

} // namespace pipeline
} // namespace wowee
