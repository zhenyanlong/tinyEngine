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

    static bool saveFromGltf(const std::string& gltfPath,
                             const std::string& astRelPath,
                             const std::string& binaryRelPath,
                             const std::string& rootModelAst,
                             std::string* err = nullptr);
};
