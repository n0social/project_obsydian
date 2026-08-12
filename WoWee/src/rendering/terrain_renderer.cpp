#include "rendering/terrain_renderer.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_buffer.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_utils.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/frustum.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "core/logger.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <cstring>

namespace wowee {
namespace rendering {

// Matches set 1 binding 7 in terrain.frag.glsl
struct TerrainParamsUBO {
    int32_t layerCount;
    int32_t hasLayer1;
    int32_t hasLayer2;
    int32_t hasLayer3;
};

TerrainRenderer::TerrainRenderer() = default;

TerrainRenderer::~TerrainRenderer() {
    shutdown();
}

bool TerrainRenderer::initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout,
                                  pipeline::AssetManager* assets) {
    vkCtx = ctx;
    assetManager = assets;

    if (!vkCtx || !assetManager) {
        LOG_ERROR("TerrainRenderer: null context or asset manager");
        return false;
    }

    LOG_INFO("Initializing terrain renderer (Vulkan)");
    VkDevice device = vkCtx->getDevice();

    // --- Create material descriptor set layout (set 1) ---
    // bindings 0-6: combined image samplers (base + 3 layer + 3 alpha)
    // binding 7: uniform buffer (TerrainParams)
    std::vector<VkDescriptorSetLayoutBinding> materialBindings(8);
    for (uint32_t i = 0; i < 7; i++) {
        materialBindings[i] = {};
        materialBindings[i].binding = i;
        materialBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        materialBindings[i].descriptorCount = 1;
        materialBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    materialBindings[7] = {};
    materialBindings[7].binding = 7;
    materialBindings[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    materialBindings[7].descriptorCount = 1;
    materialBindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    materialSetLayout = createDescriptorSetLayout(device, materialBindings);
    if (!materialSetLayout) {
        LOG_ERROR("TerrainRenderer: failed to create material set layout");
        return false;
    }

    // --- Create descriptor pool ---
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_MATERIAL_SETS * 7 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_MATERIAL_SETS },
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = MAX_MATERIAL_SETS;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &materialDescPool) != VK_SUCCESS) {
        LOG_ERROR("TerrainRenderer: failed to create descriptor pool");
        return false;
    }

    // --- Create pipeline layout ---
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(GPUPushConstants);

    std::vector<VkDescriptorSetLayout> setLayouts = { perFrameLayout, materialSetLayout };
    pipelineLayout = createPipelineLayout(device, setLayouts, { pushRange });
    if (!pipelineLayout) {
        LOG_ERROR("TerrainRenderer: failed to create pipeline layout");
        return false;
    }

    // --- Load shaders ---
    VkShaderModule vertShader, fragShader;
    if (!vertShader.loadFromFile(device, "assets/shaders/terrain.vert.spv")) {
        LOG_ERROR("TerrainRenderer: failed to load vertex shader");
        return false;
    }
    if (!fragShader.loadFromFile(device, "assets/shaders/terrain.frag.spv")) {
        LOG_ERROR("TerrainRenderer: failed to load fragment shader");
        return false;
    }

    // --- Vertex input ---
    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(pipeline::TerrainVertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> vertexAttribs(4);
    vertexAttribs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,
        static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, position)) };
    vertexAttribs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,
        static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, normal)) };
    vertexAttribs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,
        static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, texCoord)) };
    vertexAttribs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT,
        static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, layerUV)) };

    // --- Build fill pipeline (base for derivatives — shared state optimization) ---
    VkRenderPass mainPass = vkCtx->getImGuiRenderPass();

    pipeline = PipelineBuilder()
        .setShaders(vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                    fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({ vertexBinding }, vertexAttribs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(mainPass)
        .setDynamicStates({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR })
        .setFlags(VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT)
        .build(device, vkCtx->getPipelineCache());

    if (!pipeline) {
        LOG_ERROR("TerrainRenderer: failed to create fill pipeline");
        vertShader.destroy();
        fragShader.destroy();
        return false;
    }

    // --- Build wireframe pipeline (derivative of fill) ---
    wireframePipeline = PipelineBuilder()
        .setShaders(vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                    fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({ vertexBinding }, vertexAttribs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE)
        .setDepthTest(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(mainPass)
        .setDynamicStates({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR })
        .setFlags(VK_PIPELINE_CREATE_DERIVATIVE_BIT)
        .setBasePipeline(pipeline)
        .build(device, vkCtx->getPipelineCache());

    if (!wireframePipeline) {
        LOG_WARNING("TerrainRenderer: wireframe pipeline not available");
    }

    vertShader.destroy();
    fragShader.destroy();

    // --- Create fallback textures ---
    whiteTexture = std::make_unique<VkTexture>();
    uint8_t whitePixel[4] = {255, 255, 255, 255};
    whiteTexture->upload(*vkCtx, whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);
    whiteTexture->createSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                 VK_SAMPLER_ADDRESS_MODE_REPEAT);

    opaqueAlphaTexture = std::make_unique<VkTexture>();
    uint8_t opaqueAlpha = 255;
    opaqueAlphaTexture->upload(*vkCtx, &opaqueAlpha, 1, 1, VK_FORMAT_R8_UNORM, false);
    opaqueAlphaTexture->createSampler(device, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    textureCacheBudgetBytes_ =
        envSizeMBOrDefault("WOWEE_TERRAIN_TEX_CACHE_MB", 4096) * 1024ull * 1024ull;
    LOG_INFO("Terrain texture cache budget: ", textureCacheBudgetBytes_ / (1024 * 1024), " MB");

    // Allocate mega vertex/index buffers and indirect draw buffer.
    // All terrain chunks share these buffers, eliminating per-chunk VB/IB rebinds.
    {
        VmaAllocator allocator = vkCtx->getAllocator();

        // Mega vertex buffer (host-visible for direct write during chunk upload)
        VkBufferCreateInfo vbCI{};
        vbCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbCI.size = static_cast<VkDeviceSize>(MEGA_VB_MAX_VERTS) * sizeof(pipeline::TerrainVertex);
        vbCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo vbAllocCI{};
        vbAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        vbAllocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo vbInfo{};
        if (vmaCreateBuffer(allocator, &vbCI, &vbAllocCI,
                &megaVB_, &megaVBAlloc_, &vbInfo) == VK_SUCCESS) {
            megaVBMapped_ = vbInfo.pMappedData;
        } else {
            LOG_WARNING("TerrainRenderer: mega VB allocation failed, per-chunk fallback");
        }

        // Mega index buffer
        VkBufferCreateInfo ibCI{};
        ibCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ibCI.size = static_cast<VkDeviceSize>(MEGA_IB_MAX_INDICES) * sizeof(uint32_t);
        ibCI.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        VmaAllocationCreateInfo ibAllocCI{};
        ibAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ibAllocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo ibInfo{};
        if (vmaCreateBuffer(allocator, &ibCI, &ibAllocCI,
                &megaIB_, &megaIBAlloc_, &ibInfo) == VK_SUCCESS) {
            megaIBMapped_ = ibInfo.pMappedData;
        } else {
            LOG_WARNING("TerrainRenderer: mega IB allocation failed, per-chunk fallback");
        }

        // Indirect draw command buffer
        VkBufferCreateInfo indCI{};
        indCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        indCI.size = MAX_INDIRECT_DRAWS * sizeof(VkDrawIndexedIndirectCommand);
        indCI.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        VmaAllocationCreateInfo indAllocCI{};
        indAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        indAllocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo indInfo{};
        if (vmaCreateBuffer(allocator, &indCI, &indAllocCI,
                &indirectBuffer_, &indirectAlloc_, &indInfo) == VK_SUCCESS) {
            indirectMapped_ = indInfo.pMappedData;
        } else {
            LOG_WARNING("TerrainRenderer: indirect buffer allocation failed");
        }

        LOG_INFO("Terrain mega buffers: VB=", vbCI.size / (1024*1024), "MB IB=",
                 ibCI.size / (1024*1024), "MB indirect=",
                 indCI.size / 1024, "KB");
    }

    LOG_INFO("Terrain renderer initialized (Vulkan)");
    return true;
}

void TerrainRenderer::recreatePipelines() {
    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();

    // Destroy old pipelines (keep layouts)
    if (pipeline) { vkDestroyPipeline(device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }
    if (wireframePipeline) { vkDestroyPipeline(device, wireframePipeline, nullptr); wireframePipeline = VK_NULL_HANDLE; }

    // Load shaders
    VkShaderModule vertShader, fragShader;
    if (!vertShader.loadFromFile(device, "assets/shaders/terrain.vert.spv")) {
        LOG_ERROR("TerrainRenderer::recreatePipelines: failed to load vertex shader");
        return;
    }
    if (!fragShader.loadFromFile(device, "assets/shaders/terrain.frag.spv")) {
        LOG_ERROR("TerrainRenderer::recreatePipelines: failed to load fragment shader");
        vertShader.destroy();
        return;
    }

    // Vertex input (same as initialize)
    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(pipeline::TerrainVertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> vertexAttribs(4);
    vertexAttribs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,
        static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, position)) };
    vertexAttribs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,
        static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, normal)) };
    vertexAttribs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,
        static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, texCoord)) };
    vertexAttribs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT,
        static_cast<uint32_t>(offsetof(pipeline::TerrainVertex, layerUV)) };

    VkRenderPass mainPass = vkCtx->getImGuiRenderPass();

    // Rebuild fill pipeline (base for derivatives — shared state optimization)
    pipeline = PipelineBuilder()
        .setShaders(vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                    fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({ vertexBinding }, vertexAttribs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(mainPass)
        .setDynamicStates({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR })
        .setFlags(VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT)
        .build(device, vkCtx->getPipelineCache());

    if (!pipeline) {
        LOG_ERROR("TerrainRenderer::recreatePipelines: failed to create fill pipeline");
    }

    // Rebuild wireframe pipeline (derivative of fill)
    wireframePipeline = PipelineBuilder()
        .setShaders(vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                    fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({ vertexBinding }, vertexAttribs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE)
        .setDepthTest(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(vkCtx->getMsaaSamples())
        .setLayout(pipelineLayout)
        .setRenderPass(mainPass)
        .setDynamicStates({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR })
        .setFlags(VK_PIPELINE_CREATE_DERIVATIVE_BIT)
        .setBasePipeline(pipeline)
        .build(device, vkCtx->getPipelineCache());

    if (!wireframePipeline) {
        LOG_WARNING("TerrainRenderer::recreatePipelines: wireframe pipeline not available");
    }

    vertShader.destroy();
    fragShader.destroy();
}

void TerrainRenderer::shutdown() {
    LOG_INFO("Shutting down terrain renderer");

    if (!vkCtx) return;
    VkDevice device = vkCtx->getDevice();
    VmaAllocator allocator = vkCtx->getAllocator();

    vkDeviceWaitIdle(device);

    clear();
    // clear() defers chunk destruction, and no further frames will run to drain
    // the queue — so run it now, while the descriptor pools those lambdas free
    // sets from are still alive. Without this every resident chunk's vertex and
    // index buffers outlived the device: twenty thousand of each.
    vkCtx->flushDeferredCleanup();

    for (auto& [path, entry] : textureCache) {
        if (entry.texture) entry.texture->destroy(device, allocator);
    }
    textureCache.clear();
    textureCacheBytes_ = 0;
    textureCacheCounter_ = 0;
    failedTextureCache_.clear();
    loggedTextureLoadFails_.clear();
    textureBudgetRejectWarnings_ = 0;

    if (whiteTexture) { whiteTexture->destroy(device, allocator); whiteTexture.reset(); }
    if (opaqueAlphaTexture) { opaqueAlphaTexture->destroy(device, allocator); opaqueAlphaTexture.reset(); }

    if (pipeline) { vkDestroyPipeline(device, pipeline, nullptr); pipeline = VK_NULL_HANDLE; }
    if (wireframePipeline) { vkDestroyPipeline(device, wireframePipeline, nullptr); wireframePipeline = VK_NULL_HANDLE; }
    if (pipelineLayout) { vkDestroyPipelineLayout(device, pipelineLayout, nullptr); pipelineLayout = VK_NULL_HANDLE; }
    if (materialDescPool) { vkDestroyDescriptorPool(device, materialDescPool, nullptr); materialDescPool = VK_NULL_HANDLE; }
    if (materialSetLayout) { vkDestroyDescriptorSetLayout(device, materialSetLayout, nullptr); materialSetLayout = VK_NULL_HANDLE; }

    // Shadow pipeline cleanup
    if (shadowPipeline_) { vkDestroyPipeline(device, shadowPipeline_, nullptr); shadowPipeline_ = VK_NULL_HANDLE; }
    if (shadowPipelineLayout_) { vkDestroyPipelineLayout(device, shadowPipelineLayout_, nullptr); shadowPipelineLayout_ = VK_NULL_HANDLE; }
    if (shadowParamsPool_) { vkDestroyDescriptorPool(device, shadowParamsPool_, nullptr); shadowParamsPool_ = VK_NULL_HANDLE; shadowParamsSet_ = VK_NULL_HANDLE; }
    if (shadowParamsLayout_) { vkDestroyDescriptorSetLayout(device, shadowParamsLayout_, nullptr); shadowParamsLayout_ = VK_NULL_HANDLE; }
    if (shadowParamsUBO_) { vmaDestroyBuffer(allocator, shadowParamsUBO_, shadowParamsAlloc_); shadowParamsUBO_ = VK_NULL_HANDLE; shadowParamsAlloc_ = VK_NULL_HANDLE; }

    // Destroy mega buffers and indirect draw buffer
    if (megaVB_) { vmaDestroyBuffer(allocator, megaVB_, megaVBAlloc_); megaVB_ = VK_NULL_HANDLE; megaVBAlloc_ = VK_NULL_HANDLE; megaVBMapped_ = nullptr; }
    if (megaIB_) { vmaDestroyBuffer(allocator, megaIB_, megaIBAlloc_); megaIB_ = VK_NULL_HANDLE; megaIBAlloc_ = VK_NULL_HANDLE; megaIBMapped_ = nullptr; }
    if (indirectBuffer_) { vmaDestroyBuffer(allocator, indirectBuffer_, indirectAlloc_); indirectBuffer_ = VK_NULL_HANDLE; indirectAlloc_ = VK_NULL_HANDLE; indirectMapped_ = nullptr; }
    megaVBUsed_ = 0;
    megaIBUsed_ = 0;

    vkCtx = nullptr;
}

bool TerrainRenderer::loadTerrain(const pipeline::TerrainMesh& mesh,
                                   const std::vector<std::string>& texturePaths,
                                   int tileX, int tileY) {
    if (mesh.validChunkCount == 0) {
        LOG_WARNING("loadTerrain[", tileX, ",", tileY, "]: mesh has 0 valid chunks (", texturePaths.size(), " textures)");
        return false;
    }
    LOG_DEBUG("Loading terrain mesh: ", mesh.validChunkCount, " chunks");

    vkCtx->beginUploadBatch();

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            const auto& chunk = mesh.getChunk(x, y);
            if (!chunk.isValid()) continue;

            TerrainChunkGPU gpuChunk = uploadChunk(chunk);
            if (!gpuChunk.isValid()) {
                LOG_WARNING("Failed to upload chunk [", x, ",", y, "]");
                continue;
            }

            calculateBoundingSphere(gpuChunk, chunk);

            // Load textures for this chunk
            if (!chunk.layers.empty()) {
                uint32_t baseTexId = chunk.layers[0].textureId;
                if (baseTexId < texturePaths.size()) {
                    gpuChunk.baseTexture = loadTexture(texturePaths[baseTexId]);
                } else {
                    LOG_WARNING("Terrain[", tileX, ",", tileY, "] chunk[", x, ",", y,
                                "] base textureId ", baseTexId, " >= texturePaths size ",
                                texturePaths.size(), " — white fallback");
                    gpuChunk.baseTexture = whiteTexture.get();
                }

                for (size_t i = 1; i < chunk.layers.size() && i < 4; i++) {
                    const auto& layer = chunk.layers[i];
                    int li = static_cast<int>(i) - 1;

                    VkTexture* layerTex = whiteTexture.get();
                    if (layer.textureId < texturePaths.size()) {
                        layerTex = loadTexture(texturePaths[layer.textureId]);
                    } else {
                        LOG_WARNING("Terrain[", tileX, ",", tileY, "] chunk[", x, ",", y,
                                    "] layer[", i, "] textureId ", layer.textureId,
                                    " >= texturePaths size ", texturePaths.size(),
                                    " — white fallback");
                    }
                    gpuChunk.layerTextures[li] = layerTex;

                    VkTexture* alphaTex = opaqueAlphaTexture.get();
                    if (!layer.alphaData.empty()) {
                        alphaTex = createAlphaTexture(layer.alphaData);
                    }
                    gpuChunk.alphaTextures[li] = alphaTex;
                    gpuChunk.layerCount = static_cast<int>(i);
                }
            } else {
                gpuChunk.baseTexture = whiteTexture.get();
            }

            gpuChunk.tileX = tileX;
            gpuChunk.tileY = tileY;

            // Create per-chunk params UBO
            TerrainParamsUBO params{};
            params.layerCount = gpuChunk.layerCount;
            params.hasLayer1 = gpuChunk.layerCount >= 1 ? 1 : 0;
            params.hasLayer2 = gpuChunk.layerCount >= 2 ? 1 : 0;
            params.hasLayer3 = gpuChunk.layerCount >= 3 ? 1 : 0;

            VkBufferCreateInfo bufCI{};
            bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufCI.size = sizeof(TerrainParamsUBO);
            bufCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            VmaAllocationCreateInfo allocCI{};
            allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo mapInfo{};
            // Check return value — a null UBO handle would cause the GPU to
            // read from an invalid descriptor, crashing the driver under
            // memory pressure instead of gracefully skipping the chunk.
            if (vmaCreateBuffer(vkCtx->getAllocator(), &bufCI, &allocCI,
                                &gpuChunk.paramsUBO, &gpuChunk.paramsAlloc, &mapInfo) != VK_SUCCESS) {
                LOG_WARNING("Terrain chunk UBO allocation failed — skipping chunk");
                destroyChunkGPU(gpuChunk);
                continue;
            }
            if (mapInfo.pMappedData) {
                std::memcpy(mapInfo.pMappedData, &params, sizeof(params));
            }

            gpuChunk.materialSet = allocateMaterialSet();
            if (!gpuChunk.materialSet) {
                destroyChunkGPU(gpuChunk);
                continue;
            }
            writeMaterialDescriptors(gpuChunk.materialSet, gpuChunk);

            chunks.push_back(std::move(gpuChunk));
        }
    }

    vkCtx->endUploadBatch();

    LOG_DEBUG("Loaded ", chunks.size(), " terrain chunks to GPU");
    return !chunks.empty();
}

bool TerrainRenderer::loadTerrainIncremental(const pipeline::TerrainMesh& mesh,
                                              const std::vector<std::string>& texturePaths,
                                              int tileX, int tileY,
                                              int& chunkIndex, int maxChunksPerCall) {
    // Batch all GPU uploads (VBs, IBs, textures) into a single command buffer
    // submission with one fence wait, instead of one per buffer/texture.
    vkCtx->beginUploadBatch();

    int uploaded = 0;
    while (chunkIndex < 256 && uploaded < maxChunksPerCall) {
        int cy = chunkIndex / 16;
        int cx = chunkIndex % 16;
        chunkIndex++;

        const auto& chunk = mesh.getChunk(cx, cy);
        if (!chunk.isValid()) continue;

        TerrainChunkGPU gpuChunk = uploadChunk(chunk);
        if (!gpuChunk.isValid()) continue;

        calculateBoundingSphere(gpuChunk, chunk);

        if (!chunk.layers.empty()) {
            uint32_t baseTexId = chunk.layers[0].textureId;
            if (baseTexId < texturePaths.size()) {
                gpuChunk.baseTexture = loadTexture(texturePaths[baseTexId]);
            } else {
                LOG_WARNING("Terrain[", tileX, ",", tileY, "] chunk[", cx, ",", cy,
                            "] base textureId ", baseTexId, " >= texturePaths size ",
                            texturePaths.size(), " — white fallback");
                gpuChunk.baseTexture = whiteTexture.get();
            }

            for (size_t i = 1; i < chunk.layers.size() && i < 4; i++) {
                const auto& layer = chunk.layers[i];
                int li = static_cast<int>(i) - 1;

                VkTexture* layerTex = whiteTexture.get();
                if (layer.textureId < texturePaths.size()) {
                    layerTex = loadTexture(texturePaths[layer.textureId]);
                } else {
                    LOG_WARNING("Terrain[", tileX, ",", tileY, "] chunk[", cx, ",", cy,
                                "] layer[", i, "] textureId ", layer.textureId,
                                " >= texturePaths size ", texturePaths.size(),
                                " — white fallback");
                }
                gpuChunk.layerTextures[li] = layerTex;

                VkTexture* alphaTex = opaqueAlphaTexture.get();
                if (!layer.alphaData.empty()) {
                    alphaTex = createAlphaTexture(layer.alphaData);
                }
                gpuChunk.alphaTextures[li] = alphaTex;
                gpuChunk.layerCount = static_cast<int>(i);
            }
        } else {
            gpuChunk.baseTexture = whiteTexture.get();
        }

        gpuChunk.tileX = tileX;
        gpuChunk.tileY = tileY;

        TerrainParamsUBO params{};
        params.layerCount = gpuChunk.layerCount;
        params.hasLayer1 = gpuChunk.layerCount >= 1 ? 1 : 0;
        params.hasLayer2 = gpuChunk.layerCount >= 2 ? 1 : 0;
        params.hasLayer3 = gpuChunk.layerCount >= 3 ? 1 : 0;

        VkBufferCreateInfo bufCI{};
        bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCI.size = sizeof(TerrainParamsUBO);
        bufCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo allocCI{};
        allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo mapInfo{};
        if (vmaCreateBuffer(vkCtx->getAllocator(), &bufCI, &allocCI,
                            &gpuChunk.paramsUBO, &gpuChunk.paramsAlloc, &mapInfo) != VK_SUCCESS) {
            LOG_WARNING("Terrain chunk UBO allocation failed (incremental) — skipping chunk");
            destroyChunkGPU(gpuChunk);
            continue;
        }
        if (mapInfo.pMappedData) {
            std::memcpy(mapInfo.pMappedData, &params, sizeof(params));
        }

        gpuChunk.materialSet = allocateMaterialSet();
        if (!gpuChunk.materialSet) {
            destroyChunkGPU(gpuChunk);
            continue;
        }
        writeMaterialDescriptors(gpuChunk.materialSet, gpuChunk);

        chunks.push_back(std::move(gpuChunk));
        uploaded++;
    }

    vkCtx->endUploadBatch();

    return chunkIndex >= 256;
}

TerrainChunkGPU TerrainRenderer::uploadChunk(const pipeline::ChunkMesh& chunk) {
    TerrainChunkGPU gpuChunk;

    gpuChunk.worldX = chunk.worldX;
    gpuChunk.worldY = chunk.worldY;
    gpuChunk.worldZ = chunk.worldZ;
    gpuChunk.indexCount = static_cast<uint32_t>(chunk.indices.size());
    gpuChunk.vertexCount = static_cast<uint32_t>(chunk.vertices.size());

    VkDeviceSize vbSize = chunk.vertices.size() * sizeof(pipeline::TerrainVertex);
    AllocatedBuffer vb = uploadBuffer(*vkCtx, chunk.vertices.data(), vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    gpuChunk.vertexBuffer = vb.buffer;
    gpuChunk.vertexAlloc = vb.allocation;

    VkDeviceSize ibSize = chunk.indices.size() * sizeof(pipeline::TerrainIndex);
    AllocatedBuffer ib = uploadBuffer(*vkCtx, chunk.indices.data(), ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    gpuChunk.indexBuffer = ib.buffer;
    gpuChunk.indexAlloc = ib.allocation;

    // Also copy into mega buffers for indirect drawing
    uint32_t vertCount = static_cast<uint32_t>(chunk.vertices.size());
    uint32_t idxCount = static_cast<uint32_t>(chunk.indices.size());
    if (megaVBMapped_ && megaIBMapped_ &&
        megaVBUsed_ + vertCount <= MEGA_VB_MAX_VERTS &&
        megaIBUsed_ + idxCount <= MEGA_IB_MAX_INDICES) {
        // Copy vertices
        auto* vbDst = static_cast<pipeline::TerrainVertex*>(megaVBMapped_) + megaVBUsed_;
        std::memcpy(vbDst, chunk.vertices.data(), vertCount * sizeof(pipeline::TerrainVertex));
        // Copy indices
        auto* ibDst = static_cast<uint32_t*>(megaIBMapped_) + megaIBUsed_;
        std::memcpy(ibDst, chunk.indices.data(), idxCount * sizeof(uint32_t));

        gpuChunk.megaBaseVertex = static_cast<int32_t>(megaVBUsed_);
        gpuChunk.megaFirstIndex = megaIBUsed_;
        megaVBUsed_ += vertCount;
        megaIBUsed_ += idxCount;
    }

    return gpuChunk;
}

VkTexture* TerrainRenderer::loadTexture(const std::string& path) {
    auto normalizeKey = [](std::string key) {
        std::replace(key.begin(), key.end(), '/', '\\');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    };
    std::string key = normalizeKey(path);

    auto it = textureCache.find(key);
    if (it != textureCache.end()) {
        it->second.lastUse = ++textureCacheCounter_;
        return it->second.texture.get();
    }
    pipeline::BLPImage blp = assetManager->loadTexture(key);
    if (!blp.isValid()) {
        // Return white fallback but don't cache the failure — allow retry
        // on next tile load in case the asset becomes available.
        if (loggedTextureLoadFails_.insert(key).second) {
            LOG_WARNING("Failed to load texture: ", path);
        }
        return whiteTexture.get();
    }

    size_t base = static_cast<size_t>(blp.width) * static_cast<size_t>(blp.height) * 4ull;
    size_t approxBytes = base + (base / 3);
    if (textureCacheBytes_ + approxBytes > textureCacheBudgetBytes_) {
        if (textureBudgetRejectWarnings_ < 3) {
            LOG_WARNING("Terrain texture cache full (", textureCacheBytes_ / (1024 * 1024),
                        " MB / ", textureCacheBudgetBytes_ / (1024 * 1024),
                        " MB), rejecting texture: ", path);
        }
        ++textureBudgetRejectWarnings_;
        return whiteTexture.get();
    }

    auto tex = std::make_unique<VkTexture>();
    if (!tex->upload(*vkCtx, blp.data.data(), blp.width, blp.height,
                      VK_FORMAT_R8G8B8A8_UNORM, true)) {
        LOG_WARNING("Failed to upload texture to GPU: ", path);
        return whiteTexture.get();
    }
    tex->createSampler(vkCtx->getDevice(), VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                        VK_SAMPLER_ADDRESS_MODE_REPEAT);

    VkTexture* raw = tex.get();
    TextureCacheEntry e;
    e.texture = std::move(tex);
    e.approxBytes = approxBytes;
    e.lastUse = ++textureCacheCounter_;
    textureCacheBytes_ += e.approxBytes;
    textureCache[key] = std::move(e);

    return raw;
}

void TerrainRenderer::uploadPreloadedTextures(
    const std::unordered_map<std::string, pipeline::BLPImage>& textures) {
    auto normalizeKey = [](std::string key) {
        std::replace(key.begin(), key.end(), '/', '\\');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    };
    // Batch all texture uploads into a single command buffer submission
    vkCtx->beginUploadBatch();

    for (const auto& [path, blp] : textures) {
        std::string key = normalizeKey(path);
        if (textureCache.find(key) != textureCache.end()) continue;
        if (!blp.isValid()) continue;

        auto tex = std::make_unique<VkTexture>();
        if (!tex->upload(*vkCtx, blp.data.data(), blp.width, blp.height,
                          VK_FORMAT_R8G8B8A8_UNORM, true)) continue;
        tex->createSampler(vkCtx->getDevice(), VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                            VK_SAMPLER_ADDRESS_MODE_REPEAT);

        TextureCacheEntry e;
        e.texture = std::move(tex);
        size_t base = static_cast<size_t>(blp.width) * static_cast<size_t>(blp.height) * 4ull;
        e.approxBytes = base + (base / 3);
        e.lastUse = ++textureCacheCounter_;
        textureCacheBytes_ += e.approxBytes;
        textureCache[key] = std::move(e);
    }

    vkCtx->endUploadBatch();
}

VkTexture* TerrainRenderer::createAlphaTexture(const std::vector<uint8_t>& alphaData) {
    if (alphaData.empty()) return opaqueAlphaTexture.get();

    std::vector<uint8_t> expanded;
    const uint8_t* src = alphaData.data();
    if (alphaData.size() < 4096) {
        expanded.assign(4096, 255);
        std::copy(alphaData.begin(), alphaData.end(), expanded.begin());
        src = expanded.data();
    }

    auto tex = std::make_unique<VkTexture>();
    if (!tex->upload(*vkCtx, src, 64, 64, VK_FORMAT_R8_UNORM, false)) {
        return opaqueAlphaTexture.get();
    }
    tex->createSampler(vkCtx->getDevice(), VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    VkTexture* raw = tex.get();
    static uint64_t alphaCounter = 0;
    std::string key = "__alpha_" + std::to_string(++alphaCounter);
    TextureCacheEntry e;
    e.texture = std::move(tex);
    e.approxBytes = 64 * 64;
    e.lastUse = ++textureCacheCounter_;
    textureCacheBytes_ += e.approxBytes;
    textureCache[key] = std::move(e);

    return raw;
}

VkDescriptorSet TerrainRenderer::allocateMaterialSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = materialDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &materialSetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(vkCtx->getDevice(), &allocInfo, &set) != VK_SUCCESS) {
        static uint64_t failCount = 0;
        ++failCount;
        if (failCount <= 8 || (failCount % 256) == 0) {
            LOG_WARNING("TerrainRenderer: failed to allocate material descriptor set (count=", failCount, ")");
        }
        return VK_NULL_HANDLE;
    }
    return set;
}

void TerrainRenderer::writeMaterialDescriptors(VkDescriptorSet set, const TerrainChunkGPU& chunk) {
    VkTexture* white = whiteTexture.get();
    VkTexture* opaque = opaqueAlphaTexture.get();

    VkDescriptorImageInfo imageInfos[7];
    imageInfos[0] = (chunk.baseTexture ? chunk.baseTexture : white)->descriptorInfo();
    for (int i = 0; i < 3; i++) {
        imageInfos[1 + i] = (chunk.layerTextures[i] ? chunk.layerTextures[i] : white)->descriptorInfo();
        imageInfos[4 + i] = (chunk.alphaTextures[i] ? chunk.alphaTextures[i] : opaque)->descriptorInfo();
    }

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = chunk.paramsUBO;
    bufInfo.offset = 0;
    bufInfo.range = sizeof(TerrainParamsUBO);

    VkWriteDescriptorSet writes[8] = {};
    for (int i = 0; i < 7; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }
    writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet = set;
    writes[7].dstBinding = 7;
    writes[7].descriptorCount = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[7].pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(vkCtx->getDevice(), 8, writes, 0, nullptr);
}

void TerrainRenderer::render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, const Camera& camera) {
    if (chunks.empty() || !pipeline) {
        static int emptyLog = 0;
        if (++emptyLog <= 3)
            LOG_WARNING("TerrainRenderer::render: chunks=", chunks.size(), " pipeline=", (pipeline != VK_NULL_HANDLE));
        return;
    }

    // One-time diagnostic: log chunk nearest to camera
    static bool loggedDiag = false;
    if (!loggedDiag && !chunks.empty()) {
        loggedDiag = true;
        glm::vec3 cam = camera.getPosition();
        // Find chunk nearest to camera
        const TerrainChunkGPU* nearest = nullptr;
        float nearestDist = std::numeric_limits<float>::max();
        for (const auto& ch : chunks) {
            float dx = ch.boundingSphereCenter.x - cam.x;
            float dy = ch.boundingSphereCenter.y - cam.y;
            float dz = ch.boundingSphereCenter.z - cam.z;
            float d = dx*dx + dy*dy + dz*dz;
            if (d < nearestDist) { nearestDist = d; nearest = &ch; }
        }
        if (nearest) {
            float d2d = std::sqrt((nearest->boundingSphereCenter.x-cam.x)*(nearest->boundingSphereCenter.x-cam.x) +
                                  (nearest->boundingSphereCenter.y-cam.y)*(nearest->boundingSphereCenter.y-cam.y));
            LOG_INFO("Terrain diag: chunks=", chunks.size(),
                     " cam=(", cam.x, ",", cam.y, ",", cam.z, ")",
                     " nearest_center=(", nearest->boundingSphereCenter.x, ",", nearest->boundingSphereCenter.y, ",", nearest->boundingSphereCenter.z, ")",
                     " dist2d=", d2d, " dist3d=", std::sqrt(nearestDist),
                     " radius=", nearest->boundingSphereRadius,
                     " matSet=", (nearest->materialSet != VK_NULL_HANDLE ? "ok" : "NULL"));
        }
    }

    VkPipeline activePipeline = (wireframe && wireframePipeline) ? wireframePipeline : pipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                             0, 1, &perFrameSet, 0, nullptr);

    GPUPushConstants push{};
    push.model = glm::mat4(1.0f);
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                        0, sizeof(GPUPushConstants), &push);

    Frustum frustum;
    if (frustumCullingEnabled) {
        glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
        frustum.extractFromMatrix(viewProj);
    }

    glm::vec3 camPos = camera.getPosition();
    const float maxTerrainDistSq = maxViewDistance_ * maxViewDistance_;

    renderedChunks = 0;
    culledChunks = 0;

    // Use mega VB + IB when available.
    // Bind mega buffers once, then use direct draws with base vertex/index offsets.
    const bool useMegaBuffers = (megaVB_ && megaIB_);
    if (useMegaBuffers) {
        VkDeviceSize megaOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &megaVB_, &megaOffset);
        vkCmdBindIndexBuffer(cmd, megaIB_, 0, VK_INDEX_TYPE_UINT32);
    }

    for (const auto& chunk : chunks) {
        if (!chunk.isValid() || !chunk.materialSet) continue;

        float dx = chunk.boundingSphereCenter.x - camPos.x;
        float dy = chunk.boundingSphereCenter.y - camPos.y;
        float distSq = dx * dx + dy * dy;
        if (distSq > maxTerrainDistSq) {
            culledChunks++;
            continue;
        }

        if (frustumCullingEnabled && !isChunkVisible(chunk, frustum)) {
            culledChunks++;
            continue;
        }

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                 1, 1, &chunk.materialSet, 0, nullptr);

        if (useMegaBuffers && chunk.megaBaseVertex >= 0) {
            // Direct draw from mega buffer — single VB/IB already bound
            vkCmdDrawIndexed(cmd, chunk.indexCount, 1,
                             chunk.megaFirstIndex, chunk.megaBaseVertex, 0);
        } else {
            // Fallback: per-chunk VB/IB bind + direct draw
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &chunk.vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, chunk.indexCount, 1, 0, 0, 0);
        }
        renderedChunks++;
    }

}

bool TerrainRenderer::initializeShadow(VkRenderPass shadowRenderPass) {
    if (!vkCtx || shadowRenderPass == VK_NULL_HANDLE) return false;
    if (shadowPipeline_ != VK_NULL_HANDLE) return true;  // already initialised
    VkDevice device = vkCtx->getDevice();
    VmaAllocator allocator = vkCtx->getAllocator();

    // ShadowParams UBO — terrain uses no bones, no texture, no alpha test
    VkBufferCreateInfo bufCI{};
    bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCI.size = sizeof(ShadowParamsUBO);
    bufCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocInfo{};
    if (vmaCreateBuffer(allocator, &bufCI, &allocCI,
            &shadowParamsUBO_, &shadowParamsAlloc_, &allocInfo) != VK_SUCCESS) {
        LOG_ERROR("TerrainRenderer: failed to create shadow params UBO");
        return false;
    }
    ShadowParamsUBO defaultParams{};
    std::memcpy(allocInfo.pMappedData, &defaultParams, sizeof(defaultParams));

    // Descriptor set layout: binding 0 = combined sampler (unused), binding 1 = ShadowParams UBO
    VkDescriptorSetLayoutBinding layoutBindings[2]{};
    layoutBindings[0].binding = 0;
    layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[0].descriptorCount = 1;
    layoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.bindingCount = 2;
    layoutCI.pBindings = layoutBindings;
    if (vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &shadowParamsLayout_) != VK_SUCCESS) {
        LOG_ERROR("TerrainRenderer: failed to create shadow params set layout");
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets = 1;
    poolCI.poolSizeCount = 2;
    poolCI.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolCI, nullptr, &shadowParamsPool_) != VK_SUCCESS) {
        LOG_ERROR("TerrainRenderer: failed to create shadow params pool");
        return false;
    }

    VkDescriptorSetAllocateInfo setAlloc{};
    setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAlloc.descriptorPool = shadowParamsPool_;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts = &shadowParamsLayout_;
    if (vkAllocateDescriptorSets(device, &setAlloc, &shadowParamsSet_) != VK_SUCCESS) {
        LOG_ERROR("TerrainRenderer: failed to allocate shadow params set");
        return false;
    }

    // Write descriptors — sampler uses whiteTexture as dummy (useTexture=0 so never sampled)
    VkDescriptorBufferInfo bufInfo{ shadowParamsUBO_, 0, sizeof(ShadowParamsUBO) };
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView   = whiteTexture->getImageView();
    imgInfo.sampler     = whiteTexture->getSampler();

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = shadowParamsSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &imgInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = shadowParamsSet_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &bufInfo;
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    // Pipeline layout: set 0 = shadowParamsLayout_, push 128 bytes (lightSpaceMatrix + model)
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset = 0;
    pc.size = 128;
    shadowPipelineLayout_ = createPipelineLayout(device, {shadowParamsLayout_}, {pc});
    if (!shadowPipelineLayout_) {
        LOG_ERROR("TerrainRenderer: failed to create shadow pipeline layout");
        return false;
    }

    VkShaderModule vertShader, fragShader;
    if (!vertShader.loadFromFile(device, "assets/shaders/shadow.vert.spv")) {
        LOG_ERROR("TerrainRenderer: failed to load shadow vertex shader");
        return false;
    }
    if (!fragShader.loadFromFile(device, "assets/shaders/shadow.frag.spv")) {
        LOG_ERROR("TerrainRenderer: failed to load shadow fragment shader");
        vertShader.destroy();
        return false;
    }

    // Terrain vertex layout: pos(0,off0) normal(1,off12) texCoord(2,off24) layerUV(3,off32)
    // stride = sizeof(TerrainVertex) = 44 bytes
    // Shadow shader expects: aPos(loc0), aTexCoord(loc1), aBoneWeights(loc2), aBoneIndicesF(loc3)
    // Alias unused bone attrs to position (offset 0); useBones=0 so they are never read.
    const uint32_t stride = static_cast<uint32_t>(sizeof(pipeline::TerrainVertex));
    VkVertexInputBindingDescription vertBind{};
    vertBind.binding = 0;
    vertBind.stride = stride;
    vertBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputAttributeDescription> vertAttrs = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0},   // aPos         -> position
        {1, 0, VK_FORMAT_R32G32_SFLOAT,       24},  // aTexCoord    -> texCoord (unused)
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,  0},  // aBoneWeights -> position (unused)
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT,  0},  // aBoneIndices -> position (unused)
    };

    shadowPipeline_ = PipelineBuilder()
        .setShaders(vertShader.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                    fragShader.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({vertBind}, vertAttrs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setDepthTest(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setDepthBias(0.05f, 0.20f)
        .setNoColorAttachment()
        .setLayout(shadowPipelineLayout_)
        .setRenderPass(shadowRenderPass)
        .setDynamicStates({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR})
        .build(device, vkCtx->getPipelineCache());

    vertShader.destroy();
    fragShader.destroy();

    if (!shadowPipeline_) {
        LOG_ERROR("TerrainRenderer: failed to create shadow pipeline");
        return false;
    }
    LOG_INFO("TerrainRenderer shadow pipeline initialized");
    return true;
}

void TerrainRenderer::renderShadow(VkCommandBuffer cmd, const glm::mat4& lightSpaceMatrix,
                                    const glm::vec3& shadowCenter, float shadowRadius) {
    if (!shadowPipeline_ || !shadowParamsSet_) return;
    if (chunks.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout_,
        0, 1, &shadowParamsSet_, 0, nullptr);

    // Identity model matrix — terrain vertices are already in world space
    static const glm::mat4 identity(1.0f);
    ShadowPush push{ lightSpaceMatrix, identity };
    vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, 128, &push);

    // Bind mega buffers once for shadow pass (same as opaque)
    const bool useMegaShadow = (megaVB_ && megaIB_);
    if (useMegaShadow) {
        VkDeviceSize megaOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &megaVB_, &megaOffset);
        vkCmdBindIndexBuffer(cmd, megaIB_, 0, VK_INDEX_TYPE_UINT32);
    }

    for (const auto& chunk : chunks) {
        if (!chunk.isValid()) continue;

        // Sphere-cull chunk against shadow region
        glm::vec3 diff = chunk.boundingSphereCenter - shadowCenter;
        float distSq = glm::dot(diff, diff);
        float combinedRadius = shadowRadius + chunk.boundingSphereRadius;
        if (distSq > combinedRadius * combinedRadius) continue;

        if (useMegaShadow && chunk.megaBaseVertex >= 0) {
            vkCmdDrawIndexed(cmd, chunk.indexCount, 1, chunk.megaFirstIndex, chunk.megaBaseVertex, 0);
        } else {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &chunk.vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, chunk.indexCount, 1, 0, 0, 0);
        }
    }
}

void TerrainRenderer::removeTile(int tileX, int tileY) {
    int removed = 0;
    auto it = chunks.begin();
    while (it != chunks.end()) {
        if (it->tileX == tileX && it->tileY == tileY) {
            destroyChunkGPU(*it);
            it = chunks.erase(it);
            removed++;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        LOG_DEBUG("Removed ", removed, " terrain chunks for tile [", tileX, ",", tileY, "]");
    }
}

void TerrainRenderer::clear() {
    if (!vkCtx) return;

    for (auto& chunk : chunks) {
        destroyChunkGPU(chunk);
    }
    chunks.clear();
    renderedChunks = 0;
}

void TerrainRenderer::destroyChunkGPU(TerrainChunkGPU& chunk) {
    if (!vkCtx) return;

    VkDevice device = vkCtx->getDevice();
    VmaAllocator allocator = vkCtx->getAllocator();

    // These resources may still be referenced by in-flight command buffers from
    // previous frames. Defer actual destruction until this frame slot is safe.
    ::VkBuffer vertexBuffer = chunk.vertexBuffer;
    VmaAllocation vertexAlloc = chunk.vertexAlloc;
    ::VkBuffer indexBuffer = chunk.indexBuffer;
    VmaAllocation indexAlloc = chunk.indexAlloc;
    ::VkBuffer paramsUBO = chunk.paramsUBO;
    VmaAllocation paramsAlloc = chunk.paramsAlloc;
    VkDescriptorPool pool = materialDescPool;
    VkDescriptorSet materialSet = chunk.materialSet;

    std::vector<VkTexture*> alphaTextures;
    alphaTextures.reserve(chunk.ownedAlphaTextures.size());
    for (auto& tex : chunk.ownedAlphaTextures) {
        alphaTextures.push_back(tex.release());
    }

    chunk.vertexBuffer = VK_NULL_HANDLE;
    chunk.vertexAlloc = VK_NULL_HANDLE;
    chunk.indexBuffer = VK_NULL_HANDLE;
    chunk.indexAlloc = VK_NULL_HANDLE;
    chunk.paramsUBO = VK_NULL_HANDLE;
    chunk.paramsAlloc = VK_NULL_HANDLE;
    chunk.materialSet = VK_NULL_HANDLE;
    chunk.ownedAlphaTextures.clear();

    vkCtx->deferAfterAllFrameFences([device, allocator, vertexBuffer, vertexAlloc, indexBuffer, indexAlloc,
                                     paramsUBO, paramsAlloc, pool, materialSet, alphaTextures]() {
        if (vertexBuffer) {
            AllocatedBuffer ab{}; ab.buffer = vertexBuffer; ab.allocation = vertexAlloc;
            destroyBuffer(allocator, ab);
        }
        if (indexBuffer) {
            AllocatedBuffer ab{}; ab.buffer = indexBuffer; ab.allocation = indexAlloc;
            destroyBuffer(allocator, ab);
        }
        if (paramsUBO) {
            AllocatedBuffer ab{}; ab.buffer = paramsUBO; ab.allocation = paramsAlloc;
            destroyBuffer(allocator, ab);
        }
        if (materialSet && pool) {
            VkDescriptorSet set = materialSet;
            vkFreeDescriptorSets(device, pool, 1, &set);
        }
        for (VkTexture* tex : alphaTextures) {
            if (!tex) continue;
            tex->destroy(device, allocator);
            delete tex;
        }
    });
}

int TerrainRenderer::getTriangleCount() const {
    int total = 0;
    for (const auto& chunk : chunks) {
        total += chunk.indexCount / 3;
    }
    return total;
}

bool TerrainRenderer::isChunkVisible(const TerrainChunkGPU& chunk, const Frustum& frustum) {
    return frustum.intersectsSphere(chunk.boundingSphereCenter, chunk.boundingSphereRadius);
}

void TerrainRenderer::calculateBoundingSphere(TerrainChunkGPU& gpuChunk,
                                                const pipeline::ChunkMesh& meshChunk) {
    if (meshChunk.vertices.empty()) {
        gpuChunk.boundingSphereRadius = 0.0f;
        gpuChunk.boundingSphereCenter = glm::vec3(0.0f);
        return;
    }

    glm::vec3 min(std::numeric_limits<float>::max());
    glm::vec3 max(std::numeric_limits<float>::lowest());

    for (const auto& vertex : meshChunk.vertices) {
        glm::vec3 pos(vertex.position[0], vertex.position[1], vertex.position[2]);
        min = glm::min(min, pos);
        max = glm::max(max, pos);
    }

    gpuChunk.boundingSphereCenter = (min + max) * 0.5f;

    float maxDistSq = 0.0f;
    for (const auto& vertex : meshChunk.vertices) {
        glm::vec3 pos(vertex.position[0], vertex.position[1], vertex.position[2]);
        glm::vec3 diff = pos - gpuChunk.boundingSphereCenter;
        float distSq = glm::dot(diff, diff);
        maxDistSq = std::max(maxDistSq, distSq);
    }

    gpuChunk.boundingSphereRadius = std::sqrt(maxDistSq);
}

} // namespace rendering
} // namespace wowee
