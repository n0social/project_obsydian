#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <atomic>
#include <chrono>

namespace wowee {
namespace rendering {

static constexpr uint32_t MAX_LOCAL_LIGHTS = 64;

// Must match the PerFrame UBO layout in all shaders (std140 alignment)
struct GPUPerFrameData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 lightSpaceMatrix;
    glm::vec4 lightDir;       // xyz = direction, w = unused
    glm::vec4 lightColor;     // xyz = color, w = unused
    glm::vec4 ambientColor;   // xyz = color, w = unused
    glm::vec4 viewPos;        // xyz = camera pos, w = unused
    glm::vec4 fogColor;       // xyz = color, w = unused
    glm::vec4 fogParams;      // x = fogStart, y = fogEnd, z = time, w = unused
    glm::vec4 shadowParams;   // x = enabled(0/1), y = strength, z = unused, w = unused
    glm::vec4 localLightPosRadius[MAX_LOCAL_LIGHTS];       // xyz = position, w = radius
    glm::vec4 localLightColorIntensity[MAX_LOCAL_LIGHTS];  // rgb = color, w = intensity
    glm::ivec4 localLightMeta;                             // x = active light count
};

// Push constants for the model matrix (most common case)
struct GPUPushConstants {
    glm::mat4 model;
};

// Push constants for shadow rendering passes
struct ShadowPush {
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
};

// Uniform buffer for shadow rendering parameters (matches shader std140 layout)
struct ShadowParamsUBO {
    int32_t useBones;
    int32_t useTexture;
    int32_t alphaTest;
    int32_t foliageSway;
    float windTime;
    float foliageMotionDamp;
};

// Timer utility for performance profiling queries.
// Uses atomics because floor-height queries are dispatched on async threads
// from CameraController while the main thread may read the counters.
struct QueryTimer {
    std::atomic<double>* totalMs = nullptr;
    std::atomic<uint32_t>* callCount = nullptr;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    QueryTimer(std::atomic<double>* total, std::atomic<uint32_t>* calls)
        : totalMs(total), callCount(calls) {}
    ~QueryTimer() {
        if (callCount) {
            callCount->fetch_add(1, std::memory_order_relaxed);
        }
        if (totalMs) {
            auto end = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            // Relaxed is fine for diagnostics — exact ordering doesn't matter.
            double old = totalMs->load(std::memory_order_relaxed);
            while (!totalMs->compare_exchange_weak(old, old + ms, std::memory_order_relaxed)) {}
        }
    }
};

} // namespace rendering
} // namespace wowee
