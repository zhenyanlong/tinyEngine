#include "SceneManager.hpp"
// Workaround: tinyobjloader's embedded fast_float library marks SIMD-using
// helpers as `FASTFLOAT_CONSTEXPR20 = constexpr` whenever the host stdlib
// reports __cpp_lib_constexpr_algorithms >= 201806L. MSVC 19.43+ in C++20
// mode satisfies that test but rejects the resulting constexpr functions
// because they call non-constexpr byteswap/SIMD intrinsics. Pre-poison the
// fast_float feature-detect include guard and supply non-constexpr stand-ins
// before pulling tiny_obj_loader.h in.
#define FASTFLOAT_CONSTEXPR_FEATURE_DETECT_H
#define FASTFLOAT_CONSTEXPR14
#define FASTFLOAT_HAS_BIT_CAST 0
#define FASTFLOAT_HAS_IS_CONSTANT_EVALUATED 0
#define FASTFLOAT_IF_CONSTEXPR17(x) if (x)
#define FASTFLOAT_CONSTEXPR20
#define FASTFLOAT_IS_CONSTEXPR 0
#define FASTFLOAT_DETAIL_MUST_DEFINE_CONSTEXPR_VARIABLE 0
#include <tiny_obj_loader.h>
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <string>

namespace {

bool endsWithIgnoreCase(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - n + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

static std::string sanitize(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (char c : in) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') out += c;
        else out += '_';
    }
    if (out.empty()) out = "mat";
    return out;
}

static std::string resolveGltfImageRel(const cgltf_image* img,
                                const std::filesystem::path& gltfDir,
                                const std::filesystem::path& resRoot)
{
    if (!img || !img->uri) return {};
    const std::string uri = img->uri;
    if (uri.rfind("data:", 0) == 0) return {};
    namespace fs = std::filesystem;
    fs::path abs = (gltfDir / uri).lexically_normal();
    std::error_code ec;
    fs::path rel = fs::relative(abs, resRoot, ec);
    if (ec || rel.empty()) return {};
    return rel.generic_string();
}

static std::string dumpGltfMaterialAstImpl(const cgltf_material& mat,
                                 const std::string& baseName, int primIndex,
                                 const std::filesystem::path& gltfDir,
                                 const std::filesystem::path& resRoot,
                                 const std::string& materialSubFolder = "")
{
    namespace fs = std::filesystem;
    const std::string matName = mat.name ? sanitize(mat.name)
                                         : ("prim" + std::to_string(primIndex));
    const std::string folderPrefix = materialSubFolder.empty()
        ? std::string("materials/")
        : std::string("materials/") + materialSubFolder + "/";
    const std::string fileRel = folderPrefix + sanitize(baseName) + "_" + matName + ".ast";
    const fs::path    fileAbs = resRoot / fileRel;

    std::error_code ec;
    fs::create_directories(fileAbs.parent_path(), ec);

    std::string albedoRel, normalRel, mrRel, aoRel, emissiveRel;
    glm::vec4   baseColor(1.f);
    float       metallic  = 0.f, roughness = 1.f;
    glm::vec4   emissive(0.f, 0.f, 0.f, 1.f);
    float       emissiveIntensity = 0.f;

    if (mat.has_pbr_metallic_roughness) {
        const auto& m = mat.pbr_metallic_roughness;
        baseColor = { m.base_color_factor[0], m.base_color_factor[1],
                      m.base_color_factor[2], m.base_color_factor[3] };
        metallic  = m.metallic_factor;
        roughness = m.roughness_factor;
        if (m.base_color_texture.texture && m.base_color_texture.texture->image)
            albedoRel = resolveGltfImageRel(m.base_color_texture.texture->image, gltfDir, resRoot);
        if (m.metallic_roughness_texture.texture && m.metallic_roughness_texture.texture->image)
            mrRel    = resolveGltfImageRel(m.metallic_roughness_texture.texture->image, gltfDir, resRoot);
    }
    if (mat.normal_texture.texture && mat.normal_texture.texture->image)
        normalRel    = resolveGltfImageRel(mat.normal_texture.texture->image, gltfDir, resRoot);
    if (mat.occlusion_texture.texture && mat.occlusion_texture.texture->image)
        aoRel        = resolveGltfImageRel(mat.occlusion_texture.texture->image, gltfDir, resRoot);
    if (mat.emissive_texture.texture && mat.emissive_texture.texture->image)
        emissiveRel  = resolveGltfImageRel(mat.emissive_texture.texture->image, gltfDir, resRoot);

    emissive = { mat.emissive_factor[0], mat.emissive_factor[1], mat.emissive_factor[2], 1.f };
    if (emissive.r + emissive.g + emissive.b > 1e-4f || !emissiveRel.empty())
        emissiveIntensity = 1.f;

    auto q = [](const std::string& s) { return std::string("\"") + s + "\""; };
    std::ofstream out(fileAbs);
    if (!out.is_open()) {
        std::cerr << "[glTF] cannot open .ast for write: " << fileAbs << "\n";
        return {};
    }
    out << "{\n";
    out << "  \"name\": " << q(matName) << ",\n";
    out << "  \"type\": \"Mesh\",\n";
    out << "  \"params\": {\n";
    out << "    \"baseColor\": [" << baseColor.r << ", " << baseColor.g << ", "
                                  << baseColor.b << ", " << baseColor.a << "],\n";
    out << "    \"metallic\":  " << metallic  << ",\n";
    out << "    \"roughness\": " << roughness << ",\n";
    out << "    \"emissiveColor\": [" << emissive.r << ", " << emissive.g << ", "
                                       << emissive.b << ", 1.0],\n";
    out << "    \"emissiveIntensity\": " << emissiveIntensity << "\n";
    out << "  },\n";
    out << "  \"textures\": {\n";
    bool first = true;
    auto writeTex = [&](const char* key, const std::string& path) {
        if (path.empty()) return;
        if (!first) out << ",\n";
        out << "    " << q(key) << ": " << q(path);
        first = false;
    };
    writeTex("albedo",            albedoRel);
    writeTex("normal",            normalRel);
    writeTex("metallicRoughness", mrRel);
    writeTex("ao",                aoRel);
    writeTex("emissive",          emissiveRel);
    out << "\n  }\n";
    out << "}\n";
    return fileRel;
}
} // namespace

// ─── Public static: glTF material dump ────────────────────────────────────────

std::string SceneManager::dumpGltfMaterialAst(const void* cgltfMaterial,
                                               const std::string& baseName,
                                               int primIndex,
                                               const std::string& gltfDir,
                                               const std::string& resRoot,
                                               const std::string& materialSubFolder)
{
    const auto& mat = *static_cast<const cgltf_material*>(cgltfMaterial);
    return dumpGltfMaterialAstImpl(mat, baseName, primIndex, gltfDir, resRoot, materialSubFolder);
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void SceneManager::destroyBuf(const VulkanContext& ctx, VkBuffer& buf, VkDeviceMemory& mem)
{
    auto dev = ctx.getDevice();
    if (buf != VK_NULL_HANDLE) { vkDestroyBuffer(dev, buf, nullptr); buf = VK_NULL_HANDLE; }
    if (mem != VK_NULL_HANDLE) { vkFreeMemory(dev, mem, nullptr);    mem = VK_NULL_HANDLE; }
}

// ─── Model Loading ───────────────────────────────────────────────────────────

void SceneManager::loadModel(const std::string& path, const glm::vec3& position,
                              const BufferManager& bufMgr)
{
    // 兼容旧接口：加载到第一个 ModelEntity 槽位
    // 调用方负责在调用前销毁旧 GPU 资源（destroyModelBuffers + vkDeviceWaitIdle）
    if (modelEntities_.empty()) {
        createModelEntity(path, position, bufMgr);
    } else {
        // 替换已有实体 0：loadModelFrom* 会覆盖 vertex/index/subMeshes 并创建新缓冲
        if (endsWithIgnoreCase(path, ".gltf") || endsWithIgnoreCase(path, ".glb"))
            loadModelFromGltf(path, position, bufMgr);
        else
            loadModelFromObj(path, position, bufMgr);
    }
}

uint64_t SceneManager::createModelEntity(const std::string& path, const glm::vec3& position,
                                          const BufferManager& bufMgr)
{
    // 先创建一个空的 ModelEntity 并加入列表，再将数据填入
    static uint64_t nextEntityId = 1000;
    ModelEntity ent;
    ent.entityId    = nextEntityId++;
    ent.transform.position = position;
    ent.displayName = std::filesystem::path(path).stem().string();

    modelEntities_.push_back(std::move(ent));
    ModelEntity& ref = modelEntities_.back();

    if (endsWithIgnoreCase(path, ".gltf") || endsWithIgnoreCase(path, ".glb"))
        loadModelFromGltf(path, position, bufMgr);
    else
        loadModelFromObj(path, position, bufMgr);

    return ref.entityId;
}

void SceneManager::loadModelFromObj(const std::string& path, const glm::vec3& position,
                                     const BufferManager& bufMgr)
{
    // 填充最后一个实体
    ModelEntity& ent = modelEntities_.back();
    ent.transform.position = position;

    // OBJ 不支持骨骼动画
    skeleton_.reset();
    animationClips_.clear();

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str()))
        throw std::runtime_error("Failed to load model: " + path + "\n" + err + "\n" + warn);

    ent.vertices.clear();
    ent.indices.clear();
    ent.subMeshes.clear();
    ent.subMeshMaterials.clear();
    ent.autoAstPaths.clear();
    modelLocalBoundsMin_ = glm::vec3(FLT_MAX);
    modelLocalBoundsMax_ = glm::vec3(-FLT_MAX);

    std::unordered_map<Vertex, uint32_t, VertexHash> unique;
    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            Vertex v{};
            v.pos = { attrib.vertices[3 * idx.vertex_index + 0],
                      attrib.vertices[3 * idx.vertex_index + 1],
                      attrib.vertices[3 * idx.vertex_index + 2] };
            if (idx.texcoord_index >= 0) {
                v.texCoord = { attrib.texcoords[2 * idx.texcoord_index + 0],
                               1.0f - attrib.texcoords[2 * idx.texcoord_index + 1] };
            }
            if (idx.normal_index >= 0 && !attrib.normals.empty()) {
                v.normal = { attrib.normals[3 * idx.normal_index + 0],
                             attrib.normals[3 * idx.normal_index + 1],
                             attrib.normals[3 * idx.normal_index + 2] };
            }
            v.color = { 1.0f, 1.0f, 1.0f };

            modelLocalBoundsMin_ = glm::min(modelLocalBoundsMin_, v.pos);
            modelLocalBoundsMax_ = glm::max(modelLocalBoundsMax_, v.pos);

            if (!unique.count(v)) {
                unique[v] = static_cast<uint32_t>(ent.vertices.size());
                ent.vertices.push_back(v);
            }
            ent.indices.push_back(unique[v]);
        }
    }

    SubMesh sm{};
    sm.indexOffset  = 0;
    sm.indexCount   = static_cast<uint32_t>(ent.indices.size());
    sm.materialSlot = -1;
    ent.subMeshes.push_back(sm);

    bufMgr.createVertexBuffer(ent.vertices, ent.vertexBuffer, ent.vertexMemory);
    bufMgr.createIndexBuffer(ent.indices, ent.indexBuffer, ent.indexMemory);
    ent.indexCount = static_cast<uint32_t>(ent.indices.size());
}

void SceneManager::loadModelFromGltf(const std::string& path, const glm::vec3& position,
                                      const BufferManager& bufMgr)
{
    ModelEntity& ent = modelEntities_.back();
    ent.transform.position = position;

    cgltf_options opts{};
    cgltf_data*   data = nullptr;
    cgltf_result  r = cgltf_parse_file(&opts, path.c_str(), &data);
    if (r != cgltf_result_success || !data) {
        throw std::runtime_error("Failed to parse glTF: " + path);
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("Failed to load glTF buffers: " + path);
    }

    namespace fs = std::filesystem;
    const fs::path gltfPath = fs::path(path);
    const fs::path gltfDir  = gltfPath.parent_path();
    // 从模型文件路径向上查找 res/ 目录，不依赖 CWD
    fs::path resRoot = gltfPath.parent_path();  // 从 gltf 所在目录开始
    while (resRoot.has_parent_path() && resRoot.filename() != "res")
        resRoot = resRoot.parent_path();
    if (resRoot.filename() != "res")
        resRoot = fs::absolute(fs::path("res")); // fallback
    const std::string baseName = gltfPath.stem().string();

    skeleton_.reset();
    animationClips_.clear();

    // 收集所有 skin 中的全部 joint 节点，合并为统一骨架
    std::unordered_map<const cgltf_node*, int> globalNodeToBone;
    if (data->skins_count > 0) {
        auto skel = std::make_shared<Skeleton>();
        std::vector<const cgltf_node*> boneNodes;

        for (cgltf_size si = 0; si < data->skins_count; ++si) {
            const cgltf_skin& skin = data->skins[si];
            for (cgltf_size j = 0; j < skin.joints_count; ++j) {
                const cgltf_node* node = skin.joints[j];
                if (globalNodeToBone.count(node)) continue; // 已包含
                const int boneIndex = static_cast<int>(skel->bones.size());
                globalNodeToBone[node] = boneIndex;

                Bone bone;
                bone.name = node->name ? node->name : ("bone_" + std::to_string(si) + "_" + std::to_string(j));
                skel->boneNameToIndex[bone.name] = boneIndex;
                bone.parentIndex = -1;

                float local[16];
                cgltf_node_transform_local(node, local);
                bone.localBindTransform = glm::make_mat4(local);
                float world[16];
                cgltf_node_transform_world(node, world);
                bone.globalBindTransform = glm::make_mat4(world);

                if (skin.inverse_bind_matrices) {
                    float ibm[16];
                    cgltf_accessor_read_float(skin.inverse_bind_matrices, j, ibm, 16);
                    bone.inverseBindMatrix = glm::make_mat4(ibm);
                } else {
                    bone.inverseBindMatrix = glm::mat4(1.f);
                }

                skel->bones.push_back(std::move(bone));
                boneNodes.push_back(node);
            }
        }

        for (size_t i = 0; i < boneNodes.size(); ++i) {
            const cgltf_node* parent = boneNodes[i] ? boneNodes[i]->parent : nullptr;
            while (parent) {
                auto it = globalNodeToBone.find(parent);
                if (it != globalNodeToBone.end()) {
                    skel->bones[i].parentIndex = it->second;
                    break;
                }
                parent = parent->parent;
            }
        }

        const int totalBones = static_cast<int>(skel->bones.size());
        for (cgltf_size si = 0; si < data->skins_count; ++si) {
            if (data->skins[si].joints_count > static_cast<cgltf_size>(kMaxBones)) {
                std::cerr << "[glTF] skin " << si << " has "
                          << data->skins[si].joints_count
                          << " joints, but GPU skinning supports only "
                          << kMaxBones << " per skin; extra joints will be clamped.\n";
            }
        }

        skeleton_ = skel;
        std::cout << "[glTF] parsed combined skeleton with " << totalBones
                  << " bones from " << data->skins_count << " skin(s)\n";
    }

    // 为每个 cgltf_skin 构建 skin-local-index → global-bone-index 的映射表
    // 用于将 JOINTS_0 中基于皮肤局部的索引重定向到合并骨架
    std::vector<std::vector<int>> skinRemap(data->skins_count);
    for (cgltf_size si = 0; si < data->skins_count; ++si) {
        const cgltf_skin& skin = data->skins[si];
        skinRemap[si].resize(skin.joints_count);
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            auto it = globalNodeToBone.find(skin.joints[j]);
            skinRemap[si][j] = (it != globalNodeToBone.end()) ? it->second : 0;
        }
    }
    if (skeleton_) {
        skeleton_->skinBoneIndices = skinRemap;
    }

    std::unordered_map<const cgltf_mesh*, int> meshToSkin;
    bool warnedSharedMeshMultipleSkins = false;
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh || !node.skin) continue;

        const int skinIndex = static_cast<int>(node.skin - data->skins);
        if (skinIndex < 0 || skinIndex >= static_cast<int>(data->skins_count))
            continue;

        auto [it, inserted] = meshToSkin.emplace(node.mesh, skinIndex);
        if (!inserted && it->second != skinIndex && !warnedSharedMeshMultipleSkins) {
            std::cerr << "[glTF] mesh is referenced by multiple skins; "
                      << "using the first skin for merged geometry\n";
            warnedSharedMeshMultipleSkins = true;
        }
    }

    ent.vertices.clear();
    ent.indices.clear();
    ent.subMeshes.clear();
    ent.subMeshMaterials.clear();
    ent.autoAstPaths.clear();
    modelLocalBoundsMin_ = glm::vec3(FLT_MAX);
    modelLocalBoundsMax_ = glm::vec3(-FLT_MAX);

    int globalPrimIndex = 0;
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi, ++globalPrimIndex) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles || !prim.indices) continue;

            const cgltf_accessor* posAcc = nullptr;
            const cgltf_accessor* uvAcc  = nullptr;
            const cgltf_accessor* nrmAcc = nullptr;
            const cgltf_accessor* tanAcc = nullptr;
            const cgltf_accessor* jntAcc = nullptr;
            const cgltf_accessor* wgtAcc = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& at = prim.attributes[a];
                if      (at.type == cgltf_attribute_type_position && !posAcc) posAcc = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && !uvAcc)  uvAcc  = at.data;
                else if (at.type == cgltf_attribute_type_normal   && !nrmAcc) nrmAcc = at.data;
                else if (at.type == cgltf_attribute_type_tangent  && !tanAcc) tanAcc = at.data;
                else if (at.type == cgltf_attribute_type_joints   && !jntAcc) jntAcc = at.data;
                else if (at.type == cgltf_attribute_type_weights  && !wgtAcc) wgtAcc = at.data;
            }
            if (!posAcc) continue;

            const uint32_t baseVertex = static_cast<uint32_t>(ent.vertices.size());
            const cgltf_size vcount = posAcc->count;
            ent.vertices.reserve(ent.vertices.size() + vcount);
            const int boneLimit = skeleton_
                ? std::min(static_cast<int>(skeleton_->bones.size()), kMaxBones)
                : 0;
            bool warnedBoneIndexOutOfRange = false;

            // 检测当前 primitive 使用的是哪个 skin（对多 skin 模型的自动匹配）
            int primSkinIdx = 0;
            if (auto it = meshToSkin.find(&mesh); it != meshToSkin.end()) {
                primSkinIdx = it->second;
            } else if (skeleton_ && jntAcc && wgtAcc && skinRemap.size() > 1) {
                std::cerr << "[glTF] primitive has skinning attributes but no node skin binding for "
                          << baseName << "; falling back to skin 0\n";
            }
            const std::vector<int> usedRemap = skinRemap.empty() ? std::vector<int>{} : skinRemap[primSkinIdx];
            const int skinBoneLimit = !usedRemap.empty()
                ? std::min(static_cast<int>(usedRemap.size()), kMaxBones)
                : boneLimit;

            auto clampJointIndex = [&](int joint) -> int {
                if (joint < 0 || joint >= skinBoneLimit) {
                    if (!warnedBoneIndexOutOfRange) {
                        std::cerr << "[glTF] joint index exceeds supported bone range for "
                                  << baseName << "; clamping to bone 0\n";
                        warnedBoneIndexOutOfRange = true;
                    }
                    return 0;
                }
                return joint;
            };

            // 读取 JOINTS_0 的辅助 lambda（cgltf 无内置整数读取函数）
            auto readJoints = [&](const cgltf_accessor* acc, cgltf_size idx) -> glm::ivec4 {
                if (!acc || idx >= acc->count || !acc->buffer_view || boneLimit <= 0) return {0, 0, 0, 0};
                cgltf_uint joints[4]{};
                if (!cgltf_accessor_read_uint(acc, idx, joints, 4)) return {0, 0, 0, 0};
                glm::ivec4 r(static_cast<int>(joints[0]), static_cast<int>(joints[1]),
                             static_cast<int>(joints[2]), static_cast<int>(joints[3]));
                // 将 skin-local joint index 重映射到合并骨架的 bone index
                for (int c = 0; c < 4; ++c) {
                    r[c] = clampJointIndex(r[c]);
                }
                return r;
            };
            const bool hasSkinData = (skeleton_ != nullptr) && jntAcc && wgtAcc;

            for (cgltf_size i = 0; i < vcount; ++i) {
                Vertex v{};
                float p[3]{};
                cgltf_accessor_read_float(posAcc, i, p, 3);
                v.pos      = { p[0], p[1], p[2] };
                v.color    = { 1.f, 1.f, 1.f };
                v.texCoord = { 0.f, 0.f };
                if (uvAcc) {
                    float uv[2]{};
                    cgltf_accessor_read_float(uvAcc, i, uv, 2);
                    v.texCoord = { uv[0], uv[1] };
                }
                if (nrmAcc) {
                    float n[3]{};
                    cgltf_accessor_read_float(nrmAcc, i, n, 3);
                    v.normal = { n[0], n[1], n[2] };
                }
                if (tanAcc) {
                    float t[4]{};
                    cgltf_accessor_read_float(tanAcc, i, t, 4);
                    v.tangent = { t[0], t[1], t[2], t[3] };
                }
                if (hasSkinData) {
                    v.boneIndices = readJoints(jntAcc, i);
                    cgltf_accessor_read_float(wgtAcc, i, glm::value_ptr(v.boneWeights), 4);
                    const float weightSum = v.boneWeights.x + v.boneWeights.y
                                          + v.boneWeights.z + v.boneWeights.w;
                    if (weightSum > 1e-6f)
                        v.boneWeights /= weightSum;
                    else
                        v.boneWeights = {1.f, 0.f, 0.f, 0.f};
                }
                ent.vertices.push_back(v);
                modelLocalBoundsMin_ = glm::min(modelLocalBoundsMin_, v.pos);
                modelLocalBoundsMax_ = glm::max(modelLocalBoundsMax_, v.pos);
            }

            if (hasSkinData) {
                ent.hasSkin_ = true;
                std::cout << "[glTF] loaded skinning data for " << baseName
                          << " (primitive " << pi << ")\n";
            }

            const uint32_t indexOffset = static_cast<uint32_t>(ent.indices.size());
            const cgltf_accessor* idxAcc = prim.indices;
            ent.indices.reserve(ent.indices.size() + idxAcc->count);
            for (cgltf_size i = 0; i < idxAcc->count; ++i) {
                const cgltf_size srcIdx = cgltf_accessor_read_index(idxAcc, i);
                ent.indices.push_back(baseVertex + static_cast<uint32_t>(srcIdx));
            }
            const uint32_t indexCount = static_cast<uint32_t>(ent.indices.size()) - indexOffset;

            int slot = -1;
            std::string astRel;
            if (prim.material) {
                // 计算预期的 .ast 文件名
                const std::string matName = prim.material->name
                    ? sanitize(prim.material->name)
                    : ("prim" + std::to_string(globalPrimIndex));
                astRel = "materials/" + sanitize(baseName) + "_" + matName + ".ast";

                // 如果 .ast 已存在（import 时已预生成），跳过重复生成
                if (!fs::exists(resRoot / astRel)) {
                    astRel = dumpGltfMaterialAstImpl(*prim.material, baseName, globalPrimIndex,
                                                     gltfDir, resRoot);
                }
                if (!astRel.empty()) {
                    slot = static_cast<int>(ent.autoAstPaths.size());
                    ent.autoAstPaths.push_back(astRel);
                    ent.subMeshMaterials.push_back(0u);
                }
            }

            SubMesh sm{};
            sm.indexOffset  = indexOffset;
            sm.indexCount   = indexCount;
            sm.skinIndex    = hasSkinData ? primSkinIdx : -1;
            sm.materialSlot = slot;
            ent.subMeshes.push_back(sm);
        }
    }

    // ── Parse animations ─────────────────────────────────────────────────────
    if (skeleton_) {
        for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
            const cgltf_animation& anim = data->animations[ai];
            AnimationClip clip;
            clip.name = anim.name ? anim.name : ("anim_" + std::to_string(ai));
            clip.duration = 0.f;

            for (cgltf_size ci = 0; ci < anim.channels_count; ++ci) {
                const cgltf_animation_channel& ch = anim.channels[ci];
                const cgltf_animation_sampler&   sm = anim.samplers[ch.sampler - anim.samplers];

                // 查找 boneIndex（使用合并后的全局映射）
                auto it = globalNodeToBone.find(ch.target_node);
                if (it == globalNodeToBone.end()) continue;

                AnimChannel ac;
                ac.boneIndex = it->second;

                // 目标属性
                switch (ch.target_path) {
                case cgltf_animation_path_type_translation:
                    ac.target = AnimChannel::Target::Translation; break;
                case cgltf_animation_path_type_rotation:
                    ac.target = AnimChannel::Target::Rotation;    break;
                case cgltf_animation_path_type_scale:
                    ac.target = AnimChannel::Target::Scale;       break;
                default:
                    continue; // 跳过 weights 等
                }

                // 插值类型
                switch (sm.interpolation) {
                case cgltf_interpolation_type_linear:
                    ac.interp = AnimInterpolation::Linear; break;
                case cgltf_interpolation_type_step:
                    ac.interp = AnimInterpolation::Step;   break;
                case cgltf_interpolation_type_cubic_spline:
                    ac.interp = AnimInterpolation::CubicSpline; break;
                default:
                    ac.interp = AnimInterpolation::Linear; break;
                }

                // 读取时间戳
                const cgltf_accessor* inputAcc = sm.input;
                ac.times.resize(inputAcc->count);
                for (cgltf_size k = 0; k < inputAcc->count; ++k) {
                    cgltf_accessor_read_float(inputAcc, k, &ac.times[k], 1);
                }

                // 读取值
                const cgltf_accessor* outputAcc = sm.output;
                const cgltf_size compCount = (ac.target == AnimChannel::Target::Rotation) ? 4 : 3;
                ac.values.resize(outputAcc->count);
                for (cgltf_size k = 0; k < outputAcc->count; ++k) {
                    float tmp[4]{};
                    cgltf_accessor_read_float(outputAcc, k, tmp, compCount);
                    if (compCount == 3) {
                        ac.values[k] = glm::vec4(tmp[0], tmp[1], tmp[2], 0.f);
                    } else {
                        // quaternion xyzw → 存入 vec4
                        ac.values[k] = glm::vec4(tmp[0], tmp[1], tmp[2], tmp[3]);
                    }
                }

                // 更新 clip 时长
                if (!ac.times.empty()) {
                    clip.duration = std::max(clip.duration, ac.times.back());
                }

                clip.channels.push_back(std::move(ac));
            }

            if (!clip.channels.empty()) {
                animationClips_.push_back(std::move(clip));
                std::cout << "[glTF] parsed animation \"" << animationClips_.back().name
                          << "\" with " << animationClips_.back().channels.size()
                          << " channels, duration=" << animationClips_.back().duration << "s\n";
            }
        }
    }

    cgltf_free(data);

    if (ent.vertices.empty() || ent.indices.empty()) {
        throw std::runtime_error("glTF has no usable triangle data: " + path);
    }

    bufMgr.createVertexBuffer(ent.vertices, ent.vertexBuffer, ent.vertexMemory);
    bufMgr.createIndexBuffer(ent.indices, ent.indexBuffer, ent.indexMemory);
    ent.indexCount = static_cast<uint32_t>(ent.indices.size());

    if (!ent.autoAstPaths.empty()) {
        std::cout << "[glTF] dumped " << ent.autoAstPaths.size()
                  << " .ast file(s) under res/materials/ for " << baseName << "\n";
    }
}

// ─── Multi-Entity Operations ─────────────────────────────────────────────────

SceneManager::ModelEntity* SceneManager::getModelEntity(uint64_t id)
{
    for (auto& e : modelEntities_) {
        if (e.entityId == id) return &e;
    }
    return nullptr;
}

bool SceneManager::removeModelEntity(uint64_t entityId, const VulkanContext& ctx)
{
    for (auto it = modelEntities_.begin(); it != modelEntities_.end(); ++it) {
        if (it->entityId == entityId) {
            destroyBuf(ctx, it->vertexBuffer, it->vertexMemory);
            destroyBuf(ctx, it->indexBuffer,  it->indexMemory);
            modelEntities_.erase(it);
            return true;
        }
    }
    return false;
}

void SceneManager::destroyModelBuffers(const VulkanContext& ctx)
{
    // 销毁所有模型实体的 GPU 缓冲
    for (auto& ent : modelEntities_) {
        destroyBuf(ctx, ent.vertexBuffer, ent.vertexMemory);
        destroyBuf(ctx, ent.indexBuffer,  ent.indexMemory);
        ent.indexCount = 0;
    }
}

void SceneManager::setEntityTransform(uint64_t id, const ObjectTransform& t)
{
    if (auto* e = getModelEntity(id)) e->transform = t;
}

void SceneManager::setEntityMaterial(uint64_t id, uint32_t matId)
{
    if (auto* e = getModelEntity(id)) e->materialId = matId;
}

void SceneManager::setEntityVisibility(uint64_t id, bool v)
{
    if (auto* e = getModelEntity(id)) e->visible = v;
}

// ─── Compat Accessors ────────────────────────────────────────────────────────

VkBuffer SceneManager::getVertexBuffer() const
{
    return modelEntities_.empty() ? VK_NULL_HANDLE : modelEntities_[0].vertexBuffer;
}

VkBuffer SceneManager::getIndexBuffer() const
{
    return modelEntities_.empty() ? VK_NULL_HANDLE : modelEntities_[0].indexBuffer;
}

uint32_t SceneManager::getModelIndexCount() const
{
    return modelEntities_.empty() ? 0 : modelEntities_[0].indexCount;
}

glm::vec3 SceneManager::getModelPosition() const
{
    return modelEntities_.empty() ? glm::vec3(0.f) : modelEntities_[0].transform.position;
}

void SceneManager::setModelPosition(const glm::vec3& p)
{
    if (!modelEntities_.empty()) modelEntities_[0].transform.position = p;
}

const std::vector<SceneManager::SubMesh>& SceneManager::getModelSubMeshes() const
{
    static const std::vector<SubMesh> kEmpty;
    return modelEntities_.empty() ? kEmpty : modelEntities_[0].subMeshes;
}

const std::vector<std::string>& SceneManager::getModelAutoAstPaths() const
{
    static const std::vector<std::string> kEmpty;
    return modelEntities_.empty() ? kEmpty : modelEntities_[0].autoAstPaths;
}

uint32_t SceneManager::getModelMaterialId() const
{
    return modelEntities_.empty() ? 0 : modelEntities_[0].materialId;
}

void SceneManager::setModelMaterialId(uint32_t id)
{
    if (!modelEntities_.empty()) modelEntities_[0].materialId = id;
}

uint32_t SceneManager::getModelSubMeshMaterialId(int slot) const
{
    if (modelEntities_.empty()) return 0;
    const auto& smm = modelEntities_[0].subMeshMaterials;
    if (slot < 0 || slot >= (int)smm.size()) return modelEntities_[0].materialId;
    return (smm[slot] != 0u) ? smm[slot] : modelEntities_[0].materialId;
}

void SceneManager::setModelSubMeshMaterialId(int slot, uint32_t id)
{
    if (modelEntities_.empty() || slot < 0) return;
    auto& smm = modelEntities_[0].subMeshMaterials;
    if ((int)smm.size() <= slot) smm.resize(slot + 1, 0u);
    smm[slot] = id;
}

// ─── Box System ──────────────────────────────────────────────────────────────

void SceneManager::createCubeTemplate(const BufferManager& bufMgr)
{
    cubeTemplateVertices_.clear();
    cubeTemplateIndices_.clear();

    const float s = 0.5f;
    const glm::vec3 c = { 1.0f, 1.0f, 1.0f };

    auto addFace = [&](glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3) {
        uint32_t base = static_cast<uint32_t>(cubeTemplateVertices_.size());
        cubeTemplateVertices_.push_back({ v0, c, {0.f, 0.f} });
        cubeTemplateVertices_.push_back({ v1, c, {1.f, 0.f} });
        cubeTemplateVertices_.push_back({ v2, c, {1.f, 1.f} });
        cubeTemplateVertices_.push_back({ v3, c, {0.f, 1.f} });
        cubeTemplateIndices_.insert(cubeTemplateIndices_.end(),
            { base+0, base+1, base+2, base+2, base+3, base+0 });
    };

    addFace({-s,-s,+s},{+s,-s,+s},{+s,+s,+s},{-s,+s,+s});
    addFace({+s,-s,-s},{-s,-s,-s},{-s,+s,-s},{+s,+s,-s});
    addFace({-s,-s,-s},{-s,-s,+s},{-s,+s,+s},{-s,+s,-s});
    addFace({+s,-s,+s},{+s,-s,-s},{+s,+s,-s},{+s,+s,+s});
    addFace({-s,+s,+s},{+s,+s,+s},{+s,+s,-s},{-s,+s,-s});
    addFace({-s,-s,-s},{+s,-s,-s},{+s,-s,+s},{-s,-s,+s});

    bufMgr.createVertexBuffer(cubeTemplateVertices_, cubeVertexBuffer_, cubeVertexMemory_);
    bufMgr.createIndexBuffer(cubeTemplateIndices_, cubeIndexBuffer_, cubeIndexMemory_);
    cubeIndexCount_ = static_cast<uint32_t>(cubeTemplateIndices_.size());
}

void SceneManager::destroy(const VulkanContext& ctx)
{
    destroyBuf(ctx, instanceBuffer_,   instanceMemory_);
    destroyBuf(ctx, cubeIndexBuffer_,  cubeIndexMemory_);
    destroyBuf(ctx, cubeVertexBuffer_, cubeVertexMemory_);
    for (auto& ent : modelEntities_) {
        destroyBuf(ctx, ent.vertexBuffer, ent.vertexMemory);
        destroyBuf(ctx, ent.indexBuffer,  ent.indexMemory);
    }
}

RenderEntityId SceneManager::addBox(const glm::vec3& position,
                                     const VulkanContext& ctx, const BufferManager& bufMgr)
{
    RenderEntityId id = nextId_++;
    boxes_[id] = position;
    rebuildInstanceBuffer(ctx, bufMgr);
    return id;
}

bool SceneManager::removeBox(RenderEntityId id, const VulkanContext& ctx, const BufferManager& bufMgr)
{
    auto it = boxes_.find(id);
    if (it == boxes_.end()) return false;
    boxes_.erase(it);
    boxMaterialIds_.erase(id);
    rebuildInstanceBuffer(ctx, bufMgr);
    return true;
}

glm::vec3 SceneManager::getBoxPosition(RenderEntityId id) const
{
    auto it = boxes_.find(id);
    return (it != boxes_.end()) ? it->second : glm::vec3(0.0f);
}

void SceneManager::setBoxPosition(RenderEntityId id, const glm::vec3& pos,
                                   const VulkanContext& ctx, const BufferManager& bufMgr)
{
    if (!boxes_.count(id)) return;
    boxes_[id] = pos;
    rebuildInstanceBuffer(ctx, bufMgr);
}

uint32_t SceneManager::getBoxMaterialId(RenderEntityId eid) const
{
    auto it = boxMaterialIds_.find(eid);
    return (it != boxMaterialIds_.end()) ? it->second : 0u;
}

void SceneManager::rebuildInstanceBuffer(const VulkanContext& ctx, const BufferManager& bufMgr)
{
    vkDeviceWaitIdle(ctx.getDevice());
    destroyBuf(ctx, instanceBuffer_, instanceMemory_);
    boxRangeEntityIds_.clear();

    std::vector<std::pair<RenderEntityId, glm::vec3>> sorted(boxes_.begin(), boxes_.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    instanceCount_ = static_cast<uint32_t>(sorted.size());
    if (instanceCount_ == 0) return;

    std::vector<InstanceData> instances;
    instances.reserve(instanceCount_);
    for (const auto& kv : sorted) {
        instances.push_back(InstanceData{ glm::translate(glm::mat4(1.f), kv.second) });
        boxRangeEntityIds_.push_back(kv.first);
    }

    bufMgr.createInstanceBuffer(instances.data(),
                                sizeof(InstanceData) * instanceCount_,
                                instanceBuffer_, instanceMemory_);
}
