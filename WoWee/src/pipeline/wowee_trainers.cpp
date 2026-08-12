#include "pipeline/wowee_trainers.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'T', 'R', 'N'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wtrn") {
        base += ".wtrn";
    }
    return base;
}

} // namespace

const WoweeTrainer::Entry* WoweeTrainer::findByNpc(uint32_t npcId) const {
    for (const auto& e : entries) {
        if (e.npcId == npcId) return &e;
    }
    return nullptr;
}

std::string WoweeTrainer::kindMaskName(uint8_t k) {
    std::string s;
    if (k & Trainer) s += "trainer";
    if (k & Vendor)  { if (!s.empty()) s += "+"; s += "vendor"; }
    if (s.empty())   s = "-";
    return s;
}

bool WoweeTrainerLoader::save(const WoweeTrainer& cat,
                              const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.npcId);
        writePOD(os, e.kindMask);
        uint8_t pad[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad), 3);
        writeStr(os, e.greeting);
        uint16_t spellCount = static_cast<uint16_t>(
            e.spells.size() > 0xFFFF ? 0xFFFF : e.spells.size());
        uint16_t itemCount = static_cast<uint16_t>(
            e.items.size() > 0xFFFF ? 0xFFFF : e.items.size());
        writePOD(os, spellCount);
        writePOD(os, itemCount);
        for (uint16_t k = 0; k < spellCount; ++k) {
            const auto& s = e.spells[k];
            writePOD(os, s.spellId);
            writePOD(os, s.moneyCostCopper);
            writePOD(os, s.requiredSkillId);
            writePOD(os, s.requiredSkillRank);
            writePOD(os, s.requiredLevel);
        }
        for (uint16_t k = 0; k < itemCount; ++k) {
            const auto& it = e.items[k];
            writePOD(os, it.itemId);
            writePOD(os, it.stockCount);
            writePOD(os, it.restockSec);
            writePOD(os, it.extendedCost);
            writePOD(os, it.moneyCostCopper);
        }
    }
    return os.good();
}

WoweeTrainer WoweeTrainerLoader::load(const std::string& basePath) {
    WoweeTrainer out;
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
        if (!readPOD(is, e.npcId) || !readPOD(is, e.kindMask)) {
            out.entries.clear(); return out;
        }
        uint8_t pad[3];
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readStr(is, e.greeting)) { out.entries.clear(); return out; }
        uint16_t spellCount = 0, itemCount = 0;
        if (!readPOD(is, spellCount) || !readPOD(is, itemCount)) {
            out.entries.clear(); return out;
        }
        e.spells.resize(spellCount);
        for (uint16_t k = 0; k < spellCount; ++k) {
            auto& s = e.spells[k];
            if (!readPOD(is, s.spellId) ||
                !readPOD(is, s.moneyCostCopper) ||
                !readPOD(is, s.requiredSkillId) ||
                !readPOD(is, s.requiredSkillRank) ||
                !readPOD(is, s.requiredLevel)) {
                out.entries.clear(); return out;
            }
        }
        e.items.resize(itemCount);
        for (uint16_t k = 0; k < itemCount; ++k) {
            auto& it = e.items[k];
            if (!readPOD(is, it.itemId) ||
                !readPOD(is, it.stockCount) ||
                !readPOD(is, it.restockSec) ||
                !readPOD(is, it.extendedCost) ||
                !readPOD(is, it.moneyCostCopper)) {
                out.entries.clear(); return out;
            }
        }
    }
    return out;
}

bool WoweeTrainerLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeTrainer WoweeTrainerLoader::makeStarter(const std::string& catalogName) {
    WoweeTrainer c;
    c.name = catalogName;
    {
        // npcId 4001 matches WCRT.makeStarter / makeMerchants
        // (Bartleby innkeeper).
        WoweeTrainer::Entry e;
        e.npcId = 4001;
        e.kindMask = WoweeTrainer::Trainer | WoweeTrainer::Vendor;
        e.greeting = "Welcome to the inn, traveler. What can I do for you?";
        // Train First Aid (skillId 129 in WSKL.makeProfessions).
        e.spells.push_back({4001, 100, 129, 1, 1});  // teaches First Aid
        // Sell starter items (itemIds match WIT.makeStarter:
        // 2=Linen Vest, 3=Healing Potion). Use moneyCost=0 to
        // mean "use WIT.buyPrice".
        e.items.push_back({2, WoweeTrainer::kUnlimitedStock, 0, 0, 0});
        e.items.push_back({3, WoweeTrainer::kUnlimitedStock, 0, 0, 0});
        e.items.push_back({4, 1, 86400, 0, 0});  // 1 unique item / 24h
        c.entries.push_back(e);
    }
    return c;
}

WoweeTrainer WoweeTrainerLoader::makeMageTrainer(const std::string& catalogName) {
    WoweeTrainer c;
    c.name = catalogName;
    {
        // npcId 4003 = alchemist NPC repurposed as a mage
        // trainer for the demo. Spell IDs match WSPL.makeMage.
        WoweeTrainer::Entry e;
        e.npcId = 4003;
        e.kindMask = WoweeTrainer::Trainer;
        e.greeting = "Magic is a craft. Will you learn?";
        // Each spell costs scaling copper, requires reagent
        // skill (none here), and a minimum character level.
        e.spells.push_back({116,  100,    0, 0,  4});   // Frostbolt @ lvl 4
        e.spells.push_back({133,  100,    0, 0,  1});   // Fireball @ lvl 1
        e.spells.push_back({1459, 1000,   0, 0,  10});  // Arcane Int @ lvl 10
        e.spells.push_back({1953, 5000,   0, 0,  20});  // Blink @ lvl 20
        c.entries.push_back(e);
    }
    return c;
}

WoweeTrainer WoweeTrainerLoader::makeWeaponVendor(const std::string& catalogName) {
    WoweeTrainer c;
    c.name = catalogName;
    {
        // npcId 4002 = smith from WCRT.makeMerchants. Sells
        // weapons matching WIT.makeWeapons itemIds.
        WoweeTrainer::Entry e;
        e.npcId = 4002;
        e.kindMask = WoweeTrainer::Vendor;
        e.greeting = "Strong steel for sturdy folk. Take a look.";
        e.items.push_back({1001, WoweeTrainer::kUnlimitedStock, 0, 0, 0});  // Apprentice Sword
        e.items.push_back({1002, WoweeTrainer::kUnlimitedStock, 0, 0, 0});  // Journeyman Blade
        e.items.push_back({1003, 3, 3600, 0, 0});  // Steelthorn Edge: 3 in stock, refresh 1h
        e.items.push_back({1004, 1, 7200, 0, 0});  // Bloodforged: 1 in stock, refresh 2h
        e.items.push_back({1005, 0, 0, 0, 0});      // Doombringer: out of stock by default
        c.entries.push_back(e);
    }
    return c;
}

} // namespace pipeline
} // namespace wowee
