#include "SceneSnapshot.hpp"

#include "SceneManager.hpp"
#include "camera.hpp"

#include <algorithm>
#include <vector>

namespace {

nlohmann::json vec3ToJson(const glm::vec3& value)
{
    return nlohmann::json::array({value.x, value.y, value.z});
}

nlohmann::json quatToJson(const glm::quat& value)
{
    return nlohmann::json::array({value.x, value.y, value.z, value.w});
}

nlohmann::json transformToJson(const ObjectTransform& transform)
{
    return {
        {"position", vec3ToJson(transform.position)},
        {"rotation", quatToJson(transform.rotation)},
        {"scale", vec3ToJson(transform.scale)}
    };
}

const char* animatorParamTypeName(AnimatorParam::Type type)
{
    switch (type) {
    case AnimatorParam::Type::Float:   return "float";
    case AnimatorParam::Type::Int:     return "int";
    case AnimatorParam::Type::Bool:    return "bool";
    case AnimatorParam::Type::Trigger: return "trigger";
    }
    return "unknown";
}

nlohmann::json animatorParamValue(const AnimatorParam& param)
{
    switch (param.type) {
    case AnimatorParam::Type::Float:   return param.value.f;
    case AnimatorParam::Type::Int:     return param.value.i;
    case AnimatorParam::Type::Bool:
    case AnimatorParam::Type::Trigger: return param.value.b;
    }
    return nullptr;
}

} // namespace

nlohmann::json SceneSnapshot::capture(const SceneManager& sceneManager,
                                      const Camera& camera,
                                      uint64_t selectedBoxEntityId)
{
    nlohmann::json entities = nlohmann::json::array();
    for (const auto& entity : sceneManager.getModelEntities()) {
        nlohmann::json materialSlots = nlohmann::json::array();
        for (uint32_t materialId : entity.subMeshMaterials) {
            materialSlots.push_back(materialId);
        }

        nlohmann::json clipNames = nlohmann::json::array();
        for (const auto& clip : entity.animationClips) {
            clipNames.push_back(clip.name);
        }

        nlohmann::json animatorParams = nlohmann::json::array();
        for (const auto& param : entity.animatorController.params()) {
            animatorParams.push_back({
                {"name", param.name},
                {"type", animatorParamTypeName(param.type)},
                {"value", animatorParamValue(param)}
            });
        }

        entities.push_back({
            {"entityId", entity.entityId},
            {"type", entity.type == SceneManager::ModelEntity::Type::Camera ? "camera" : "mesh"},
            {"displayName", entity.displayName},
            {"astRelPath", entity.astRelPath},
            {"transform", transformToJson(entity.transform)},
            {"visible", entity.visible},
            {"selected", entity.selected},
            {"materialId", entity.materialId},
            {"subMeshMaterialIds", std::move(materialSlots)},
            {"hasSkin", entity.hasSkin_},
            {"animation", {
                {"clipCount", entity.animationClips.size()},
                {"clipNames", std::move(clipNames)},
                {"assetPath", entity.animationAssetPath},
                {"previewClipIndex", entity.previewClipIndex},
                {"previewTime", entity.previewTime},
                {"previewSpeed", entity.previewSpeed},
                {"controllerPath", entity.animatorControllerPath},
                {"animator", {
                    {"currentState", entity.animatorController.currentStateName()},
                    {"nextState", entity.animatorController.nextStateName()},
                    {"transitioning", entity.animatorController.isTransitioning()},
                    {"blendProgress", entity.animatorController.blendProgress()},
                    {"parameters", std::move(animatorParams)}
                }}
            }}
        });
    }

    std::vector<uint64_t> boxIds;
    boxIds.reserve(sceneManager.getBoxes().size());
    for (const auto& [entityId, position] : sceneManager.getBoxes()) {
        (void)position;
        boxIds.push_back(entityId);
    }
    std::sort(boxIds.begin(), boxIds.end());

    nlohmann::json boxes = nlohmann::json::array();
    for (uint64_t entityId : boxIds) {
        boxes.push_back({
            {"entityId", entityId},
            {"position", vec3ToJson(sceneManager.getBoxes().at(entityId))},
            {"materialId", sceneManager.getBoxMaterialId(entityId)},
            {"selected", entityId == selectedBoxEntityId}
        });
    }

    nlohmann::json result = {
        {"schemaVersion", 1},
        {"counts", {
            {"entities", entities.size()},
            {"boxes", boxes.size()}
        }},
        {"entities", std::move(entities)},
        {"boxes", std::move(boxes)},
        {"camera", {
            {"position", vec3ToJson(camera.Position)},
            {"orientation", quatToJson(camera.GetOrientation())},
            {"forward", vec3ToJson(camera.Forward)},
            {"up", vec3ToJson(camera.Up)},
            {"fovDeg", camera.FovDeg},
            {"nearPlane", camera.NearPlane},
            {"farPlane", camera.FarPlane},
            {"aspectRatio", camera.AspectRatio},
            {"speed", camera.SPEED}
        }}
    };
    return result;
}
