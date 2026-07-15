#include "SceneManager.hpp"
#include "FbxImporter.hpp"
#include "MaterialAssetLoader.hpp"
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
        ? std::string("content/")
        : std::string("content/") + materialSubFolder + "/";
    const std::string fileRel = folderPrefix + sanitize(baseName) + "_" + matName + ".material.ast";
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
    out << "  \"type\": \"Material\",\n";
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

std::string SceneManager::normalizeModelPath(const std::string& path)
{
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (ec)
        p = std::filesystem::absolute(std::filesystem::path(path), ec);
    return ec ? std::filesystem::path(path).lexically_normal().generic_string()
              : p.lexically_normal().generic_string();
}

bool SceneManager::applyCachedModelResource(ModelEntity& ent,
                                            const std::string& key,
                                            const glm::vec3& position)
{
    auto it = modelResourceCache_.find(key);
    if (it == modelResourceCache_.end())
        return false;

    const CachedModelResource& res = it->second;
    ent.transform.position = position;
    ent.vertices.clear();
    ent.indices.clear();
    ent.vertexBuffer = res.vertexBuffer;
    ent.vertexMemory = res.vertexMemory;
    ent.indexBuffer = res.indexBuffer;
    ent.indexMemory = res.indexMemory;
    ent.ownsMeshBuffers = false;
    ent.meshResourceKey = key;
    ent.indexCount = res.indexCount;
    ent.subMeshes = res.subMeshes;
    ent.subMeshMaterials.assign(res.autoAstPaths.size(), 0u);
    ent.autoAstPaths = res.autoAstPaths;
    ent.hasSkin_ = res.hasSkin;
    ent.skeleton = res.skeleton;          // 共享 skeleton（shared_ptr 引用计数）
    ent.animationClips = res.animationClips; // 复制 clips（轻量）
    modelLocalBoundsMin_ = res.boundsMin;
    modelLocalBoundsMax_ = res.boundsMax;
    return true;
}

void SceneManager::cacheModelResourceFromEntity(const std::string& key, ModelEntity& ent)
{
    if (key.empty() || modelResourceCache_.count(key))
        return;
    if (ent.vertexBuffer == VK_NULL_HANDLE || ent.indexBuffer == VK_NULL_HANDLE)
        return;

    CachedModelResource res;
    res.vertexBuffer = ent.vertexBuffer;
    res.vertexMemory = ent.vertexMemory;
    res.indexBuffer = ent.indexBuffer;
    res.indexMemory = ent.indexMemory;
    res.indexCount = ent.indexCount;
    res.subMeshes = ent.subMeshes;
    res.autoAstPaths = ent.autoAstPaths;
    res.hasSkin = ent.hasSkin_;
    res.skeleton = ent.skeleton;
    res.animationClips = ent.animationClips;
    res.boundsMin = modelLocalBoundsMin_;
    res.boundsMax = modelLocalBoundsMax_;
    modelResourceCache_[key] = std::move(res);

    ent.vertices.clear();
    ent.vertices.shrink_to_fit();
    ent.indices.clear();
    ent.indices.shrink_to_fit();
    ent.ownsMeshBuffers = false;
    ent.meshResourceKey = key;
}

void SceneManager::releaseEntityMeshBuffers(const VulkanContext& ctx, ModelEntity& ent)
{
    if (ent.ownsMeshBuffers) {
        destroyBuf(ctx, ent.vertexBuffer, ent.vertexMemory);
        destroyBuf(ctx, ent.indexBuffer, ent.indexMemory);
    } else {
        ent.vertexBuffer = VK_NULL_HANDLE;
        ent.vertexMemory = VK_NULL_HANDLE;
        ent.indexBuffer = VK_NULL_HANDLE;
        ent.indexMemory = VK_NULL_HANDLE;
    }
    ent.vertices.clear();
    ent.vertices.shrink_to_fit();
    ent.indices.clear();
    ent.indices.shrink_to_fit();
    ent.meshResourceKey.clear();
    ent.ownsMeshBuffers = true;
}

void SceneManager::destroyCachedModelResources(const VulkanContext& ctx)
{
    for (auto& [_, res] : modelResourceCache_) {
        destroyBuf(ctx, res.vertexBuffer, res.vertexMemory);
        destroyBuf(ctx, res.indexBuffer, res.indexMemory);
    }
    modelResourceCache_.clear();
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
        else if (endsWithIgnoreCase(path, ".fbx"))
            loadModelFromFbx(path, position, bufMgr);
        else if (endsWithIgnoreCase(path, ".obj"))
            loadModelFromObj(path, position, bufMgr);
        else
            throw std::runtime_error("Unsupported model format: " + path);
    }
}

uint64_t SceneManager::createModelEntity(const std::string& path,
                                          const glm::vec3& position,
                                          const BufferManager& bufMgr,
                                          bool useResourceCache)
{
    // 先创建一个空的 ModelEntity 并加入列表，再将数据填入
    static uint64_t nextEntityId = 1000;
    ModelEntity ent;
    ent.entityId    = nextEntityId++;
    ent.transform.position = position;
    ent.displayName = std::filesystem::path(path).stem().string();

    modelEntities_.push_back(std::move(ent));
    ModelEntity& ref = modelEntities_.back();

    const std::string resourceKey = useResourceCache ? normalizeModelPath(path) : std::string{};
    if (useResourceCache && applyCachedModelResource(ref, resourceKey, position))
        return ref.entityId;

    if (endsWithIgnoreCase(path, ".gltf") || endsWithIgnoreCase(path, ".glb"))
        loadModelFromGltf(path, position, bufMgr);
    else if (endsWithIgnoreCase(path, ".fbx"))
        loadModelFromFbx(path, position, bufMgr);
    else if (endsWithIgnoreCase(path, ".obj"))
        loadModelFromObj(path, position, bufMgr);
    else {
        modelEntities_.pop_back();
        throw std::runtime_error("Unsupported model format: " + path);
    }

    if (useResourceCache)
        cacheModelResourceFromEntity(resourceKey, ref);

    return ref.entityId;
}

void SceneManager::loadModelFromObj(const std::string& path, const glm::vec3& position,
                                     const BufferManager& bufMgr)
{
    // 填充最后一个实体
    ModelEntity& ent = modelEntities_.back();
    ent.transform.position = position;

    // OBJ 不支持骨骼动画
    ent.skeleton.reset();
    ent.animationClips.clear();

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
    ent.ownsMeshBuffers = true;
    ent.meshResourceKey.clear();
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

void SceneManager::loadModelFromFbx(const std::string& path, const glm::vec3& position,
                                    const BufferManager& bufMgr)
{
    ModelEntity& ent = modelEntities_.back();
    ent.transform.position = position;

    FbxImportResult imported;
    std::string error;
    if (!FbxImporter::load(path, imported, &error))
        throw std::runtime_error("Failed to load FBX: " + path + "\n" + error);

    ent.vertices = std::move(imported.vertices);
    ent.indices = std::move(imported.indices);
    ent.subMeshes.clear();
    ent.subMeshes.reserve(imported.subMeshes.size());
    for (const FbxImportedSubMesh& source : imported.subMeshes) {
        SubMesh subMesh;
        subMesh.indexOffset = source.indexOffset;
        subMesh.indexCount = source.indexCount;
        subMesh.skinIndex = source.skinIndex;
        subMesh.materialSlot = source.materialSlot;
        ent.subMeshes.push_back(subMesh);
    }
    ent.subMeshMaterials.assign(imported.materials.size(), 0u);
    ent.autoAstPaths.clear();
    ent.hasSkin_ = imported.hasSkin;
    ent.skeleton = std::move(imported.skeleton);
    ent.animationClips = std::move(imported.animationClips);
    if (ent.skeleton)
        ent.animatorController.configureFromClips(ent.animationClips);
    ent.ownsMeshBuffers = true;
    ent.meshResourceKey.clear();

    modelLocalBoundsMin_ = imported.boundsMin;
    modelLocalBoundsMax_ = imported.boundsMax;
    bufMgr.createVertexBuffer(ent.vertices, ent.vertexBuffer, ent.vertexMemory);
    bufMgr.createIndexBuffer(ent.indices, ent.indexBuffer, ent.indexMemory);
    ent.indexCount = static_cast<uint32_t>(ent.indices.size());

    std::cout << "[FBX] loaded " << ent.vertices.size() << " vertices, "
              << ent.indices.size() << " indices, " << ent.subMeshes.size()
              << " submeshes, " << ent.animationClips.size() << " animation(s)\n";
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

    ent.skeleton.reset();
    ent.animationClips.clear();

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

        ent.skeleton = skel;
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
    if (ent.skeleton) {
        ent.skeleton->skinBoneIndices = skinRemap;
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
    ent.ownsMeshBuffers = true;
    ent.meshResourceKey.clear();
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
            const int boneLimit = ent.skeleton
                ? std::min(static_cast<int>(ent.skeleton->bones.size()), kMaxBones)
                : 0;
            bool warnedBoneIndexOutOfRange = false;

            // 检测当前 primitive 使用的是哪个 skin（对多 skin 模型的自动匹配）
            int primSkinIdx = 0;
            if (auto it = meshToSkin.find(&mesh); it != meshToSkin.end()) {
                primSkinIdx = it->second;
            } else if (ent.skeleton && jntAcc && wgtAcc && skinRemap.size() > 1) {
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
            const bool hasSkinData = (ent.skeleton != nullptr) && jntAcc && wgtAcc;

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
    if (ent.skeleton) {
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
                ent.animationClips.push_back(std::move(clip));
                std::cout << "[glTF] parsed animation \"" << ent.animationClips.back().name
                          << "\" with " << ent.animationClips.back().channels.size()
                          << " channels, duration=" << ent.animationClips.back().duration << "s\n";
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
            releaseEntityMeshBuffers(ctx, *it);
            modelEntities_.erase(it);
            return true;
        }
    }
    return false;
}

void SceneManager::destroyModelBuffers(const VulkanContext& ctx)
{
    destroyCachedModelResources(ctx);
    // 销毁所有模型实体的 GPU 缓冲
    for (auto& ent : modelEntities_) {
        releaseEntityMeshBuffers(ctx, ent);
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

void SceneManager::setEntityAnimationData(uint64_t entityId,
                                         std::shared_ptr<Skeleton> skeleton,
                                         std::vector<AnimationClip> clips)
{
    if (auto* e = getModelEntity(entityId)) {
        e->skeleton = std::move(skeleton);
        e->animationClips = std::move(clips);
        // 仅当实体没有已绑定的 AnimatorController 时才创建默认状态机。
        // 若实体已从 .scene.json 恢复了 controller，则保留现有 Controller 不覆盖。
        if (e->animatorControllerPath.empty())
            e->animatorController.configureFromClips(e->animationClips);
    }
}

std::shared_ptr<Skeleton> SceneManager::getEntitySkeleton(uint64_t entityId) const
{
    for (const auto& e : modelEntities_)
        if (e.entityId == entityId) return e.skeleton;
    return nullptr;
}

const std::vector<AnimationClip>& SceneManager::getEntityAnimationClips(uint64_t entityId) const
{
    static const std::vector<AnimationClip> kEmpty;
    for (const auto& e : modelEntities_)
        if (e.entityId == entityId) return e.animationClips;
    return kEmpty;
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
    destroyBuf(ctx, cameraModelIndexBuffer_,  cameraModelIndexMemory_);
    destroyBuf(ctx, cameraModelVertexBuffer_, cameraModelVertexMemory_);
    cameraModelLoaded_ = false;
    destroyCachedModelResources(ctx);
    for (auto& ent : modelEntities_) {
        releaseEntityMeshBuffers(ctx, ent);
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

// ─── Camera Entity Management ────────────────────────────────────────────────

bool SceneManager::isCameraEntity(uint64_t entityId) const
{
    const auto* ent = const_cast<SceneManager*>(this)->getModelEntity(entityId);
    return ent && ent->isCamera();
}

void SceneManager::loadCameraModelOnce(const VulkanContext& ctx, const BufferManager& bufMgr)
{
    if (cameraModelLoaded_) return;

    const std::string cameraModelKey = "content/Camera/Camera.mesh.ast";
    auto it = modelResourceCache_.find(cameraModelKey);
    if (it != modelResourceCache_.end()) {
        cameraModelVertexBuffer_ = it->second.vertexBuffer;
        cameraModelVertexMemory_ = it->second.vertexMemory;
        cameraModelIndexBuffer_ = it->second.indexBuffer;
        cameraModelIndexMemory_ = it->second.indexMemory;
        cameraModelIndexCount_ = it->second.indexCount;
        cameraModelSubMeshes_ = it->second.subMeshes;
        cameraModelLoaded_ = true;
        return;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const std::string resRoot = MaterialAssetLoader::getResRoot();
    const std::string fbxPath = resRoot + "/bin/mesh/Camera.fbx";

    FbxImportResult fbx;
    std::string fbxError;
    if (FbxImporter::load(fbxPath, fbx, &fbxError) && !fbx.vertices.empty() && !fbx.indices.empty()) {
        vertices = std::move(fbx.vertices);
        indices = std::move(fbx.indices);
        cameraModelSubMeshes_.clear();
        cameraModelSubMeshes_.reserve(fbx.subMeshes.size());
        for (const auto& s : fbx.subMeshes)
            cameraModelSubMeshes_.push_back({ s.indexOffset, s.indexCount, s.skinIndex, s.materialSlot });
    } else {
        std::cerr << "[SceneManager] Camera.fbx load failed (" << fbxPath
                  << "): " << fbxError << " — falling back to placeholder cube\n";
        constexpr float s = 0.3f;
        vertices = {
            {{-s, -s, -s}, {0,0,0}, {0,0}}, {{s, -s, -s}, {0,0,0}, {1,0}},
            {{s, s, -s}, {0,0,0}, {1,1}}, {{-s, s, -s}, {0,0,0}, {0,1}},
            {{-s, -s, s}, {0,0,0}, {0,0}}, {{s, -s, s}, {0,0,0}, {1,0}},
            {{s, s, s}, {0,0,0}, {1,1}}, {{-s, s, s}, {0,0,0}, {0,1}},
        };
        indices = {0,1,2,2,3,0, 1,5,6,6,2,1, 5,4,7,7,6,5, 4,0,3,3,7,4, 3,2,6,6,7,3, 4,5,1,1,0,4};
        SubMesh sm{};
        sm.indexOffset = 0;
        sm.indexCount = static_cast<uint32_t>(indices.size());
        sm.materialSlot = -1;
        cameraModelSubMeshes_ = { sm };
    }

    cameraModelIndexCount_ = static_cast<uint32_t>(indices.size());

    bufMgr.createVertexBuffer(vertices, cameraModelVertexBuffer_, cameraModelVertexMemory_);
    bufMgr.createIndexBuffer(indices, cameraModelIndexBuffer_, cameraModelIndexMemory_);

    cameraModelLoaded_ = true;
}

uint64_t SceneManager::createCameraEntity(const glm::vec3& position,
                                            const glm::quat& orientation,
                                            const VulkanContext& ctx,
                                            const BufferManager& bufMgr)
{
    loadCameraModelOnce(ctx, bufMgr);

    ModelEntity entity;
    entity.entityId = nextEntityId_++;
    entity.type = ModelEntity::Type::Camera;
    entity.displayName = "Camera_" + std::to_string(entity.entityId);

    entity.vertexBuffer = cameraModelVertexBuffer_;
    entity.vertexMemory = cameraModelVertexMemory_;
    entity.indexBuffer = cameraModelIndexBuffer_;
    entity.indexMemory = cameraModelIndexMemory_;
    entity.ownsMeshBuffers = false;
    entity.indexCount = cameraModelIndexCount_;
    entity.subMeshes = cameraModelSubMeshes_;
    entity.visible = true;

    entity.transform.position = position;
    entity.transform.rotation = orientation;
    entity.transform.scale = glm::vec3(1.0f);

    // Camera 模型 mesh 的默认朝向与预览的 -Z 前向相差 Y -90°，
    // 这里额外给模型几何体施加一个 Y -90° 偏移，使显示朝向与预览朝向一致。
    // 注意：此偏移只作用于渲染矩阵，cameraData.orientation 仍使用 Actor 原始朝向。
    entity.modelRotationOffset = glm::angleAxis(glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    entity.cameraData.position = position;
    entity.cameraData.orientation = orientation;
    entity.cameraData.fovDeg = 45.0f;
    entity.cameraData.aspectRatio = 4.0f / 3.0f;
    entity.cameraData.nearPlane = 0.1f;
    entity.cameraData.farPlane = 100.0f;

    modelEntities_.push_back(std::move(entity));
    return modelEntities_.back().entityId;
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
