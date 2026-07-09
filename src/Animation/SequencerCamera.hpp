#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

/**
 * @brief Sequencer 摄像机，封装独立的相机状态供 PiP 小窗和 CameraPath 回放使用。
 *
 * 与主 Camera 类解耦，提供独立的 position/orientation/fov/aspectRatio。
 */
struct SequencerCamera {
    std::string name;

    glm::vec3 position{0.f, 0.f, 5.f};
    glm::quat orientation{1.f, 0.f, 0.f, 0.f};
    float     fovDeg       = 45.f;
    float     aspectRatio  = 4.f / 3.f;
    float     nearPlane    = 0.1f;
    float     farPlane     = 100.f;

    glm::mat4 getViewMatrix() const;
    /** @brief 标准投影矩阵（无 Vulkan Y-flip） */
    glm::mat4 getProjMatrix() const;
    /** @brief Vulkan NDC 投影矩阵（Y-flip，与主 Camera 一致） */
    glm::mat4 getProjMatrixVulkan() const;

    /** @brief 从主 Camera 同步属性（位置、朝向、FOV、Aspect） */
    void syncFromMainCamera(const class Camera& mainCam);

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;
};