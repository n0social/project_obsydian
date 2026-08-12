#pragma once

#include <glm/glm.hpp>

namespace wowee::core {

struct SpawnPreset {
    const char* key;
    const char* label;
    const char* mapName;        // Map name for ADT paths (e.g., "Azeroth")
    glm::vec3 spawnCanonical;   // Canonical WoW coords: +X=North, +Y=West, +Z=Up
    float yawDeg;
    float pitchDeg;
    bool snapToGround;          // true=terrain/WMO floor search, false=preserve explicit Z
};

// Spawn positions in canonical WoW world coordinates (X=north, Y=west, Z=up).
// Tile is computed from position via: tileN = floor(32 - wowN / 533.33333)
inline const SpawnPreset SPAWN_PRESETS[] = {
    {"goldshire",  "Goldshire",      "Azeroth", glm::vec3(   62.0f, -9464.0f, 200.0f), 0.0f, -5.0f, true},
    {"stormwind",  "Stormwind Gate", "Azeroth", glm::vec3(  425.0f, -9176.0f, 120.0f), 35.0f, -8.0f, true},
    // Stormwind city center plaza on WMO floor (not terrain intersection).
    {"sw_plaza",   "Stormwind Plaza","Azeroth", glm::vec3(  620.0f, -8830.0f,  95.0f), 180.0f, -8.0f, false},
    {"ironforge",  "Ironforge",      "Azeroth", glm::vec3( -882.0f, -4981.0f, 510.0f), -20.0f, -8.0f, true},
    {"westfall",   "Westfall",       "Azeroth", glm::vec3( 1215.0f,-10440.0f,  80.0f), 10.0f, -8.0f, true},
};

inline constexpr int SPAWN_PRESET_COUNT = static_cast<int>(sizeof(SPAWN_PRESETS) / sizeof(SPAWN_PRESETS[0]));

} // namespace wowee::core
