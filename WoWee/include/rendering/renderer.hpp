#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <vector>
#include <future>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "rendering/vk_frame_data.hpp"
#include "rendering/vk_utils.hpp"
#include "rendering/sky_system.hpp"
#include "pipeline/custom_zone_discovery.hpp"

namespace wowee {
namespace core { class Window; }
namespace rendering { class VkContext; }
namespace game { class World; class ZoneManager; class GameHandler; }
namespace audio { class AudioCoordinator; }
namespace pipeline { class AssetManager; }

namespace rendering {

class Camera;
class CameraController;
class TerrainRenderer;
class TerrainManager;
class PerformanceHUD;
class WaterRenderer;
class Skybox;
class Celestial;
class StarField;
class Clouds;
class LensFlare;
class Weather;
class Lightning;
class LightingManager;
class SwimEffects;
class MountDust;
class LevelUpEffect;
class ChargeEffect;
class CharacterRenderer;
class WMORenderer;
class M2Renderer;
class Minimap;
namespace world_map { class WorldMapFacade; }
using WorldMap = world_map::WorldMapFacade;
class QuestMarkerRenderer;
class FootprintRenderer;
class CharacterPreview;
class AmdFsr3Runtime;
class SpellVisualSystem;
class PostProcessPipeline;
class AnimationController;
class LevelUpEffect;
class ChargeEffect;
class SwimEffects;
class RenderGraph;
class OverlaySystem;
class HiZSystem;

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool initialize(core::Window* window);
    void shutdown();

    void beginFrame();
    void endFrame();

    void renderWorld(game::World* world, game::GameHandler* gameHandler = nullptr);

    /**
     * Update renderer (camera, etc.)
     */
    void update(float deltaTime);

    /**
     * Load test terrain for debugging
     * @param assetManager Asset manager to load terrain data
     * @param adtPath Path to ADT file (e.g., "World\\Maps\\Azeroth\\Azeroth_32_49.adt")
     */
    bool loadTestTerrain(pipeline::AssetManager* assetManager, const std::string& adtPath);

    /**
     * Initialize all sub-renderers (WMO, M2, Character, terrain, water, minimap, etc.)
     * without loading any ADT tile.  Used by WMO-only maps (dungeons/raids/BGs).
     */
    bool initializeRenderers(pipeline::AssetManager* assetManager, const std::string& mapName);

    /**
     * Enable/disable terrain rendering
     */
    void setTerrainEnabled(bool enabled) { terrainEnabled = enabled; }

    /**
     * Enable/disable wireframe mode
     */
    void setWireframeMode(bool enabled);

    /**
     * Load terrain tiles around position
     * @param mapName Map name (e.g., "Azeroth", "Kalimdor")
     * @param centerX Center tile X coordinate
     * @param centerY Center tile Y coordinate
     * @param radius Load radius in tiles
     */
    bool loadTerrainArea(const std::string& mapName, int centerX, int centerY, int radius = 1);

    /**
     * Enable/disable terrain streaming
     */
    void setTerrainStreaming(bool enabled);

    /**
     * Render performance HUD
     */
    void renderHUD();

    Camera* getCamera() { return camera.get(); }
    CameraController* getCameraController() { return cameraController.get(); }
    TerrainRenderer* getTerrainRenderer() const { return terrainRenderer.get(); }
    TerrainManager* getTerrainManager() const { return terrainManager.get(); }
    PerformanceHUD* getPerformanceHUD() { return performanceHUD.get(); }
    WaterRenderer* getWaterRenderer() const { return waterRenderer.get(); }
    Skybox* getSkybox() const { return skySystem ? skySystem->getSkybox() : nullptr; }
    Celestial* getCelestial() const { return skySystem ? skySystem->getCelestial() : nullptr; }
    StarField* getStarField() const { return skySystem ? skySystem->getStarField() : nullptr; }
    Clouds* getClouds() const { return skySystem ? skySystem->getClouds() : nullptr; }
    LensFlare* getLensFlare() const { return skySystem ? skySystem->getLensFlare() : nullptr; }
    Weather* getWeather() const { return weather.get(); }
    Lightning* getLightning() const { return lightning.get(); }
    CharacterRenderer* getCharacterRenderer() const { return characterRenderer.get(); }
    WMORenderer* getWMORenderer() const { return wmoRenderer.get(); }
    M2Renderer* getM2Renderer() const { return m2Renderer.get(); }
    Minimap* getMinimap() const { return minimap.get(); }
    WorldMap* getWorldMap() const { return worldMap.get(); }
    QuestMarkerRenderer* getQuestMarkerRenderer() const { return questMarkerRenderer.get(); }
    FootprintRenderer* getFootprintRenderer() const { return footprintRenderer.get(); }
    SkySystem* getSkySystem() const { return skySystem.get(); }
    const std::string& getCurrentZoneName() const;
    uint32_t getCurrentZoneId() const;
    bool isPlayerIndoors() const { return playerIndoors_; }
    VkContext* getVkContext() const { return vkCtx; }
    VkDescriptorSetLayout getPerFrameSetLayout() const { return perFrameSetLayout; }
    VkRenderPass getShadowRenderPass() const { return shadowRenderPass; }

    // Third-person character follow
    void setCharacterFollow(uint32_t instanceId);
    glm::vec3& getCharacterPosition() { return characterPosition; }
    uint32_t getCharacterInstanceId() const { return characterInstanceId; }
    float getCharacterYaw() const { return characterYaw; }
    void setCharacterYaw(float yawDeg) { characterYaw = yawDeg; }

    // Screenshot capture — copies swapchain image to PNG file
    bool captureScreenshot(const std::string& outputPath);

    // Spell visual effects (SMSG_PLAY_SPELL_VISUAL / SMSG_PLAY_SPELL_IMPACT)
    // Delegates to SpellVisualSystem (owned by Renderer)
    SpellVisualSystem* getSpellVisualSystem() const { return spellVisualSystem_.get(); }

    // Combat visual state (compound: resets AnimationController + SpellVisualSystem)
    void resetCombatVisualState();

    // Sub-system accessors (§4.2)
    AnimationController* getAnimationController() const { return animationController_.get(); }
    LevelUpEffect* getLevelUpEffect() const { return levelUpEffect.get(); }
    ChargeEffect* getChargeEffect() const { return chargeEffect.get(); }
    SwimEffects* getSwimEffects() const { return swimEffects.get(); }

    // Selection circle for targeted entity
    void setSelectionCircle(const glm::vec3& pos, float radius, const glm::vec3& color);
    void clearSelectionCircle();

    // CPU timing stats (milliseconds, last frame).
    double getLastUpdateMs() const { return lastUpdateMs; }
    double getLastRenderMs() const { return lastRenderMs; }
    double getLastCameraUpdateMs() const { return lastCameraUpdateMs; }
    double getLastTerrainRenderMs() const { return lastTerrainRenderMs; }
    double getLastWMORenderMs() const { return lastWMORenderMs; }
    double getLastM2RenderMs() const { return lastM2RenderMs; }
    // Audio coordinator — owned by Application, set via setAudioCoordinator().
    void setAudioCoordinator(audio::AudioCoordinator* ac) { audioCoordinator_ = ac; }
    audio::AudioCoordinator* getAudioCoordinator() { return audioCoordinator_; }
    game::ZoneManager* getZoneManager() { return zoneManager.get(); }
    LightingManager* getLightingManager() { return lightingManager.get(); }

    const std::vector<pipeline::CustomZoneInfo>& getCustomZones() const { return customZones_; }

private:
    // True when water is drawn in the scene continuation pass rather than in
    // the scene pass itself (see renderWorld).
    bool waterDrawsInContinuePass() const;

    /// Point the swim spray at whichever pass the water ends up drawing in, so
    /// it can be recorded after the water rather than under it. Must run before
    /// the spray's pipelines are built, and again whenever they are rebuilt.
    void syncSwimEffectsTargetPass();
    void refreshSwimEffectsPass();

    /// True when the spray's pipelines were built for the water continuation
    /// pass. Draw sites test this rather than waterDrawsInContinuePass() so a
    /// mid-run mode change cannot record a pipeline into an incompatible pass.
    bool swimEffectsDrawWithWater_ = false;

    void runDeferredWorldInitStep(float deltaTime);

    core::Window* window = nullptr;
    std::unique_ptr<Camera> camera;
    std::unique_ptr<CameraController> cameraController;
    std::unique_ptr<TerrainRenderer> terrainRenderer;
    std::unique_ptr<TerrainManager> terrainManager;
    std::unique_ptr<PerformanceHUD> performanceHUD;
    std::unique_ptr<WaterRenderer> waterRenderer;
    std::unique_ptr<Skybox> skybox;
    std::unique_ptr<Celestial> celestial;
    std::unique_ptr<StarField> starField;
    std::unique_ptr<Clouds> clouds;
    std::unique_ptr<LensFlare> lensFlare;
    std::unique_ptr<Weather> weather;
    std::unique_ptr<Lightning> lightning;
    std::unique_ptr<LightingManager> lightingManager;
    std::unique_ptr<SkySystem> skySystem;  // Coordinator for sky rendering
    std::unique_ptr<SwimEffects> swimEffects;
    std::unique_ptr<MountDust> mountDust;
    std::unique_ptr<LevelUpEffect> levelUpEffect;
    std::unique_ptr<ChargeEffect> chargeEffect;
    std::unique_ptr<CharacterRenderer> characterRenderer;
    std::unique_ptr<WMORenderer> wmoRenderer;
    std::unique_ptr<M2Renderer> m2Renderer;
    std::unique_ptr<M2Renderer> outlandSkyRenderer_;
    std::string outlandSkyPath_;
    uint32_t outlandSkyInstanceId_ = 0;
    std::unique_ptr<Minimap> minimap;
    std::unique_ptr<WorldMap> worldMap;
    std::unique_ptr<QuestMarkerRenderer> questMarkerRenderer;
    std::unique_ptr<FootprintRenderer> footprintRenderer;
    audio::AudioCoordinator* audioCoordinator_ = nullptr;  // Owned by Application
    std::unique_ptr<AnimationController> animationController_;  // §4.2
    std::unique_ptr<game::ZoneManager> zoneManager;
    // Shadow mapping (Vulkan)
    static constexpr uint32_t SHADOW_MAP_SIZE = 4096;
    // Per-frame shadow resources: each in-flight frame has its own depth image and
    // framebuffer so that frame N's shadow read and frame N+1's shadow write don't
    // race on the same image across concurrent GPU submissions.
    // Array size must match MAX_FRAMES (= 2, defined in the private section below).
    VkImage shadowDepthImage[2] = {};
    VmaAllocation shadowDepthAlloc[2] = {};
    VkImageView shadowDepthView[2] = {};
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer[2] = {};
    VkImageLayout shadowDepthLayout_[2] = {};
    glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
    glm::vec3 shadowCenter = glm::vec3(0.0f);
    bool shadowCenterInitialized = false;
    bool shadowsEnabled = true;
    float shadowDistance_ = 300.0f;  // Shadow frustum half-extent (default: 300 units)
    float viewDistance_ = 1200.0f;
    uint32_t shadowFrameCounter_ = 0;


public:
    // Character preview registration (for off-screen composite pass)
    void registerPreview(CharacterPreview* preview);
    void unregisterPreview(CharacterPreview* preview);

    void setShadowsEnabled(bool enabled) { shadowsEnabled = enabled; }
    bool areShadowsEnabled() const { return shadowsEnabled; }
    void setShadowDistance(float dist) { shadowDistance_ = glm::clamp(dist, 40.0f, 500.0f); }
    float getShadowDistance() const { return shadowDistance_; }
    void setViewDistance(float distance);
    float getViewDistance() const { return viewDistance_; }
    int getTerrainLoadRadius() const;
    int getTerrainUnloadRadius() const { return getTerrainLoadRadius() + 3; }
    void setMsaaSamples(VkSampleCountFlagBits samples);

    // Post-process pipeline API — delegates to PostProcessPipeline (§4.3)
    PostProcessPipeline* getPostProcessPipeline() const;
    void setFSREnabled(bool enabled);
    void setFSR2Enabled(bool enabled);

    void setWaterRefractionEnabled(bool enabled);
    bool isWaterRefractionEnabled() const;

private:
    void applyMsaaChange();
    bool ensureOutlandSkybox();
    VkSampleCountFlagBits pendingMsaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    bool msaaChangePending_ = false;
    void renderShadowPass();
    glm::mat4 computeLightSpaceMatrix();

    std::vector<pipeline::CustomZoneInfo> customZones_;
    pipeline::AssetManager* cachedAssetManager = nullptr;

    // Spell visual effects — owned SpellVisualSystem (extracted from Renderer §4.4)
    std::unique_ptr<SpellVisualSystem> spellVisualSystem_;

    // Post-process pipeline — owns all FSR/FXAA/FSR2 state (extracted §4.3)
    std::unique_ptr<PostProcessPipeline> postProcessPipeline_;

    bool playerIndoors_ = false;  // Cached WMO inside state for macro conditionals
    bool deferredWorldInitEnabled_ = true;
    bool deferredWorldInitPending_ = false;
    uint8_t deferredWorldInitStage_ = 0;
    float deferredWorldInitCooldown_ = 0.0f;

    // Third-person character state
    glm::vec3 characterPosition = glm::vec3(0.0f);
    uint32_t characterInstanceId = 0;
    float characterYaw = 0.0f;



    // Selection circle + overlay rendering (owned by OverlaySystem)
    std::unique_ptr<OverlaySystem> overlaySystem_;



    // Vulkan frame state
    VkContext* vkCtx = nullptr;
    VkCommandBuffer currentCmd = VK_NULL_HANDLE;
    uint32_t currentImageIndex = 0;

    // Per-frame UBO + descriptors (set 0)
    static constexpr uint32_t MAX_FRAMES = 2;
    VkDescriptorSetLayout perFrameSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool sceneDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet perFrameDescSets[MAX_FRAMES] = {};
    VkBuffer perFrameUBOs[MAX_FRAMES] = {};
    VmaAllocation perFrameUBOAllocs[MAX_FRAMES] = {};
    void* perFrameUBOMapped[MAX_FRAMES] = {};
    GPUPerFrameData currentFrameData{};
    float globalTime = 0.0f;

    // Per-frame reflection UBO (mirrors camera for planar reflections)
    VkBuffer reflPerFrameUBO = VK_NULL_HANDLE;
    VmaAllocation reflPerFrameUBOAlloc = VK_NULL_HANDLE;
    void* reflPerFrameUBOMapped = nullptr;
    VkDescriptorSet reflPerFrameDescSet[MAX_FRAMES] = {};

    bool createPerFrameResources();
    void destroyPerFrameResources();
    void updatePerFrameUBO();
    void setupWater1xPass();
    void renderReflectionPass();

    // ── Multithreaded secondary command buffer recording ──
    // Indices into secondaryCmds_ arrays
    static constexpr uint32_t SEC_SKY       = 0;  // sky (main thread)
    static constexpr uint32_t SEC_TERRAIN   = 1;  // terrain (worker 0)
    static constexpr uint32_t SEC_WMO       = 2;  // WMO (worker 1)
    static constexpr uint32_t SEC_SELECTION = 3;  // selection circle (main thread)
    static constexpr uint32_t SEC_CHARS     = 4;  // characters (worker 2)
    static constexpr uint32_t SEC_M2        = 5;  // M2 + particles + glow (worker 3)
    static constexpr uint32_t SEC_POST      = 6;  // water + weather + effects (worker 4)
    static constexpr uint32_t SEC_IMGUI     = 7;  // ImGui (main thread, non-FSR only)
    static constexpr uint32_t NUM_SECONDARIES = 8;
    static constexpr uint32_t NUM_WORKERS = 5;

    // Per-worker command pools (thread-safe: one pool per thread)
    VkCommandPool workerCmdPools_[NUM_WORKERS] = {};
    // Main-thread command pool for its secondary buffers
    VkCommandPool mainSecondaryCmdPool_ = VK_NULL_HANDLE;
    // Pre-allocated secondary command buffers [secondaryIndex][frameInFlight]
    VkCommandBuffer secondaryCmds_[NUM_SECONDARIES][MAX_FRAMES] = {};

    bool parallelRecordingEnabled_ = false;  // set true after pools/buffers created
    float lastDeltaTime_ = 0.0f;           // cached for post-process pipeline
    bool createSecondaryCommandResources();
    void destroySecondaryCommandResources();
    VkCommandBuffer beginSecondary(uint32_t secondaryIndex);
    void setSecondaryViewportScissor(VkCommandBuffer cmd);

    // Cached render pass state for secondary buffer inheritance
    VkRenderPass activeRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer activeFramebuffer_ = VK_NULL_HANDLE;
    VkExtent2D activeRenderExtent_ = {0, 0};

    // Active character previews for off-screen rendering
    std::vector<CharacterPreview*> activePreviews_;

    bool terrainEnabled = true;
    bool terrainLoaded = false;

    bool ghostMode_ = false;  // set each frame from gameHandler->isPlayerGhost()

    // Render Graph — declarative pass ordering with automatic barriers
    std::unique_ptr<RenderGraph> renderGraph_;
    void buildFrameGraph(game::GameHandler* gameHandler);

    // HiZ occlusion culling — builds depth pyramid each frame
    std::unique_ptr<HiZSystem> hizSystem_;

    // CPU timing stats (last frame/update).
    double lastUpdateMs = 0.0;
    double lastRenderMs = 0.0;
    double lastCameraUpdateMs = 0.0;
    double lastTerrainRenderMs = 0.0;
    double lastWMORenderMs = 0.0;
    double lastM2RenderMs = 0.0;
};

} // namespace rendering
} // namespace wowee
