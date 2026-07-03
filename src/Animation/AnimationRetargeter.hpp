#pragma once

#include "AnimationClip.hpp"
#include "Skeleton.hpp"

#include <string>
#include <unordered_map>
#include <vector>

struct RetargetResult {
    std::vector<AnimationClip> retargetedClips;
    std::vector<std::string> unmatchedBones;
    int matchedCount = 0;
};

class AnimationRetargeter {
public:
    static std::unordered_map<int, int> buildBoneNameMap(
        const Skeleton& srcSkeleton,
        const Skeleton& dstSkeleton);

    static RetargetResult retargetClips(
        const std::vector<AnimationClip>& srcClips,
        const Skeleton& srcSkeleton,
        const Skeleton& dstSkeleton);
};