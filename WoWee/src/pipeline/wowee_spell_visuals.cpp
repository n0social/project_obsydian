#include "pipeline/wowee_spell_visuals.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'V', 'K'};
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wsvk") {
        base += ".wsvk";
    }
    return base;
}

} // namespace

const WoweeSpellVisualKit::Entry*
WoweeSpellVisualKit::findById(uint32_t visualKitId) const {
    for (const auto& e : entries)
        if (e.visualKitId == visualKitId) return &e;
    return nullptr;
}

bool WoweeSpellVisualKitLoader::save(const WoweeSpellVisualKit& cat,
                                     const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.visualKitId);
        writeStr(os, e.name);
        writeStr(os, e.description);
        writeStr(os, e.castEffectModelPath);
        writeStr(os, e.projectileModelPath);
        writeStr(os, e.impactEffectModelPath);
        writeStr(os, e.handEffectModelPath);
        writePOD(os, e.precastAnimId);
        writePOD(os, e.castAnimId);
        writePOD(os, e.impactAnimId);
        writePOD(os, e.castSoundId);
        writePOD(os, e.impactSoundId);
        writePOD(os, e.projectileSpeed);
        writePOD(os, e.projectileGravity);
        writePOD(os, e.castDurationMs);
        writePOD(os, e.impactRadius);
    }
    return os.good();
}

WoweeSpellVisualKit WoweeSpellVisualKitLoader::load(
    const std::string& basePath) {
    WoweeSpellVisualKit out;
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
        if (!readPOD(is, e.visualKitId)) {
            out.entries.clear(); return out;
        }
        if (!readStr(is, e.name) || !readStr(is, e.description) ||
            !readStr(is, e.castEffectModelPath) ||
            !readStr(is, e.projectileModelPath) ||
            !readStr(is, e.impactEffectModelPath) ||
            !readStr(is, e.handEffectModelPath)) {
            out.entries.clear(); return out;
        }
        if (!readPOD(is, e.precastAnimId) ||
            !readPOD(is, e.castAnimId) ||
            !readPOD(is, e.impactAnimId) ||
            !readPOD(is, e.castSoundId) ||
            !readPOD(is, e.impactSoundId) ||
            !readPOD(is, e.projectileSpeed) ||
            !readPOD(is, e.projectileGravity) ||
            !readPOD(is, e.castDurationMs) ||
            !readPOD(is, e.impactRadius)) {
            out.entries.clear(); return out;
        }
    }
    return out;
}

bool WoweeSpellVisualKitLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeSpellVisualKit WoweeSpellVisualKitLoader::makeStarter(
    const std::string& catalogName) {
    WoweeSpellVisualKit c;
    c.name = catalogName;
    {
        WoweeSpellVisualKit::Entry e;
        e.visualKitId = 1; e.name = "Frostbolt";
        e.description = "Mage frostbolt — slow icy projectile.";
        e.castEffectModelPath = "Spells/Cast/Frost/cast_frost.m2";
        e.projectileModelPath = "Spells/Missiles/frostbolt.m2";
        e.impactEffectModelPath = "Spells/Impact/Frost/impact_frost.m2";
        e.handEffectModelPath = "Spells/Hand/frost_hand_glow.m2";
        e.castAnimId = 54;        // ChannelCast from WANI combat
        e.castSoundId = 100;      // WSND cross-ref
        e.impactSoundId = 101;
        e.projectileSpeed = 25.0f;
        e.castDurationMs = 2500;
        e.impactRadius = 0.0f;    // single-target
        c.entries.push_back(e);
    }
    {
        WoweeSpellVisualKit::Entry e;
        e.visualKitId = 2; e.name = "Fireball";
        e.description = "Mage fireball — fast fiery projectile + AoE.";
        e.castEffectModelPath = "Spells/Cast/Fire/cast_fire.m2";
        e.projectileModelPath = "Spells/Missiles/fireball.m2";
        e.impactEffectModelPath = "Spells/Impact/Fire/impact_fire.m2";
        e.handEffectModelPath = "Spells/Hand/fire_hand_glow.m2";
        e.castAnimId = 54;
        e.castSoundId = 102;
        e.impactSoundId = 103;
        e.projectileSpeed = 35.0f;
        e.castDurationMs = 3500;
        e.impactRadius = 5.0f;    // splash
        c.entries.push_back(e);
    }
    {
        WoweeSpellVisualKit::Entry e;
        e.visualKitId = 3; e.name = "HealingTouch";
        e.description = "Druid healing — golden glow on caster + target.";
        e.castEffectModelPath = "Spells/Cast/Holy/cast_holy.m2";
        e.impactEffectModelPath = "Spells/Impact/Holy/heal_glow.m2";
        e.handEffectModelPath = "Spells/Hand/holy_hand_glow.m2";
        e.castAnimId = 54;
        e.castSoundId = 104;
        e.impactSoundId = 105;
        e.projectileSpeed = 0.0f;  // instant — no projectile
        e.castDurationMs = 3000;
        c.entries.push_back(e);
    }
    return c;
}

WoweeSpellVisualKit WoweeSpellVisualKitLoader::makeCombat(
    const std::string& catalogName) {
    WoweeSpellVisualKit c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name,
                    uint32_t castAnim, uint32_t impactAnim,
                    float projSpeed, float gravity,
                    uint32_t durMs, float radius,
                    const char* castModel, const char* projModel,
                    const char* impactModel, const char* desc) {
        WoweeSpellVisualKit::Entry e;
        e.visualKitId = id; e.name = name; e.description = desc;
        e.castEffectModelPath = castModel;
        e.projectileModelPath = projModel;
        e.impactEffectModelPath = impactModel;
        e.castAnimId = castAnim;
        e.impactAnimId = impactAnim;
        e.projectileSpeed = projSpeed;
        e.projectileGravity = gravity;
        e.castDurationMs = durMs;
        e.impactRadius = radius;
        c.entries.push_back(e);
    };
    add(100, "SwordImpact",  17, 0, 0.0f, 0.0f,    0, 0.0f,
        "", "", "Spells/Impact/Physical/sword_hit.m2",
        "Sword strike sparks on impact — no cast effect.");
    add(101, "ArrowShot",    40, 0, 60.0f, 0.05f, 800, 0.0f,
        "Spells/Cast/Physical/draw_bow.m2",
        "Spells/Missiles/arrow.m2",
        "Spells/Impact/Physical/arrow_hit.m2",
        "Arrow with slight gravity drop.");
    add(102, "GroundPound",  18, 0, 0.0f, 0.0f, 1200, 8.0f,
        "Spells/Cast/Physical/heave.m2", "",
        "Spells/Impact/Physical/ground_shockwave.m2",
        "AoE ground pound — large impact radius.");
    add(103, "ParryFlash",   53, 0, 0.0f, 0.0f,  500, 0.0f,
        "Spells/Cast/Physical/parry_flash.m2", "", "",
        "Quick parry sparks — instant cast.");
    add(104, "DeflectShield", 0, 0, 0.0f, 0.0f,  500, 0.0f,
        "Spells/Cast/Physical/shield_deflect.m2", "", "",
        "Shield reflection visual — no projectile.");
    return c;
}

WoweeSpellVisualKit WoweeSpellVisualKitLoader::makeUtility(
    const std::string& catalogName) {
    WoweeSpellVisualKit c;
    c.name = catalogName;
    auto add = [&](uint32_t id, const char* name, uint32_t durMs,
                    const char* castModel, const char* impactModel,
                    const char* desc) {
        WoweeSpellVisualKit::Entry e;
        e.visualKitId = id; e.name = name; e.description = desc;
        e.castEffectModelPath = castModel;
        e.impactEffectModelPath = impactModel;
        e.castAnimId = 54;       // ChannelCast
        e.castDurationMs = durMs;
        c.entries.push_back(e);
    };
    add(200, "PortalCast",       10000,
        "Spells/Cast/Arcane/portal_cast.m2",
        "Spells/Impact/Arcane/portal_open.m2",
        "Mage portal — long channel + persistent doorway.");
    add(201, "HearthstoneReturn", 10000,
        "Spells/Cast/Arcane/hearth_glow.m2",
        "Spells/Impact/Arcane/hearth_arrive.m2",
        "Hearthstone teleport — channel + arrival flash.");
    add(202, "MountSummon",        1500,
        "Spells/Cast/Nature/mount_summon.m2", "",
        "Quick mount spawn animation.");
    add(203, "Resurrect",          5000,
        "Spells/Cast/Holy/resurrect_cast.m2",
        "Spells/Impact/Holy/resurrect_target.m2",
        "Resurrect — golden beam + corpse glow.");
    return c;
}

} // namespace pipeline
} // namespace wowee
