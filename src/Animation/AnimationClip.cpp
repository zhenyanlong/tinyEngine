#include "AnimationClip.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

BoneLocalTransform decomposeLocalTransform(const glm::mat4& m) {
	BoneLocalTransform parts;
	glm::vec3 skew{};
	glm::vec4 perspective{};
	if (glm::decompose(m, parts.scale, parts.rotation, parts.translation, skew, perspective)) {
		parts.rotation = glm::normalize(parts.rotation);
	}
	return parts;
}

/** @brief 在已排序的 times 中二分查找 t 所在的区间 [idx, idx+1]，返回 idx */
int findInterval(const std::vector<float>& times, float t) {
	if (times.empty()) return -1;
	if (t <= times.front()) return 0;
	for (size_t i = 0; i + 1 < times.size(); ++i) {
		if (t >= times[i] && t <= times[i + 1]) return static_cast<int>(i);
	}
	return static_cast<int>(times.size()) - 2;
}

/** @brief Hermite 插值（glTF CUBICSPLINE） */
glm::vec4 cubicSpline(const glm::vec4& v0, const glm::vec4& outTan0,
                      const glm::vec4& v1, const glm::vec4& inTan1,
                      float t, float tDelta)
{
	const float t2 = t * t;
	const float t3 = t2 * t;
	const float h00 =  2.f * t3 - 3.f * t2 + 1.f;
	const float h10 =        t3 - 2.f * t2 + t;
	const float h01 = -2.f * t3 + 3.f * t2;
	const float h11 =        t3 -       t2;
	return h00 * v0 + h10 * tDelta * outTan0 + h01 * v1 + h11 * tDelta * inTan1;
}

} // anonymous namespace

glm::mat4 BoneLocalTransform::toMatrix() const {
	const glm::mat4 mT = glm::translate(glm::mat4(1.f), translation);
	const glm::mat4 mR = glm::mat4_cast(glm::normalize(rotation));
	const glm::mat4 mS = glm::scale(glm::mat4(1.f), scale);
	return mT * mR * mS;
}

glm::mat4 AnimationClip::evaluateBoneLocalTransform(int boneIndex, float t) const {
	return evaluateBoneLocalTransform(boneIndex, t, glm::mat4(1.f));
}

glm::mat4 AnimationClip::evaluateBoneLocalTransform(int boneIndex, float t,
                                                    const glm::mat4& fallbackLocalTransform) const {
	return evaluateBoneLocalTransformParts(boneIndex, t, fallbackLocalTransform).toMatrix();
}

BoneLocalTransform AnimationClip::evaluateBoneLocalTransformParts(
	int boneIndex, float t, const glm::mat4& fallbackLocalTransform) const {
	// 收集该骨骼的 T/R/S 通道
	const AnimChannel* chT = nullptr;
	const AnimChannel* chR = nullptr;
	const AnimChannel* chS = nullptr;
	for (const auto& ch : channels) {
		if (ch.boneIndex != boneIndex) continue;
		if      (ch.target == AnimChannel::Target::Translation) chT = &ch;
		else if (ch.target == AnimChannel::Target::Rotation)    chR = &ch;
		else if (ch.target == AnimChannel::Target::Scale)       chS = &ch;
	}

	// 求值函数
	auto evalVec = [](const AnimChannel& ch, float tt) -> glm::vec3 {
		const int n = static_cast<int>(ch.times.size());
		if (n == 0) return glm::vec3(0.f);
		if (ch.interp == AnimInterpolation::CubicSpline) {
			if (tt <= ch.times.front()) return glm::vec3(ch.values[1]);
			if (tt >= ch.times.back())  return glm::vec3(ch.values[(n - 1) * 3 + 1]);
		} else {
			if (tt <= ch.times.front()) return glm::vec3(ch.values.front());
			if (tt >= ch.times.back())  return glm::vec3(ch.values.back());
		}

		const int i = findInterval(ch.times, tt);
		if (i < 0 || i + 1 >= n) {
			return ch.interp == AnimInterpolation::CubicSpline
				? glm::vec3(ch.values[(n - 1) * 3 + 1])
				: glm::vec3(ch.values.back());
		}

		const float t0 = ch.times[i];
		const float t1 = ch.times[i + 1];
		const float dt = t1 - t0;
		const float alpha = (dt > 1e-8f) ? (tt - t0) / dt : 0.f;

		switch (ch.interp) {
		case AnimInterpolation::Step:
			return glm::vec3(ch.values[i]);
		case AnimInterpolation::Linear:
			return glm::mix(glm::vec3(ch.values[i]), glm::vec3(ch.values[i + 1]), alpha);
		case AnimInterpolation::CubicSpline: {
			const int k = i * 3;
			// values 布局：[inTangent, value, outTangent] per keyframe
			const glm::vec4 inTan0  = ch.values[k + 0];
			const glm::vec4 val0    = ch.values[k + 1];
			const glm::vec4 outTan0 = ch.values[k + 2];
			const glm::vec4 inTan1  = ch.values[k + 3];
			const glm::vec4 val1    = ch.values[k + 4];
			const glm::vec4 outTan1 = ch.values[k + 5];
			return glm::vec3(cubicSpline(val0, outTan0, val1, inTan1, alpha, dt));
		}
		}
		return glm::vec3(ch.values[i]);
	};

	auto evalQuat = [](const AnimChannel& ch, float tt) -> glm::quat {
		const int n = static_cast<int>(ch.times.size());
		if (n == 0) return glm::quat(1.f, 0.f, 0.f, 0.f);
		const auto toQuat = [](const glm::vec4& v) {
			return glm::quat(v.w, v.x, v.y, v.z);
		};
		if (ch.interp == AnimInterpolation::CubicSpline) {
			if (tt <= ch.times.front()) return toQuat(ch.values[1]);
			if (tt >= ch.times.back())  return toQuat(ch.values[(n - 1) * 3 + 1]);
		} else {
			if (tt <= ch.times.front()) return toQuat(ch.values.front());
			if (tt >= ch.times.back())  return toQuat(ch.values.back());
		}

		const int i = findInterval(ch.times, tt);
		if (i < 0 || i + 1 >= n) {
			return ch.interp == AnimInterpolation::CubicSpline
				? toQuat(ch.values[(n - 1) * 3 + 1])
				: toQuat(ch.values.back());
		}

		const float t0 = ch.times[i];
		const float t1 = ch.times[i + 1];
		const float dt = t1 - t0;
		const float alpha = (dt > 1e-8f) ? (tt - t0) / dt : 0.f;

		switch (ch.interp) {
		case AnimInterpolation::Step:
			return toQuat(ch.values[i]);
		case AnimInterpolation::Linear:
			return glm::slerp(toQuat(ch.values[i]), toQuat(ch.values[i + 1]), alpha);
		case AnimInterpolation::CubicSpline: {
			const int k = i * 3;
			const glm::quat inTan0  = toQuat(ch.values[k + 0]);
			const glm::quat val0    = toQuat(ch.values[k + 1]);
			const glm::quat outTan0 = toQuat(ch.values[k + 2]);
			const glm::quat inTan1  = toQuat(ch.values[k + 3]);
			const glm::quat val1    = toQuat(ch.values[k + 4]);
			const glm::quat outTan1 = toQuat(ch.values[k + 5]);
			const glm::vec4 r = cubicSpline(glm::vec4(val0.x, val0.y, val0.z, val0.w),
			                                glm::vec4(outTan0.x, outTan0.y, outTan0.z, outTan0.w),
			                                glm::vec4(val1.x, val1.y, val1.z, val1.w),
			                                glm::vec4(inTan1.x, inTan1.y, inTan1.z, inTan1.w),
			                                alpha, dt);
			return glm::normalize(glm::quat(r.w, r.x, r.y, r.z));
		}
		}
		return toQuat(ch.values[i]);
	};

	// 默认值：T=(0,0,0), R=identity, S=(1,1,1)
	const BoneLocalTransform fallback = decomposeLocalTransform(fallbackLocalTransform);
	BoneLocalTransform result;
	result.translation = chT ? evalVec(*chT, t) : fallback.translation;
	result.rotation = glm::normalize(chR ? evalQuat(*chR, t) : fallback.rotation);
	result.scale = chS ? evalVec(*chS, t) : fallback.scale;
	return result;
}
