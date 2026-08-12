#include "audio/footstep_manager.hpp"
#include "audio/audio_engine.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <string>

namespace wowee {
namespace audio {

namespace {

std::vector<std::string> buildClassicFootstepSet(const std::string& material) {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'L'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\mFootMediumLarge" + material + std::string(1, c) + ".wav");
    }
    return out;
}

std::vector<std::string> buildAltFootstepSet(const std::string& folder, const std::string& stem) {
    std::vector<std::string> out;
    for (int i = 1; i <= 8; ++i) {
        char index[4];
        std::snprintf(index, sizeof(index), "%02d", i);
        out.push_back("Sound\\Character\\Footsteps\\" + folder + "\\" + stem + "_" + index + ".wav");
    }
    return out;
}

std::vector<std::string> buildHorseFootstepSet(const std::string& material) {
    std::vector<std::string> out;
    for (int i = 1; i <= 5; ++i) {
        char index[3];
        std::snprintf(index, sizeof(index), "%02d", i);
        out.push_back("Sound\\Creature\\Horse\\mFootstepsHorse" + material + index + ".wav");
    }
    return out;
}

std::vector<std::string> buildHugeFootstepSet(const std::string& material) {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'E'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\mFootHuge" + material + std::string(1, c) + ".wav");
    }
    return out;
}

// Water footsteps do not follow the mFootMediumLarge<material> naming the solid
// surfaces use — there is no mFootMediumLargeWater in the data at all, so the
// water surface was loading an empty clip set and walking through the shallows
// was silent. The real files live under Footsteps\\WaterSplash.
std::vector<std::string> buildWaterFootstepSet() {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'E'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\WaterSplash\\FootStepsMediumWater" +
                      std::string(1, c) + ".wav");
    }
    return out;
}

std::vector<std::string> buildHugeWaterFootstepSet() {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'E'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\FootstepsHugeWater" + std::string(1, c) + ".wav");
    }
    return out;
}

} // namespace

FootstepManager::FootstepManager() : rng(std::random_device{}()) {}

FootstepManager::~FootstepManager() {
    shutdown();
}

bool FootstepManager::initialize(pipeline::AssetManager* assets) {
    assetManager = assets;
    sampleCount = 0;
    for (auto& surface : surfaces) {
        surface.clips.clear();
    }
    for (auto& surface : horseSurfaces) {
        surface.clips.clear();
    }
    for (auto& surface : hugeSurfaces) {
        surface.clips.clear();
    }

    if (!assetManager) {
        return false;
    }

    preloadSurface(surfaces, FootstepSurface::STONE, buildClassicFootstepSet("Stone"), "character");
    preloadSurface(surfaces, FootstepSurface::DIRT, buildClassicFootstepSet("Dirt"), "character");
    preloadSurface(surfaces, FootstepSurface::GRASS, buildClassicFootstepSet("Grass"), "character");
    preloadSurface(surfaces, FootstepSurface::WOOD, buildClassicFootstepSet("Wood"), "character");
    preloadSurface(surfaces, FootstepSurface::SNOW, buildClassicFootstepSet("Snow"), "character");
    preloadSurface(surfaces, FootstepSurface::WATER, buildWaterFootstepSet(), "character");

    // Alternate naming seen in some builds (especially metals).
    preloadSurface(surfaces, FootstepSurface::METAL,
                   buildAltFootstepSet("MediumLargeMetalFootsteps", "MediumLargeFootstepMetal"), "character");
    if (surfaces[static_cast<size_t>(FootstepSurface::METAL)].clips.empty()) {
        preloadSurface(surfaces, FootstepSurface::METAL, buildClassicFootstepSet("Metal"), "character");
    }

    preloadSurface(horseSurfaces, FootstepSurface::STONE, buildHorseFootstepSet("Stone"), "horse");
    preloadSurface(horseSurfaces, FootstepSurface::DIRT, buildHorseFootstepSet("Dirt"), "horse");
    preloadSurface(horseSurfaces, FootstepSurface::GRASS, buildHorseFootstepSet("Grass"), "horse");
    preloadSurface(horseSurfaces, FootstepSurface::WOOD, buildHorseFootstepSet("Wood"), "horse");
    preloadSurface(horseSurfaces, FootstepSurface::SNOW, buildHorseFootstepSet("Snow"), "horse");

    preloadSurface(hugeSurfaces, FootstepSurface::STONE, buildHugeFootstepSet("Stone"), "huge");
    preloadSurface(hugeSurfaces, FootstepSurface::DIRT, buildHugeFootstepSet("Dirt"), "huge");
    preloadSurface(hugeSurfaces, FootstepSurface::GRASS, buildHugeFootstepSet("Grass"), "huge");
    preloadSurface(hugeSurfaces, FootstepSurface::WOOD, buildHugeFootstepSet("Wood"), "huge");
    preloadSurface(hugeSurfaces, FootstepSurface::SNOW, buildHugeFootstepSet("Snow"), "huge");
    preloadSurface(hugeSurfaces, FootstepSurface::WATER, buildHugeWaterFootstepSet(), "huge");

    LOG_INFO("Footstep manager initialized (", sampleCount, " clips)");
    return sampleCount > 0;
}

void FootstepManager::shutdown() {
    for (auto& surface : surfaces) {
        surface.clips.clear();
    }
    for (auto& surface : horseSurfaces) {
        surface.clips.clear();
    }
    for (auto& surface : hugeSurfaces) {
        surface.clips.clear();
    }
    sampleCount = 0;
    assetManager = nullptr;
}

void FootstepManager::update(float) {
    // No longer needed - AudioEngine handles cleanup internally
}

void FootstepManager::playFootstep(FootstepSurface surface, bool sprinting) {
    if (!assetManager || sampleCount == 0 || !AudioEngine::instance().isInitialized()) {
        return;
    }
    playRandomStep(surface, FootstepBank::CHARACTER,
                   sprinting ? 0.09f : 0.14f,
                   sprinting ? 1.0f : 0.88f);
}

void FootstepManager::playMountFootstep(FootstepSurface surface, FootstepBank bank) {
    if (!assetManager || sampleCount == 0 || !AudioEngine::instance().isInitialized()) {
        return;
    }
    // Gallop hoofbeats land close together — allow a tighter interval than
    // on-foot steps, and soften padded paws (wolf/tiger/raptor) slightly.
    const float volumeMul = (bank == FootstepBank::CHARACTER) ? 0.85f : 1.0f;
    playRandomStep(surface, bank, 0.06f, volumeMul);
}

void FootstepManager::preloadSurface(SurfaceSamples* bank, FootstepSurface surface,
                                     const std::vector<std::string>& candidates,
                                     const char* bankName) {
    if (!assetManager) {
        return;
    }

    auto& list = bank[static_cast<size_t>(surface)].clips;
    for (const std::string& path : candidates) {
        if (!assetManager->fileExists(path)) {
            continue;
        }
        auto data = assetManager->readFile(path);
        if (data.empty()) {
            continue;
        }
        list.push_back({path, std::move(data)});
        sampleCount++;
    }

    if (!list.empty()) {
        LOG_INFO("Footsteps ", bankName, "/", surfaceName(surface), ": loaded ", list.size(), " clips");
    }
}

bool FootstepManager::playRandomStep(FootstepSurface surface, FootstepBank bank,
                                     float minInterval, float volumeMul) {
    auto now = std::chrono::steady_clock::now();
    if (lastPlayTime.time_since_epoch().count() != 0) {
        float elapsed = std::chrono::duration<float>(now - lastPlayTime).count();
        if (elapsed < minInterval) {
            return false;
        }
    }

    const auto surfaceIndex = static_cast<size_t>(surface);
    const std::vector<Sample>* list = nullptr;
    switch (bank) {
        case FootstepBank::HORSE: list = &horseSurfaces[surfaceIndex].clips; break;
        case FootstepBank::HEAVY: list = &hugeSurfaces[surfaceIndex].clips; break;
        default:                  list = &surfaces[surfaceIndex].clips; break;
    }

    if (list->empty() && bank != FootstepBank::CHARACTER) {
        // Mount archives do not author metal or water variants everywhere.
        // Hooves on metal are closest to the stone bank; water keeps the
        // normal splash bank.
        if (surface == FootstepSurface::WATER) {
            list = &surfaces[surfaceIndex].clips;
        } else if (bank == FootstepBank::HORSE) {
            list = &horseSurfaces[static_cast<size_t>(FootstepSurface::STONE)].clips;
        } else {
            list = &hugeSurfaces[static_cast<size_t>(FootstepSurface::STONE)].clips;
        }
    }
    if (list->empty()) {
        list = &surfaces[static_cast<size_t>(FootstepSurface::STONE)].clips;
        if (list->empty()) return false;
    }

    // Pick a random clip
    std::uniform_int_distribution<size_t> clipDist(0, list->size() - 1);
    const Sample& sample = (*list)[clipDist(rng)];

    // Subtle variation for less repetitive cadence
    std::uniform_real_distribution<float> pitchDist(0.97f, 1.05f);
    std::uniform_real_distribution<float> volumeDist(0.92f, 1.00f);
    float pitch = pitchDist(rng);
    float volume = volumeDist(rng) * volumeMul * volumeScale;
    if (volume > 1.0f) volume = 1.0f;
    if (volume < 0.1f) volume = 0.1f;

    // Play using AudioEngine (non-blocking, no process spawn!)
    bool success = AudioEngine::instance().playSound2D(sample.data, volume, pitch);

    if (success) {
        lastPlayTime = now;
        return true;
    }

    return false;
}

const char* FootstepManager::surfaceName(FootstepSurface surface) {
    switch (surface) {
        case FootstepSurface::STONE: return "stone";
        case FootstepSurface::DIRT: return "dirt";
        case FootstepSurface::GRASS: return "grass";
        case FootstepSurface::WOOD: return "wood";
        case FootstepSurface::METAL: return "metal";
        case FootstepSurface::WATER: return "water";
        case FootstepSurface::SNOW: return "snow";
        default: return "unknown";
    }
}

} // namespace audio
} // namespace wowee
