#pragma once

#include "AnimationClip.hpp"
#include "Skeleton.hpp"

#include <memory>
#include <string>
#include <vector>

struct AnimationAsset {
    std::shared_ptr<Skeleton> skeleton;
    std::vector<AnimationClip> clips;
    std::string name;
    std::string rootModelAst;
    std::string binaryPath;
};

class AnimationAssetLoader {
public:
    static constexpr const char* kResRoot = "res/";

    static void setResRoot(const std::string& resRoot);
    static std::string getResRoot();

    static bool save(const std::string& astRelPath,
                     const std::string& binaryRelPath,
                     const std::string& rootModelAst,
                     const Skeleton& skeleton,
                     const std::vector<AnimationClip>& clips,
                     std::string* err = nullptr);

    static bool load(const std::string& astRelPath,
                     AnimationAsset& out,
                     std::string* err = nullptr);

    /** @brief 仅重命名 Anim .ast 中的 clip 元数据；二进制关键帧无需重写。 */
    static bool renameClip(const std::string& astRelPath,
                           const std::string& oldName,
                           const std::string& newName,
                           std::string* err = nullptr);

    static bool saveFromGltf(const std::string& gltfPath,
                             const std::string& astRelPath,
                             const std::string& binaryRelPath,
                             const std::string& rootModelAst,
                             std::string* err = nullptr);
};
