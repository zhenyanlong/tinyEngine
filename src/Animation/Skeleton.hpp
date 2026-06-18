#pragma once

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct Bone {
    std::string name;
    int         parentIndex = -1;
    glm::mat4   inverseBindMatrix{1.f};
    glm::mat4   localBindTransform{1.f};
    glm::mat4   globalBindTransform{1.f};
};

struct Skeleton {
    std::vector<Bone> bones;
    std::vector<std::vector<int>> skinBoneIndices;
    std::unordered_map<std::string, int> boneNameToIndex;

    void computeFinalMatrices(const std::vector<glm::mat4>& localTransforms,
                              std::vector<glm::mat4>& finalBoneMatrices) const;
};
