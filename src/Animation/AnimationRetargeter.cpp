#include "AnimationRetargeter.hpp"

#include <algorithm>
#include <iostream>
#include <unordered_set>

std::unordered_map<int, int> AnimationRetargeter::buildBoneNameMap(
    const Skeleton& srcSkeleton,
    const Skeleton& dstSkeleton)
{
    std::unordered_map<int, int> boneMap;

    for (int srcIdx = 0; srcIdx < static_cast<int>(srcSkeleton.bones.size()); ++srcIdx) {
        const auto& srcName = srcSkeleton.bones[srcIdx].name;
        const auto it = dstSkeleton.boneNameToIndex.find(srcName);
        if (it != dstSkeleton.boneNameToIndex.end()) {
            boneMap[srcIdx] = it->second;
        }
    }

    return boneMap;
}

RetargetResult AnimationRetargeter::retargetClips(
    const std::vector<AnimationClip>& srcClips,
    const Skeleton& srcSkeleton,
    const Skeleton& dstSkeleton)
{
    const auto boneMap = buildBoneNameMap(srcSkeleton, dstSkeleton);

    std::unordered_set<int> matchedSrcBones;
    RetargetResult result;

    for (const auto& clip : srcClips) {
        AnimationClip retargetedClip;
        retargetedClip.name = clip.name;
        retargetedClip.duration = clip.duration;

        for (const auto& ch : clip.channels) {
            const auto it = boneMap.find(ch.boneIndex);
            if (it == boneMap.end()) continue;

            AnimChannel newCh = ch;
            newCh.boneIndex = it->second;
            retargetedClip.channels.push_back(std::move(newCh));
            matchedSrcBones.insert(ch.boneIndex);
        }

        if (!retargetedClip.channels.empty())
            result.retargetedClips.push_back(std::move(retargetedClip));
    }

    result.matchedCount = static_cast<int>(matchedSrcBones.size());
    for (int srcIdx = 0; srcIdx < static_cast<int>(srcSkeleton.bones.size()); ++srcIdx) {
        if (!matchedSrcBones.count(srcIdx))
            result.unmatchedBones.push_back(srcSkeleton.bones[srcIdx].name);
    }

    if (!result.unmatchedBones.empty()) {
        std::cout << "[Retarget] " << result.matchedCount << " bones matched, "
                  << result.unmatchedBones.size() << " unmatched:\n";
        for (const auto& name : result.unmatchedBones)
            std::cout << "  - " << name << "\n";
    }

    return result;
}