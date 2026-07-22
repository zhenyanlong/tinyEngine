#include "camera.hpp"
#include <glm/gtc/matrix_transform.hpp>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static constexpr float kMaxPitch = 89.0f * 3.14159265358979323846f / 180.0f; // ~1.5533 rad

static float smoothstep01(float x)
{
	x = glm::clamp(x, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

/**
 * @brief Construct from position + look-at target.
 *        Derives pitchAccum_/yawAccum_ from the forward vector, then builds the quaternion.
 */
Camera::Camera(glm::vec3 position, glm::vec3 target, glm::vec3 worldup)
{
	Position = position;
	WorldUp  = worldup;

	glm::vec3 fwd = glm::normalize(target - position);
	pitchAccum_   = glm::asin(glm::clamp(fwd.y, -1.0f, 1.0f));
	yawAccum_     = glm::atan(fwd.x, -fwd.z);
	RebuildOrientation();
}

/**
 * @brief Construct from position + pitch/yaw angles (radians).
 */
Camera::Camera(glm::vec3 position, float pitch, float yaw, glm::vec3 worldup)
{
	Position    = position;
	WorldUp     = worldup;
	pitchAccum_ = glm::clamp(pitch, -kMaxPitch, kMaxPitch);
	yawAccum_   = yaw;
	RebuildOrientation();
}

// ---------------------------------------------------------------------------
// Private: rebuild quaternion & axis vectors
// ---------------------------------------------------------------------------

/**
 * @brief Rebuild orientation_ from pitchAccum_ / yawAccum_, then refresh Forward/Right/Up.
 *
 * Composition order: yaw around world Y first, then pitch around local X. No roll.
 * Equivalent to standard FPS-camera Euler rotation, stored as a quaternion.
 */
void Camera::RebuildOrientation()
{
	glm::quat yawQ   = glm::angleAxis(yawAccum_,   WorldUp);
	glm::quat pitchQ = glm::angleAxis(pitchAccum_, glm::vec3(1.0f, 0.0f, 0.0f));
	orientation_     = glm::normalize(yawQ * pitchQ);
	UpdataCameraVectors();
}

void Camera::SetPitchYaw(float pitch, float yaw)
{
    pitchAccum_ = glm::clamp(pitch, glm::radians(-89.0f), glm::radians(89.0f));
    yawAccum_   = yaw;
    RebuildOrientation();
}

void Camera::SetOrientation(const glm::quat& q)
{
    orientation_ = glm::normalize(q);
    UpdataCameraVectors();
    // 回解 pitch/yaw，保持与 SmoothFocus 兼容
    pitchAccum_ = glm::asin(glm::clamp(Forward.y, -1.0f, 1.0f));
    yawAccum_   = glm::atan(Forward.x, -Forward.z);
}

/**
 * @brief Apply orientation_ to basis vectors, producing Forward / Right / Up,
 *        and rebuild worldTransform_ (Camera-to-World matrix).
 *
 * Convention: camera local space has forward = -Z, right = +X, up = +Y.
 *
 * worldTransform_ column layout:
 *   col 0 = Right    (camera X axis in world space)
 *   col 1 = Up       (camera Y axis in world space)
 *   col 2 = -Forward (camera Z axis, right-handed camera looks along -Z)
 *   col 3 = Position (world position, w=1)
 */
void Camera::UpdataCameraVectors()
{
	Forward = glm::normalize(orientation_ * glm::vec3( 0.0f,  0.0f, -1.0f));
	Right   = glm::normalize(orientation_ * glm::vec3( 1.0f,  0.0f,  0.0f));
	Up      = glm::normalize(orientation_ * glm::vec3( 0.0f,  1.0f,  0.0f));

	worldTransform_[0] = glm::vec4(Right,    0.0f);
	worldTransform_[1] = glm::vec4(Up,       0.0f);
	worldTransform_[2] = glm::vec4(-Forward, 0.0f);
	worldTransform_[3] = glm::vec4(Position, 1.0f);
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

/**
 * @brief Derive View matrix from worldTransform_ (Camera-to-World), avoiding glm::lookAt / glm::inverse.
 *
 * View = inverse(worldTransform_).
 * Because the rotation part is orthogonal (no scale), inverse(R) = transpose(R).
 *   Rt   = transpose(mat3(worldTransform_))
 *   view = mat4(Rt),  view[3] = vec4( -(Rt * Position), 1 )
 */
glm::mat4 Camera::GetViewMatrix() const
{
	glm::mat3 R  = glm::mat3(worldTransform_);
	glm::mat3 Rt = glm::transpose(R);
	glm::mat4 view{ glm::mat4(Rt) };
	view[3] = glm::vec4(-(Rt * Position), 1.0f);
	return view;
}

void Camera::SetAspectRatio(float width, float height)
{
	AspectRatio = (height > 0.0f) ? (width / height) : 1.0f;
}

glm::mat4 Camera::GetProjectionMatrix() const
{
	glm::mat4 p = glm::perspective(glm::radians(FovDeg), AspectRatio, NearPlane, FarPlane);
	p[1][1] *= -1.0f; // Vulkan NDC Y-flip
	return p;
}

glm::mat4 Camera::GetProjectionMatrixNoFlip() const
{
	return glm::perspective(glm::radians(FovDeg), AspectRatio, NearPlane, FarPlane);
}

glm::mat4 Camera::GetViewProjectionMatrix() const
{
	return GetProjectionMatrix() * GetViewMatrix();
}

glm::vec3 Camera::GetWorldPosition() const
{
	return Position;
}

glm::mat4 Camera::GetWorldTransform() const
{
	return worldTransform_;
}

/**
 * @brief FPS-style rotation using incremental quaternions (fixes arc-drift bug).
 *
 * Root cause: rebuilding the quaternion from accumulated yawAccum_/pitchAccum_ each frame
 * causes horizontal drags to arc along a latitude line when pitch != 0.
 *
 * Fix: apply incremental rotations each frame:
 *   - yaw   delta: rotate around world Y axis
 *   - pitch delta: rotate around local Right   -> vertical drag   = vertical screen motion
 * After applying, back-solve yawAccum_/pitchAccum_ so SmoothFocus can still use RebuildOrientation.
 */
void Camera::ProcessMouseMovement(float deltaX, float deltaY)
{
	// Incremental quaternions: yaw is always world-up; pitch remains camera-local right.
	const glm::vec3 yawAxis = glm::length(WorldUp) > 1e-6f
		? glm::normalize(WorldUp) : glm::vec3(0.0f, 1.0f, 0.0f);
	const glm::quat deltaYaw   = glm::angleAxis(-deltaX * SensitivityYaw,   yawAxis);
	const glm::quat deltaPitch = glm::angleAxis(-deltaY * SensitivityPitch, Right);

	// Apply pitch first in local space, then yaw around world Y
	glm::quat candidate = glm::normalize(deltaYaw * deltaPitch * orientation_);

	// Clamp pitch: if new orientation exceeds +-89 deg, keep only the yaw part
	const glm::vec3 candFwd = glm::normalize(candidate * glm::vec3(0.0f, 0.0f, -1.0f));
	if (glm::abs(candFwd.y) > glm::sin(kMaxPitch)) {
		candidate = glm::normalize(deltaYaw * orientation_);
	}

	orientation_ = candidate;
	UpdataCameraVectors(); // refresh Forward / Right / Up / worldTransform_

	// Back-solve yawAccum_ / pitchAccum_ for use by SmoothFocus -> RebuildOrientation
	yawAccum_   = glm::atan(Forward.x, -Forward.z);
	pitchAccum_ = glm::asin(glm::clamp(Forward.y, -1.0f, 1.0f));
}

/**
 * @brief Update Position based on speedX/Y/Z, SPEED, and deltaTime (frame-rate independent).
 *        Only the translation column of worldTransform_ is updated; rotation is unchanged.
 */
void Camera::UpdataCameraPosition(float deltaTime)
{
	const glm::vec3 vertical = glm::length(WorldUp) > 1e-6f
		? glm::normalize(WorldUp) : glm::vec3(0.0f, 1.0f, 0.0f);
	Position += (Forward * speedZ + Right * speedX + vertical * speedY) * SPEED * deltaTime;
	worldTransform_[3] = glm::vec4(Position, 1.0f);
}

void Camera::SetSpeed(float speed)
{
	SPEED = speed;
}

/**
 * @brief Begin a smooth-focus animation toward a world point.
 */
void Camera::BeginSmoothFocus(const glm::vec3& worldFocusPoint, float cameraDistance, float durationSec)
{
	smoothFocusTarget_  = worldFocusPoint;
	smoothFocusActive_  = true;
	smoothFocusT_       = 0.f;
	smoothFocusDuration_ = durationSec > 0.01f ? durationSec : 0.65f;
	smoothFocusPos0_    = Position;

	// Update orbit center so right-drag continues to orbit around this point after focus
	orbitCenter_ = worldFocusPoint;

	glm::vec3 w = Position - worldFocusPoint;
	if (glm::length(w) < 1e-3f)
		w = glm::vec3(0.35f, 0.4f, 1.0f);
	w = glm::normalize(w);
	const float dist  = glm::max(0.05f, cameraDistance);
	smoothFocusPos1_  = worldFocusPoint + w * dist;
}

/**
 * @brief Advance the smooth-focus animation each frame.
 *
 * At animation end, writes the look direction back into pitchAccum_/yawAccum_ and
 * rebuilds the quaternion, so subsequent WASD movement aligns with the new look direction.
 * RebuildOrientation -> UpdataCameraVectors -> worldTransform_ fully rebuilt.
 */
void Camera::UpdateSmoothFocus(float deltaTime)
{
	if (!smoothFocusActive_)
		return;

	smoothFocusT_ += deltaTime / smoothFocusDuration_;
	const float u  = smoothstep01(smoothFocusT_);
	Position       = glm::mix(smoothFocusPos0_, smoothFocusPos1_, u);

	glm::vec3 toFocus = smoothFocusTarget_ - Position;
	const float len   = glm::length(toFocus);
	if (len > 1e-5f) {
		glm::vec3 fwd = toFocus / len;
		pitchAccum_ = glm::asin(glm::clamp(fwd.y, -1.0f, 1.0f));
		yawAccum_   = glm::atan(fwd.x, -fwd.z);
		RebuildOrientation(); // -> UpdataCameraVectors -> worldTransform_ including Position
	} else {
		// Direction unchanged; only update the translation column
		worldTransform_[3] = glm::vec4(Position, 1.0f);
	}

	if (smoothFocusT_ >= 1.0f)
		smoothFocusActive_ = false;
}
