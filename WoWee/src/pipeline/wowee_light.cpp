#include "pipeline/wowee_light.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace wowee {
namespace pipeline {

namespace {

constexpr char kMagic[4] = {'W', 'O', 'L', 'A'};
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

} // namespace

bool WoweeLightLoader::save(const WoweeLight& light,
                            const std::string& basePath) {
    std::string path = basePath;
    if (path.size() < 4 || path.substr(path.size() - 4) != ".wol") {
        path += ".wol";
    }
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;
    os.write(kMagic, 4);
    writePOD(os, kVersion);
    uint32_t nameLen = static_cast<uint32_t>(light.name.size());
    writePOD(os, nameLen);
    if (nameLen > 0) os.write(light.name.data(), nameLen);
    uint32_t kfCount = static_cast<uint32_t>(light.keyframes.size());
    writePOD(os, kfCount);
    for (const auto& kf : light.keyframes) {
        writePOD(os, kf.timeOfDayMin);
        writePOD(os, kf.ambientColor);
        writePOD(os, kf.directionalColor);
        writePOD(os, kf.directionalDir);
        writePOD(os, kf.fogColor);
        writePOD(os, kf.fogStart);
        writePOD(os, kf.fogEnd);
    }
    return os.good();
}

WoweeLight WoweeLightLoader::load(const std::string& basePath) {
    WoweeLight out;
    std::string path = basePath;
    if (path.size() < 4 || path.substr(path.size() - 4) != ".wol") {
        path += ".wol";
    }
    std::ifstream is(path, std::ios::binary);
    if (!is) return out;
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) return out;
    uint32_t version = 0;
    if (!readPOD(is, version)) return out;
    if (version != kVersion) return out;
    uint32_t nameLen = 0;
    if (!readPOD(is, nameLen)) return out;
    if (nameLen > 0) {
        out.name.resize(nameLen);
        is.read(out.name.data(), nameLen);
        if (is.gcount() != static_cast<std::streamsize>(nameLen)) {
            out.name.clear();
            return out;
        }
    }
    uint32_t kfCount = 0;
    if (!readPOD(is, kfCount)) return out;
    out.keyframes.resize(kfCount);
    for (auto& kf : out.keyframes) {
        if (!readPOD(is, kf.timeOfDayMin) ||
            !readPOD(is, kf.ambientColor) ||
            !readPOD(is, kf.directionalColor) ||
            !readPOD(is, kf.directionalDir) ||
            !readPOD(is, kf.fogColor) ||
            !readPOD(is, kf.fogStart) ||
            !readPOD(is, kf.fogEnd)) {
            out.keyframes.clear();
            return out;
        }
    }
    return out;
}

bool WoweeLightLoader::exists(const std::string& basePath) {
    std::string path = basePath;
    if (path.size() < 4 || path.substr(path.size() - 4) != ".wol") {
        path += ".wol";
    }
    std::ifstream is(path, std::ios::binary);
    return is.good();
}

WoweeLight::Keyframe WoweeLightLoader::sampleAtTime(
        const WoweeLight& light, uint32_t timeMin) {
    if (light.keyframes.empty()) return WoweeLight::Keyframe{};
    if (light.keyframes.size() == 1) return light.keyframes.front();
    timeMin = timeMin % 1440;
    // Find the keyframe pair (a, b) such that a.t <= timeMin < b.t.
    // Wrap: if timeMin is before the first keyframe or at/after the
    // last, blend between (last, first + 1440).
    const auto& kfs = light.keyframes;
    auto it = std::upper_bound(kfs.begin(), kfs.end(), timeMin,
        [](uint32_t t, const WoweeLight::Keyframe& kf) {
            return t < kf.timeOfDayMin;
        });
    const WoweeLight::Keyframe* a;
    const WoweeLight::Keyframe* b;
    uint32_t aT, bT;
    if (it == kfs.begin() || it == kfs.end()) {
        // Wrap-around: between last and first (+ 1440).
        a = &kfs.back();
        b = &kfs.front();
        aT = a->timeOfDayMin;
        bT = b->timeOfDayMin + 1440;
        if (it == kfs.begin()) {
            // timeMin is BEFORE the first keyframe, so we're in
            // the wrap window. Shift query into [aT, bT) by adding
            // 1440 to it.
            timeMin += 1440;
        }
    } else {
        b = &(*it);
        a = &(*(it - 1));
        aT = a->timeOfDayMin;
        bT = b->timeOfDayMin;
    }
    float t = (bT == aT) ? 0.0f
                          : static_cast<float>(timeMin - aT) /
                            static_cast<float>(bT - aT);
    WoweeLight::Keyframe out;
    out.timeOfDayMin = timeMin % 1440;
    out.ambientColor     = a->ambientColor     + t * (b->ambientColor     - a->ambientColor);
    out.directionalColor = a->directionalColor + t * (b->directionalColor - a->directionalColor);
    out.directionalDir   = a->directionalDir   + t * (b->directionalDir   - a->directionalDir);
    out.fogColor         = a->fogColor         + t * (b->fogColor         - a->fogColor);
    out.fogStart         = a->fogStart         + t * (b->fogStart         - a->fogStart);
    out.fogEnd           = a->fogEnd           + t * (b->fogEnd           - a->fogEnd);
    return out;
}

WoweeLight WoweeLightLoader::makeCave(const std::string& zoneName) {
    WoweeLight out;
    out.name = zoneName;
    // Single dim keyframe (caves don't change with time-of-day).
    out.keyframes.push_back({
        720,                                    // noon (arbitrary)
        glm::vec3(0.05f, 0.05f, 0.07f),         // very dim cool ambient
        glm::vec3(0.10f, 0.10f, 0.14f),         // faint indirect bounce
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.04f, 0.05f, 0.07f),         // near-black fog
        15.0f, 80.0f                            // heavy short-range fog
    });
    return out;
}

WoweeLight WoweeLightLoader::makeDungeon(const std::string& zoneName) {
    WoweeLight out;
    out.name = zoneName;
    // Single moody warm-torchlit keyframe.
    out.keyframes.push_back({
        720,
        glm::vec3(0.18f, 0.14f, 0.10f),         // warm dim ambient
        glm::vec3(0.55f, 0.40f, 0.25f),         // amber torchlight tint
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.10f, 0.08f, 0.06f),         // dark warm fog
        25.0f, 200.0f                           // medium fog range
    });
    return out;
}

WoweeLight WoweeLightLoader::makeNight(const std::string& zoneName) {
    WoweeLight out;
    out.name = zoneName;
    // Single dark-night keyframe (e.g., always-night zones like
    // some druid graves or shadow-realm scenes).
    out.keyframes.push_back({
        0,
        glm::vec3(0.06f, 0.07f, 0.12f),         // cold dim blue ambient
        glm::vec3(0.18f, 0.20f, 0.32f),         // moonlight-tinted directional
        glm::vec3(0.30f, -0.94f, 0.0f),         // moon at low angle
        glm::vec3(0.05f, 0.06f, 0.10f),         // near-black blue fog
        80.0f, 500.0f                           // far fog (open night air)
    });
    return out;
}

WoweeLight WoweeLightLoader::makeDefaultDayNight(
        const std::string& zoneName) {
    WoweeLight out;
    out.name = zoneName;
    // Midnight: cold + dim, blue-tinted ambient, sun straight down
    // (it's behind the world).
    out.keyframes.push_back({
        0,
        glm::vec3(0.06f, 0.07f, 0.10f),
        glm::vec3(0.10f, 0.12f, 0.20f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.05f, 0.06f, 0.10f),
        40.0f, 200.0f
    });
    // Dawn (6:00): warm horizon glow, sun rising from -X.
    out.keyframes.push_back({
        360,
        glm::vec3(0.30f, 0.25f, 0.20f),
        glm::vec3(0.95f, 0.70f, 0.55f),
        glm::vec3(0.86f, -0.50f, 0.0f),
        glm::vec3(0.80f, 0.55f, 0.45f),
        100.0f, 600.0f
    });
    // Noon (12:00): bright + neutral, sun overhead.
    out.keyframes.push_back({
        720,
        glm::vec3(0.40f, 0.42f, 0.44f),
        glm::vec3(1.00f, 0.97f, 0.92f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.65f, 0.72f, 0.82f),
        120.0f, 800.0f
    });
    // Dusk (18:00): orange-red glow, sun setting toward +X.
    out.keyframes.push_back({
        1080,
        glm::vec3(0.32f, 0.22f, 0.18f),
        glm::vec3(0.95f, 0.55f, 0.30f),
        glm::vec3(-0.86f, -0.50f, 0.0f),
        glm::vec3(0.85f, 0.50f, 0.35f),
        100.0f, 500.0f
    });
    return out;
}

} // namespace pipeline
} // namespace wowee
