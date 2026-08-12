# Sky System & Azeroth Astronomy

## Overview

The sky rendering system in wowee follows World of Warcraft's WotLK (3.3.5a) architecture, where skyboxes are **authoritative** and procedural elements serve as fallbacks only. This document explains the lore-accurate celestial system, implementation details, and critical anti-patterns to avoid.

---

## Architecture

### Component Hierarchy

```
SkySystem (coordinator)
├── Skybox (M2 model, AUTHORITATIVE - includes baked stars)
├── StarField (procedural, DEBUG/FALLBACK ONLY)
├── Celestial (sun + White Lady + Blue Child)
├── Clouds (atmospheric layer)
└── LensFlare (sun glow effect)
```

### Rendering Pipeline

```
LightingManager (DBC-driven)
  ↓ Light.dbc + LightParams.dbc + time-of-day bands
  ↓ produces: directionalDir, diffuseColor, skyColors, cloudDensity, fogDensity
  ↓
SkyParams (interface struct)
  ↓ adds: gameTime, skyboxModelId, skyboxHasStars
  ↓
SkySystem::render(camera, params)
  ├─→ Skybox first (far plane, camera-locked)
  ├─→ StarField (ONLY if debugMode OR skybox missing)
  ├─→ Celestial (sun + 2 moons, uses directionalDir + gameTime)
  ├─→ Clouds (atmospheric layer)
  └─→ LensFlare (screen-space sun glow)
```

---

## Celestial Bodies (Lore)

### The Two Moons of Azeroth

Azeroth has **two moons** visible in the night sky, both significant to the world's lore:

#### **White Lady** (Primary Moon)
- **Appearance**: Larger, brighter, pale white color (RGB: 0.8, 0.85, 1.0)
- **Size**: 40-unit diameter billboard
- **Intensity**: Full brightness (1.0)
- **Lore**: Tied to Elune, the Night Elf moon goddess
- **Cycle**: 30 game days per phase cycle (~12 real-world hours)

#### **Blue Child** (Secondary Moon)
- **Appearance**: Smaller, dimmer, pale blue color (RGB: 0.7, 0.8, 1.0)
- **Size**: 30-unit diameter billboard
- **Intensity**: 70% of White Lady's brightness
- **Position**: Offset to the right and slightly lower (+80 X, -40 Z)
- **Cycle**: 27 game days per phase cycle (~10.8 real-world hours, slightly faster)

#### **Visibility**
- **Night hours**: 19:00 - 5:00 (both moons visible)
- **Fade transitions**:
  - Fade in: 19:00 - 21:00 (dusk)
  - Full intensity: 21:00 - 3:00 (night)
  - Fade out: 3:00 - 5:00 (dawn)
- **Day hours**: 5:00 - 19:00 (moons not rendered)

### The Sun

- **Positioning**: Driven by `LightingManager::directionalDir`
  - Placement: `sunPosition = -directionalDir * 800` (light comes FROM sun)
  - Fallback: Time-based arc if no lighting manager (sunrise 6:00, peak 12:00, sunset 18:00)
- **Color**: Uses `LightingManager::diffuseColor` (DBC-driven, changes with time-of-day)
  - Sunrise/sunset: Orange/red tones
  - Midday: Bright yellow-white
- **Size**: 50-unit diameter billboard
- **Visibility**: 5:00 - 19:00 with intensity fade at transitions

---

## Deterministic Moon Phases

### Server Time-Driven (NOT deltaTime)

Moon phases are computed from **server game time**, ensuring:
- ✅ **Deterministic**: Same game time always produces same moon phases
- ✅ **Persistent**: Phases consistent across sessions and server restarts
- ✅ **Lore-feeling**: Realistic cycles tied to game world time, not arbitrary timers

### Calculation Formula

```cpp
float computePhaseFromGameTime(float gameTime, float cycleDays) {
    constexpr float SECONDS_PER_GAME_DAY = 1440.0f;  // 1 game day = 24 real minutes
    float gameDays = gameTime / SECONDS_PER_GAME_DAY;
    float phase = fmod(gameDays / cycleDays, 1.0f);
    return (phase < 0.0f) ? phase + 1.0f : phase;  // Ensure positive
}

// Applied per moon
whiteLadyPhase = computePhaseFromGameTime(gameTime, 30.0f);  // 30 game days
blueChildPhase = computePhaseFromGameTime(gameTime, 27.0f);  // 27 game days
```

### Phase Representation

- **Value range**: 0.0 - 1.0
  - `0.0` = New moon (dark)
  - `0.25` = First quarter (right half lit)
  - `0.5` = Full moon (fully lit)
  - `0.75` = Last quarter (left half lit)
  - `1.0` = New moon (wraps to 0.0)
- **Shader-driven**: Phase uniform passed to fragment shader for crescent/gibbous rendering

### Fallback Mode (Development)

If `gameTime < 0.0` (server time unavailable):
- Uses deltaTime accumulator: `moonPhaseTimer += deltaTime`
- Short cycle durations (4 minutes / 3.5 minutes) for quick testing
- **NOT used in production**: Should always use server time

---

## Sky Dome Rendering

### Camera-Locked Behavior (WoW Standard)

```cpp
// Vertex shader transformation
mat4 viewNoTranslation = mat4(mat3(view));  // Strip translation, keep rotation
gl_Position = projection * viewNoTranslation * vec4(aPos, 1.0);
gl_Position = gl_Position.xyww;  // Force far plane depth
```

**Why this works:**
- ✅ **Translation ignored**: Sky centered on camera (doesn't "swim" when moving)
- ✅ **Rotation applied**: Sky follows camera look direction (feels "attached to view")
- ✅ **Far plane depth**: Always renders behind world geometry
- ✅ **Celestial sphere illusion**: Stars/sky appear infinitely distant

### Time-Based Sky Drift (Optional)

Subtle rotation for atmospheric effect:

```cpp
float skyYawRotation = gameTime * skyRotationRate;
skyDomeMatrix = rotate(skyDomeMatrix, skyYawRotation, vec3(0, 0, 1));  // Yaw only
```

**Per-zone rotation rates:**
- Azeroth continents: `0.00001` rad/sec (very slow, barely noticeable)
- Outland: `0.00005` rad/sec (faster, "weird" alien sky feel)
- Northrend: `0.00002` rad/sec (subtle drift, aurora-like)
- Static zones: `0.0` (no rotation)

**Implementation status:** Not yet active (waiting for M2 skybox loading)

---

## Critical Anti-Patterns

### ❌ DO NOT: Latitude-Based Star Rotation

**Why it's wrong:**
- Azeroth is **not modeled as a spherical planet** with latitude/longitude in WoW client
- No coherent coordinate system for Earth-style celestial mechanics
- Stars are part of **authored skybox M2 models**, not dynamically positioned
- Breaks zone identity (Duskwood's gloomy sky shouldn't behave like Barrens)

**What happens if you do it anyway:**
- Stars won't match Blizzard's authored skyboxes when M2 models load
- Lore/continuity breaks (geographically close zones with different star rotation)
- "Swimming" stars during movement
- Undermines the "WoW feel"

**Correct approach:**
```cpp
// ✅ Per-zone artistic constants (NOT geography)
struct SkyProfile {
    float celestialTilt;      // Artistic pitch/roll (Outland = 15°, Azeroth = 0°)
    float skyYawOffset;       // Alignment offset for authored skybox
    float skyRotationRate;    // Time-based drift (0 = static)
};
```

### ❌ DO NOT: Always Render Procedural Stars

**Why it's wrong:**
- Skyboxes contain **baked stars** as part of zone mood/identity
- Procedural stars over skybox stars = double stars, visual clash
- Different zones have dramatically different skies (Outland purple nebulae, Northrend auroras)

**Correct gating logic:**
```cpp
bool renderProceduralStars = false;
if (debugSkyMode) {
    renderProceduralStars = true;  // Debug: force for testing fog/cloud attenuation
} else if (proceduralStarsEnabled) {
    renderProceduralStars = !params.skyboxHasStars;  // Fallback ONLY if skybox missing
}
```

**skyboxHasStars flag:**
- Set to `true` when M2 skybox loaded and has star layer (query materials/textures)
- Set to `false` for gradient skybox (placeholder, no baked stars)
- Prevents procedural stars from "leaking" once real skyboxes load

### ❌ DO NOT: Universal Dual Moon Setup

**Why it's wrong:**
- Not all maps/continents have same celestial bodies
- Azeroth: White Lady + Blue Child (two moons)
- Outland: Different sky (alien world, broken planet)
- Forcing two moons everywhere breaks lore

**Correct approach:**
```cpp
struct SkyProfile {
    bool dualMoons;  // Azeroth = true, Outland = false
    // ... other per-map settings
};

// In Celestial::render()
if (dualMoonMode_ && mapUsesAzerothSky) {
    renderBlueChild(camera, timeOfDay);
}
```

---

## Integration Points

### SkyParams Struct (Interface)

```cpp
struct SkyParams {
    // Sun/moon positioning
    glm::vec3 directionalDir;   // From LightingManager (sun direction)
    glm::vec3 sunColor;          // From LightingManager (DBC diffuse color)

    // Sky colors (for skybox tinting/blending, future)
    glm::vec3 skyTopColor;
    glm::vec3 skyMiddleColor;
    glm::vec3 skyBand1Color;
    glm::vec3 skyBand2Color;

    // Atmospheric effects (star/moon occlusion)
    float cloudDensity;          // 0-1, from LightingManager
    float fogDensity;            // 0-1, from LightingManager
    float horizonGlow;           // 0-1, atmospheric scattering

    // Time
    float timeOfDay;             // 0-24 hours (for sun/moon visibility)
    float gameTime;              // Server time in seconds (for moon phases)

    // Skybox control (future: LightSkybox.dbc)
    uint32_t skyboxModelId;      // Which M2 skybox to load
    bool skyboxHasStars;         // Does skybox include baked stars?
};
```

### Star Occlusion by Weather

Clouds and fog affect star visibility:

```cpp
// In StarField::render()
float intensity = getStarIntensity(timeOfDay);  // Time-based (night = 1.0, day = 0.0)
intensity *= (1.0f - glm::clamp(cloudDensity * 0.7f, 0.0f, 1.0f));  // Heavy clouds hide stars
intensity *= (1.0f - glm::clamp(fogDensity * 0.3f, 0.0f, 1.0f));    // Fog dims stars

if (intensity <= 0.01f) {
    return;  // Don't render invisible stars
}
```

**Result:** Cloudy/foggy nights have fewer visible stars (realistic behavior)

---

## Future: M2 Skybox System

### LightSkybox.dbc Integration

**DBC Chain:**
```
Light.dbc (spatial volumes)
  ↓ lightParamsId (per weather condition)
LightParams.dbc (profile mapping)
  ↓ skyboxId
LightSkybox.dbc (model paths)
  ↓ M2 model name
Environments\Stars\*.m2 (actual sky dome models)
```

**Skybox Loading Flow:**
1. Query `lightParamsId` from active light volume(s)
2. Look up `skyboxId` in LightParams.dbc
3. Load M2 model path from LightSkybox.dbc
4. Load/cache M2 skybox model
5. Query model materials → set `skyboxHasStars = true` if star textures found
6. Render skybox, disable procedural stars

### Skybox Transition Blending

**Problem:** Hard swaps between skyboxes at zone boundaries look bad

**Solution:** Blend skyboxes using same volume weighting as lighting:

```cpp
// In SkySystem::render() — Vulkan path
// Bind a sky pipeline whose VkPipelineColorBlendAttachmentState is
// configured for additive blending (srcColor=SRC_ALPHA, dstColor=ONE,
// blendOp=ADD), then render each skybox in weight order:
if (activeVolumes.size() >= 2) {
    // Primary skybox (alpha = volumes[0].weight)
    skybox1->render(camera, volumes[0].weight);
    // Secondary skybox blends additively on top
    skybox2->render(camera, volumes[1].weight);
}
```

**Result:** Smooth crossfade between zone skies, no popping

### SkyProfile Configuration

**Per-map/continent settings:**

```cpp
std::map<uint32_t, SkyProfile> skyProfiles = {
    // Azeroth (Eastern Kingdoms)
    {0, {
        .skyboxModelId = 123,
        .celestialTilt = 0.0f,           // No tilt, standard orientation
        .skyYawOffset = 0.0f,
        .skyRotationRate = 0.00001f,     // Very slow drift
        .dualMoons = true                // White Lady + Blue Child
    }},

    // Kalimdor
    {1, {
        .skyboxModelId = 124,
        .celestialTilt = 0.0f,
        .skyYawOffset = 0.0f,
        .skyRotationRate = 0.00001f,
        .dualMoons = true
    }},

    // Outland (Burning Crusade)
    {530, {
        .skyboxModelId = 456,
        .celestialTilt = 15.0f,          // Tilted, alien feel
        .skyYawOffset = 45.0f,           // Rotated alignment
        .skyRotationRate = 0.00005f,     // Faster, "weird" drift
        .dualMoons = false               // Different celestial setup
    }},

    // Northrend (Wrath of the Lich King)
    {571, {
        .skyboxModelId = 789,
        .celestialTilt = 0.0f,
        .skyYawOffset = 0.0f,
        .skyRotationRate = 0.00002f,     // Subtle aurora-like drift
        .dualMoons = true
    }}
};
```

---

## Implementation Checklist

### ✅ Completed
- [x] SkySystem coordinator class
- [x] Skybox camera-locked rendering (translation ignored)
- [x] Procedural stars gated by `skyboxHasStars` flag
- [x] Two moons (White Lady + Blue Child) with independent phases
- [x] Deterministic moon phases from server `gameTime`
- [x] Sun positioning from lighting `directionalDir`
- [x] Star occlusion by cloud/fog density
- [x] SkyParams interface for lighting integration

### 🚧 Future Enhancements
- [ ] Load M2 skybox models (parse LightSkybox.dbc)
- [ ] Query M2 materials to detect baked stars
- [ ] Skybox transition blending (weighted crossfade)
- [ ] SkyProfile per map/continent
- [ ] Time-based sky rotation (optional drift)
- [ ] Moon position from shared sky arc system (not fixed offsets)
- [ ] Support for zone-specific celestial setups (Outland, etc.)

---

## Code References

**Key Files:**
- `include/rendering/sky_system.hpp` - Coordinator, SkyParams struct
- `src/rendering/sky_system.cpp` - Render pipeline, star gating logic
- `include/rendering/celestial.hpp` - Sun + dual moon system
- `src/rendering/celestial.cpp` - Moon phase calculations, rendering
- `include/rendering/starfield.hpp` - Procedural star fallback
- `src/rendering/starfield.cpp` - Star intensity + occlusion
- `include/rendering/skybox.hpp` - Camera-locked sky dome
- `src/rendering/skybox.cpp` - Sky dome vertex shader

**Integration Points:**
- `src/rendering/renderer.cpp` - Populates SkyParams from LightingManager
- `include/rendering/lighting_manager.hpp` - Provides directionalDir, colors, fog/cloud

---

## References

- **WoW 3.3.5a Client**: Environments\Stars\*.m2 (skybox models)
- **DBC Files**: Light.dbc, LightParams.dbc, LightSkybox.dbc, LightIntBand.dbc, LightFloatBand.dbc
- **WoWDev Wiki**: https://wowdev.wiki/Light.dbc (lighting system documentation)
- **Lore Sources**:
  - White Lady / Blue Child: https://wowpedia.fandom.com/wiki/Moon
  - Elune connection: https://wowpedia.fandom.com/wiki/Elune
