#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

/** @brief 单根骨骼（关节） */
struct Bone {
	std::string name;
	int         parentIndex = -1;        ///< -1 表示根骨骼
	glm::mat4   inverseBindMatrix{};     ///< glTF skin.inverseBindMatrices[i]
	glm::mat4   localBindTransform{};    ///< 绑定姿势下的局部 TRS（来自 glTF node）
};

/** @brief 整个骨架 */
struct Skeleton {
	std::vector<Bone>                  bones;           ///< 深度优先排列，父骨骼下标总 < 子骨骼
	std::unordered_map<std::string, int> boneNameToIndex;

	/** @brief 根据每根骨骼的局部变换，递推出最终骨骼矩阵 */
	void computeFinalMatrices(
		const std::vector<glm::mat4>& localTransforms,   ///< 输入：每帧由动画求值系统填入
		std::vector<glm::mat4>&       finalBoneMatrices  ///< 输出：上传到 GPU
	) const;
};
