#include "pipeline/wowee_stable_slots.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'T', 'C'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wstc") {
        base += ".wstc";
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

const WoweeStableSlot::Entry*
WoweeStableSlot::findById(uint32_t slotId) const {
    for (const auto& e : entries)
        if (e.slotId == slotId) return &e;
    return nullptr;
}

int WoweeStableSlot::unlockedSlotCount(uint8_t characterLevel) const {
    int n = 0;
    for (const auto& e : entries) {
        if (characterLevel >= e.minLevelToUnlock) ++n;
    }
    return n;
}

bool WoweeStableSlotLoader::save(const WoweeStableSlot& cat,
                                  const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.slotId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writePOD(os, e.displayOrder);
        writePOD(os, e.minLevelToUnlock);
        writePOD(os, e.isPremium);
        writePOD(os, e.pad0);
        writePOD(os, e.copperCost);
        writePOD(os, e.iconColorRGBA);
    }
    return os.good();
}

WoweeStableSlot WoweeStableSlotLoader::load(const std::string& basePath) {
    WoweeStableSlot out;
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
        if (!readPOD(is, e.slotId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.displayOrder) ||
            !readPOD(is, e.minLevelToUnlock) ||
            !readPOD(is, e.isPremium) ||
            !readPOD(is, e.pad0) ||
            !readPOD(is, e.copperCost) ||
            !readPOD(is, e.iconColorRGBA)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeStableSlotLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeStableSlot WoweeStableSlotLoader::makeStandard(
    const std::string& catalogName) {
    using S = WoweeStableSlot;
    WoweeStableSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t order,
                    uint8_t lvl, uint32_t cop, const char* desc) {
        S::Entry e;
        e.slotId = id; e.name = name; e.description = desc;
        e.displayOrder = order;
        e.minLevelToUnlock = lvl;
        e.copperCost = cop;
        e.iconColorRGBA = packRgba(140, 100, 60);   // stable brown
        c.entries.push_back(e);
    };
    // 5 canonical slots: active + 4 stabled. Active is
    // always unlocked (lvl 10 = hunter trainable), then
    // stabled slots open at 20/30/40/50 with escalating
    // gold costs.
    add(1, "ActivePet",       0, 10,       0,
        "Active pet slot — auto-unlocked at hunter lvl 10.");
    add(2, "StableSlot1",     1, 20,    1000,
        "Stable slot 1 — unlocks at lvl 20 for 10 silver.");
    add(3, "StableSlot2",     2, 30,    5000,
        "Stable slot 2 — unlocks at lvl 30 for 50 silver.");
    add(4, "StableSlot3",     3, 40,   20000,
        "Stable slot 3 — unlocks at lvl 40 for 2 gold.");
    add(5, "StableSlot4",     4, 50,  100000,
        "Stable slot 4 — unlocks at lvl 50 for 10 gold.");
    return c;
}

WoweeStableSlot WoweeStableSlotLoader::makeCata(
    const std::string& catalogName) {
    using S = WoweeStableSlot;
    WoweeStableSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t order,
                    uint8_t lvl, uint32_t cop, const char* desc) {
        S::Entry e;
        e.slotId = id; e.name = name; e.description = desc;
        e.displayOrder = order;
        e.minLevelToUnlock = lvl;
        e.copperCost = cop;
        e.iconColorRGBA = packRgba(160, 120, 80);
        c.entries.push_back(e);
    };
    // Cata-era 6-slot layout: active + 5 stabled.
    add(100, "ActivePet",        0, 10,       0,
        "Active pet — auto-unlocked at lvl 10.");
    add(101, "CataStableSlot1",  1, 20,    1000,
        "Stable slot 1 — lvl 20, 10s.");
    add(102, "CataStableSlot2",  2, 30,    5000,
        "Stable slot 2 — lvl 30, 50s.");
    add(103, "CataStableSlot3",  3, 40,   20000,
        "Stable slot 3 — lvl 40, 2g.");
    add(104, "CataStableSlot4",  4, 50,  100000,
        "Stable slot 4 — lvl 50, 10g.");
    add(105, "CataStableSlot5",  5, 60,  250000,
        "Stable slot 5 — lvl 60, 25g (Cataclysm extension).");
    return c;
}

WoweeStableSlot WoweeStableSlotLoader::makePremium(
    const std::string& catalogName) {
    using S = WoweeStableSlot;
    WoweeStableSlot c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint8_t order,
                    const char* desc) {
        S::Entry e;
        e.slotId = id; e.name = name; e.description = desc;
        e.displayOrder = order;
        e.minLevelToUnlock = 1;
        e.isPremium = 1;
        e.copperCost = 0;
        e.iconColorRGBA = packRgba(240, 180, 240);   // donor pink
        c.entries.push_back(e);
    };
    // Server-custom donator-only slots — no level gate,
    // no gold cost; access controlled by external donor
    // status check.
    add(200, "DonatorSlot1", 6, "Donator slot 1 — premium, no level/gold gate.");
    add(201, "DonatorSlot2", 7, "Donator slot 2 — premium, no level/gold gate.");
    add(202, "DonatorSlot3", 8, "Donator slot 3 — premium, no level/gold gate.");
    add(203, "AnniversarySlot", 9, "Anniversary slot — premium, server event reward.");
    return c;
}

} // namespace pipeline
} // namespace wowee
