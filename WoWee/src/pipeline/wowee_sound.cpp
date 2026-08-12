#include "pipeline/wowee_sound.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'S', 'N', 'D'};
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
    if (n > (1u << 20)) return false;  // 1 MiB sanity cap
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
    if (base.size() < 5 || base.substr(base.size() - 5) != ".wsnd") {
        base += ".wsnd";
    }
    return base;
}

} // namespace

const WoweeSound::Entry* WoweeSound::findById(uint32_t soundId) const {
    for (const auto& e : entries) {
        if (e.soundId == soundId) return &e;
    }
    return nullptr;
}

const char* WoweeSound::kindName(uint8_t k) {
    switch (k) {
        case Sfx:     return "sfx";
        case Music:   return "music";
        case Ambient: return "ambient";
        case Ui:      return "ui";
        case Voice:   return "voice";
        case Spell:   return "spell";
        case Combat:  return "combat";
        default:      return "unknown";
    }
}

bool WoweeSoundLoader::save(const WoweeSound& cat,
                            const std::string& basePath) {
    std::ofstream os(normalizePath(basePath), std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    writeStr(os, cat.name);
    uint32_t entryCount = static_cast<uint32_t>(cat.entries.size());
    writePOD(os, entryCount);
    for (const auto& e : cat.entries) {
        writePOD(os, e.soundId);
        writePOD(os, e.kind);
        uint8_t pad[3] = {0, 0, 0};
        os.write(reinterpret_cast<const char*>(pad), 3);
        writePOD(os, e.flags);
        writePOD(os, e.volume);
        writePOD(os, e.minDistance);
        writePOD(os, e.maxDistance);
        writeStr(os, e.filePath);
        writeStr(os, e.label);
    }
    return os.good();
}

WoweeSound WoweeSoundLoader::load(const std::string& basePath) {
    WoweeSound out;
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
    if (entryCount > (1u << 20)) return out;  // 1M cap
    out.entries.resize(entryCount);
    for (auto& e : out.entries) {
        if (!readPOD(is, e.soundId)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.kind))    { out.entries.clear(); return out; }
        uint8_t pad[3];
        is.read(reinterpret_cast<char*>(pad), 3);
        if (is.gcount() != 3) { out.entries.clear(); return out; }
        if (!readPOD(is, e.flags))       { out.entries.clear(); return out; }
        if (!readPOD(is, e.volume))      { out.entries.clear(); return out; }
        if (!readPOD(is, e.minDistance)) { out.entries.clear(); return out; }
        if (!readPOD(is, e.maxDistance)) { out.entries.clear(); return out; }
        if (!readStr(is, e.filePath))    { out.entries.clear(); return out; }
        if (!readStr(is, e.label))       { out.entries.clear(); return out; }
    }
    return out;
}

bool WoweeSoundLoader::exists(const std::string& basePath) {
    std::ifstream is(normalizePath(basePath), std::ios::binary);
    return is.good();
}

WoweeSound WoweeSoundLoader::makeStarter(const std::string& catalogName) {
    WoweeSound c;
    c.name = catalogName;
    c.entries.push_back({1, WoweeSound::Sfx, 0,                    1.0f,  5.0f,  30.0f, "Sound/Sfx/footstep_grass.ogg",  "Footstep (grass)"});
    c.entries.push_back({2, WoweeSound::Music, WoweeSound::Stream, 0.7f,  0.0f,   0.0f, "Sound/Music/main_theme.ogg",    "Main theme"});
    c.entries.push_back({3, WoweeSound::Ambient,
                          WoweeSound::Loop | WoweeSound::Is3D,      0.5f, 10.0f,  60.0f, "Sound/Ambient/forest_loop.ogg", "Forest ambience"});
    c.entries.push_back({4, WoweeSound::Ui, 0,                     1.0f,  0.0f,   0.0f, "Sound/Ui/button_click.ogg",     "UI button click"});
    c.entries.push_back({5, WoweeSound::Voice, WoweeSound::Is3D,   1.0f,  3.0f,  20.0f, "Sound/Voice/vendor_greet.ogg",  "Vendor greeting"});
    c.entries.push_back({6, WoweeSound::Spell, WoweeSound::Is3D,   0.9f,  4.0f,  40.0f, "Sound/Spell/fireball_cast.ogg", "Fireball cast"});
    c.entries.push_back({7, WoweeSound::Combat, WoweeSound::Is3D,  0.9f,  3.0f,  25.0f, "Sound/Combat/sword_clang.ogg",  "Sword clang"});
    return c;
}

WoweeSound WoweeSoundLoader::makeAmbient(const std::string& catalogName) {
    WoweeSound c;
    c.name = catalogName;
    c.entries.push_back({100, WoweeSound::Ambient,
                          WoweeSound::Loop | WoweeSound::Is3D, 0.4f, 12.0f, 80.0f,
                          "Sound/Ambient/birds_loop.ogg",   "Birds (loop)"});
    c.entries.push_back({101, WoweeSound::Ambient,
                          WoweeSound::Loop | WoweeSound::Is3D, 0.3f, 20.0f, 120.0f,
                          "Sound/Ambient/wind_loop.ogg",    "Wind (loop)"});
    c.entries.push_back({102, WoweeSound::Sfx, WoweeSound::Is3D, 0.8f, 4.0f, 25.0f,
                          "Sound/Sfx/footstep_grass.ogg",   "Footstep grass"});
    c.entries.push_back({103, WoweeSound::Sfx, WoweeSound::Is3D, 0.8f, 4.0f, 25.0f,
                          "Sound/Sfx/footstep_dirt.ogg",    "Footstep dirt"});
    c.entries.push_back({104, WoweeSound::Sfx, WoweeSound::Is3D, 0.8f, 4.0f, 25.0f,
                          "Sound/Sfx/footstep_leaves.ogg",  "Footstep leaves"});
    return c;
}

WoweeSound WoweeSoundLoader::makeTavern(const std::string& catalogName) {
    WoweeSound c;
    c.name = catalogName;
    c.entries.push_back({200, WoweeSound::Ambient,
                          WoweeSound::Loop | WoweeSound::Is3D, 0.5f, 6.0f, 40.0f,
                          "Sound/Ambient/fire_crackle.ogg", "Fire crackle (loop)"});
    c.entries.push_back({201, WoweeSound::Ambient,
                          WoweeSound::Loop, 0.4f, 0.0f, 0.0f,
                          "Sound/Ambient/crowd_murmur.ogg", "Crowd murmur (loop)"});
    c.entries.push_back({202, WoweeSound::Sfx, WoweeSound::Is3D, 0.9f, 3.0f, 15.0f,
                          "Sound/Sfx/drink_clink.ogg",      "Drink clink"});
    c.entries.push_back({203, WoweeSound::Sfx, WoweeSound::Is3D, 0.7f, 4.0f, 20.0f,
                          "Sound/Sfx/door_creak.ogg",       "Door creak"});
    c.entries.push_back({204, WoweeSound::Music, WoweeSound::Stream, 0.6f, 0.0f, 0.0f,
                          "Sound/Music/tavern_lute.ogg",    "Tavern lute"});
    return c;
}

} // namespace pipeline
} // namespace wowee
