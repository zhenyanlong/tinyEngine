
#ifndef VERTEX
#define VERTEX
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>
#include <array>
#include <vulkan/vulkan_core.h>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal{0.f, 0.f, 0.f};   // location=4 (location=3 reserved for InstanceData)
    glm::vec4 tangent{0.f, 0.f, 0.f, 0.f}; // xyz=tangent, w=bitangent sign; (0,0,0,0) means "absent"

    ~Vertex() = default;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    // Note: location=3 is intentionally skipped; it belongs to per-instance data
    // used by the box pipeline. The mesh pipeline simply doesn't bind binding=1
    // and the unused location=3 input is harmless.
    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 4;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, normal);

        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 5;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex, tangent);

        return attributeDescriptions;
    }

    bool operator==(const Vertex& other) const {
        return pos == other.pos && color == other.color && texCoord == other.texCoord
            && normal == other.normal && tangent == other.tangent;
    }
};

struct VertexHash {
    std::size_t operator()(const Vertex& vertex) const {
        auto hash1 = std::hash<glm::vec3>()(vertex.pos);
        auto hash2 = std::hash<glm::vec2>()(vertex.texCoord);
        auto hash3 = std::hash<glm::vec3>()(vertex.color);
        auto hash4 = std::hash<glm::vec3>()(vertex.normal);
        return hash1 ^ (hash2 << 1) ^ (hash3 << 2) ^ (hash4 << 3);
    }
};

/** @brief Per-instance data for GPU instanced box rendering (binding=1, instance rate) */
struct InstanceData {
    glm::mat4 modelMatrix;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 1;
        desc.stride = sizeof(InstanceData);
        desc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attrs{};
        for (uint32_t i = 0; i < 4; ++i) {
            attrs[i].binding  = 1;
            attrs[i].location = 3 + i;
            attrs[i].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
            attrs[i].offset   = sizeof(glm::vec4) * i;
        }
        return attrs;
    }
};

#endif // !VERTEX
