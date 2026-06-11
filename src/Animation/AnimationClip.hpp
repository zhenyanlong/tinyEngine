#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

/** @brief 插值类型 */
enum class AnimInterpolation { Step, Linear, CubicSpline };

/** @brief 一个通道对应一根骨骼的一个属性（平移/旋转/缩放） */
struct AnimChannel {
	int               boneIndex = -1;
	enum class Target { Translation, Rotation, Scale } target = Target::Translation;
	AnimInterpolation interp    = AnimInterpolation::Linear;
	std::vector<float>     times;   ///< 关键帧时间戳（秒）
	std::vector<glm::vec4> values;  ///< vec3 for T/S，quat(xyzw) for R，统一用 vec4 存储
};

/** @brief 一段骨骼动画 */
struct AnimationClip {
	std::string              name;
	float                    duration = 0.f;   ///< max(channel.times.back())
	std::vector<AnimChannel> channels;

	/** @brief 对单根骨骼求值某一时间点的局部 TRS 矩阵（4x4） */
	glm::mat4 evaluateBoneLocalTransform(int boneIndex, float t) const;
};
