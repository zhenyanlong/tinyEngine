#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <optional>
#include <vector>
#include <cstdint>

#define WIDTH  1280
#define HEIGHT 720

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec4 materialTint;
    alignas(16) glm::vec4 boxMaterialTint;
    alignas(16) glm::vec4 emissive;     // rgb = emissive color, w = intensity
    // PBR additions -- all aligned to 16 bytes (std140-friendly)
    alignas(16) glm::vec4 cameraPos;    // xyz = world camera position, w unused
    alignas(16) glm::vec4 lightDir;     // xyz = world-space *to-light* direction (normalized)
    alignas(16) glm::vec4 lightColor;   // rgb = radiance, a = ambient strength
    alignas(16) glm::vec4 pbrFactors;   // x=metallic, y=roughness, z=ao, w=normalScale
    // Pre-computed matrix cache -- appended at end to preserve old shader binding offsets.
    alignas(16) glm::mat4 viewProj;     // proj * view; shaders use this to save one matrix multiply
    alignas(16) glm::mat4 invView;      // inverse(view); used for lighting vector transforms / env mapping
    alignas(16) glm::mat4 invProj;      // inverse(proj); reserved for depth->world reconstruction in post-FX
};

/**
 * @brief Main pipeline push constants (vertex stage, 128 bytes).
 *
 * Passed per-draw via vkCmdPushConstants with VK_SHADER_STAGE_VERTEX_BIT.
 * Requires VkPhysicalDeviceLimits::maxPushConstantsSize >= 128 (guaranteed on all desktop GPUs).
 */
struct PushConstants {
    glm::mat4 model;        ///< Model-to-world matrix           (offset   0, 64 bytes)
    glm::mat4 normalMatrix; ///< transpose(inverse(mat3(model))) (offset  64, 64 bytes)
};

constexpr int kMaxBones = 256;

/** @brief 骨骼最终变换矩阵 UBO（std140，8192 字节），binding=6，vertex stage */
struct BoneMatricesUBO {
    alignas(16) glm::mat4 bones[kMaxBones];
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    [[nodiscard]] bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

using RenderEntityId = uint64_t;
