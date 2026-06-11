#include "Skeleton.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <cassert>
#include <cmath>
#include <cstdio>

void Skeleton::computeFinalMatrices(
	const std::vector<glm::mat4>& localTransforms,
	std::vector<glm::mat4>&       finalBoneMatrices) const
{
	const int n = static_cast<int>(bones.size());
	finalBoneMatrices.resize(n);

	std::vector<glm::mat4> globalTransforms(n);

	for (int i = 0; i < n; ++i) {
		if (bones[i].parentIndex < 0) {
			globalTransforms[i] = localTransforms[i];
		} else {
			globalTransforms[i] = globalTransforms[bones[i].parentIndex] * localTransforms[i];
		}
		finalBoneMatrices[i] = globalTransforms[i] * bones[i].inverseBindMatrix;
	}
}

// ── 单元测试 ────────────────────────────────────────────────────────────

namespace {

bool mat4Near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f) {
	for (int col = 0; col < 4; ++col) {
		for (int row = 0; row < 4; ++row) {
			if (std::fabs(a[col][row] - b[col][row]) > eps) return false;
		}
	}
	return true;
}

void testSkeleton() {
	// 构造三骨骼链：root → child1 → child2
	Skeleton skel;
	skel.bones.resize(3);

	// Bone 0: root, identity bind
	skel.bones[0].name = "root";
	skel.bones[0].parentIndex = -1;
	skel.bones[0].inverseBindMatrix = glm::mat4(1.f);
	skel.bones[0].localBindTransform = glm::mat4(1.f);
	skel.boneNameToIndex["root"] = 0;

	// Bone 1: child of root, bind translation (2, 0, 0)
	skel.bones[1].name = "spine";
	skel.bones[1].parentIndex = 0;
	skel.bones[1].inverseBindMatrix = glm::mat4(1.f);
	skel.bones[1].localBindTransform = glm::translate(glm::mat4(1.f), glm::vec3(2.f, 0.f, 0.f));
	skel.boneNameToIndex["spine"] = 1;

	// Bone 2: child of spine, bind translation (0, 1, 0)
	skel.bones[2].name = "head";
	skel.bones[2].parentIndex = 1;
	skel.bones[2].inverseBindMatrix = glm::mat4(1.f);
	skel.bones[2].localBindTransform = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 1.f, 0.f));
	skel.boneNameToIndex["head"] = 2;

	// 设置动画帧的局部变换
	std::vector<glm::mat4> locals(3);
	// root: 绕 Z 旋转 90°
	locals[0] = glm::eulerAngleZ(glm::radians(90.f));
	// spine: 沿自身 X 平移 1
	locals[1] = glm::translate(glm::mat4(1.f), glm::vec3(1.f, 0.f, 0.f));
	// head: identity
	locals[2] = glm::mat4(1.f);

	std::vector<glm::mat4> finals;
	skel.computeFinalMatrices(locals, finals);

	// 验证 Bone 0: global = local = rotZ90
	assert(mat4Near(finals[0], locals[0]));

	// 验证 Bone 1: global = global[0] * local[1]
	// global[0] = rotZ90, local[1] = trans(1,0,0)
	// rotZ90 * trans(1,0,0) → trans(0,1,0) * rotZ90
	glm::mat4 expected1 = locals[0] * locals[1];
	assert(mat4Near(finals[1], expected1));

	// 验证 Bone 2: global = global[1] * local[2] = global[1]
	assert(mat4Near(finals[2], expected1));

	// 验证位置: spine 在局部 (1,0,0)，经过 root rotZ90 后应在世界 (0,1,0)
	glm::vec3 spineWorldPos = finals[1] * glm::vec4(0.f, 0.f, 0.f, 1.f);
	assert(std::fabs(spineWorldPos.x - 0.f) < 1e-4f);
	assert(std::fabs(spineWorldPos.y - 1.f) < 1e-4f);
	assert(std::fabs(spineWorldPos.z - 0.f) < 1e-4f);

	// 验证 head: 在 spine 局部 (0,1,0)，spine 世界位置 (0,1,0)，head 世界位置 (0,2,0)
	glm::vec3 headWorldPos = finals[2] * glm::vec4(0.f, 0.f, 0.f, 1.f);
	assert(std::fabs(headWorldPos.x - 0.f) < 1e-4f);
	assert(std::fabs(headWorldPos.y - 2.f) < 1e-4f);
	assert(std::fabs(headWorldPos.z - 0.f) < 1e-4f);

	std::printf("[testSkeleton] All assertions passed.\n");
}

} // anonymous namespace
