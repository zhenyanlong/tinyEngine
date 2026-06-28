#pragma once

#include "Animation/AnimationClip.hpp"
#include "Animation/Skeleton.hpp"
#include "vectex.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct FbxImportedTexture {
    std::string filename;
    std::vector<uint8_t> embeddedContent;
};

struct FbxImportedMaterial {
    std::string name;
    glm::vec4 baseColor{1.f};
    float roughness = 0.5f;
    float metallic = 0.f;
    glm::vec4 emissiveColor{0.f, 0.f, 0.f, 1.f};
    FbxImportedTexture albedoTexture;
    FbxImportedTexture normalTexture;
    FbxImportedTexture emissiveTexture;
};

struct FbxImportedSubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    int skinIndex = -1;
    int materialSlot = -1;
};

struct FbxImportResult {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<FbxImportedSubMesh> subMeshes;
    std::vector<FbxImportedMaterial> materials;
    std::shared_ptr<Skeleton> skeleton;
    std::vector<AnimationClip> animationClips;
    glm::vec3 boundsMin{0.f};
    glm::vec3 boundsMax{0.f};
    bool hasSkin = false;
};

/** @brief 使用 ufbx 将 FBX 转换为 tinyEngine 的 CPU 侧模型与动画数据。 */
class FbxImporter {
public:
    static bool load(const std::string& path,
                     FbxImportResult& out,
                     std::string* error = nullptr);
};
