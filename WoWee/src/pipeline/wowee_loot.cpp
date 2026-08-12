#include "pipeline/wowee_loot.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'L', 'O', 'T'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wlot") {
        base += ".wlot";
    }
    return base;
}

} // namespace

const WoweeLoot::Entry* WoweeLoot::findByCreatureId(uint32_t creatureId) const {
    for (const auto& e : entries) {
        if (e.creatureId == creatureId) return &e;
    }
    return nullptr;
}

bool WoweeLootLoader::save(const WoweeLoot& cat,
                           const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.creatureId);
        writePOD(os, e.flags);
        writePOD(os, e.dropCount);
        uint8_t pad[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad), 3);
        writePOD(os, e.moneyMinCopper);
        writePOD(os, e.moneyMaxCopper);
        uint32_t dropN = static_cast<uint32_t>(e.itemDrops.size());
        writePOD(os, dropN);
        for (const auto& d : e.itemDrops) {
            writePOD(os, d.itemId);
            writePOD(os, d.chancePercent);
            writePOD(os, d.minQty);
            writePOD(os, d.maxQty);
            writePOD(os, d.flags);
            uint8_t dpad = 0;
            writePOD(os, dpad);
        }
    }
    return os.good();
}

WoweeLoot WoweeLootLoader::load(const std::string& basePath) {
    WoweeLoot out;
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
        if (!readPOD(is, e.creatureId) ||
            !readPOD(is, e.flags) ||
            !readPOD(is, e.dropCount)) {
            out.entries.clear(); return out;
        }
        uint8_t pad[3];
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.moneyMinCopper) ||
            !readPOD(is, e.moneyMaxCopper)) {
            out.entries.clear(); return out;
        }
        uint32_t dropN = 0;
        if (!readPOD(is, dropN)) { out.entries.clear(); return out; }
        if (dropN > (1u << 16)) { out.entries.clear(); return out; }
        e.itemDrops.resize(dropN);
        for (auto& d : e.itemDrops) {
            if (!readPOD(is, d.itemId) ||
                !readPOD(is, d.chancePercent) ||
                !readPOD(is, d.minQty) ||
                !readPOD(is, d.maxQty) ||
                !readPOD(is, d.flags)) {
                out.entries.clear(); return out;
            }
            uint8_t dpad = 0;
            if (!readPOD(is, dpad)) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeLootLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeLoot WoweeLootLoader::makeStarter(const std::string& catalogName) {
    WoweeLoot c;
    c.name = catalogName;
    {
        WoweeLoot::Entry e;
        e.creatureId = 1;
        e.dropCount = 1;
        e.moneyMinCopper = 0; e.moneyMaxCopper = 50;
        e.itemDrops.push_back({3, 50.0f, 1, 1, 0});  // healing potion 50%
        c.entries.push_back(e);
    }
    return c;
}

WoweeLoot WoweeLootLoader::makeBandit(const std::string& catalogName) {
    WoweeLoot c;
    c.name = catalogName;
    {
        WoweeLoot::Entry e;
        e.creatureId = 1000;     // matches the camp spawns from WSPN
        e.dropCount = 2;
        e.moneyMinCopper = 5; e.moneyMaxCopper = 50;
        // Each item is rolled independently against its
        // chancePercent; the dropCount=2 means up to 2
        // distinct items per kill (the runtime is responsible
        // for pickin which 2 to roll first).
        e.itemDrops.push_back({2,    35.0f, 1, 1, 0});  // linen vest @ 35%
        e.itemDrops.push_back({101,  25.0f, 1, 3, 0});  // bolt of cloth @ 25%
        e.itemDrops.push_back({1001, 10.0f, 1, 1, 0});  // apprentice sword @ 10%
        e.itemDrops.push_back({102,  60.0f, 1, 1, 0});  // ale flask @ 60%
        c.entries.push_back(e);
    }
    return c;
}

WoweeLoot WoweeLootLoader::makeBoss(const std::string& catalogName) {
    WoweeLoot c;
    c.name = catalogName;
    {
        WoweeLoot::Entry e;
        e.creatureId = 9999;
        e.flags = 0;
        e.dropCount = 4;
        // Boss money: 50..200 silver = 5000..20000 copper.
        e.moneyMinCopper = 5000;
        e.moneyMaxCopper = 20000;
        // Guaranteed quest item.
        e.itemDrops.push_back({4, 100.0f, 1, 1,
            WoweeLoot::QuestRequired | WoweeLoot::AlwaysDrop});
        // Common drops.
        e.itemDrops.push_back({2,    80.0f, 1, 1, 0});   // chest
        e.itemDrops.push_back({1002, 40.0f, 1, 1, 0});   // journeyman blade
        e.itemDrops.push_back({2002, 30.0f, 1, 1, 0});   // iron chest
        // Group-only epic drop (low chance).
        e.itemDrops.push_back({1004,  5.0f, 1, 1,
            WoweeLoot::GroupRollOnly});                  // bloodforged
        // Mass-loot trade goods.
        e.itemDrops.push_back({101,  90.0f, 2, 5, 0});   // bolt of cloth
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
