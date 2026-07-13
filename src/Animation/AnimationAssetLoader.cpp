#include "AnimationAssetLoader.hpp"

#include "VulkanTypes.hpp"
#include "nlohmann/json.hpp"

#include <cgltf.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <unordered_map>

using nlohmann::json;

namespace {

constexpr char kMagic[8] = { 'T', 'E', 'A', 'N', 'I', 'M', '1', '\0' };
std::filesystem::path gResRoot = AnimationAssetLoader::kResRoot;

std::string resolveResPath(const std::string& rel)
{
    const std::filesystem::path relPath(rel);
    if (relPath.is_absolute())
        return relPath.string();
    return (gResRoot / relPath).string();
}

const char* targetToString(AnimChannel::Target target)
{
    switch (target) {
    case AnimChannel::Target::Translation: return "Translation";
    case AnimChannel::Target::Rotation:    return "Rotation";
    case AnimChannel::Target::Scale:       return "Scale";
    }
    return "Translation";
}

AnimChannel::Target targetFromString(const std::string& s)
{
    if (s == "Rotation") return AnimChannel::Target::Rotation;
    if (s == "Scale")    return AnimChannel::Target::Scale;
    return AnimChannel::Target::Translation;
}

const char* interpToString(AnimInterpolation interp)
{
    switch (interp) {
    case AnimInterpolation::Step:        return "Step";
    case AnimInterpolation::Linear:      return "Linear";
    case AnimInterpolation::CubicSpline: return "CubicSpline";
    }
    return "Linear";
}

AnimInterpolation interpFromString(const std::string& s)
{
    if (s == "Step")        return AnimInterpolation::Step;
    if (s == "CubicSpline") return AnimInterpolation::CubicSpline;
    return AnimInterpolation::Linear;
}

template <typename T>
bool writeRaw(std::ofstream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(out);
}

template <typename T>
bool readRaw(std::ifstream& in, T& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

bool writeMatrix(std::ofstream& out, const glm::mat4& m)
{
    out.write(reinterpret_cast<const char*>(&m[0][0]), sizeof(float) * 16);
    return static_cast<bool>(out);
}

bool readMatrix(std::ifstream& in, glm::mat4& m)
{
    in.read(reinterpret_cast<char*>(&m[0][0]), sizeof(float) * 16);
    return static_cast<bool>(in);
}

std::string stemName(const std::string& astRelPath)
{
    return std::filesystem::path(astRelPath).stem().string();
}

} // namespace

void AnimationAssetLoader::setResRoot(const std::string& resRoot)
{
    gResRoot = resRoot.empty()
        ? std::filesystem::path(kResRoot)
        : std::filesystem::path(resRoot);
}

std::string AnimationAssetLoader::getResRoot()
{
    return gResRoot.string();
}

bool AnimationAssetLoader::save(const std::string& astRelPath,
                                const std::string& binaryRelPath,
                                const std::string& rootModelAst,
                                const Skeleton& skeleton,
                                const std::vector<AnimationClip>& clips,
                                std::string* err)
{
    const std::filesystem::path astPath = resolveResPath(astRelPath);
    const std::filesystem::path binPath = resolveResPath(binaryRelPath);
    std::error_code ec;
    std::filesystem::create_directories(astPath.parent_path(), ec);
    std::filesystem::create_directories(binPath.parent_path(), ec);

    std::ofstream bin(binPath, std::ios::binary);
    if (!bin.is_open()) {
        if (err) *err = "cannot create binary: " + binPath.string();
        return false;
    }

    bin.write(kMagic, sizeof(kMagic));
    const uint32_t version = 1;
    const uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());
    const uint32_t clipCount = static_cast<uint32_t>(clips.size());
    if (!writeRaw(bin, version) || !writeRaw(bin, boneCount) || !writeRaw(bin, clipCount)) {
        if (err) *err = "failed writing binary header: " + binPath.string();
        return false;
    }

    for (const auto& bone : skeleton.bones) {
        if (!writeMatrix(bin, bone.inverseBindMatrix)
            || !writeMatrix(bin, bone.localBindTransform)
            || !writeMatrix(bin, bone.globalBindTransform)) {
            if (err) *err = "failed writing bone matrices: " + binPath.string();
            return false;
        }
    }

    for (const auto& clip : clips) {
        const uint32_t channelCount = static_cast<uint32_t>(clip.channels.size());
        if (!writeRaw(bin, channelCount)) {
            if (err) *err = "failed writing channel count: " + binPath.string();
            return false;
        }
        for (const auto& ch : clip.channels) {
            const uint32_t timeCount = static_cast<uint32_t>(ch.times.size());
            const uint32_t valueCount = static_cast<uint32_t>(ch.values.size());
            if (!writeRaw(bin, timeCount) || !writeRaw(bin, valueCount)) {
                if (err) *err = "failed writing channel sizes: " + binPath.string();
                return false;
            }
            if (timeCount > 0) {
                bin.write(reinterpret_cast<const char*>(ch.times.data()), sizeof(float) * timeCount);
            }
            if (valueCount > 0) {
                bin.write(reinterpret_cast<const char*>(ch.values.data()), sizeof(glm::vec4) * valueCount);
            }
            if (!bin) {
                if (err) *err = "failed writing channel data: " + binPath.string();
                return false;
            }
        }
    }

    json j;
    j["version"] = 1;
    j["type"] = "Anim";
    j["name"] = stemName(astRelPath);
    j["rootModel"] = rootModelAst;
    j["binary"] = binaryRelPath;

    json bones = json::array();
    for (const auto& bone : skeleton.bones) {
        bones.push_back({
            { "name", bone.name },
            { "parentIndex", bone.parentIndex }
        });
    }
    j["skeleton"]["boneCount"] = skeleton.bones.size();
    j["skeleton"]["bones"] = std::move(bones);

    json skins = json::array();
    for (const auto& skin : skeleton.skinBoneIndices) {
        skins.push_back({ { "boneIndices", skin } });
    }
    j["skeleton"]["skins"] = std::move(skins);

    json clipJson = json::array();
    for (const auto& clip : clips) {
        json cj;
        cj["name"] = clip.name;
        cj["duration"] = clip.duration;
        cj["channels"] = json::array();
        for (const auto& ch : clip.channels) {
            cj["channels"].push_back({
                { "boneIndex", ch.boneIndex },
                { "target", targetToString(ch.target) },
                { "interpolation", interpToString(ch.interp) },
                { "timeCount", ch.times.size() },
                { "valueCount", ch.values.size() }
            });
        }
        clipJson.push_back(std::move(cj));
    }
    j["clips"] = std::move(clipJson);

    std::ofstream ast(astPath);
    if (!ast.is_open()) {
        if (err) *err = "cannot create ast: " + astPath.string();
        return false;
    }
    ast << j.dump(2) << "\n";
    return true;
}

bool AnimationAssetLoader::load(const std::string& astRelPath,
                                AnimationAsset& out,
                                std::string* err)
{
    const std::filesystem::path astPath = resolveResPath(astRelPath);
    std::ifstream ast(astPath);
    if (!ast.is_open()) {
        if (err) *err = "cannot open ast: " + astPath.string();
        return false;
    }

    json j;
    try {
        ast >> j;
    } catch (const std::exception& e) {
        if (err) *err = std::string("json parse error: ") + e.what();
        return false;
    }

    if (j.value("type", std::string{}) != "Anim") {
        if (err) *err = "asset is not type Anim: " + astRelPath;
        return false;
    }

    const std::string binaryRelPath = j.value("binary", std::string{});
    if (binaryRelPath.empty()) {
        if (err) *err = "Anim asset has no binary field: " + astRelPath;
        return false;
    }

    const std::filesystem::path binPath = resolveResPath(binaryRelPath);
    std::ifstream bin(binPath, std::ios::binary);
    if (!bin.is_open()) {
        if (err) *err = "cannot open binary: " + binPath.string();
        return false;
    }

    char magic[8]{};
    bin.read(magic, sizeof(magic));
    if (std::string(magic, magic + sizeof(magic)) != std::string(kMagic, kMagic + sizeof(kMagic))) {
        if (err) *err = "invalid animation binary magic: " + binPath.string();
        return false;
    }

    uint32_t version = 0;
    uint32_t boneCount = 0;
    uint32_t clipCount = 0;
    if (!readRaw(bin, version) || !readRaw(bin, boneCount) || !readRaw(bin, clipCount) || version != 1) {
        if (err) *err = "invalid animation binary header: " + binPath.string();
        return false;
    }

    auto skeleton = std::make_shared<Skeleton>();
    const auto& boneMeta = j["skeleton"].value("bones", json::array());
    skeleton->bones.resize(boneCount);
    for (uint32_t i = 0; i < boneCount; ++i) {
        Bone& bone = skeleton->bones[i];
        if (i < boneMeta.size()) {
            bone.name = boneMeta[i].value("name", std::string("bone_") + std::to_string(i));
            bone.parentIndex = boneMeta[i].value("parentIndex", -1);
        } else {
            bone.name = "bone_" + std::to_string(i);
            bone.parentIndex = -1;
        }
        skeleton->boneNameToIndex[bone.name] = static_cast<int>(i);
        if (!readMatrix(bin, bone.inverseBindMatrix)
            || !readMatrix(bin, bone.localBindTransform)
            || !readMatrix(bin, bone.globalBindTransform)) {
            if (err) *err = "failed reading bone matrices: " + binPath.string();
            return false;
        }
    }

    const auto& skinMeta = j["skeleton"].value("skins", json::array());
    for (const auto& skin : skinMeta) {
        std::vector<int> remap;
        if (skin.contains("boneIndices") && skin["boneIndices"].is_array()) {
            for (const auto& idx : skin["boneIndices"]) {
                remap.push_back(idx.get<int>());
            }
        }
        skeleton->skinBoneIndices.push_back(std::move(remap));
    }

    std::vector<AnimationClip> clips;
    const auto& clipMeta = j.value("clips", json::array());
    clips.reserve(clipCount);
    for (uint32_t ci = 0; ci < clipCount; ++ci) {
        AnimationClip clip;
        if (ci < clipMeta.size()) {
            clip.name = clipMeta[ci].value("name", std::string("clip_") + std::to_string(ci));
            clip.duration = clipMeta[ci].value("duration", 0.f);
        } else {
            clip.name = "clip_" + std::to_string(ci);
        }

        // 同名 clip 自动去重：追加 _1, _2 后缀
        {
            std::string dedupName = clip.name;
            int suffix = 1;
            while (std::any_of(clips.begin(), clips.end(),
                               [&](const AnimationClip& c) { return c.name == dedupName; })) {
                dedupName = clip.name + "_" + std::to_string(suffix++);
            }
            if (dedupName != clip.name) {
                std::cerr << "[AnimAsset] duplicate clip name \"" << clip.name
                          << "\" renamed to \"" << dedupName << "\"\n";
                clip.name = dedupName;
            }
        }

        uint32_t channelCount = 0;
        if (!readRaw(bin, channelCount)) {
            if (err) *err = "failed reading channel count: " + binPath.string();
            return false;
        }
        const auto channelsMeta = (ci < clipMeta.size())
            ? clipMeta[ci].value("channels", json::array())
            : json::array();

        clip.channels.reserve(channelCount);
        for (uint32_t chIndex = 0; chIndex < channelCount; ++chIndex) {
            AnimChannel ch;
            if (chIndex < channelsMeta.size()) {
                ch.boneIndex = channelsMeta[chIndex].value("boneIndex", -1);
                ch.target = targetFromString(channelsMeta[chIndex].value("target", std::string("Translation")));
                ch.interp = interpFromString(channelsMeta[chIndex].value("interpolation", std::string("Linear")));
            }

            uint32_t timeCount = 0;
            uint32_t valueCount = 0;
            if (!readRaw(bin, timeCount) || !readRaw(bin, valueCount)) {
                if (err) *err = "failed reading channel sizes: " + binPath.string();
                return false;
            }

            ch.times.resize(timeCount);
            ch.values.resize(valueCount);
            if (timeCount > 0) {
                bin.read(reinterpret_cast<char*>(ch.times.data()), sizeof(float) * timeCount);
            }
            if (valueCount > 0) {
                bin.read(reinterpret_cast<char*>(ch.values.data()), sizeof(glm::vec4) * valueCount);
            }
            if (!bin) {
                if (err) *err = "failed reading channel data: " + binPath.string();
                return false;
            }
            clip.channels.push_back(std::move(ch));
        }
        clips.push_back(std::move(clip));
    }

    out.skeleton = std::move(skeleton);
    out.clips = std::move(clips);
    out.name = j.value("name", stemName(astRelPath));
    out.rootModelAst = j.value("rootModel", std::string{});
    out.binaryPath = binaryRelPath;
    return true;
}

bool AnimationAssetLoader::renameClip(const std::string& astRelPath,
                                      const std::string& oldName,
                                      const std::string& newName,
                                      std::string* err)
{
    if (newName.empty()) {
        if (err) *err = "animation clip name cannot be empty";
        return false;
    }
    if (oldName == newName) return true;

    const std::filesystem::path astPath = resolveResPath(astRelPath);
    json j;
    {
        // Windows 不允许 rename 仍被打开的文件，因此解析完成后必须先销毁输入流。
        std::ifstream in(astPath);
        if (!in.is_open()) {
            if (err) *err = "cannot open animation asset: " + astPath.string();
            return false;
        }
        try {
            in >> j;
        } catch (const std::exception& e) {
            if (err) *err = std::string("json parse error: ") + e.what();
            return false;
        }
    }

    if (!j.contains("clips") || !j["clips"].is_array()) {
        if (err) *err = "animation asset has no clips array: " + astPath.string();
        return false;
    }

    json* target = nullptr;
    for (auto& clip : j["clips"]) {
        if (!clip.is_object()) continue;
        const std::string name = clip.value("name", std::string{});
        if (name == newName) {
            if (err) *err = "animation clip name already exists: " + newName;
            return false;
        }
        if (name == oldName) {
            if (target) {
                if (err) *err = "animation asset contains duplicate clip name: " + oldName;
                return false;
            }
            target = &clip;
        }
    }
    if (!target) {
        if (err) *err = "animation clip not found: " + oldName;
        return false;
    }
    (*target)["name"] = newName;

    const std::filesystem::path tempPath = astPath.string() + ".rename.tmp";
    const std::filesystem::path backupPath = astPath.string() + ".rename.bak";
    {
        std::ofstream out(tempPath, std::ios::trunc);
        if (!out.is_open()) {
            if (err) *err = "cannot create temporary animation asset: " + tempPath.string();
            return false;
        }
        out << j.dump(2) << '\n';
        if (!out.good()) {
            if (err) *err = "failed writing temporary animation asset: " + tempPath.string();
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::remove(backupPath, ec);
    ec.clear();
    std::filesystem::rename(astPath, backupPath, ec);
    if (ec) {
        std::filesystem::remove(tempPath);
        if (err) *err = "cannot prepare animation asset replacement (error "
            + std::to_string(ec.value()) + ")";
        return false;
    }

    std::filesystem::rename(tempPath, astPath, ec);
    if (ec) {
        std::error_code restoreError;
        std::filesystem::rename(backupPath, astPath, restoreError);
        if (err) *err = "cannot replace animation asset (error "
            + std::to_string(ec.value()) + ", restore "
            + std::to_string(restoreError.value()) + ")";
        return false;
    }
    std::filesystem::remove(backupPath, ec);
    return true;
}

bool AnimationAssetLoader::saveFromGltf(const std::string& gltfPath,
                                        const std::string& astRelPath,
                                        const std::string& binaryRelPath,
                                        const std::string& rootModelAst,
                                        std::string* err)
{
    cgltf_options opts{};
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse_file(&opts, gltfPath.c_str(), &data);
    if (r != cgltf_result_success || !data) {
        if (err) *err = "failed to parse glTF: " + gltfPath;
        return false;
    }

    if (cgltf_load_buffers(&opts, data, gltfPath.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        if (err) *err = "failed to load glTF buffers: " + gltfPath;
        return false;
    }

    std::shared_ptr<Skeleton> skeleton;
    std::unordered_map<const cgltf_node*, int> globalNodeToBone;

    if (data->skins_count > 0) {
        auto skel = std::make_shared<Skeleton>();
        std::vector<const cgltf_node*> boneNodes;

        for (cgltf_size si = 0; si < data->skins_count; ++si) {
            const cgltf_skin& skin = data->skins[si];
            for (cgltf_size j = 0; j < skin.joints_count; ++j) {
                const cgltf_node* node = skin.joints[j];
                if (globalNodeToBone.count(node)) continue;

                const int boneIndex = static_cast<int>(skel->bones.size());
                globalNodeToBone[node] = boneIndex;

                Bone bone;
                bone.name = node->name ? node->name
                    : ("bone_" + std::to_string(si) + "_" + std::to_string(j));
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

        std::vector<std::vector<int>> skinRemap(data->skins_count);
        for (cgltf_size si = 0; si < data->skins_count; ++si) {
            const cgltf_skin& skin = data->skins[si];
            skinRemap[si].resize(skin.joints_count);
            for (cgltf_size j = 0; j < skin.joints_count; ++j) {
                auto it = globalNodeToBone.find(skin.joints[j]);
                skinRemap[si][j] = (it != globalNodeToBone.end()) ? it->second : 0;
            }
            if (skin.joints_count > static_cast<cgltf_size>(kMaxBones)) {
                std::cerr << "[AnimAsset] skin " << si << " has " << skin.joints_count
                          << " joints; GPU palettes will clamp to " << kMaxBones << "\n";
            }
        }
        skel->skinBoneIndices = std::move(skinRemap);
        skeleton = std::move(skel);
    }

    std::vector<AnimationClip> clips;
    if (skeleton && data->animations_count > 0) {
        for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
            const cgltf_animation& anim = data->animations[ai];
            AnimationClip clip;
            clip.name = anim.name ? anim.name : ("clip_" + std::to_string(ai));
            clip.duration = 0.f;

            for (cgltf_size ci = 0; ci < anim.channels_count; ++ci) {
                const cgltf_animation_channel& ch = anim.channels[ci];
                if (!ch.target_node || !ch.sampler) continue;

                auto boneIt = globalNodeToBone.find(ch.target_node);
                if (boneIt == globalNodeToBone.end()) continue;

                AnimChannel ac;
                ac.boneIndex = boneIt->second;
                if (ch.target_path == cgltf_animation_path_type_translation)
                    ac.target = AnimChannel::Target::Translation;
                else if (ch.target_path == cgltf_animation_path_type_rotation)
                    ac.target = AnimChannel::Target::Rotation;
                else if (ch.target_path == cgltf_animation_path_type_scale)
                    ac.target = AnimChannel::Target::Scale;
                else
                    continue;

                const cgltf_animation_sampler& sm = *ch.sampler;
                if (sm.interpolation == cgltf_interpolation_type_step)
                    ac.interp = AnimInterpolation::Step;
                else if (sm.interpolation == cgltf_interpolation_type_cubic_spline)
                    ac.interp = AnimInterpolation::CubicSpline;
                else
                    ac.interp = AnimInterpolation::Linear;

                const cgltf_accessor* inputAcc = sm.input;
                const cgltf_accessor* outputAcc = sm.output;
                if (!inputAcc || !outputAcc) continue;

                ac.times.resize(inputAcc->count);
                for (cgltf_size k = 0; k < inputAcc->count; ++k) {
                    cgltf_accessor_read_float(inputAcc, k, &ac.times[k], 1);
                }

                const cgltf_size compCount =
                    (ac.target == AnimChannel::Target::Rotation) ? 4 : 3;
                ac.values.resize(outputAcc->count);
                for (cgltf_size k = 0; k < outputAcc->count; ++k) {
                    float tmp[4]{};
                    cgltf_accessor_read_float(outputAcc, k, tmp, compCount);
                    ac.values[k] = (compCount == 3)
                        ? glm::vec4(tmp[0], tmp[1], tmp[2], 0.f)
                        : glm::vec4(tmp[0], tmp[1], tmp[2], tmp[3]);
                }

                if (!ac.times.empty())
                    clip.duration = std::max(clip.duration, ac.times.back());
                clip.channels.push_back(std::move(ac));
            }

            if (!clip.channels.empty())
                clips.push_back(std::move(clip));
        }
    }

    cgltf_free(data);

    if (!skeleton || skeleton->bones.empty() || clips.empty()) {
        if (err) *err = "glTF has no skeletal animation data: " + gltfPath;
        return false;
    }

    return save(astRelPath, binaryRelPath, rootModelAst, *skeleton, clips, err);
}
