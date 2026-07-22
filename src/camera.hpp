#ifndef CAMERA_H
#define CAMERA_H
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif // !GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtx/quaternion.hpp>

/**
 * @brief WASD 移动方向枚举，供上层调用者按需扩展。
 */
enum Movement {
	FORWARD,
	BACKWARD,
	LEFT,
	RIGHT
};

/**
 * @class Camera
 * @brief FPS 风格自由相机，姿态由四元数 orientation_ 描述。
 *
 * @details
 * - 方向由两个独立累积量驱动：
 *     yawAccum_   — 绕世界 WorldUp 轴偏航（无限制）
 *     pitchAccum_ — 绕局部 X 轴俯仰（钳位 ±89°）
 *   合成：orientation_ = yawQuat * pitchQuat
 * - Forward / Right / Up 由 orientation_ 旋转基向量得出，每帧更新。
 * - GetViewMatrix() 使用 glm::lookAt(Position, Position+Forward, WorldUp)。
 * - UpdataCameraPosition(dt) 接受 deltaTime，保证帧率无关移动速度。
 * - SmoothFocus 平滑插值位置并写回 pitchAccum_/yawAccum_，不干扰四元数逻辑。
 */
class Camera
{
public:
	/**
	 * @brief 由"位置 + 注视目标 + 世界上方向"构造。
	 * @param position 相机世界坐标
	 * @param target   注视点（世界坐标）
	 * @param worldup  世界上方向，通常 (0,1,0)
	 */
	Camera(glm::vec3 position, glm::vec3 target, glm::vec3 worldup);

	/**
	 * @brief 由"位置 + 俯仰/偏航（弧度）+ 世界上方向"构造。
	 * @param position 相机世界坐标
	 * @param pitch    俯仰角（弧度），抬头为正，钳位 ±89°
	 * @param yaw      偏航角（弧度），绕 WorldUp 旋转
	 * @param worldup  世界上方向
	 */
	Camera(glm::vec3 position, float pitch, float yaw, glm::vec3 worldup);

	/** @brief 相机世界坐标 */
	glm::vec3 Position;
	/** @brief 单位前向量，由 orientation_ 推导，每帧更新 */
	glm::vec3 Forward;
	/** @brief 单位右向量 */
	glm::vec3 Right;
	/** @brief 单位上向量（相机本地） */
	glm::vec3 Up;
	/** @brief 世界上方向参考，通常 (0,1,0) */
	glm::vec3 WorldUp;

	/** @brief 俯仰灵敏度（弧度/像素），影响鼠标 Y 轴 */
	float SensitivityPitch = 0.003f;
	/** @brief 偏航灵敏度（弧度/像素），影响鼠标 X 轴 */
	float SensitivityYaw   = 0.003f;

	/** @brief 沿 Right 方向的归一化速度输入 [-1,1]，由 A/D 键驱动 */
	float speedX = 0.0f;
	/** @brief 沿 WorldUp 的归一化升降输入：E=+1，Q=-1。 */
	float speedY = 0.0f;
	/** @brief 沿 Forward 方向的归一化速度输入 [-1,1]，由 W/S 键驱动 */
	float speedZ = 0.0f;

	/** @brief 每单位时间的基础移动速度（单位/秒），由 UI 滑条设置 */
	float SPEED = 5.0f;

	// ── Projection 参数 ──────────────────────────────────────────────────────
	/** @brief 水平/垂直视角（度），默认 45° */
	float FovDeg     = 45.0f;
	/** @brief 近裁剪平面距离 */
	float NearPlane  = 0.1f;
	/** @brief 远裁剪平面距离 */
	float FarPlane   = 500.0f;
	/** @brief 宽高比（width/height），由 SetAspectRatio 设置 */
	float AspectRatio = 1.0f;

	/**
	 * @brief 在 Swapchain 尺寸变化时同步更新宽高比。
	 * @param width  帧缓冲宽度（像素）
	 * @param height 帧缓冲高度（像素）
	 */
	void SetAspectRatio(float width, float height);

/**
 * @brief 返回 View 矩阵，由 worldTransform_ 推导（无 glm::lookAt，O(1) 转置旋转）。
 */
glm::mat4 GetViewMatrix() const;

/**
 * @brief 返回含 Vulkan Y-flip 的 Projection 矩阵（供正常渲染用）。
 */
glm::mat4 GetProjectionMatrix() const;

/**
 * @brief 返回不含 Y-flip 的 Projection 矩阵（供 ImGuizmo 等 OpenGL 惯例代码使用）。
 */
glm::mat4 GetProjectionMatrixNoFlip() const;

/**
 * @brief 返回预乘 proj * view（供 UBO/Shader 直接使用，省一次矩阵乘法）。
 */
glm::mat4 GetViewProjectionMatrix() const;

/**
 * @brief 返回相机世界坐标，直接取 worldTransform_[3]，替代外部 glm::inverse(view)[3]。
 */
glm::vec3 GetWorldPosition() const;

/**
 * @brief 返回相机的 Camera-to-World 矩阵，供需要相机位姿的外部系统使用。
 */
glm::mat4 GetWorldTransform() const;

	/**
	 * @brief FPS 风格旋转：右键拖拽时相机原地转头（yaw/pitch）。
	 *        鼠标向右 → yaw 减小 → 相机右转 → 场景向左偏移。
	 * @param deltaX 当前帧相对上一帧的 X 像素位移（向右为正）
	 * @param deltaY 当前帧相对上一帧的 Y 像素位移（向下为正）
	 */
	void ProcessMouseMovement(float deltaX, float deltaY);

	/**
	 * @brief 设置轨道旋转的中心点（世界坐标），通常为模型中心。
	 *        BeginSmoothFocus 会自动调用；模型加载后也应手动调用一次。
	 */
	void SetOrbitCenter(const glm::vec3& center) { orbitCenter_ = center; }

	/**
	 * @brief 根据 speedX/Y/Z、SPEED 和 deltaTime 更新 Position。
	 * @param deltaTime 本帧时长（秒），保证移动速度帧率无关
	 */
	void UpdataCameraPosition(float deltaTime);

	/** @brief 设置移动速度基准值，通常由 UI 滑条调用 */
	void SetSpeed(float speed);

	/**
	 * @brief 开始平滑聚焦动画，将相机平滑移向 worldFocusPoint 附近并注视该点。
	 * @param worldFocusPoint 目标聚焦点（世界坐标）
	 * @param cameraDistance  动画结束时相机距目标点的距离
	 * @param durationSec     动画时长（秒），<0.01 时使用默认 0.65s
	 */
	void BeginSmoothFocus(const glm::vec3& worldFocusPoint, float cameraDistance, float durationSec);

	/**
	 * @brief 逐帧推进平滑聚焦，应每帧调用并传入 deltaTime。
	 */
	void UpdateSmoothFocus(float deltaTime);

	/** @brief 平滑聚焦进行中时返回 true，期间应禁用 WASD 位移 */
	[[nodiscard]] bool IsSmoothFocusActive() const { return smoothFocusActive_; }

	/** @brief 返回当前俯仰角（弧度） */
	[[nodiscard]] float GetPitch() const { return pitchAccum_; }

	/** @brief 返回当前偏航角（弧度） */
	[[nodiscard]] float GetYaw()   const { return yawAccum_; }

	/** @brief 强制设置俯仰/偏航并重建方向向量（供场景加载用） */
	void SetPitchYaw(float pitch, float yaw);

	/** @brief 直接设置相机四元数姿态（供场景加载用），自动回解 pitch/yaw */
	void SetOrientation(const glm::quat& q);

	/** @brief 返回相机姿态四元数（供场景保存用） */
	[[nodiscard]] const glm::quat& GetOrientation() const { return orientation_; }

private:
	/** @brief 相机姿态四元数，由 yawAccum_ 和 pitchAccum_ 组合而成 */
	glm::quat orientation_;

	/** @brief 累积偏航角（弧度），绕 WorldUp 旋转，无限制 */
	float yawAccum_   = 0.0f;
	/** @brief 累积俯仰角（弧度），钳位在 [-89°, +89°] */
	float pitchAccum_ = 0.0f;

	/** @brief 相机的 Camera-to-World 矩阵；从 UpdataCameraVectors() 维护，始终与 Position/orientation_ 同步 */
	glm::mat4 worldTransform_{ 1.0f };

	/** @brief 从 orientation_ 重建 Forward / Right / Up，同时重建 worldTransform_ */
	void UpdataCameraVectors();

	/** @brief 由 pitchAccum_ / yawAccum_ 重建 orientation_，然后调用 UpdataCameraVectors */
	void RebuildOrientation();

	bool      smoothFocusActive_   = false;
	float     smoothFocusT_        = 0.f;
	float     smoothFocusDuration_ = 0.65f;
	glm::vec3 smoothFocusPos0_{};
	glm::vec3 smoothFocusPos1_{};
	glm::vec3 smoothFocusTarget_{};

	/** @brief 轨道旋转中心（世界坐标），右键拖拽时相机围绕此点旋转 */
	glm::vec3 orbitCenter_{ 0.0f, 0.0f, 0.0f };
};
#endif
