#pragma once

#include "pipeline/terrain_mesh.hpp"
#include "pipeline/blp_loader.hpp"
#include "rendering/camera.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <algorithm>

namespace wowee {

// Forward declarations
namespace pipeline { class AssetManager; }

namespace rendering {

class VkContext;
class VkTexture;
class Frustum;

/**
 * GPU-side terrain chunk data (Vulkan)
 */
struct TerrainChunkGPU {
    ::VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc = VK_NULL_HANDLE;
    ::VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAlloc = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    // Material descriptor set (set 1: 7 samplers + params UBO)
    VkDescriptorSet materialSet = VK_NULL_HANDLE;

    // Per-chunk params UBO (hasLayer1/2/3)
    ::VkBuffer paramsUBO = VK_NULL_HANDLE;
    VmaAllocation paramsAlloc = VK_NULL_HANDLE;

    // Texture handles (owned by cache, NOT destroyed per-chunk)
    VkTexture* baseTexture = nullptr;
    VkTexture* layerTextures[3] = {nullptr, nullptr, nullptr};
    VkTexture* alphaTextures[3] = {nullptr, nullptr, nullptr};
    int layerCount = 0;

    // Per-chunk alpha textures (owned by this chunk, destroyed on removal)
    std::vector<std::unique_ptr<VkTexture>> ownedAlphaTextures;

    // World position for culling
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;

    // Owning tile coordinates (for per-tile removal)
    int tileX = -1, tileY = -1;

    // Bounding sphere for frustum culling
    float boundingSphereRadius = 0.0f;
    glm::vec3 boundingSphereCenter = glm::vec3(0.0f);

    // Offsets into mega buffers for indirect drawing (-1 = not in mega buffer)
    int32_t megaBaseVertex = -1;
    uint32_t megaFirstIndex = 0;
    uint32_t vertexCount = 0;

    bool isValid() const { return vertexBuffer != VK_NULL_HANDLE && indexBuffer != VK_NULL_HANDLE; }
};

/**
 * Terrain renderer (Vulkan)
 */
class TerrainRenderer {
public:
    TerrainRenderer();
    ~TerrainRenderer();

    /**
     * Initialize terrain renderer
     * @param ctx Vulkan context
     * @param perFrameLayout Descriptor set layout for set 0 (per-frame UBO)
     * @param assetManager Asset manager for loading textures
     */
    bool initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout,
                    pipeline::AssetManager* assetManager);

    void shutdown();

    bool loadTerrain(const pipeline::TerrainMesh& mesh,
                     const std::vector<std::string>& texturePaths,
                     int tileX = -1, int tileY = -1);

    /// Upload a batch of terrain chunks incrementally. Returns true when all chunks done.
    /// chunkIndex is updated to the next chunk to process (0-255 row-major).
    bool loadTerrainIncremental(const pipeline::TerrainMesh& mesh,
                                const std::vector<std::string>& texturePaths,
                                int tileX, int tileY,
                                int& chunkIndex, int maxChunksPerCall = 16);

    void removeTile(int tileX, int tileY);

    void uploadPreloadedTextures(const std::unordered_map<std::string, pipeline::BLPImage>& textures);

    /**
     * Render terrain
     * @param cmd Command buffer to record into
     * @param perFrameSet Per-frame descriptor set (set 0)
     * @param camera Camera for frustum culling
     */
    void render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, const Camera& camera);

    /**
     * Initialize terrain shadow pipeline (must be called after initialize()).
     * @param shadowRenderPass  Depth-only render pass used for the shadow map.
     */
    bool initializeShadow(VkRenderPass shadowRenderPass);

    /**
     * Render terrain into the shadow depth map.
     * @param cmd               Command buffer (inside shadow render pass).
     * @param lightSpaceMatrix  Orthographic light-space transform.
     * @param shadowCenter      World-space centre of shadow coverage.
     * @param shadowRadius      Cull radius around shadowCenter.
     */
    void renderShadow(VkCommandBuffer cmd, const glm::mat4& lightSpaceMatrix,
                      const glm::vec3& shadowCenter, float shadowRadius);

    bool hasShadowPipeline() const { return shadowPipeline_ != VK_NULL_HANDLE; }

    void clear();

    void recreatePipelines();

    void setWireframe(bool enabled) { wireframe = enabled; }
    void setFrustumCulling(bool enabled) { frustumCullingEnabled = enabled; }
    void setFogEnabled(bool enabled) { fogEnabled = enabled; }
    bool isFogEnabled() const { return fogEnabled; }
    void setViewDistance(float distance) { maxViewDistance_ = std::clamp(distance, 400.0f, 2400.0f); }

    void setShadowMap(VkDescriptorImageInfo /*depthInfo*/, const glm::mat4& /*lightSpaceMat*/) {}
    void clearShadowMap() {}

    int getChunkCount() const { return static_cast<int>(chunks.size()); }
    int getRenderedChunkCount() const { return renderedChunks; }
    int getCulledChunkCount() const { return culledChunks; }
    int getTriangleCount() const;
    VkContext* getVkContext() const { return vkCtx; }

private:
    TerrainChunkGPU uploadChunk(const pipeline::ChunkMesh& chunk);
    VkTexture* loadTexture(const std::string& path);
    VkTexture* createAlphaTexture(const std::vector<uint8_t>& alphaData);
    bool isChunkVisible(const TerrainChunkGPU& chunk, const Frustum& frustum);
    void calculateBoundingSphere(TerrainChunkGPU& chunk, const pipeline::ChunkMesh& meshChunk);
    VkDescriptorSet allocateMaterialSet();
    void writeMaterialDescriptors(VkDescriptorSet set, const TerrainChunkGPU& chunk);
    void destroyChunkGPU(TerrainChunkGPU& chunk);

    VkContext* vkCtx = nullptr;
    pipeline::AssetManager* assetManager = nullptr;

    // Main pipelines
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline wireframePipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;

    // Shadow pipeline
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadowParamsLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool shadowParamsPool_ = VK_NULL_HANDLE;
    VkDescriptorSet shadowParamsSet_ = VK_NULL_HANDLE;
    VkBuffer shadowParamsUBO_ = VK_NULL_HANDLE;
    VmaAllocation shadowParamsAlloc_ = VK_NULL_HANDLE;

    // Descriptor pool for material sets
    VkDescriptorPool materialDescPool = VK_NULL_HANDLE;
    static constexpr uint32_t MAX_MATERIAL_SETS = 65536;

    // Loaded terrain chunks
    std::vector<TerrainChunkGPU> chunks;

    // Texture cache (path -> VkTexture)
    struct TextureCacheEntry {
        std::unique_ptr<VkTexture> texture;
        size_t approxBytes = 0;
        uint64_t lastUse = 0;
    };
    std::unordered_map<std::string, TextureCacheEntry> textureCache;
    size_t textureCacheBytes_ = 0;
    uint64_t textureCacheCounter_ = 0;
    size_t textureCacheBudgetBytes_ = 4096ull * 1024 * 1024;
    std::unordered_set<std::string> failedTextureCache_;
    std::unordered_set<std::string> loggedTextureLoadFails_;
    uint32_t textureBudgetRejectWarnings_ = 0;

    // Fallback textures
    std::unique_ptr<VkTexture> whiteTexture;
    std::unique_ptr<VkTexture> opaqueAlphaTexture;

    // Rendering state
    bool wireframe = false;
    bool frustumCullingEnabled = true;
    bool fogEnabled = true;
    float maxViewDistance_ = 1200.0f;
    int renderedChunks = 0;
    int culledChunks = 0;

    // Mega vertex/index buffers for indirect drawing
    // All terrain chunks share a single VB + IB, eliminating per-chunk rebinds.
    // Indirect draw commands are built CPU-side each frame for visible chunks.
    VkBuffer megaVB_ = VK_NULL_HANDLE;
    VmaAllocation megaVBAlloc_ = VK_NULL_HANDLE;
    void* megaVBMapped_ = nullptr;
    VkBuffer megaIB_ = VK_NULL_HANDLE;
    VmaAllocation megaIBAlloc_ = VK_NULL_HANDLE;
    void* megaIBMapped_ = nullptr;
    uint32_t megaVBUsed_ = 0;  // vertices used
    uint32_t megaIBUsed_ = 0;  // indices used
    static constexpr uint32_t MEGA_VB_MAX_VERTS   = 1536 * 1024; // ~1.5M verts × 44B ≈ 64MB
    static constexpr uint32_t MEGA_IB_MAX_INDICES  = 6 * 1024 * 1024; // 6M indices × 4B = 24MB

    VkBuffer indirectBuffer_ = VK_NULL_HANDLE;
    VmaAllocation indirectAlloc_ = VK_NULL_HANDLE;
    void* indirectMapped_ = nullptr;
    static constexpr uint32_t MAX_INDIRECT_DRAWS = 8192;
};

} // namespace rendering
} // namespace wowee
