#include "FbxImporter.hpp"

#include "VulkanTypes.hpp"
#include "ufbx.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace {

glm::vec3 toVec3(ufbx_vec3 v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

glm::quat toQuat(ufbx_quat q)
{
    return glm::normalize(glm::quat(static_cast<float>(q.w), static_cast<float>(q.x),
                                    static_cast<float>(q.y), static_cast<float>(q.z)));
}

glm::mat4 toMat4(const ufbx_matrix& m)
{
    glm::mat4 result(1.f);
    for (int column = 0; column < 4; ++column) {
        result[column][0] = static_cast<float>(m.cols[column].x);
        result[column][1] = static_cast<float>(m.cols[column].y);
        result[column][2] = static_cast<float>(m.cols[column].z);
    }
    return result;
}

glm::mat4 toMat4(const ufbx_transform& transform)
{
    return glm::translate(glm::mat4(1.f), toVec3(transform.translation))
         * glm::mat4_cast(toQuat(transform.rotation))
         * glm::scale(glm::mat4(1.f), toVec3(transform.scale));
}

std::string toString(ufbx_string value)
{
    return value.data ? std::string(value.data, value.length) : std::string{};
}

FbxImportedTexture readTexture(const ufbx_material_map& map)
{
    FbxImportedTexture result;
    const ufbx_texture* texture = map.texture;
    if (!texture || !map.texture_enabled)
        return result;

    if (texture->type != UFBX_TEXTURE_FILE && texture->file_textures.count > 0)
        texture = texture->file_textures.data[0];
    if (!texture || texture->type != UFBX_TEXTURE_FILE)
        return result;

    result.filename = toString(texture->filename);
    if (texture->content.data && texture->content.size > 0) {
        const auto* begin = reinterpret_cast<const uint8_t*>(texture->content.data);
        result.embeddedContent.assign(begin, begin + texture->content.size);
    }
    return result;
}

void appendVec3Channel(AnimationClip& clip,
                       int boneIndex,
                       AnimChannel::Target target,
                       const ufbx_baked_vec3_list& keys)
{
    if (keys.count == 0) return;
    AnimChannel channel;
    channel.boneIndex = boneIndex;
    channel.target = target;
    channel.interp = AnimInterpolation::Linear;
    channel.times.reserve(keys.count);
    channel.values.reserve(keys.count);
    for (const ufbx_baked_vec3& key : keys) {
        channel.times.push_back(static_cast<float>(key.time));
        channel.values.emplace_back(static_cast<float>(key.value.x),
                                    static_cast<float>(key.value.y),
                                    static_cast<float>(key.value.z), 0.f);
    }
    clip.channels.push_back(std::move(channel));
}

void appendQuatChannel(AnimationClip& clip,
                       int boneIndex,
                       const ufbx_baked_quat_list& keys)
{
    if (keys.count == 0) return;
    AnimChannel channel;
    channel.boneIndex = boneIndex;
    channel.target = AnimChannel::Target::Rotation;
    channel.interp = AnimInterpolation::Linear;
    channel.times.reserve(keys.count);
    channel.values.reserve(keys.count);
    for (const ufbx_baked_quat& key : keys) {
        const glm::quat q = toQuat(key.value);
        channel.times.push_back(static_cast<float>(key.time));
        channel.values.emplace_back(q.x, q.y, q.z, q.w);
    }
    clip.channels.push_back(std::move(channel));
}

} // namespace

bool FbxImporter::load(const std::string& path, FbxImportResult& out, std::string* error)
{
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.generate_missing_normals = true;
    opts.load_external_files = true;

    ufbx_error loadError{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &loadError);
    if (!scene) {
        if (error) *error = toString(loadError.description);
        return false;
    }

    FbxImportResult result;
    result.boundsMin = glm::vec3(FLT_MAX);
    result.boundsMax = glm::vec3(-FLT_MAX);

    // 材质 typed_id 与 scene->materials 的顺序一致，可直接作为 materialSlot。
    result.materials.reserve(scene->materials.count);
    for (const ufbx_material* material : scene->materials) {
        FbxImportedMaterial dst;
        dst.name = toString(material->name);
        if (dst.name.empty()) dst.name = "material_" + std::to_string(result.materials.size());

        const ufbx_vec4 base = material->pbr.base_color.value_vec4;
        dst.baseColor = {static_cast<float>(base.x), static_cast<float>(base.y),
                         static_cast<float>(base.z), static_cast<float>(base.w)};
        dst.roughness = std::clamp(static_cast<float>(material->pbr.roughness.value_real), 0.f, 1.f);
        dst.metallic = std::clamp(static_cast<float>(material->pbr.metalness.value_real), 0.f, 1.f);
        const ufbx_vec3 emissive = material->pbr.emission_color.value_vec3;
        dst.emissiveColor = {static_cast<float>(emissive.x), static_cast<float>(emissive.y),
                             static_cast<float>(emissive.z), 1.f};
        dst.albedoTexture = readTexture(material->pbr.base_color);
        if (dst.albedoTexture.filename.empty() && dst.albedoTexture.embeddedContent.empty())
            dst.albedoTexture = readTexture(material->fbx.diffuse_color);
        dst.normalTexture = readTexture(material->pbr.normal_map);
        if (dst.normalTexture.filename.empty() && dst.normalTexture.embeddedContent.empty())
            dst.normalTexture = readTexture(material->fbx.normal_map);
        if (dst.normalTexture.filename.empty() && dst.normalTexture.embeddedContent.empty())
            dst.normalTexture = readTexture(material->fbx.bump);
        dst.emissiveTexture = readTexture(material->pbr.emission_color);
        if (dst.emissiveTexture.filename.empty() && dst.emissiveTexture.embeddedContent.empty())
            dst.emissiveTexture = readTexture(material->fbx.emission_color);
        if (dst.emissiveTexture.filename.empty() && dst.emissiveTexture.embeddedContent.empty())
            dst.emissiveTexture = readTexture(material->fbx.emission_factor);
        result.materials.push_back(std::move(dst));
    }

    // 收集所有 skin 及其骨骼节点，同时包含未直接加权但维持层级所需的祖先节点。
    std::vector<const ufbx_skin_deformer*> skins;
    std::unordered_map<const ufbx_skin_deformer*, int> skinToIndex;
    std::unordered_set<const ufbx_node*> boneNodes;
    for (const ufbx_mesh* mesh : scene->meshes) {
        for (const ufbx_skin_deformer* skin : mesh->skin_deformers) {
            if (skinToIndex.emplace(skin, static_cast<int>(skins.size())).second)
                skins.push_back(skin);
            for (const ufbx_skin_cluster* cluster : skin->clusters) {
                for (const ufbx_node* node = cluster->bone_node; node; node = node->parent)
                    boneNodes.insert(node);
            }
        }
    }

    std::unordered_map<const ufbx_node*, int> nodeToBone;
    std::vector<const ufbx_node*> orderedBoneNodes;
    if (!boneNodes.empty()) {
        result.skeleton = std::make_shared<Skeleton>();
        for (const ufbx_node* node : scene->nodes) {
            if (!boneNodes.count(node)) continue;
            const int boneIndex = static_cast<int>(result.skeleton->bones.size());
            nodeToBone[node] = boneIndex;
            orderedBoneNodes.push_back(node);

            Bone bone;
            bone.name = toString(node->name);
            if (bone.name.empty()) bone.name = "bone_" + std::to_string(boneIndex);
            if (result.skeleton->boneNameToIndex.count(bone.name))
                bone.name += "_" + std::to_string(boneIndex);
            result.skeleton->boneNameToIndex[bone.name] = boneIndex;
            bone.localBindTransform = toMat4(node->local_transform);
            bone.globalBindTransform = toMat4(node->node_to_world);
            result.skeleton->bones.push_back(std::move(bone));
        }

        for (size_t i = 0; i < orderedBoneNodes.size(); ++i) {
            const ufbx_node* parent = orderedBoneNodes[i]->parent;
            while (parent) {
                const auto it = nodeToBone.find(parent);
                if (it != nodeToBone.end()) {
                    result.skeleton->bones[i].parentIndex = it->second;
                    break;
                }
                parent = parent->parent;
            }
        }

        std::vector<bool> hasInverseBind(result.skeleton->bones.size(), false);
        result.skeleton->skinBoneIndices.resize(skins.size());
        for (size_t skinIndex = 0; skinIndex < skins.size(); ++skinIndex) {
            const ufbx_skin_deformer* skin = skins[skinIndex];
            auto& remap = result.skeleton->skinBoneIndices[skinIndex];
            const size_t clusterCount = std::min(skin->clusters.count,
                                                 static_cast<size_t>(kMaxBones));
            remap.reserve(clusterCount);
            for (size_t clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex) {
                const ufbx_skin_cluster* cluster = skin->clusters.data[clusterIndex];
                const auto it = nodeToBone.find(cluster->bone_node);
                const int boneIndex = it != nodeToBone.end() ? it->second : 0;
                remap.push_back(boneIndex);
                if (boneIndex >= 0 && static_cast<size_t>(boneIndex) < hasInverseBind.size()
                    && !hasInverseBind[boneIndex]) {
                    result.skeleton->bones[boneIndex].inverseBindMatrix = toMat4(cluster->geometry_to_bone);
                    hasInverseBind[boneIndex] = true;
                }
            }
        }
    }

    std::unordered_map<Vertex, uint32_t, VertexHash> uniqueVertices;
    for (const ufbx_node* node : scene->nodes) {
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || node->is_root) continue;

        const ufbx_skin_deformer* skin = mesh->skin_deformers.count > 0
            ? mesh->skin_deformers.data[0] : nullptr;
        const int skinIndex = skin ? skinToIndex[skin] : -1;
        const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&node->geometry_to_world);
        std::vector<uint32_t> triangleIndices(mesh->max_face_triangles * 3);

        for (const ufbx_mesh_part& part : mesh->material_parts) {
            if (part.num_triangles == 0) continue;
            const uint32_t indexOffset = static_cast<uint32_t>(result.indices.size());

            for (uint32_t faceIndex : part.face_indices) {
                const ufbx_face face = mesh->faces.data[faceIndex];
                const uint32_t triangleCount = ufbx_triangulate_face(
                    triangleIndices.data(), triangleIndices.size(), mesh, face);

                for (uint32_t corner = 0; corner < triangleCount * 3; ++corner) {
                    uint32_t triangleCorner = corner;
                    if (mesh->reversed_winding && corner % 3 == 1) triangleCorner = corner + 1;
                    else if (mesh->reversed_winding && corner % 3 == 2) triangleCorner = corner - 1;
                    const uint32_t vertexIndex = triangleIndices[triangleCorner];

                    const ufbx_vec3 sourcePosition = ufbx_get_vertex_vec3(&mesh->vertex_position, vertexIndex);
                    const ufbx_vec3 sourceNormal = ufbx_get_vertex_vec3(&mesh->vertex_normal, vertexIndex);
                    ufbx_vec3 position = sourcePosition;
                    ufbx_vec3 normal = sourceNormal;
                    if (!skin) {
                        position = ufbx_transform_position(&node->geometry_to_world, sourcePosition);
                        normal = ufbx_transform_direction(&normalMatrix, sourceNormal);
                    }

                    Vertex vertex{};
                    vertex.pos = toVec3(position);
                    vertex.normal = glm::normalize(toVec3(normal));
                    vertex.color = {1.f, 1.f, 1.f};
                    if (mesh->vertex_uv.exists) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, vertexIndex);
                        // FBX/DCC UV 原点位于左下，而 stb_image 上传后的纹理原点位于左上。
                        // 仅在 FBX 导入边界翻转 V，保持 OBJ/glTF 各自现有约定不变。
                        vertex.texCoord = {static_cast<float>(uv.x),
                                           1.0f - static_cast<float>(uv.y)};
                    }
                    if (mesh->vertex_tangent.exists) {
                        ufbx_vec3 tangent = ufbx_get_vertex_vec3(&mesh->vertex_tangent, vertexIndex);
                        ufbx_vec3 bitangent = mesh->vertex_bitangent.exists
                            ? ufbx_get_vertex_vec3(&mesh->vertex_bitangent, vertexIndex)
                            : ufbx_vec3{};
                        if (!skin) {
                            tangent = ufbx_transform_direction(&normalMatrix, tangent);
                            bitangent = ufbx_transform_direction(&normalMatrix, bitangent);
                        }
                        const glm::vec3 tangentVec = glm::normalize(toVec3(tangent));
                        const glm::vec3 bitangentVec = toVec3(bitangent);
                        const float sign = glm::dot(glm::cross(vertex.normal, tangentVec), bitangentVec) < 0.f
                            ? -1.f : 1.f;
                        vertex.tangent = glm::vec4(tangentVec, sign);
                    }

                    if (skin) {
                        const uint32_t logicalVertex = mesh->vertex_indices.data[vertexIndex];
                        const ufbx_skin_vertex weights = skin->vertices.data[logicalVertex];
                        float totalWeight = 0.f;
                        int influence = 0;
                        for (size_t wi = 0; wi < weights.num_weights && influence < 4; ++wi) {
                            const ufbx_skin_weight weight = skin->weights.data[weights.weight_begin + wi];
                            if (weight.cluster_index >= static_cast<size_t>(kMaxBones)) continue;
                            vertex.boneIndices[influence] = static_cast<int>(weight.cluster_index);
                            vertex.boneWeights[influence] = static_cast<float>(weight.weight);
                            totalWeight += vertex.boneWeights[influence];
                            ++influence;
                        }
                        if (totalWeight > 1e-6f) vertex.boneWeights /= totalWeight;
                        else vertex.boneWeights = {1.f, 0.f, 0.f, 0.f};
                        result.hasSkin = true;
                    }

                    const ufbx_vec3 boundPosition = ufbx_transform_position(
                        &node->geometry_to_world, sourcePosition);
                    result.boundsMin = glm::min(result.boundsMin, toVec3(boundPosition));
                    result.boundsMax = glm::max(result.boundsMax, toVec3(boundPosition));

                    auto [it, inserted] = uniqueVertices.emplace(
                        vertex, static_cast<uint32_t>(result.vertices.size()));
                    if (inserted) result.vertices.push_back(vertex);
                    result.indices.push_back(it->second);
                }
            }

            FbxImportedSubMesh subMesh;
            subMesh.indexOffset = indexOffset;
            subMesh.indexCount = static_cast<uint32_t>(result.indices.size()) - indexOffset;
            subMesh.skinIndex = skinIndex;
            const ufbx_material* material = part.index < node->materials.count
                ? node->materials.data[part.index] : nullptr;
            subMesh.materialSlot = material ? static_cast<int>(material->typed_id) : -1;
            result.subMeshes.push_back(subMesh);
        }
    }

    if (result.skeleton) {
        for (const ufbx_anim_stack* stack : scene->anim_stacks) {
            ufbx_bake_opts bakeOpts{};
            bakeOpts.trim_start_time = true;
            bakeOpts.resample_rate = 30.0;
            bakeOpts.key_reduction_enabled = true;
            bakeOpts.key_reduction_rotation = true;

            ufbx_error bakeError{};
            ufbx_baked_anim* baked = ufbx_bake_anim(scene, stack->anim, &bakeOpts, &bakeError);
            if (!baked) continue;

            AnimationClip clip;
            clip.name = toString(stack->name);
            if (clip.name.empty()) clip.name = "anim_" + std::to_string(result.animationClips.size());
            clip.duration = static_cast<float>(baked->playback_duration);
            for (size_t boneIndex = 0; boneIndex < orderedBoneNodes.size(); ++boneIndex) {
                ufbx_baked_node* bakedNode = ufbx_find_baked_node(baked,
                                                                  const_cast<ufbx_node*>(orderedBoneNodes[boneIndex]));
                if (!bakedNode) continue;
                appendVec3Channel(clip, static_cast<int>(boneIndex),
                                  AnimChannel::Target::Translation, bakedNode->translation_keys);
                appendQuatChannel(clip, static_cast<int>(boneIndex), bakedNode->rotation_keys);
                appendVec3Channel(clip, static_cast<int>(boneIndex),
                                  AnimChannel::Target::Scale, bakedNode->scale_keys);
            }
            ufbx_free_baked_anim(baked);
            if (!clip.channels.empty()) result.animationClips.push_back(std::move(clip));
        }
    }

    ufbx_free_scene(scene);

    if (result.vertices.empty() || result.indices.empty()) {
        if (error) *error = "FBX contains no usable triangle meshes";
        return false;
    }
    out = std::move(result);
    return true;
}
