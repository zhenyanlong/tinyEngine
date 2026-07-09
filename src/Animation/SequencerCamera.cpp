#include "SequencerCamera.hpp"

#include "../camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

glm::vec3 SequencerCamera::forward() const
{
    return glm::normalize(orientation * glm::vec3(0.0f, 0.0f, -1.0f));
}

glm::vec3 SequencerCamera::right() const
{
    return glm::normalize(orientation * glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 SequencerCamera::up() const
{
    return glm::normalize(orientation * glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 SequencerCamera::getViewMatrix() const
{
    const glm::vec3 fwd = forward();
    const glm::vec3 upDir = up();
    return glm::lookAt(position, position + fwd, upDir);
}

glm::mat4 SequencerCamera::getProjMatrix() const
{
    return glm::perspective(glm::radians(fovDeg), aspectRatio, nearPlane, farPlane);
}

glm::mat4 SequencerCamera::getProjMatrixVulkan() const
{
    glm::mat4 p = glm::perspective(glm::radians(fovDeg), aspectRatio, nearPlane, farPlane);
    p[1][1] *= -1.0f; // Vulkan NDC Y-flip
    return p;
}

void SequencerCamera::syncFromMainCamera(const Camera& mainCam)
{
    position   = mainCam.Position;
    orientation = mainCam.GetOrientation();
    fovDeg     = mainCam.FovDeg;
    aspectRatio = mainCam.AspectRatio;
    nearPlane  = mainCam.NearPlane;
    farPlane   = mainCam.FarPlane;
}