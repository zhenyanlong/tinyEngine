#include "Application.hpp"
#include "Animation/AnimationAssetLoader.hpp"
#include "Animation/AnimationRetargeter.hpp"
#include "FbxImporter.hpp"
#include "MaterialAssetLoader.hpp"
#include "Mcp/SceneSnapshot.hpp"
#include "SceneSerializer.hpp"
#include "TinyEngineDebug.hpp"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cstring>
#include "cgltf.h"
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// STB_IMAGE_IMPLEMENTATION defined globally; only TextureManager.cpp implements it
#undef STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

namespace {

using nlohmann::json;

std::string sanitizeAssetName(std::string name)
{
    for (char& c : name) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?'
            || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    return name;
}

std::string normalizeSequenceBindingPath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0)
        path.erase(0, 2);
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return path;
}

std::string inferLegacySequenceTargetName(const SequenceTrack& track)
{
    std::string name;
    std::string prefix;
    std::string suffix;
    if (track.type == TrackType::TransformKeyframe) {
        name = track.keyframeTrack.name.empty() ? track.name : track.keyframeTrack.name;
        prefix = "[T] ";
        suffix = " Transform";
    } else if (track.type == TrackType::AnimatorKeyframe) {
        name = track.animatorTrack.name.empty() ? track.name : track.animatorTrack.name;
        prefix = "[A] ";
        suffix = " Animator";
    } else {
        return {};
    }

    if (name.rfind(prefix, 0) != 0 || name.size() <= prefix.size() + suffix.size()
        || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return {};
    }
    return name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
}

void getSequenceBindingMetadata(const SequenceTrack& track,
                                uint64_t& entityId,
                                const std::string*& astRelPath,
                                const std::string*& displayName)
{
    if (track.type == TrackType::TransformKeyframe) {
        entityId = track.keyframeTrack.targetEntityId;
        astRelPath = &track.keyframeTrack.targetAstRelPath;
        displayName = &track.keyframeTrack.targetDisplayName;
    } else {
        entityId = track.animatorTrack.targetEntityId;
        astRelPath = &track.animatorTrack.targetAstRelPath;
        displayName = &track.animatorTrack.targetDisplayName;
    }
}

void setSequenceBindingMetadata(SequenceTrack& track,
                                const SceneManager::ModelEntity& entity)
{
    if (track.type == TrackType::TransformKeyframe) {
        track.keyframeTrack.targetEntityId = entity.entityId;
        track.keyframeTrack.targetAstRelPath = entity.astRelPath;
        track.keyframeTrack.targetDisplayName = entity.displayName;
    } else if (track.type == TrackType::AnimatorKeyframe) {
        track.animatorTrack.targetEntityId = entity.entityId;
        track.animatorTrack.targetAstRelPath = entity.astRelPath;
        track.animatorTrack.targetDisplayName = entity.displayName;
    }
}

bool sequenceBindingMetadataMatches(const SceneManager::ModelEntity& entity,
                                    const std::string& astRelPath,
                                    const std::string& displayName)
{
    if (!astRelPath.empty()
        && normalizeSequenceBindingPath(entity.astRelPath) != normalizeSequenceBindingPath(astRelPath)) {
        return false;
    }
    if (!displayName.empty() && entity.displayName != displayName)
        return false;
    return !astRelPath.empty() || !displayName.empty();
}

std::filesystem::path makeUniquePath(const std::filesystem::path& desired)
{
    if (!std::filesystem::exists(desired))
        return desired;

    const auto parent = desired.parent_path();
    const auto stem = desired.stem().string();
    const auto ext = desired.extension().string();
    for (int i = 1; i < 10000; ++i) {
        char suffix[16]{};
        std::snprintf(suffix, sizeof(suffix), "_%03d", i);
        std::filesystem::path candidate = parent / (stem + suffix + ext);
        if (!std::filesystem::exists(candidate))
            return candidate;
    }
    return desired;
}

std::string makeContentRelDir(const std::string& subFolder, const std::string& assetName)
{
    std::string rel = "content/";
    if (!subFolder.empty()) {
        rel += subFolder;
        if (rel.back() != '/') rel += "/";
    }
    rel += assetName;
    return rel;
}

std::string relativeToRes(const std::filesystem::path& path, const std::filesystem::path& resRoot)
{
    std::error_code ec;
    const auto rel = std::filesystem::relative(path, resRoot, ec);
    return ec ? path.generic_string() : rel.generic_string();
}

std::string toLowerCopy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size()
        && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string stripResPrefix(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.rfind("res/", 0) == 0)
        return path.substr(4);
    return path;
}

std::string defaultAnimBaseName(const std::string& meshAstRelPath)
{
    std::string base = std::filesystem::path(meshAstRelPath).stem().string();
    if (endsWith(base, ".mesh"))
        base.resize(base.size() - 5);
    return sanitizeAssetName(base.empty() ? std::string("animation") : base);
}

json invalidParams(std::string message)
{
    return {{"error", {{"code", "invalid_params"}, {"message", std::move(message)}}}};
}

bool finiteVec3(const json& value, glm::vec3& result)
{
    if (!value.is_array() || value.size() != 3)
        return false;
    for (size_t i = 0; i < 3; ++i) {
        if (!value[i].is_number()) return false;
        const double component = value[i].get<double>();
        if (!std::isfinite(component)) return false;
        result[static_cast<int>(i)] = static_cast<float>(component);
    }
    return true;
}

bool finiteQuatXyzw(const json& value, glm::quat& result)
{
    if (!value.is_array() || value.size() != 4)
        return false;
    float components[4]{};
    for (size_t i = 0; i < 4; ++i) {
        if (!value[i].is_number()) return false;
        const double component = value[i].get<double>();
        if (!std::isfinite(component)) return false;
        components[i] = static_cast<float>(component);
    }
    result = glm::quat(components[3], components[0], components[1], components[2]);
    const float norm = glm::length(result);
    if (!std::isfinite(norm) || norm < 1e-6f)
        return false;
    result = glm::normalize(result);
    return true;
}

bool applyTransformParams(const json& params, ObjectTransform& transform,
                          bool& changed, std::string& error)
{
    changed = false;
    if (!params.is_object()) {
        error = "params must be an object";
        return false;
    }

    if (params.contains("position")) {
        glm::vec3 position;
        if (!finiteVec3(params["position"], position)) {
            error = "position must be three finite numbers";
            return false;
        }
        transform.position = position;
        changed = true;
    }

    if (params.contains("scale")) {
        glm::vec3 scale;
        if (!finiteVec3(params["scale"], scale)
            || glm::any(glm::lessThanEqual(scale, glm::vec3(1e-4f)))
            || glm::any(glm::greaterThan(scale, glm::vec3(1000.f)))) {
            error = "scale must contain three finite values in (0.0001, 1000]";
            return false;
        }
        transform.scale = scale;
        changed = true;
    }

    const bool hasQuaternion = params.contains("rotationQuaternion");
    const bool hasEuler = params.contains("rotationEulerDeg");
    if (hasQuaternion && hasEuler) {
        error = "rotationQuaternion and rotationEulerDeg are mutually exclusive";
        return false;
    }
    if (hasQuaternion) {
        glm::quat rotation;
        if (!finiteQuatXyzw(params["rotationQuaternion"], rotation)) {
            error = "rotationQuaternion must be a non-zero finite [x, y, z, w] array";
            return false;
        }
        transform.rotation = rotation;
        changed = true;
    } else if (hasEuler) {
        glm::vec3 eulerDeg;
        if (!finiteVec3(params["rotationEulerDeg"], eulerDeg)) {
            error = "rotationEulerDeg must be three finite numbers";
            return false;
        }
        transform.rotation = glm::normalize(glm::quat(glm::radians(eulerDeg)));
        changed = true;
    }
    return true;
}

json transformJson(const ObjectTransform& transform)
{
    return {
        {"position", {transform.position.x, transform.position.y, transform.position.z}},
        {"rotation", {transform.rotation.x, transform.rotation.y,
                      transform.rotation.z, transform.rotation.w}},
        {"scale", {transform.scale.x, transform.scale.y, transform.scale.z}}
    };
}

const char* modelTypeName(ModelType type)
{
    switch (type) {
    case ModelType::OBJ: return "obj";
    case ModelType::GLTF: return "gltf";
    case ModelType::GLB: return "glb";
    case ModelType::FBX: return "fbx";
    default: return "unknown";
    }
}

} // namespace

Application::Application()
    : camera_(glm::vec3(0.f, -4.f, 4.f), glm::radians(45.f), 0.0f, glm::vec3(0.f, 1.f, 0.f))
{}

// ─── GLFW callback implementations ───────────────────────────────────────────

void Application::scrollCallback(GLFWwindow* w, double /*xoffset*/, double yoffset)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!app) return;
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
    constexpr float kStep = 0.05f;
    constexpr float kMin  = 0.01f;
    constexpr float kMax  = 5.0f;
    app->camera_.SPEED = glm::clamp(app->camera_.SPEED + static_cast<float>(yoffset) * kStep,
                                    kMin, kMax);
}

void Application::framebufferResizeCallback(GLFWwindow* w, int, int)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(w));
    if (app) app->framebufferResized_ = true;
}

void Application::mouseButtonCallback(GLFWwindow* w, int button, int action, int mods)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(w));
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        app->rightMouseDown_ = (action == GLFW_PRESS);
        if (action == GLFW_RELEASE)
            app->firstMouse_ = true;
    }
    if (app && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) return;
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
        double cx = 0.0, cy = 0.0;
        glfwGetCursorPos(w, &cx, &cy);
        app->tryPickMainModel(static_cast<float>(cx), static_cast<float>(cy));
    }
}

void Application::mouseCallback(GLFWwindow* w, double xpos, double ypos)
{
    auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!app || !app->rightMouseDown_ || app->sequenceCameraActive_
        || app->sequenceCaptureActive_) return;
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;

    if (app->firstMouse_) {
        app->lastX_ = static_cast<float>(xpos);
        app->lastY_ = static_cast<float>(ypos);
        app->firstMouse_ = false;
    }
    const float dx = static_cast<float>(xpos) - app->lastX_;
    const float dy = static_cast<float>(ypos) - app->lastY_;
    app->lastX_ = static_cast<float>(xpos);
    app->lastY_ = static_cast<float>(ypos);
    app->camera_.ProcessMouseMovement(dx, dy);
}

// ─── Public entry ─────────────────────────────────────────────────────────────

void Application::run(int argc, char* argv[])
{
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mcp") {
            CommandBridge::instance().setEnabled(true);
        } else if (arg == "--port" && i + 1 < argc) {
            mcpPort_ = std::stoi(argv[++i]);
        } else if (arg == "--exit-after" && i + 1 < argc) {
            exitAfterFrames_ = std::stoi(argv[++i]);
        }
    }

    ui_ = new UIManager();
    tinyengine::debug::initRenderDoc();
    initGLFW();
    ui_->setModelDefaultPath();
    initVulkan();
    gameLoop();
}

// ─── Initialisation ───────────────────────────────────────────────────────────

void Application::initGLFW()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(WIDTH, HEIGHT, "tinyEngine", nullptr, nullptr);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
    glfwSetCursorPosCallback(window_, mouseCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetScrollCallback(window_, scrollCallback);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void Application::initVulkan()
{
    ctx_.init(window_);
    tinyengine::debug::initDebugUtils(ctx_.getInstance());

    cmdMgr_.create(ctx_);
    swapChain_.create(ctx_, window_);
    rpMgr_.create(ctx_, swapChain_);

    // Sync camera aspect ratio with the initial swapchain extent.
    {
        const VkExtent2D ext = swapChain_.getExtent();
        camera_.SetAspectRatio(static_cast<float>(ext.width), static_cast<float>(ext.height));
    }

    bufMgr_.init(ctx_, cmdMgr_);

    pipeMgr_.create(ctx_, rpMgr_,
                    ui_->vertexShaderPath, ui_->fragShaderPath,
                    ui_->boxVertShaderPath, ui_->boxFragShaderPath,
                    swapChain_.getExtent());

    fbMgr_.create(ctx_, swapChain_, rpMgr_);

    sceneMgr_.createCubeTemplate(bufMgr_);

    // 扫描模型资产注册表
    std::string resRoot;
    {
        const std::filesystem::path modelP(ui_->modelPath);
        resRoot = modelP.parent_path().parent_path().string();
        modelRegistry_.scan(resRoot);
        MaterialAssetLoader::setResRoot(resRoot);
    }

    // 初始化材质管理器（auto-load 和正常流程都需要）
    matMgr_.init(ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_,
                 swapChain_.getImageCount(), ui_->texturePath);
    sceneMgr_.setModelMaterialId(matMgr_.getDefaultMeshMaterialId());

    // 尝试自动加载 default.scene.json
    bool sceneLoadedFromJson = false;
    {
        const std::string defaultScene = resRoot + "/scenes/default.scene.json";
        if (std::filesystem::exists(std::filesystem::path(defaultScene))) {
            std::cout << "[tinyEngine] Found default.scene.json, loading...\n";
            sceneLoadedFromJson = loadScene(defaultScene);
        }
    }

    if (!sceneLoadedFromJson) {
        // 正常流程：加载默认模型 + 应用材质
        sceneMgr_.loadModel(ui_->modelPath, glm::vec3(0.f), bufMgr_);

        // Try to apply the JSON material asset to the main model.
        {
            const MaterialId mid = matMgr_.loadMaterialFromAsset(
                "materials/mainmodel.ast", ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
            if (mid != kInvalidMaterialId) sceneMgr_.setModelMaterialId(mid);
        }

        // Auto-load any .ast files dumped by the glTF loader, one per SubMesh slot.
        {
            const auto& autoPaths = sceneMgr_.getModelAutoAstPaths();
            for (int slot = 0; slot < (int)autoPaths.size(); ++slot) {
                if (autoPaths[slot].empty()) continue;
                const MaterialId mid = matMgr_.loadMaterialFromAsset(
                    autoPaths[slot], ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
                if (mid != kInvalidMaterialId)
                    sceneMgr_.setModelSubMeshMaterialId(slot, mid);
            }
        }

        // 若有骨骼数据，为每个实体的槽位材质创建蒙皮版本
        convertModelMaterialsToSkinned();
    }

    descMgr_.create(ctx_, swapChain_, pipeMgr_, bufMgr_);

    // 缩略图渲染器初始化 + 首次生成缺失缩略图
    {
        thumbnailRenderer_.create(ctx_, cmdMgr_, pipeMgr_, fbMgr_, matMgr_,
                                      sceneMgr_, bufMgr_, resRoot,
                                      ui_->vertexShaderPath, ui_->fragShaderPath);
        // 生成所有缺失的缩略图（首次启动时，之后跳过已有）
        thumbnailRenderer_.generateAll(resRoot);
        // 刷新模型注册表使新缩略图生效
        modelRegistry_.refresh();
    }

    ui_->setVulkanInstance(ctx_.getInstance(), nullptr);
    ui_->setPhysicalDevice(ctx_.getDevice(), ctx_.getPhysicalDevice());
    ui_->setVulkanRender(this);
    ui_->initIMGUI();

    // Dummy first frame to initialise ImGui draw data
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::Render();

    pickSys_.create(ctx_, cmdMgr_);
    cmdMgr_.allocateCommandBuffers(ctx_, swapChain_.getImageCount());
    cmdMgr_.createSyncObjects(ctx_, swapChain_.getImageCount());

    frameCapture_.configure(
        ctx_, swapChain_.supportsTransferSrc(), swapChain_.getImageFormat(),
        swapChain_.getExtent(),
        (std::filesystem::path(resRoot) / "bin" / "verify" / "captures").string());

    // ── MCP / IPC 初始化 ────────────────────────────────────────────────────
    if (CommandBridge::instance().enabled()) {
        CommandBridge::instance().registerBuiltinHandlers();
        registerMcpHandlers();
        ipc_ = std::make_unique<IpcServer>();
        if (!ipc_->start(mcpPort_)) {
            ipc_.reset();
            CommandBridge::instance().setEnabled(false);
            throw std::runtime_error("Failed to start MCP IPC server on 127.0.0.1:"
                                     + std::to_string(mcpPort_));
        }
        std::cout << "[MCP] IPC Server started on port " << mcpPort_ << "\n";
    }
}

// ─── Main loop ────────────────────────────────────────────────────────────────

void Application::gameLoop()
{
    while (!glfwWindowShouldClose(window_)) {
        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            std::cout << "Exit Game" << std::endl;
            vkDeviceWaitIdle(ctx_.getDevice());
            break;
        }

        // ── MCP 命令队列排出 ──────────────────────────────────────────────────
        if (CommandBridge::instance().enabled()) {
            CommandBridge::instance().drainQueue();
        }

        ++frameCount_;

        // ── exit-after 帧数检查 ──────────────────────────────────────────────
        if (exitAfterFrames_ > 0) {
            if (frameCount_ >= exitAfterFrames_) {
                std::cout << "[MCP] Reached --exit-after " << exitAfterFrames_
                          << " frames, exiting.\n";
                break;
            }
        }

        glfwPollEvents();

        const float now       = static_cast<float>(glfwGetTime());
        static float prevTick = -1.0f;
        const float dt        = (prevTick < 0.f) ? 0.f : glm::max(0.f, now - prevTick);
        prevTick = now;

        processInput(window_);

        // ── 拖拽放置更新 ─────────────────────────────────────────────
        if (ImGui::GetCurrentContext()) {
            if (auto* payload = ImGui::GetDragDropPayload();
                payload && strcmp(payload->DataType, "MODEL_ASSET") == 0) {
                if (!dragPlace.active) {
                    uint64_t assetId = *static_cast<const uint64_t*>(payload->Data);
                    beginDragPlace(assetId);
                }
                const ImVec2 mp = ImGui::GetMousePos();
                updateDragPlace(mp.x, mp.y);
            } else if (dragPlace.active) {
                endDragPlace();
            }
        } else if (dragPlace.active) {
            endDragPlace();
        }

        if (!sequenceCaptureActive_ && !camera_.IsSmoothFocusActive())
            camera_.UpdataCameraPosition(dt > 0.f ? dt : 1.f / 240.f);
        if (!sequenceCaptureActive_)
            camera_.UpdateSmoothFocus(dt > 0.f ? dt : 1.f / 240.f);


        ui_->prepareFrame();
        prepareSequenceCaptureFrame();
        const float renderDt = sequenceCaptureActive_
            ? 1.f / static_cast<float>(sequenceCaptureFps_)
            : dt;
        drawFrame(renderDt);
        finishSequenceCaptureFrame();

        if (mcpShutdownRequested_) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }

        if (ui_->refreshVulkanShader()) {
            recreateSwapChain();
        }
        ui_->setRefreshVulkanStatus(false);
    }

    if (sequenceCaptureActive_) {
        finishSequenceCapture(
            "Cancelled",
            "Application closed before sequence recording completed");
    }
    ui_->cleanUp();
    cleanUp();
}

void Application::registerMcpHandlers()
{
    auto& bridge = CommandBridge::instance();

    bridge.registerHandler("engine.status", [this](const json&) -> json {
        return {
            {"running", true},
            {"mcpEnabled", true},
            {"port", mcpPort_},
            {"frameCount", frameCount_},
            {"shutdownRequested", mcpShutdownRequested_},
            {"entityCount", sceneMgr_.getModelEntities().size()},
            {"boxCount", sceneMgr_.getBoxes().size()}
        };
    });

    bridge.registerHandler("engine.shutdown", [this](const json&) -> json {
        mcpShutdownRequested_ = true;
        return {{"accepted", true}};
    });

    bridge.registerHandler("scene.snapshot", [this](const json&) -> json {
        return SceneSnapshot::capture(sceneMgr_, camera_, pickedBoxEntityId);
    });

    bridge.registerHandler("asset.list", [this](const json& params) -> json {
        if (!params.is_object()) return invalidParams("params must be an object");
        if ((params.contains("keyword") && !params["keyword"].is_string())
            || (params.contains("folder") && !params["folder"].is_string())) {
            return invalidParams("keyword and folder must be strings");
        }
        const std::string keyword = params.value("keyword", std::string{});
        const std::string folder = params.value("folder", std::string{});

        const std::string lowerKeyword = toLowerCopy(keyword);
        json assets = json::array();
        for (const auto& asset : modelRegistry_.getAll()) {
            if (asset.modelRelPath.empty() || asset.type == ModelType::Unknown
                || asset.astType == "Material" || asset.astType == "Anim") {
                continue;
            }
            if (!folder.empty() && asset.subFolder != folder) continue;
            if (!lowerKeyword.empty()) {
                const std::string haystack = toLowerCopy(asset.name + " " + asset.astRelPath);
                if (haystack.find(lowerKeyword) == std::string::npos) continue;
            }
            assets.push_back({
                {"name", asset.name},
                {"astRelPath", asset.astRelPath},
                {"modelRelPath", asset.modelRelPath},
                {"folder", asset.subFolder},
                {"format", modelTypeName(asset.type)},
                {"hasThumbnail", asset.hasThumbnail}
            });
        }
        return {{"count", assets.size()}, {"assets", std::move(assets)}};
    });

    bridge.registerHandler("entity.place", [this](const json& params) -> json {
        if (!params.is_object() || !params.contains("astRelPath")
            || !params["astRelPath"].is_string() || params["astRelPath"].get<std::string>().empty()) {
            return invalidParams("astRelPath must be a non-empty registered asset path");
        }
        ObjectTransform transform;
        bool changed = false;
        std::string error;
        if (!applyTransformParams(params, transform, changed, error))
            return invalidParams(error);

        const std::string astRelPath = stripResPrefix(params["astRelPath"].get<std::string>());
        try {
            const uint64_t entityId = placeRegisteredModel(astRelPath, transform);
            return {
                {"entityId", entityId},
                {"astRelPath", astRelPath},
                {"transform", transformJson(transform)}
            };
        } catch (const std::exception& e) {
            return {{"error", {{"code", "asset_load_failed"}, {"message", e.what()}}}};
        }
    });

    bridge.registerHandler("entity.getTransform", [this](const json& params) -> json {
        if (!params.is_object() || !params.contains("entityId")
            || !params["entityId"].is_number_integer()) {
            return invalidParams("entityId must be a positive integer");
        }
        const int64_t rawId = params["entityId"].get<int64_t>();
        if (rawId <= 0) return invalidParams("entityId must be a positive integer");
        const auto* entity = sceneMgr_.getModelEntity(static_cast<uint64_t>(rawId));
        if (!entity) {
            return {{"error", {{"code", "entity_not_found"}, {"message", "entity does not exist"}}}};
        }
        return {{"entityId", entity->entityId}, {"transform", transformJson(entity->transform)}};
    });

    bridge.registerHandler("entity.setTransform", [this](const json& params) -> json {
        if (!params.is_object() || !params.contains("entityId")
            || !params["entityId"].is_number_integer()) {
            return invalidParams("entityId must be a positive integer");
        }
        const int64_t rawId = params["entityId"].get<int64_t>();
        if (rawId <= 0) return invalidParams("entityId must be a positive integer");
        auto* entity = sceneMgr_.getModelEntity(static_cast<uint64_t>(rawId));
        if (!entity) {
            return {{"error", {{"code", "entity_not_found"}, {"message", "entity does not exist"}}}};
        }

        ObjectTransform transform = entity->transform;
        bool changed = false;
        std::string error;
        if (!applyTransformParams(params, transform, changed, error))
            return invalidParams(error);
        if (!changed) return invalidParams("at least one transform field must be provided");

        sceneMgr_.setEntityTransform(entity->entityId, transform);
        const auto& entities = sceneMgr_.getModelEntities();
        if (!entities.empty() && entities.front().entityId == entity->entityId)
            mainModelTransform = transform;
        return {{"entityId", entity->entityId}, {"transform", transformJson(transform)}};
    });

    bridge.registerHandler("entity.delete", [this](const json& params) -> json {
        if (!params.is_object() || !params.contains("entityId")
            || !params["entityId"].is_number_integer()) {
            return invalidParams("entityId must be a positive integer");
        }
        const int64_t rawId = params["entityId"].get<int64_t>();
        if (rawId <= 0) return invalidParams("entityId must be a positive integer");
        const uint64_t entityId = static_cast<uint64_t>(rawId);
        if (!sceneMgr_.getModelEntity(entityId)) {
            return {{"error", {{"code", "entity_not_found"}, {"message", "entity does not exist"}}}};
        }
        vkDeviceWaitIdle(ctx_.getDevice());
        destroyEntitySkinBindings(*sceneMgr_.getModelEntity(entityId));
        const bool removed = sceneMgr_.removeModelEntity(entityId, ctx_);
        if (ui_ && ui_->selectedEntityId_ == entityId) ui_->selectedEntityId_ = 0;
        if (selectedCameraEntityId_ == entityId) selectedCameraEntityId_ = 0;
        return {{"entityId", entityId}, {"deleted", removed}};
    });

    bridge.registerHandler("capture.frame", [this](const json& params) -> json {
        if (!params.is_object()) return invalidParams("params must be an object");
        if (params.contains("includeUi") && !params["includeUi"].is_boolean())
            return invalidParams("includeUi must be a boolean");
        if (sequenceCaptureActive_) {
            return {{"error", {{"code", "sequence_capture_active"},
                                {"message", "Single-frame capture is unavailable while sequence recording is active"}}}};
        }
        return frameCapture_.request(frameCount_, params.value("includeUi", true));
    });

    bridge.registerHandler("capture.get", [this](const json& params) -> json {
        if (!params.contains("jobId") || !params["jobId"].is_number_integer()) {
            return {{"error", {{"code", "invalid_params"},
                                {"message", "jobId must be a positive integer"}}}};
        }
        const auto jobId = params["jobId"].get<int64_t>();
        if (jobId <= 0) {
            return {{"error", {{"code", "invalid_params"},
                                {"message", "jobId must be a positive integer"}}}};
        }
        return frameCapture_.query(static_cast<uint64_t>(jobId));
    });
}

// ─── Sequencer asset binding and capture ─────────────────────────────────────

bool Application::loadSequenceAsset(const std::string& path,
                                    std::string* error,
                                    SequenceBindingReport* bindingReport)
{
    Sequence loaded;
    if (!SequenceAssetLoader::loadSequence(path, loaded, error))
        return false;

    SequenceBindingReport report;
    const auto& entities = sceneMgr_.getModelEntities();
    for (size_t trackIndex = 0; trackIndex < loaded.tracks.size(); ++trackIndex) {
        auto& track = loaded.tracks[trackIndex];
        if (track.type != TrackType::TransformKeyframe
            && track.type != TrackType::AnimatorKeyframe) {
            continue;
        }

        uint64_t targetEntityId = 0;
        const std::string* targetAstRelPath = nullptr;
        const std::string* targetDisplayName = nullptr;
        getSequenceBindingMetadata(track, targetEntityId,
                                   targetAstRelPath, targetDisplayName);
        const std::string inferredName = inferLegacySequenceTargetName(track);

        const SceneManager::ModelEntity* boundEntity = sceneMgr_.getModelEntity(targetEntityId);
        if (boundEntity) {
            // 持久化实体 ID 是首选身份；名称或路径可能被用户重命名，命中后刷新元数据。
            setSequenceBindingMetadata(track, *boundEntity);
            continue;
        }

        std::vector<const SceneManager::ModelEntity*> candidates;
        for (const auto& entity : entities) {
            bool matches = false;
            if (!targetAstRelPath->empty() || !targetDisplayName->empty()) {
                matches = sequenceBindingMetadataMatches(entity, *targetAstRelPath,
                                                          *targetDisplayName);
            } else if (!inferredName.empty()) {
                matches = entity.displayName == inferredName;
            }
            if (matches)
                candidates.push_back(&entity);
        }

        if (candidates.size() == 1) {
            if (candidates.front()->entityId != targetEntityId)
                ++report.reboundTrackCount;
            setSequenceBindingMetadata(track, *candidates.front());
        } else {
            report.unresolvedTrackIndices.push_back(trackIndex);
        }
    }

    if (loaded.cameraShotTrack) {
        for (size_t keyIndex = 0;
             keyIndex < loaded.cameraShotTrack->keyframes.size();
             ++keyIndex) {
            auto& key = loaded.cameraShotTrack->keyframes[keyIndex];
            const auto* bound = sceneMgr_.getModelEntity(key.cameraEntityId);
            if (bound && bound->isCamera()) {
                key.targetDisplayName = bound->displayName;
                continue;
            }

            std::vector<const SceneManager::ModelEntity*> candidates;
            if (!key.targetDisplayName.empty()) {
                for (const auto& entity : entities) {
                    if (entity.isCamera()
                        && entity.displayName == key.targetDisplayName) {
                        candidates.push_back(&entity);
                    }
                }
            }
            if (candidates.size() == 1) {
                key.cameraEntityId = candidates.front()->entityId;
                key.targetDisplayName = candidates.front()->displayName;
                ++report.reboundCameraShotKeyCount;
            } else {
                key.cameraEntityId = 0;
                report.unresolvedCameraShotKeyIndices.push_back(keyIndex);
            }
        }
    }

    currentSequence_ = std::move(loaded);
    seqPlayer_.load(currentSequence_);
    seqPlayer_.setSequenceRef(currentSequence_);
    sequencerPreviewPending_ = sequencerControlEnabled_;
    lastSequencerAnimatorEvalTime_ = -1.0;
    sequenceCameraActive_ = false;
    cameraPathCache_.clear();

    for (auto& entity : sceneMgr_.getModelEntities()) {
        const bool wasSequencerDriven = entity.sequencerAnimatorActive;
        entity.hasPendingSequencerBlend = false;
        entity.sequencerAnimatorActive = false;
        if (wasSequencerDriven
            || isEntitySequencerAnimatorControlled(entity.entityId)) {
            entity.rootMotionInitialized = false;
        }
        if (isEntitySequencerAnimatorControlled(entity.entityId)) {
            entity.previewClipIndex = -1;
            entity.previewTime = 0.f;
        }
    }
    if (bindingReport)
        *bindingReport = std::move(report);
    return true;
}

bool Application::bindSequenceTrack(size_t trackIndex, uint64_t entityId)
{
    if (trackIndex >= currentSequence_.tracks.size())
        return false;
    auto& track = currentSequence_.tracks[trackIndex];
    if (track.type != TrackType::TransformKeyframe
        && track.type != TrackType::AnimatorKeyframe) {
        return false;
    }
    auto* entity = sceneMgr_.getModelEntity(entityId);
    if (!entity)
        return false;
    setSequenceBindingMetadata(track, *entity);
    for (auto& sceneEntity : sceneMgr_.getModelEntities()) {
        const bool wasSequencerDriven = sceneEntity.sequencerAnimatorActive;
        sceneEntity.hasPendingSequencerBlend = false;
        sceneEntity.sequencerAnimatorActive = false;
        if (wasSequencerDriven
            || isEntitySequencerAnimatorControlled(sceneEntity.entityId)) {
            sceneEntity.rootMotionInitialized = false;
        }
    }
    lastSequencerAnimatorEvalTime_ = -1.0;
    sequencerPreviewPending_ = sequencerControlEnabled_;
    return true;
}

bool Application::bindCameraShotKey(size_t keyIndex, uint64_t cameraEntityId)
{
    if (!currentSequence_.cameraShotTrack
        || keyIndex >= currentSequence_.cameraShotTrack->keyframes.size()) {
        return false;
    }
    const auto* entity = sceneMgr_.getModelEntity(cameraEntityId);
    if (!entity || !entity->isCamera())
        return false;

    auto& key = currentSequence_.cameraShotTrack->keyframes[keyIndex];
    key.cameraEntityId = entity->entityId;
    key.targetDisplayName = entity->displayName;
    sequencerPreviewPending_ = sequencerControlEnabled_;
    return true;
}

bool Application::beginSequenceCapture(const SequenceCaptureSettings& settings,
                                       std::string* error)
{
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        sequenceCaptureStatus_ = "Failed: " + message;
        return false;
    };

    if (sequenceCaptureActive_)
        return fail("A sequence capture is already active");
    if (!sequencerControlEnabled_)
        return fail("Enable Sequencer Control before recording");
    if (!currentSequence_.cameraShotTrack
        || currentSequence_.cameraShotTrack->keyframes.empty()) {
        return fail("The sequence has no Camera/Shot keyframes");
    }
    if (settings.fps < 1 || settings.fps > 240)
        return fail("Capture FPS must be in the range 1..240");
    if (settings.videoBitrateMbps < 1 || settings.videoBitrateMbps > 200)
        return fail("Video bitrate must be in the range 1..200 Mbps");
    if (!(settings.endTime > settings.startTime))
        return fail("Capture end time must be greater than start time");
    if (settings.startTime < 0.0)
        return fail("Capture start time cannot be negative");
    const double sequenceDuration = currentSequence_.totalDuration > 0.0
        ? currentSequence_.totalDuration
        : currentSequence_.computeTotalDuration();
    if (sequenceDuration <= 0.0)
        return fail("The sequence duration must be greater than zero");
    if (settings.endTime > sequenceDuration + 1e-6)
        return fail("Capture end time exceeds the sequence duration");
    if (frameCapture_.isBusy())
        return fail("A screenshot capture is already in progress");

    const auto* firstShot =
        currentSequence_.cameraShotTrack->evaluate(settings.startTime);
    if (!firstShot)
        return fail("No Camera/Shot keyframe is active at the capture start time");
    {
        const auto* cameraEntity =
            sceneMgr_.getModelEntity(firstShot->cameraEntityId);
        if (!cameraEntity || !cameraEntity->isCamera()) {
            return fail("The Camera/Shot key active at the capture start time "
                        "is not bound to a valid Camera Actor");
        }
    }

    for (const auto& key : currentSequence_.cameraShotTrack->keyframes) {
        const bool affectsRange =
            key.time > settings.startTime && key.time < settings.endTime;
        if (!affectsRange)
            continue;
        const auto* cameraEntity = sceneMgr_.getModelEntity(key.cameraEntityId);
        if (!cameraEntity || !cameraEntity->isCamera()) {
            return fail("Camera/Shot key at "
                        + std::to_string(key.time)
                        + "s is not bound to a valid Camera Actor");
        }
    }

    const double frameCountExact =
        (settings.endTime - settings.startTime)
        * static_cast<double>(settings.fps);
    const uint32_t totalFrames = static_cast<uint32_t>(
        std::max(1.0, std::ceil(frameCountExact - 1e-9)));

    std::string takeName = settings.takeName.empty()
        ? currentSequence_.name
        : settings.takeName;
    takeName = sanitizeAssetName(takeName);
    if (takeName.empty())
        takeName = "SequenceTake";

    const std::filesystem::path root =
        std::filesystem::path(modelRegistry_.getResRoot())
        / "bin" / "sequence_captures";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
        return fail("Cannot create capture directory: " + ec.message());

    std::filesystem::path output = root / takeName;
    for (int suffix = 2; std::filesystem::exists(output) && suffix < 10000; ++suffix) {
        std::ostringstream numbered;
        numbered << takeName << '_' << std::setw(3) << std::setfill('0') << suffix;
        output = root / numbered.str();
    }
    std::filesystem::create_directories(output, ec);
    if (ec)
        return fail("Cannot create take directory: " + ec.message());

    const VkExtent2D captureExtent = swapChain_.getExtent();
    if (captureExtent.width == 0 || captureExtent.height == 0)
        return fail("The current framebuffer has an invalid size");
    std::string captureTargetError;
    if (!sequenceCaptureTarget_.create(
            ctx_, fbMgr_, rpMgr_,
            captureExtent.width, captureExtent.height,
            &captureTargetError)) {
        return fail("Cannot create the fixed-resolution capture target: "
                    + captureTargetError);
    }
    const std::filesystem::path videoPath =
        output / (output.filename().string() + ".mp4");
    std::string encoderError;
    if (!sequenceVideoEncoder_.begin(
            videoPath,
            captureExtent.width,
            captureExtent.height,
            settings.fps,
            static_cast<uint32_t>(settings.videoBitrateMbps) * 1'000'000u,
            &encoderError)) {
        sequenceCaptureTarget_.destroy(ctx_);
        return fail("Cannot start MP4 recording: " + encoderError);
    }

    sequenceCaptureSavedTime_ = seqPlayer_.currentTime();
    sequenceCaptureSavedPlaying_ = seqPlayer_.isPlaying();
    sequenceCaptureSavedLooping_ = seqPlayer_.isLooping();
    sequenceCaptureSavedCameraPosition_ = camera_.Position;
    sequenceCaptureSavedCameraOrientation_ = camera_.GetOrientation();
    sequenceCaptureSavedCameraFov_ = camera_.FovDeg;
    sequenceCaptureSavedCameraNear_ = camera_.NearPlane;
    sequenceCaptureSavedCameraFar_ = camera_.FarPlane;
    sequenceCaptureSavedEntities_.clear();
    sequenceCaptureSavedEntities_.reserve(
        sceneMgr_.getModelEntities().size());
    for (const auto& entity : sceneMgr_.getModelEntities()) {
        sequenceCaptureSavedEntities_.push_back(
            {entity.entityId, entity.transform});
    }

    seqPlayer_.pause();
    sequenceCaptureStartTime_ = settings.startTime;
    sequenceCaptureEndTime_ = settings.endTime;
    sequenceCaptureFps_ = settings.fps;
    sequenceCaptureIncludeUi_ = settings.includeUi;
    sequenceCaptureKeepPngFrames_ = settings.keepPngFrames;
    sequenceCaptureVideoBitrateMbps_ = settings.videoBitrateMbps;
    sequenceCaptureTotalFrames_ = totalFrames;
    sequenceCaptureFrameCount_ = 0;
    sequenceCaptureWarmupTotalFrames_ = static_cast<uint32_t>(
        std::max(0.0, std::ceil(
            settings.startTime * static_cast<double>(settings.fps) - 1e-9)));
    sequenceCaptureWarmupFrameCount_ = 0;
    sequenceCaptureWidth_ = captureExtent.width;
    sequenceCaptureHeight_ = captureExtent.height;
    sequenceCaptureVideoWidth_ = sequenceVideoEncoder_.videoWidth();
    sequenceCaptureVideoHeight_ = sequenceVideoEncoder_.videoHeight();
    sequenceCaptureOutputDirectory_ = std::filesystem::absolute(output).string();
    sequenceCaptureVideoPath_ =
        sequenceVideoEncoder_.outputPath().string();
    sequenceCaptureStatus_ = sequenceCaptureWarmupTotalFrames_ > 0
        ? "Pre-rolling"
        : "Recording";
    sequenceCaptureStopRequested_ = false;
    sequenceCaptureCancelRequested_ = false;
    sequenceCaptureFramePending_ = false;
    sequenceCaptureReadbackSubmitted_ = false;
    sequenceCaptureFrameReady_ = false;
    sequenceCaptureWarmupFramePending_ = false;
    sequenceCapturePendingRgba_.clear();
    sequenceCaptureReadbackError_.clear();
    sequenceCaptureSwapchainRebuilds_ = 0;
    sequenceCaptureWindowResizeLocked_ = false;
    sequenceCaptureWindowWasResizable_ =
        glfwGetWindowAttrib(window_, GLFW_RESIZABLE) == GLFW_TRUE;
    if (settings.includeUi && sequenceCaptureWindowWasResizable_) {
        glfwSetWindowAttrib(window_, GLFW_RESIZABLE, GLFW_FALSE);
        sequenceCaptureWindowResizeLocked_ = true;
    }
    sequenceCaptureActive_ = true;
    lastSequencerAnimatorEvalTime_ = -1.0;
    for (auto& entity : sceneMgr_.getModelEntities()) {
        entity.rootMotionInitialized = false;
        entity.rootMotionClipHistory.clear();
    }
    return true;
}

void Application::stopSequenceCapture()
{
    if (sequenceCaptureActive_)
        sequenceCaptureStopRequested_ = true;
}

void Application::cancelSequenceCapture()
{
    if (sequenceCaptureActive_)
        sequenceCaptureCancelRequested_ = true;
}

void Application::prepareSequenceCaptureFrame()
{
    if (!sequenceCaptureActive_
        || sequenceCaptureFramePending_
        || sequenceCaptureWarmupFramePending_) {
        return;
    }
    if (sequenceCaptureCancelRequested_) {
        finishSequenceCapture(
            "Cancelled",
            sequenceCaptureFrameCount_ > 0
                ? "Partial video was finalized"
                : "No video frames were captured");
        return;
    }
    if (sequenceCaptureStopRequested_) {
        finishSequenceCapture(
            "Stopped",
            sequenceCaptureFrameCount_ > 0
                ? "Partial video was finalized"
                : "No video frames were captured");
        return;
    }
    if (sequenceCaptureFrameCount_ >= sequenceCaptureTotalFrames_) {
        finishSequenceCapture("Completed");
        return;
    }
    seqPlayer_.pause();

    if (sequenceCaptureWarmupFrameCount_
        < sequenceCaptureWarmupTotalFrames_) {
        const double warmupTime =
            static_cast<double>(sequenceCaptureWarmupFrameCount_)
            / static_cast<double>(sequenceCaptureFps_);
        seqPlayer_.seek(warmupTime);
        sequencerPreviewPending_ = true;
        sequenceCaptureWarmupFramePending_ = true;
        sequenceCaptureStatus_ = "Pre-rolling";
        return;
    }

    const double captureTime =
        sequenceCaptureStartTime_
        + static_cast<double>(sequenceCaptureFrameCount_)
          / static_cast<double>(sequenceCaptureFps_);
    seqPlayer_.seek(captureTime);
    sequencerPreviewPending_ = true;
    sequenceCaptureFramePending_ = true;
    sequenceCaptureReadbackSubmitted_ = false;
    sequenceCaptureFrameReady_ = false;
    sequenceCapturePendingRgba_.clear();
    sequenceCaptureReadbackError_.clear();
}

void Application::finishSequenceCaptureFrame()
{
    if (!sequenceCaptureActive_)
        return;
    if (sequenceCaptureWarmupFramePending_) {
        ++sequenceCaptureWarmupFrameCount_;
        sequenceCaptureWarmupFramePending_ = false;
        if (sequenceCaptureWarmupFrameCount_
            >= sequenceCaptureWarmupTotalFrames_) {
            sequenceCaptureStatus_ = "Recording";
        }
        return;
    }
    if (!sequenceCaptureFramePending_)
        return;

    if (!sequenceCaptureFrameReady_)
        return;
    if (!sequenceCaptureReadbackError_.empty()) {
        finishSequenceCapture(
            "Failed",
            "Cannot read the offscreen capture frame: "
                + sequenceCaptureReadbackError_);
        return;
    }

    std::string frameError;
    if (!sequenceVideoEncoder_.writeRgbaFrame(
            sequenceCapturePendingRgba_,
            sequenceCaptureFrameCount_, &frameError)) {
        finishSequenceCapture(
            "Failed", "Cannot encode video frame: " + frameError);
        return;
    }

    if (sequenceCaptureKeepPngFrames_) {
        std::ostringstream fileName;
        fileName << "frame_" << std::setw(6) << std::setfill('0')
                 << sequenceCaptureFrameCount_ << ".png";
        const std::filesystem::path destination =
            std::filesystem::path(sequenceCaptureOutputDirectory_)
            / fileName.str();
        if (stbi_write_png(
                destination.string().c_str(),
                static_cast<int>(sequenceCaptureWidth_),
                static_cast<int>(sequenceCaptureHeight_), 4,
                sequenceCapturePendingRgba_.data(),
                static_cast<int>(sequenceCaptureWidth_ * 4u)) == 0) {
            ++sequenceCaptureFrameCount_;
            finishSequenceCapture(
                "Failed", "Cannot store the offscreen capture PNG frame");
            return;
        }
    }

    ++sequenceCaptureFrameCount_;
    sequenceCaptureFramePending_ = false;
    sequenceCaptureReadbackSubmitted_ = false;
    sequenceCaptureFrameReady_ = false;
    sequenceCapturePendingRgba_.clear();
    sequenceCaptureReadbackError_.clear();

    if (sequenceCaptureCancelRequested_) {
        finishSequenceCapture("Cancelled", "Partial video was finalized");
    } else if (sequenceCaptureStopRequested_) {
        finishSequenceCapture("Stopped", "Partial video was finalized");
    } else if (sequenceCaptureFrameCount_ >= sequenceCaptureTotalFrames_) {
        finishSequenceCapture("Completed");
    }
}

void Application::finishSequenceCapture(const std::string& outcome,
                                        const std::string& message)
{
    if (!sequenceCaptureActive_)
        return;

    sequenceCaptureActive_ = false;
    sequenceCaptureFramePending_ = false;
    sequenceCaptureReadbackSubmitted_ = false;
    sequenceCaptureFrameReady_ = false;
    sequenceCaptureWarmupFramePending_ = false;
    sequenceCaptureStopRequested_ = false;
    sequenceCaptureCancelRequested_ = false;
    sequenceCapturePendingRgba_.clear();
    sequenceCaptureReadbackError_.clear();
    sequenceCaptureTarget_.destroy(ctx_);
    if (sequenceCaptureWindowResizeLocked_) {
        glfwSetWindowAttrib(
            window_, GLFW_RESIZABLE,
            sequenceCaptureWindowWasResizable_ ? GLFW_TRUE : GLFW_FALSE);
        sequenceCaptureWindowResizeLocked_ = false;
    }

    std::string finalOutcome = outcome;
    std::string finalMessage = message;
    bool videoFinalized = false;
    uintmax_t videoBytes = 0;
    if (sequenceVideoEncoder_.isActive()) {
        if (sequenceCaptureFrameCount_ == 0) {
            sequenceVideoEncoder_.abort();
            std::error_code removeError;
            std::filesystem::remove(
                sequenceCaptureVideoPath_, removeError);
        } else {
            std::string encoderError;
            videoFinalized =
                sequenceVideoEncoder_.finalize(&encoderError);
            if (!videoFinalized) {
                finalOutcome = "Failed";
                if (!finalMessage.empty())
                    finalMessage += "; ";
                finalMessage += encoderError;
            }
        }
    }
    if (videoFinalized) {
        std::error_code sizeError;
        videoBytes = std::filesystem::file_size(
            sequenceCaptureVideoPath_, sizeError);
        if (sizeError)
            videoBytes = 0;
    }

    const double capturedDuration =
        sequenceCaptureFps_ > 0
            ? static_cast<double>(sequenceCaptureFrameCount_)
                / static_cast<double>(sequenceCaptureFps_)
            : 0.0;
    const bool captureComplete =
        finalOutcome == "Completed"
        && sequenceCaptureFrameCount_ == sequenceCaptureTotalFrames_
        && videoFinalized;
    json manifest = {
        {"version", 2},
        {"sequence", currentSequence_.name},
        {"status", finalOutcome},
        {"message", finalMessage},
        {"complete", captureComplete},
        {"fps", sequenceCaptureFps_},
        {"startTime", sequenceCaptureStartTime_},
        {"endTime", sequenceCaptureEndTime_},
        {"capturedFrames", sequenceCaptureFrameCount_},
        {"plannedFrames", sequenceCaptureTotalFrames_},
        {"capturedDuration", capturedDuration},
        {"preRollFrames", sequenceCaptureWarmupFrameCount_},
        {"width", sequenceCaptureWidth_},
        {"height", sequenceCaptureHeight_},
        {"captureTarget", "fixed_offscreen"},
        {"swapchainRebuilds", sequenceCaptureSwapchainRebuilds_},
        {"lastShotCameraEntityId",
            activeSequenceCameraEntityId_ != 0
                ? json(activeSequenceCameraEntityId_) : json(nullptr)},
        {"includeUi", sequenceCaptureIncludeUi_},
        {"keepPngFrames", sequenceCaptureKeepPngFrames_},
        {"framePattern",
            sequenceCaptureKeepPngFrames_
                ? json("frame_%06d.png") : json(nullptr)},
        {"video", {
            {"file",
                std::filesystem::path(sequenceCaptureVideoPath_)
                    .filename().string()},
            {"container", "mp4"},
            {"codec", "h264"},
            {"finalized", videoFinalized},
            {"bitrateMbps", sequenceCaptureVideoBitrateMbps_},
            {"width", sequenceCaptureVideoWidth_},
            {"height", sequenceCaptureVideoHeight_},
            {"bytes", videoBytes}
        }}
    };
    if (currentSequence_.cameraShotTrack) {
        manifest["cameraShots"] = json::array();
        for (const auto& key : currentSequence_.cameraShotTrack->keyframes) {
            manifest["cameraShots"].push_back({
                {"time", key.time},
                {"cameraEntityId", key.cameraEntityId},
                {"targetDisplayName", key.targetDisplayName}
            });
        }
    }

    if (!sequenceCaptureOutputDirectory_.empty()) {
        std::ofstream out(
            std::filesystem::path(sequenceCaptureOutputDirectory_) / "capture.json");
        if (out.is_open())
            out << manifest.dump(2) << '\n';
    }

    sequenceCaptureStatus_ = finalOutcome;
    if (!finalMessage.empty())
        sequenceCaptureStatus_ += ": " + finalMessage;

    seqPlayer_.seek(sequenceCaptureSavedTime_);
    if (sequenceCaptureSavedPlaying_)
        seqPlayer_.play(sequenceCaptureSavedLooping_);
    else
        seqPlayer_.pause();
    camera_.Position = sequenceCaptureSavedCameraPosition_;
    camera_.SetOrientation(sequenceCaptureSavedCameraOrientation_);
    camera_.FovDeg = sequenceCaptureSavedCameraFov_;
    camera_.NearPlane = sequenceCaptureSavedCameraNear_;
    camera_.FarPlane = sequenceCaptureSavedCameraFar_;
    for (const auto& saved : sequenceCaptureSavedEntities_) {
        if (auto* entity = sceneMgr_.getModelEntity(saved.entityId)) {
            entity->transform = saved.transform;
            entity->syncCameraFromTransform();
        }
    }
    sequenceCaptureSavedEntities_.clear();
    sequenceCameraActive_ = false;
    activeSequenceCameraEntityId_ = 0;
    sequencerPreviewPending_ = sequencerControlEnabled_;
    lastSequencerAnimatorEvalTime_ = -1.0;
    for (auto& entity : sceneMgr_.getModelEntities()) {
        entity.rootMotionInitialized = false;
        entity.rootMotionClipHistory.clear();
    }
}

bool Application::isEntitySequencerAnimatorControlled(uint64_t entityId) const
{
    if (!sequencerControlEnabled_ || entityId == 0)
        return false;
    for (const auto& track : currentSequence_.tracks) {
        if (track.type == TrackType::AnimatorKeyframe
            && track.animatorTrack.targetEntityId == entityId
            && !track.animatorTrack.keyframes.empty()) {
            return true;
        }
    }
    return false;
}

void Application::setSequencerControlEnabled(bool enabled)
{
    if (sequencerControlEnabled_ == enabled)
        return;

    auto hasAnimatorTrack = [&](uint64_t entityId) {
        for (const auto& track : currentSequence_.tracks) {
            if (track.type == TrackType::AnimatorKeyframe
                && track.animatorTrack.targetEntityId == entityId
                && !track.animatorTrack.keyframes.empty()) {
                return true;
            }
        }
        return false;
    };

    sequencerControlEnabled_ = enabled;
    sequencerPreviewPending_ = enabled;
    sequenceCameraActive_ = false;
    lastSequencerAnimatorEvalTime_ = -1.0;

    for (auto& entity : sceneMgr_.getModelEntities()) {
        const bool wasSequencerDriven = entity.sequencerAnimatorActive;
        entity.hasPendingSequencerBlend = false;
        entity.sequencerAnimatorActive = false;
        if (!wasSequencerDriven && !hasAnimatorTrack(entity.entityId))
            continue;

        // Changing animation authority invalidates Root Motion history. When
        // Sequencer takes ownership, remove any competing manual clip preview.
        entity.rootMotionInitialized = false;
        if (enabled) {
            entity.previewClipIndex = -1;
            entity.previewTime = 0.f;
        }
    }
}

void Application::setSequencerCameraFollowEnabled(bool enabled)
{
    if (sequenceCaptureActive_ || sequencerCameraFollowEnabled_ == enabled)
        return;

    sequencerCameraFollowEnabled_ = enabled;
    sequenceCameraActive_ = false;
    if (enabled && sequencerControlEnabled_)
        sequencerPreviewPending_ = true;
}

void Application::invalidateAnimatorDefinition(
    SceneManager::ModelEntity& entity)
{
    entity.pendingSequencerBlend = {};
    entity.hasPendingSequencerBlend = false;
    entity.rootMotionInitialized = false;
    entity.rootMotionClipHistory.clear();
    entity.observedAnimatorDefinitionRevision =
        entity.animatorController.definitionRevision();

    if (isEntitySequencerAnimatorControlled(entity.entityId)) {
        sequencerPreviewPending_ = true;
        lastSequencerAnimatorEvalTime_ = -1.0;
    }
}

void Application::invalidateAnimationData(SceneManager::ModelEntity& entity)
{
    if (entity.previewClipIndex >= static_cast<int>(entity.animationClips.size())) {
        entity.previewClipIndex = -1;
        entity.previewTime = 0.f;
    }
    invalidateAnimatorDefinition(entity);
}

void Application::detectAnimatorDefinitionChanges()
{
    for (auto& entity : sceneMgr_.getModelEntities()) {
        if (entity.observedAnimatorDefinitionRevision
            == entity.animatorController.definitionRevision()) {
            continue;
        }
        invalidateAnimatorDefinition(entity);
    }
}

void Application::notifyAnimatorControllerDefinitionChanged(
    uint64_t entityId, bool synchronizeBoundInstances)
{
    auto* sourceEntity = sceneMgr_.getModelEntity(entityId);
    if (!sourceEntity) return;

    std::vector<uint64_t> affectedEntityIds{entityId};
    if (synchronizeBoundInstances
        && !sourceEntity->animatorControllerPath.empty()) {
        auto normalizedControllerPath = [&](const std::string& path) {
            if (path.empty()) return std::string{};
            std::filesystem::path resolved(path);
            if (resolved.is_relative())
                resolved = std::filesystem::path(getResRoot()) / resolved;
            std::error_code ec;
            const auto canonical =
                std::filesystem::weakly_canonical(resolved, ec);
            if (!ec) resolved = canonical;
            return normalizeSequenceBindingPath(resolved.generic_string());
        };
        const std::string sourcePath = normalizedControllerPath(
            sourceEntity->animatorControllerPath);
        for (auto& entity : sceneMgr_.getModelEntities()) {
            if (entity.entityId == entityId
                || normalizedControllerPath(entity.animatorControllerPath)
                    != sourcePath) {
                continue;
            }
            entity.animatorController.replaceDefinitionFrom(
                sourceEntity->animatorController, true);
            affectedEntityIds.push_back(entity.entityId);
        }
    }

    for (const uint64_t affectedId : affectedEntityIds) {
        if (auto* entity = sceneMgr_.getModelEntity(affectedId))
            invalidateAnimatorDefinition(*entity);
    }
}

bool Application::renameAnimatorState(uint64_t entityId,
                                      const std::string& oldName,
                                      const std::string& newName,
                                      std::string* error)
{
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (oldName.empty() || newName.empty())
        return fail("State name cannot be empty");
    if (oldName == newName)
        return true;
    if (newName == "AnyState")
        return fail("'AnyState' is reserved");

    auto* sourceEntity = sceneMgr_.getModelEntity(entityId);
    if (!sourceEntity || !sourceEntity->animatorController.hasState(oldName))
        return fail("Source State no longer exists");
    if (sourceEntity->animatorController.hasState(newName))
        return fail("A State with that name already exists");

    auto normalizedControllerPath = [&](const std::string& path) {
        if (path.empty()) return std::string{};
        std::filesystem::path resolved(path);
        if (resolved.is_relative())
            resolved = std::filesystem::path(getResRoot()) / resolved;
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(resolved, ec);
        if (!ec) resolved = canonical;
        return normalizeSequenceBindingPath(resolved.generic_string());
    };

    const std::string sourcePath =
        normalizedControllerPath(sourceEntity->animatorControllerPath);
    std::vector<uint64_t> referencedEntityIds;
    std::vector<uint64_t> renameEntityIds;
    for (auto& entity : sceneMgr_.getModelEntities()) {
        const bool sameController =
            entity.entityId == entityId
            || (!sourcePath.empty()
                && normalizedControllerPath(entity.animatorControllerPath)
                    == sourcePath);
        if (!sameController) continue;

        referencedEntityIds.push_back(entity.entityId);
        const bool hasOld = entity.animatorController.hasState(oldName);
        const bool hasNew = entity.animatorController.hasState(newName);
        if (hasOld && hasNew)
            return fail("A bound Controller instance has conflicting State names");
        if (hasOld)
            renameEntityIds.push_back(entity.entityId);
    }

    for (const uint64_t affectedId : renameEntityIds) {
        auto* entity = sceneMgr_.getModelEntity(affectedId);
        if (!entity
            || !entity->animatorController.renameState(oldName, newName)) {
            return fail("Failed to rename State in a bound Controller instance");
        }
    }

    const std::unordered_set<uint64_t> referencedIds(
        referencedEntityIds.begin(), referencedEntityIds.end());
    for (auto& track : currentSequence_.tracks) {
        if (track.type == TrackType::AnimatorKeyframe
            && referencedIds.count(track.animatorTrack.targetEntityId) != 0
            && track.animatorTrack.initialState == oldName) {
            track.animatorTrack.initialState = newName;
        }
    }

    for (const uint64_t affectedId : referencedEntityIds) {
        if (auto* entity = sceneMgr_.getModelEntity(affectedId))
            invalidateAnimatorDefinition(*entity);
    }
    if (sequencerControlEnabled_) {
        sequencerPreviewPending_ = true;
        lastSequencerAnimatorEvalTime_ = -1.0;
    }
    if (error) error->clear();
    return true;
}

void Application::destroyEntitySkinBindings(SceneManager::ModelEntity& ent)
{
    std::unordered_set<SkinBindingId> uniqueBindings;
    if (ent.skinBindingId != kInvalidSkinBindingId)
        uniqueBindings.insert(ent.skinBindingId);
    for (uint32_t rawId : ent.subMeshSkinBindings) {
        if (rawId != kInvalidSkinBindingId)
            uniqueBindings.insert(static_cast<SkinBindingId>(rawId));
    }
    for (SkinBindingId id : uniqueBindings)
        matMgr_.destroySkinBinding(id, ctx_);
    ent.skinBindingId = kInvalidSkinBindingId;
    ent.subMeshSkinBindings.clear();
}

// ─── Skinned material conversion ──────────────────────────────────────────────

void Application::convertModelMaterialsToSkinned()
{
    bool hasSkinnedEntity = false;
    for (const auto& ent : sceneMgr_.getModelEntities()) {
        if (ent.hasSkin_) {
            hasSkinnedEntity = true;
            break;
        }
    }
    if (hasSkinnedEntity)
        vkDeviceWaitIdle(ctx_.getDevice());

    for (auto& ent : sceneMgr_.getModelEntities()) {
        if (!ent.hasSkin_) {
            destroyEntitySkinBindings(ent);
            continue;
        }

        auto resolveMaterial = [&](const SceneManager::SubMesh* subMesh) -> MaterialId {
            MaterialId materialId = ent.materialId;
            if (subMesh && subMesh->materialSlot >= 0
                && subMesh->materialSlot < static_cast<int>(ent.subMeshMaterials.size())) {
                const MaterialId slotMaterial = ent.subMeshMaterials[subMesh->materialSlot];
                if (matMgr_.isValid(slotMaterial))
                    materialId = slotMaterial;
            }
            if (!matMgr_.isValid(materialId))
                materialId = matMgr_.getDefaultMeshMaterialId();
            return materialId;
        };

        if (ent.subMeshes.empty()) {
            const MaterialId materialId = resolveMaterial(nullptr);
            if (!matMgr_.isValidSkinBinding(ent.skinBindingId)
                || matMgr_.getSkinBindingMaterial(ent.skinBindingId) != materialId) {
                if (matMgr_.isValidSkinBinding(ent.skinBindingId))
                    matMgr_.destroySkinBinding(ent.skinBindingId, ctx_);
                ent.skinBindingId = matMgr_.createSkinBinding(
                    materialId, ctx_, bufMgr_, pipeMgr_);
            }
            ent.subMeshSkinBindings.clear();
            continue;
        }

        std::unordered_map<uint64_t, SkinBindingId> bindingByMaterialAndSkin;
        std::unordered_set<SkinBindingId> oldBindings;
        std::unordered_set<SkinBindingId> usedBindings;
        for (uint32_t rawId : ent.subMeshSkinBindings) {
            const SkinBindingId id = static_cast<SkinBindingId>(rawId);
            if (matMgr_.isValidSkinBinding(id)) oldBindings.insert(id);
        }

        std::vector<uint32_t> newBindings(ent.subMeshes.size(), kInvalidSkinBindingId);
        for (size_t i = 0; i < ent.subMeshes.size(); ++i) {
            const auto& subMesh = ent.subMeshes[i];
            if (subMesh.skinIndex < 0) continue;

            const MaterialId materialId = resolveMaterial(&subMesh);
            const uint64_t key = (static_cast<uint64_t>(materialId) << 32)
                               | static_cast<uint32_t>(subMesh.skinIndex);

            SkinBindingId bindingId = kInvalidSkinBindingId;
            const auto cached = bindingByMaterialAndSkin.find(key);
            if (cached != bindingByMaterialAndSkin.end()) {
                bindingId = cached->second;
            } else if (i < ent.subMeshSkinBindings.size()) {
                const SkinBindingId existing = ent.subMeshSkinBindings[i];
                if (matMgr_.isValidSkinBinding(existing)
                    && matMgr_.getSkinBindingMaterial(existing) == materialId) {
                    bindingId = existing;
                }
            }

            if (bindingId == kInvalidSkinBindingId) {
                bindingId = matMgr_.createSkinBinding(
                    materialId, ctx_, bufMgr_, pipeMgr_);
            }
            bindingByMaterialAndSkin[key] = bindingId;
            newBindings[i] = bindingId;
            if (bindingId != kInvalidSkinBindingId)
                usedBindings.insert(bindingId);
        }

        for (SkinBindingId id : oldBindings) {
            if (!usedBindings.count(id))
                matMgr_.destroySkinBinding(id, ctx_);
        }
        if (matMgr_.isValidSkinBinding(ent.skinBindingId))
            matMgr_.destroySkinBinding(ent.skinBindingId, ctx_);
        ent.skinBindingId = kInvalidSkinBindingId;
        ent.subMeshSkinBindings = std::move(newBindings);

        std::cout << "[tinyEngine] created " << usedBindings.size()
                  << " isolated skin binding(s) for entity " << ent.entityId << "\n";
    }
}

// ─── Draw frame ───────────────────────────────────────────────────────────────

void Application::drawFrame(float dt)
{
    vkWaitForFences(ctx_.getDevice(), 1, &cmdMgr_.getInFlightFence(currentFrame_), VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(ctx_.getDevice(), swapChain_.getSwapChain(),
                                            UINT64_MAX,
                                            cmdMgr_.getImageAvailableSemaphore(currentFrame_),
                                            VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapChain(); return; }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swap chain image!");

    // Animator definition edits happen in UIManager::prepareFrame(). Detect
    // every revision change before Sequencer evaluation so the current
    // playhead is recomputed in the same rendered frame.
    detectAnimatorDefinitionChanges();

    // ── Sequencer 驱动 ───────────────────────────────────────────────────────
    sequenceCameraActive_ = false;
    activeSequenceCameraEntityId_ = 0;
    {
        seqPlayer_.setSequenceRef(currentSequence_);
        const bool sequencerPreviewRequested =
            sequencerControlEnabled_ && sequencerPreviewPending_;
        const double previousAnimatorEvalTime = lastSequencerAnimatorEvalTime_;
        double evaluatedAnimatorTime = -1.0;
        SequencePlayer::FrameCallbacks seqCallbacks;
        if (sequencerControlEnabled_) {
            seqCallbacks.onCameraPathEval = [&](double localT, const std::string& pathAssetRelPath) {
                // Cache camera paths to avoid reloading from disk every frame
                auto it = cameraPathCache_.find(pathAssetRelPath);
                if (it == cameraPathCache_.end()) {
                    CameraPath path;
                    std::string err;
                    if (!SequenceAssetLoader::loadCameraPath(pathAssetRelPath, path, &err)) {
                        std::cerr << "[Sequencer] Failed to load camera path: " << err << "\n";
                        return;
                    }
                    it = cameraPathCache_.emplace(pathAssetRelPath, std::move(path)).first;
                }
                auto result = it->second.evaluate(localT);
                if (!sequencerCameraFollowEnabled_ && !sequenceCaptureActive_)
                    return;
                camera_.Position = result.position;
                camera_.SetOrientation(result.orientation);
                camera_.FovDeg = result.fovDeg;
                sequenceCameraActive_ = true;
            };

            seqCallbacks.onTransformTweenEval = [&](double localT, const TransformTweenClip& clip) {
                auto result = clip.evaluate(localT);
                if (sceneMgr_.getModelEntities().empty()) return;
                auto& ent = sceneMgr_.getModelEntities()[0];
                ent.transform.position = result.position;
                ent.transform.rotation = result.rotation;
                ent.transform.scale = result.scale;
            };

            seqCallbacks.onTransformKeyframeEval = [&](double t, uint64_t entityId, const TransformKeyframeTrack::EvalResult& result) {
                if (!result.valid) return;
                if (!seqPlayer_.isPlaying() && !sequencerPreviewRequested) return;
                auto* ent = sceneMgr_.getModelEntity(entityId);
                if (!ent) return;
                ent->transform.position = result.position;
                ent->transform.rotation = result.rotation;
                ent->transform.scale = result.scale;
                ent->syncCameraFromTransform();
            };

            seqCallbacks.onAnimatorKeyframeEval = [&](double t, uint64_t entityId,
                                                       const AnimatorKeyframeTrack::EvalResult& result) {
                auto* ent = sceneMgr_.getModelEntity(entityId);
                if (!ent || !ent->animatorController.hasStates()) return;

                // seek、拖动、循环回绕属于时间不连续点，禁止复用上一姿势的 Root Motion delta。
                if ((sequencerPreviewRequested && !sequenceCaptureActive_)
                    || (previousAnimatorEvalTime >= 0.0 && t + 1e-6 < previousAnimatorEvalTime)) {
                    ent->rootMotionInitialized = false;
                }

                // 从 0 到 t 按事件时间顺序求值；Setter 持续，Trigger 只在关键帧边界脉冲一次。
                ent->pendingSequencerBlend = ent->animatorController.computeBlendAtTime(
                    static_cast<float>(t), ent->animationClips,
                    result.timeline, result.initialState);
                ent->hasPendingSequencerBlend = true;
                evaluatedAnimatorTime = t;
            };

            seqCallbacks.onCameraShotEval =
                [&](double, const CameraShotKeyframe& keyframe) {
                    auto* cameraEntity =
                        sceneMgr_.getModelEntity(keyframe.cameraEntityId);
                    if (!cameraEntity || !cameraEntity->isCamera())
                        return;
                    cameraEntity->syncCameraFromTransform();
                    activeSequenceCameraEntityId_ = cameraEntity->entityId;
                    if (!sequencerCameraFollowEnabled_ && !sequenceCaptureActive_)
                        return;
                    camera_.Position = cameraEntity->cameraData.position;
                    camera_.SetOrientation(cameraEntity->cameraData.orientation);
                    camera_.FovDeg = cameraEntity->cameraData.fovDeg;
                    camera_.NearPlane = cameraEntity->cameraData.nearPlane;
                    camera_.FarPlane = cameraEntity->cameraData.farPlane;
                    sequenceCameraActive_ = true;
                };
        }

        seqPlayer_.update(dt, seqCallbacks);
        if (evaluatedAnimatorTime >= 0.0)
            lastSequencerAnimatorEvalTime_ = evaluatedAnimatorTime;
        if (sequencerPreviewPending_)
            sequencerPreviewPending_ = false;
    }

    // 逐实体动画采样与骨骼矩阵上传：每个实体使用自己的 skeleton/clips，
    // 不再依赖全局单例，支持多骨架动画实体同场景。
    std::vector<SkinBindingId> updatedBindings;
    const glm::mat4 view = camera_.GetViewMatrix();
    const glm::mat4 proj = camera_.GetProjectionMatrix();
    matMgr_.updateAllUBOs(imageIndex, view, proj);

    auto updateSkinBinding = [&](SkinBindingId id, const std::vector<glm::mat4>& palette) {
        if (!matMgr_.isValidSkinBinding(id)) return;
        if (std::find(updatedBindings.begin(), updatedBindings.end(), id) != updatedBindings.end())
            return;
        matMgr_.updateSkinBindingBones(id, imageIndex, palette);
        updatedBindings.push_back(id);
    };

    for (auto& ent : sceneMgr_.getModelEntities()) {
        if (!ent.hasSkin_ || !ent.skeleton || ent.skeleton->bones.empty())
            continue;

        const auto& skeleton = ent.skeleton;
        const auto& clips = ent.animationClips;

        AnimatorController::BlendCommand blendCommand;
        const bool useSequencerAnimator =
            sequencerControlEnabled_ && ent.hasPendingSequencerBlend;
        if (useSequencerAnimator != ent.sequencerAnimatorActive) {
            // Source changes must not reuse Root Motion samples from the other
            // time domain (live state machine vs. Sequencer absolute time).
            ent.rootMotionInitialized = false;
            ent.sequencerAnimatorActive = useSequencerAnimator;
        }

        if (useSequencerAnimator) {
            // Sequencer owns only entities whose AnimatorKeyframe callback produced
            // a pose. Other entities continue running their live state machines.
            blendCommand = ent.pendingSequencerBlend;
            ent.hasPendingSequencerBlend = false;
        } else if (ent.previewClipIndex >= 0 && ent.previewClipIndex < static_cast<int>(clips.size())) {
            // ── Animator Clip Preview：绕开状态机 ──
            const AnimationClip* clip = &clips[ent.previewClipIndex];
            ent.previewTime += (sequenceCaptureActive_ ? 0.f : dt)
                             * ent.previewSpeed;
            if (clip->duration > 0.f)
                ent.previewTime = std::fmod(ent.previewTime, clip->duration);
            blendCommand.poseA.samples[0] = {
                clip, ent.previewTime, 1.f
            };
            blendCommand.poseA.sampleCount = 1;
        } else {
            // No Sequencer pose owns this entity: run the live Animator normally.
            if (!ent.animatorController.hasStates() && !clips.empty()) {
                ent.animatorController.configureFromClips(clips);
            }
            blendCommand = ent.animatorController.update(
                sequenceCaptureActive_ ? 0.f : dt, clips);
        }

        auto evaluateStatePose = [](const AnimatorController::StatePoseCommand& statePose,
                                    int boneIndex,
                                    const glm::mat4& bindTransform) {
            if (statePose.sampleCount == 0 || !statePose.samples[0].clip) {
                BoneLocalTransform bindPose;
                glm::vec3 skew;
                glm::vec4 perspective;
                if (glm::decompose(bindTransform, bindPose.scale, bindPose.rotation,
                                   bindPose.translation, skew, perspective)) {
                    bindPose.rotation = glm::normalize(bindPose.rotation);
                }
                return bindPose;
            }

            BoneLocalTransform pose =
                statePose.samples[0].clip->evaluateBoneLocalTransformParts(
                    boneIndex, statePose.samples[0].sampleTime, bindTransform);
            if (statePose.sampleCount > 1 && statePose.samples[1].clip) {
                const BoneLocalTransform target =
                    statePose.samples[1].clip->evaluateBoneLocalTransformParts(
                        boneIndex, statePose.samples[1].sampleTime, bindTransform);
                const float totalWeight =
                    statePose.samples[0].weight + statePose.samples[1].weight;
                const float weight = totalWeight > 1e-6f
                    ? std::clamp(statePose.samples[1].weight / totalWeight, 0.f, 1.f)
                    : 0.f;
                pose.translation = glm::mix(pose.translation, target.translation, weight);
                pose.rotation = glm::normalize(
                    glm::slerp(pose.rotation, target.rotation, weight));
                pose.scale = glm::mix(pose.scale, target.scale, weight);
            }
            return pose;
        };

        std::vector<glm::mat4> localTransforms(skeleton->bones.size());
        for (size_t i = 0; i < skeleton->bones.size(); ++i) {
            const glm::mat4& bindTransform = skeleton->bones[i].localBindTransform;
            if (blendCommand.poseA.sampleCount == 0) {
                localTransforms[i] = bindTransform;
                continue;
            }

            BoneLocalTransform pose = evaluateStatePose(
                blendCommand.poseA, static_cast<int>(i), bindTransform);
            if (blendCommand.poseB.sampleCount > 0) {
                const BoneLocalTransform target = evaluateStatePose(
                    blendCommand.poseB, static_cast<int>(i), bindTransform);
                const float weight = std::clamp(blendCommand.blendWeight, 0.f, 1.f);
                pose.translation = glm::mix(pose.translation, target.translation, weight);
                pose.rotation = glm::normalize(glm::slerp(pose.rotation, target.rotation, weight));
                pose.scale = glm::mix(pose.scale, target.scale, weight);
            }
            localTransforms[i] = pose.toMatrix();
        }

        // ── Root Motion ────────────────────────────────────────────
        {
            struct RootDelta {
                glm::vec3 translation{0.f};
                glm::quat rotation{1.f, 0.f, 0.f, 0.f};
                bool valid = false;
            };

            auto rootBoneIndex = [&](const AnimatorState* state) {
                int rootBoneIdx = 0;
                if (state && !state->rootBoneName.empty()) {
                    auto it = skeleton->boneNameToIndex.find(state->rootBoneName);
                    if (it != skeleton->boneNameToIndex.end())
                        rootBoneIdx = it->second;
                }
                return std::clamp(rootBoneIdx, 0,
                                  static_cast<int>(skeleton->bones.size()) - 1);
            };

            if (!ent.rootMotionInitialized) {
                ent.rootMotionClipHistory.clear();
            }

            auto decomposeTransform = [](const glm::mat4& transform) {
                BoneLocalTransform result;
                glm::vec3 skew{};
                glm::vec4 perspective{};
                if (glm::decompose(
                        transform, result.scale, result.rotation,
                        result.translation, skew, perspective)) {
                    result.rotation = glm::normalize(result.rotation);
                }
                return result;
            };

            // Root Motion must be extracted in skeleton/model space. A selected
            // bone such as Mixamo Hips commonly inherits a 0.01 scale from an
            // Armature parent; using its local translation directly as world
            // displacement makes the actor move roughly 100x too far.
            auto evaluateClipRoot = [&](const AnimationClip& clip,
                                        float sampleTime,
                                        int rootBoneIdx) {
                std::vector<int> ancestorChain;
                for (int boneIndex = rootBoneIdx;
                     boneIndex >= 0
                     && boneIndex < static_cast<int>(skeleton->bones.size());
                     boneIndex = skeleton->bones[boneIndex].parentIndex) {
                    ancestorChain.push_back(boneIndex);
                }
                std::reverse(ancestorChain.begin(), ancestorChain.end());

                glm::mat4 animatedGlobal(1.f);
                bool hasParent = false;
                int parentIndex = -1;
                for (const int boneIndex : ancestorChain) {
                    const auto& bone = skeleton->bones[boneIndex];
                    const glm::mat4 animatedLocal =
                        clip.evaluateBoneLocalTransform(
                            boneIndex, sampleTime, bone.localBindTransform);
                    const glm::mat4 bindToAnimatedLocal =
                        bone.globalBindTransform
                        * glm::inverse(bone.localBindTransform)
                        * animatedLocal;
                    animatedGlobal = hasParent
                        ? animatedGlobal
                            * glm::inverse(
                                skeleton->bones[parentIndex].globalBindTransform)
                            * bindToAnimatedLocal
                        : bindToAnimatedLocal;
                    hasParent = true;
                    parentIndex = boneIndex;
                }
                return decomposeTransform(animatedGlobal);
            };

            auto evaluateStateRootDelta =
                [&](const AnimatorController::StatePoseCommand& statePose) {
                RootDelta stateDelta;
                const AnimatorState* state = statePose.state;
                if (!state || state->rootMotion != AnimatorState::RootMotionMode::Follow
                    || statePose.sampleCount == 0) {
                    return stateDelta;
                }

                const int rootBoneIdx = rootBoneIndex(state);
                std::array<RootDelta, 2> sampleDeltas{};
                float validWeight = 0.f;
                for (uint32_t sampleIndex = 0;
                     sampleIndex < statePose.sampleCount;
                     ++sampleIndex) {
                    const auto& sample = statePose.samples[sampleIndex];
                    if (!sample.clip) continue;

                    const BoneLocalTransform currentRoot = evaluateClipRoot(
                        *sample.clip, sample.sampleTime, rootBoneIdx);
                    const std::string historyKey =
                        state->name + "\n" + sample.clip->name + "\n"
                        + std::to_string(rootBoneIdx);
                    auto historyIt = ent.rootMotionClipHistory.find(historyKey);
                    if (historyIt != ent.rootMotionClipHistory.end()) {
                        const auto& previous = historyIt->second;
                        RootDelta delta;
                        delta.valid = true;

                        const bool forwardWrapped =
                            state->loop
                            && state->direction == PlaybackDirection::Forward
                            && sample.sampleTime + 1e-5f < previous.sampleTime;
                        const bool reverseWrapped =
                            state->loop
                            && state->direction == PlaybackDirection::Reverse
                            && sample.sampleTime > previous.sampleTime + 1e-5f;

                        if (forwardWrapped || reverseWrapped) {
                            const BoneLocalTransform startRoot =
                                evaluateClipRoot(*sample.clip, 0.f, rootBoneIdx);
                            const BoneLocalTransform endRoot =
                                evaluateClipRoot(*sample.clip,
                                                 sample.clip->duration,
                                                 rootBoneIdx);
                            if (forwardWrapped) {
                                delta.translation =
                                    (endRoot.translation - previous.translation)
                                    + (currentRoot.translation - startRoot.translation);
                                const glm::quat first =
                                    glm::inverse(previous.rotation) * endRoot.rotation;
                                const glm::quat second =
                                    glm::inverse(startRoot.rotation) * currentRoot.rotation;
                                delta.rotation = glm::normalize(first * second);
                            } else {
                                delta.translation =
                                    (startRoot.translation - previous.translation)
                                    + (currentRoot.translation - endRoot.translation);
                                const glm::quat first =
                                    glm::inverse(previous.rotation) * startRoot.rotation;
                                const glm::quat second =
                                    glm::inverse(endRoot.rotation) * currentRoot.rotation;
                                delta.rotation = glm::normalize(first * second);
                            }
                        } else {
                            delta.translation =
                                currentRoot.translation - previous.translation;
                            delta.rotation = glm::normalize(
                                glm::inverse(previous.rotation)
                                * currentRoot.rotation);
                        }
                        sampleDeltas[sampleIndex] = delta;
                        validWeight += sample.weight;
                    }

                    ent.rootMotionClipHistory[historyKey] = {
                        sample.sampleTime,
                        currentRoot.translation,
                        currentRoot.rotation
                    };
                }

                if (validWeight <= 1e-6f) return stateDelta;
                stateDelta.valid = true;
                if (statePose.sampleCount == 1 || !sampleDeltas[1].valid) {
                    if (sampleDeltas[0].valid) stateDelta = sampleDeltas[0];
                    return stateDelta;
                }
                if (!sampleDeltas[0].valid) {
                    stateDelta = sampleDeltas[1];
                    return stateDelta;
                }

                const float weight = std::clamp(
                    statePose.samples[1].weight / validWeight, 0.f, 1.f);
                stateDelta.translation = glm::mix(
                    sampleDeltas[0].translation,
                    sampleDeltas[1].translation, weight);
                stateDelta.rotation = glm::normalize(glm::slerp(
                    sampleDeltas[0].rotation,
                    sampleDeltas[1].rotation, weight));
                return stateDelta;
            };

            RootDelta rootDeltaA = evaluateStateRootDelta(blendCommand.poseA);
            RootDelta rootDeltaB = evaluateStateRootDelta(blendCommand.poseB);
            RootDelta finalRootDelta;
            const float transitionWeight =
                std::clamp(blendCommand.blendWeight, 0.f, 1.f);
            const glm::quat identityRotation(1.f, 0.f, 0.f, 0.f);
            if (rootDeltaA.valid && rootDeltaB.valid) {
                finalRootDelta.valid = true;
                finalRootDelta.translation = glm::mix(
                    rootDeltaA.translation, rootDeltaB.translation,
                    transitionWeight);
                finalRootDelta.rotation = glm::normalize(glm::slerp(
                    rootDeltaA.rotation, rootDeltaB.rotation,
                    transitionWeight));
            } else if (rootDeltaA.valid) {
                finalRootDelta.valid = true;
                finalRootDelta.translation =
                    rootDeltaA.translation * (1.f - transitionWeight);
                finalRootDelta.rotation = glm::normalize(glm::slerp(
                    identityRotation, rootDeltaA.rotation,
                    1.f - transitionWeight));
            } else if (rootDeltaB.valid) {
                finalRootDelta.valid = true;
                finalRootDelta.translation =
                    rootDeltaB.translation * transitionWeight;
                finalRootDelta.rotation = glm::normalize(glm::slerp(
                    identityRotation, rootDeltaB.rotation,
                    transitionWeight));
            }

            auto usesRootMotion = [](const AnimatorState* state) {
                return state
                    && state->rootMotion != AnimatorState::RootMotionMode::None;
            };
            const bool anyFollow =
                (blendCommand.stateA
                 && blendCommand.stateA->rootMotion
                    == AnimatorState::RootMotionMode::Follow)
                || (blendCommand.stateB
                    && blendCommand.stateB->rootMotion
                       == AnimatorState::RootMotionMode::Follow);

            // Remove extracted motion from each participating State before the
            // State-transition blend. Follow uses the clip's playback-start pose
            // as its in-place reference; Locked uses the skeleton bind pose.
            auto adjustedStateRootPose =
                [&](const AnimatorController::StatePoseCommand& statePose,
                    int boneIndex) {
                const glm::mat4& bindTransform =
                    skeleton->bones[boneIndex].localBindTransform;
                BoneLocalTransform pose = evaluateStatePose(
                    statePose, boneIndex, bindTransform);
                const AnimatorState* state = statePose.state;
                if (!usesRootMotion(state)
                    || rootBoneIndex(state) != boneIndex) {
                    return pose;
                }
                if (state->rootMotion
                    == AnimatorState::RootMotionMode::Locked) {
                    return decomposeTransform(bindTransform);
                }

                auto referencePose = statePose;
                for (uint32_t sampleIndex = 0;
                     sampleIndex < referencePose.sampleCount;
                     ++sampleIndex) {
                    auto& sample = referencePose.samples[sampleIndex];
                    if (!sample.clip) continue;
                    sample.sampleTime =
                        state->direction == PlaybackDirection::Reverse
                        ? sample.clip->duration : 0.f;
                }
                return evaluateStatePose(
                    referencePose, boneIndex, bindTransform);
            };

            std::array<int, 2> adjustedRootBones{-1, -1};
            size_t adjustedRootBoneCount = 0;
            auto addAdjustedRootBone = [&](const AnimatorState* state) {
                if (!usesRootMotion(state)) return;
                const int boneIndex = rootBoneIndex(state);
                for (size_t i = 0; i < adjustedRootBoneCount; ++i) {
                    if (adjustedRootBones[i] == boneIndex) return;
                }
                adjustedRootBones[adjustedRootBoneCount++] = boneIndex;
            };
            addAdjustedRootBone(blendCommand.stateA);
            addAdjustedRootBone(blendCommand.stateB);

            for (size_t i = 0; i < adjustedRootBoneCount; ++i) {
                const int boneIndex = adjustedRootBones[i];
                const glm::mat4& bindTransform =
                    skeleton->bones[boneIndex].localBindTransform;
                BoneLocalTransform pose = adjustedStateRootPose(
                    blendCommand.poseA, boneIndex);
                if (blendCommand.poseB.sampleCount > 0) {
                    const BoneLocalTransform target = adjustedStateRootPose(
                        blendCommand.poseB, boneIndex);
                    pose.translation = glm::mix(
                        pose.translation, target.translation, transitionWeight);
                    pose.rotation = glm::normalize(glm::slerp(
                        pose.rotation, target.rotation, transitionWeight));
                    pose.scale = glm::mix(
                        pose.scale, target.scale, transitionWeight);
                } else if (blendCommand.poseA.sampleCount == 0) {
                    pose = decomposeTransform(bindTransform);
                }
                localTransforms[boneIndex] = pose.toMatrix();
            }

            const bool finiteRootDelta =
                std::isfinite(finalRootDelta.translation.x)
                && std::isfinite(finalRootDelta.translation.y)
                && std::isfinite(finalRootDelta.translation.z)
                && std::isfinite(finalRootDelta.rotation.w)
                && std::isfinite(finalRootDelta.rotation.x)
                && std::isfinite(finalRootDelta.rotation.y)
                && std::isfinite(finalRootDelta.rotation.z);
            if (ent.rootMotionInitialized && anyFollow
                && finalRootDelta.valid && finiteRootDelta) {
                const glm::quat actorRotation =
                    glm::normalize(ent.transform.rotation);
                const glm::vec3 actorLocalDelta =
                    finalRootDelta.translation * ent.transform.scale;
                ent.transform.position += actorRotation * actorLocalDelta;
                ent.transform.rotation = glm::normalize(
                    actorRotation * finalRootDelta.rotation);
            } else if (finalRootDelta.valid && !finiteRootDelta) {
                std::cerr << "[RootMotion] discarded non-finite delta for entity "
                          << ent.entityId << "\n";
                ent.rootMotionClipHistory.clear();
            }
            ent.rootMotionInitialized = anyFollow && finiteRootDelta;
        }

        std::vector<glm::mat4> finalBoneMatrices;
        skeleton->computeFinalMatrices(localTransforms, finalBoneMatrices);

        auto makeSkinPalette = [&](int skinIndex) -> std::vector<glm::mat4> {
            if (skinIndex >= 0
                && skinIndex < static_cast<int>(skeleton->skinBoneIndices.size())) {
                const auto& map = skeleton->skinBoneIndices[skinIndex];
                std::vector<glm::mat4> palette;
                palette.reserve(std::min(map.size(), static_cast<size_t>(kMaxBones)));
                for (size_t i = 0; i < map.size() && i < static_cast<size_t>(kMaxBones); ++i) {
                    const int globalBone = map[i];
                    palette.push_back((globalBone >= 0
                                       && globalBone < static_cast<int>(finalBoneMatrices.size()))
                        ? finalBoneMatrices[globalBone]
                        : glm::mat4(1.f));
                }
                return palette;
            }
            return finalBoneMatrices;
        };

        if (ent.subMeshes.empty()) {
            updateSkinBinding(ent.skinBindingId, makeSkinPalette(0));
            continue;
        }

        for (size_t subMeshIndex = 0; subMeshIndex < ent.subMeshes.size(); ++subMeshIndex) {
            const auto& subMesh = ent.subMeshes[subMeshIndex];
            const SkinBindingId bindingId = subMeshIndex < ent.subMeshSkinBindings.size()
                ? static_cast<SkinBindingId>(ent.subMeshSkinBindings[subMeshIndex])
                : kInvalidSkinBindingId;
            updateSkinBinding(bindingId, makeSkinPalette(subMesh.skinIndex));
        }
    }

    VkFence& imgFence = cmdMgr_.getImageInFlight(imageIndex);
    if (imgFence != VK_NULL_HANDLE)
        vkWaitForFences(ctx_.getDevice(), 1, &imgFence, VK_TRUE, UINT64_MAX);

    VkCommandBuffer cb = cmdMgr_.getCommandBuffer(imageIndex);
    recordCommandBuffer(cb, imageIndex);

    imgFence = cmdMgr_.getInFlightFence(currentFrame_);

    VkSemaphore waitSems[]   = { cmdMgr_.getImageAvailableSemaphore(currentFrame_) };
    VkSemaphore signalSems[] = { cmdMgr_.getRenderFinishedSemaphore(currentFrame_) };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1; si.pWaitSemaphores   = waitSems;
    si.pWaitDstStageMask    = waitStages;
    si.commandBufferCount   = 1; si.pCommandBuffers   = &cb;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = signalSems;

    vkResetFences(ctx_.getDevice(), 1, &cmdMgr_.getInFlightFence(currentFrame_));
    if (vkQueueSubmit(ctx_.getGraphicsQueue(), 1, &si, cmdMgr_.getInFlightFence(currentFrame_)) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer!");

    VkSwapchainKHR sc = swapChain_.getSwapChain();
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = signalSems;
    pi.swapchainCount     = 1; pi.pSwapchains     = &sc;
    pi.pImageIndices      = &imageIndex;

    result = vkQueuePresentKHR(ctx_.getPresentQueue(), &pi);

    if (frameCapture_.isSubmitted()
        || sequenceCaptureReadbackSubmitted_) {
        VkFence captureFence = cmdMgr_.getInFlightFenceVal(currentFrame_);
        const VkResult captureWait = vkWaitForFences(
            ctx_.getDevice(), 1, &captureFence,
            VK_TRUE, UINT64_MAX);
        if (captureWait == VK_SUCCESS
            && frameCapture_.isSubmitted()) {
            frameCapture_.complete(ctx_);
        }
        if (sequenceCaptureReadbackSubmitted_
            && !sequenceCaptureFrameReady_) {
            sequenceCapturePendingRgba_.clear();
            sequenceCaptureReadbackError_.clear();
            if (captureWait != VK_SUCCESS) {
                sequenceCaptureReadbackError_ =
                    "Waiting for the offscreen capture fence failed";
            } else if (!sequenceCaptureTarget_.readRgba(
                           ctx_, sequenceCapturePendingRgba_,
                           &sequenceCaptureReadbackError_)) {
                sequenceCapturePendingRgba_.clear();
            }
            sequenceCaptureFrameReady_ = true;
        }
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Application::recordSequenceCapturePass(
    VkCommandBuffer cb, uint32_t imageIndex)
{
    if (!sequenceCaptureActive_
        || !sequenceCaptureFramePending_
        || sequenceCaptureReadbackSubmitted_
        || sequenceCaptureFrameReady_
        || !sequenceCaptureTarget_.isReady()) {
        return;
    }

    const VkExtent2D extent = sequenceCaptureTarget_.extent();
    const glm::mat4 captureView = camera_.GetViewMatrix();
    glm::mat4 captureProj = glm::perspective(
        glm::radians(camera_.FovDeg),
        static_cast<float>(extent.width)
            / static_cast<float>(extent.height),
        camera_.NearPlane, camera_.FarPlane);
    captureProj[1][1] *= -1.0f;
    matMgr_.updateAllPipUBOs(
        imageIndex, captureView, captureProj);

    std::array<VkClearValue, 2> clears{};
    if (ui_) {
        const ImVec4 color = ui_->getClearColor();
        clears[0].color = {
            color.x * color.w, color.y * color.w,
            color.z * color.w, color.w
        };
    } else {
        clears[0].color = {0.f, 0.f, 0.f, 1.f};
    }
    clears[1].depthStencil = {1.f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = rpMgr_.getPipRenderPass();
    renderPassInfo.framebuffer = sequenceCaptureTarget_.framebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount =
        static_cast<uint32_t>(clears.size());
    renderPassInfo.pClearValues = clears.data();

    TINYENGINE(cb, "Sequence Capture Render Pass");
    vkCmdBeginRenderPass(
        cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(cb, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cb, 0, 1, &scissor);

    const auto& allEntities = sceneMgr_.getModelEntities();
    const MaterialId fallbackMaterial =
        matMgr_.isValid(sceneMgr_.getModelMaterialId())
            ? sceneMgr_.getModelMaterialId()
            : matMgr_.getDefaultMeshMaterialId();

    for (const auto& entity : allEntities) {
        // Camera Actors are editor visualizations, never photographed scene
        // content. A visible camera prop should be a normal Mesh Entity.
        if (entity.isCamera()
            || !entity.visible
            || entity.indexCount == 0
            || !entity.vertexBuffer
            || !entity.indexBuffer) {
            continue;
        }

        VkBuffer vertexBuffer = entity.vertexBuffer;
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(
            cb, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(
            cb, entity.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        glm::mat4 model = entity.transform.GetModelMatrix()
            * glm::mat4_cast(entity.modelRotationOffset);
        PushConstants push{
            model,
            glm::mat4(glm::transpose(
                glm::inverse(glm::mat3(model))))
        };

        const MaterialId entityMaterial =
            matMgr_.isValid(entity.materialId)
                ? entity.materialId : fallbackMaterial;
        auto drawRange = [&](MaterialId materialId,
                             SkinBindingId skinBinding,
                             uint32_t indexOffset,
                             uint32_t indexCount) {
            if (!matMgr_.isValid(materialId))
                materialId = fallbackMaterial;
            const bool skinned =
                matMgr_.isValidSkinBinding(skinBinding);
            const VkPipeline pipeline = skinned
                ? pipeMgr_.getSkinnedPipeline()
                : matMgr_.getPipeline(
                    materialId, ctx_, pipeMgr_);
            const VkPipelineLayout layout = skinned
                ? pipeMgr_.getSkinnedPipelineLayout()
                : pipeMgr_.getMainPipelineLayout();
            const VkDescriptorSet descriptorSet = skinned
                ? matMgr_.getSkinPipDescriptorSet(
                    skinBinding, imageIndex)
                : matMgr_.getPipDescriptorSet(
                    materialId, imageIndex);

            vkCmdBindPipeline(
                cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdPushConstants(
                cb, layout, VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(PushConstants), &push);
            vkCmdBindDescriptorSets(
                cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                layout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(
                cb, indexCount, 1, indexOffset, 0, 0);
        };

        if (entity.subMeshes.empty()) {
            drawRange(
                entityMaterial, entity.skinBindingId,
                0, entity.indexCount);
        } else {
            for (size_t subMeshIndex = 0;
                 subMeshIndex < entity.subMeshes.size();
                 ++subMeshIndex) {
                const auto& subMesh =
                    entity.subMeshes[subMeshIndex];
                MaterialId materialId = entityMaterial;
                if (subMesh.materialSlot >= 0
                    && subMesh.materialSlot
                        < static_cast<int>(
                            entity.subMeshMaterials.size())) {
                    const MaterialId slotMaterial =
                        entity.subMeshMaterials[
                            subMesh.materialSlot];
                    if (matMgr_.isValid(slotMaterial))
                        materialId = slotMaterial;
                }
                const SkinBindingId skinBinding =
                    subMeshIndex
                            < entity.subMeshSkinBindings.size()
                        ? static_cast<SkinBindingId>(
                            entity.subMeshSkinBindings[
                                subMeshIndex])
                        : kInvalidSkinBindingId;
                drawRange(
                    materialId, skinBinding,
                    subMesh.indexOffset,
                    subMesh.indexCount);
            }
        }
    }

    if (sceneMgr_.getInstanceCount() > 0
        && sceneMgr_.getInstanceBuffer() != VK_NULL_HANDLE) {
        const MaterialId boxMaterial =
            matMgr_.getDefaultBoxMaterialId();
        const VkPipeline boxPipeline = matMgr_.getPipeline(
            boxMaterial, ctx_, pipeMgr_);
        vkCmdBindPipeline(
            cb, VK_PIPELINE_BIND_POINT_GRAPHICS, boxPipeline);
        const VkDescriptorSet descriptorSet =
            matMgr_.getPipDescriptorSet(
                boxMaterial, imageIndex);
        vkCmdBindDescriptorSets(
            cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeMgr_.getBoxPipelineLayout(),
            0, 1, &descriptorSet, 0, nullptr);
        VkBuffer buffers[] = {
            sceneMgr_.getCubeVertexBuffer(),
            sceneMgr_.getInstanceBuffer()
        };
        VkDeviceSize offsets[] = {0, 0};
        vkCmdBindVertexBuffers(
            cb, 0, 2, buffers, offsets);
        vkCmdBindIndexBuffer(
            cb, sceneMgr_.getCubeIndexBuffer(),
            0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(
            cb, sceneMgr_.getCubeIndexCount(),
            sceneMgr_.getInstanceCount(), 0, 0, 0);
    }

    if (sequenceCaptureIncludeUi_
        && ImGui::GetCurrentContext()) {
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData && drawData->Valid) {
            TINYENGINE(cb, "Sequence Capture ImGui Overlay");
            ImGui_ImplVulkan_RenderDrawData(drawData, cb);
        }
    }

    vkCmdEndRenderPass(cb);
    sequenceCaptureTarget_.recordReadback(cb);
    sequenceCaptureReadbackSubmitted_ = true;
}

void Application::recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex)
{
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin recording command buffer!");

    recordSequenceCapturePass(cb, imageIndex);

    // ── PiP Render Pass (if Camera Actor selected) ──────────────────────────────
    bool renderPipForCamera = false;
    SequencerCamera* pipCameraPtr = nullptr;
    
    if (selectedCameraEntityId_ != 0) {
        auto* camEnt = sceneMgr_.getModelEntity(selectedCameraEntityId_);
        if (camEnt && camEnt->isCamera() && camEnt->cameraPreviewEnabled) {
            camEnt->syncCameraFromTransform();
            pipCameraPtr = &camEnt->cameraData;
            renderPipForCamera = true;
        }
    }
    
    // Legacy PiP (via showPipWindow_)
    if (pipActive_ && !renderPipForCamera) {
        pipCameraPtr = &pipCamera_;
        renderPipForCamera = true;
    }
    
    if (!sequenceCaptureReadbackSubmitted_
        && renderPipForCamera && pipCameraPtr) {
        constexpr uint32_t pw = FramebufferManager::kPipWidth;
        constexpr uint32_t ph = FramebufferManager::kPipHeight;

        VkClearValue pipClears[2]{};
        // PiP 清除色与主 pass 保持一致（使用同一 ui_->getClearColor()），
        // 避免小窗背景色与主视口背景色不一致。
        if (ui_) {
            const ImVec4 cc = ui_->getClearColor();
            pipClears[0].color = { cc.x * cc.w, cc.y * cc.w, cc.z * cc.w, cc.w };
        } else {
            pipClears[0].color = { 0.f, 0.f, 0.f, 1.f };
        }
        pipClears[1].depthStencil = { 1.f, 0 };

        VkRenderPassBeginInfo pipRpi{};
        pipRpi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pipRpi.renderPass        = rpMgr_.getPipRenderPass();
        pipRpi.framebuffer       = fbMgr_.getPipFramebuffer();
        pipRpi.renderArea.offset = { 0, 0 };
        pipRpi.renderArea.extent = { pw, ph };
        pipRpi.clearValueCount   = 2;
        pipRpi.pClearValues      = pipClears;

        TINYENGINE(cb, "PiP Render Pass");
        vkCmdBeginRenderPass(cb, &pipRpi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport pipVp{};
        pipVp.x = 0.f; pipVp.y = 0.f;
        pipVp.width  = static_cast<float>(pw);
        pipVp.height = static_cast<float>(ph);
        pipVp.minDepth = 0.f; pipVp.maxDepth = 1.f;
        vkCmdSetViewport(cb, 0, 1, &pipVp);

        VkRect2D pipSc{};
        pipSc.offset = { 0, 0 };
        pipSc.extent = { pw, ph };
        vkCmdSetScissor(cb, 0, 1, &pipSc);

        const glm::mat4 pipView = pipCameraPtr->getViewMatrix();
        const glm::mat4 pipProj = pipCameraPtr->getProjMatrixVulkan();

        matMgr_.updateAllPipUBOs(imageIndex, pipView, pipProj);

        const auto& allEnts = sceneMgr_.getModelEntities();
        const MaterialId fallbackMat = matMgr_.isValid(sceneMgr_.getModelMaterialId())
            ? sceneMgr_.getModelMaterialId()
            : matMgr_.getDefaultMeshMaterialId();

        for (size_t ei = 0; ei < allEnts.size(); ++ei) {
            const auto& ent = allEnts[ei];
            if (!ent.visible || ent.indexCount == 0 || !ent.vertexBuffer || !ent.indexBuffer)
                continue;

            // Camera Actor meshes are editor visualizations. A camera preview
            // represents photographed output, so no Camera Actor should appear.
            if (ent.isCamera())
                continue;

            VkBuffer vb = ent.vertexBuffer; VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
            vkCmdBindIndexBuffer(cb, ent.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            // Camera 实体的模型几何体需要叠加 modelRotationOffset（如 Y -90°）使显示朝向与预览一致
            glm::mat4 entModel = ent.transform.GetModelMatrix();
            const glm::mat4 offsetRot = glm::mat4_cast(ent.modelRotationOffset);
            entModel = entModel * offsetRot;
            PushConstants push{
                entModel,
                glm::mat4(glm::transpose(glm::inverse(glm::mat3(entModel))))
            };

            const MaterialId entityMaterial = matMgr_.isValid(ent.materialId)
                ? ent.materialId : fallbackMat;
            auto drawRange = [&](MaterialId materialId, SkinBindingId skinBinding,
                                 uint32_t indexOffset, uint32_t indexCount) {
                if (!matMgr_.isValid(materialId)) materialId = fallbackMat;
                const bool skinned = matMgr_.isValidSkinBinding(skinBinding);
                const VkPipeline pipeline = skinned
                    ? pipeMgr_.getSkinnedPipeline()
                    : matMgr_.getPipeline(materialId, ctx_, pipeMgr_);
                const VkPipelineLayout layout = skinned
                    ? pipeMgr_.getSkinnedPipelineLayout()
                    : pipeMgr_.getMainPipelineLayout();
                const VkDescriptorSet descriptorSet = skinned
                    ? matMgr_.getSkinPipDescriptorSet(skinBinding, imageIndex)
                    : matMgr_.getPipDescriptorSet(materialId, imageIndex);

                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(PushConstants), &push);
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        layout, 0, 1, &descriptorSet, 0, nullptr);
                vkCmdDrawIndexed(cb, indexCount, 1, indexOffset, 0, 0);
            };

            if (ent.subMeshes.empty()) {
                drawRange(entityMaterial, ent.skinBindingId, 0, ent.indexCount);
            } else {
                for (size_t subMeshIndex = 0; subMeshIndex < ent.subMeshes.size(); ++subMeshIndex) {
                    const auto& subMesh = ent.subMeshes[subMeshIndex];
                    MaterialId materialId = entityMaterial;
                    if (subMesh.materialSlot >= 0
                        && subMesh.materialSlot < static_cast<int>(ent.subMeshMaterials.size())) {
                        const MaterialId slotMaterial = ent.subMeshMaterials[subMesh.materialSlot];
                        if (matMgr_.isValid(slotMaterial)) materialId = slotMaterial;
                    }
                    const SkinBindingId skinBinding = subMeshIndex < ent.subMeshSkinBindings.size()
                        ? static_cast<SkinBindingId>(ent.subMeshSkinBindings[subMeshIndex])
                        : kInvalidSkinBindingId;
                    drawRange(materialId, skinBinding, subMesh.indexOffset, subMesh.indexCount);
                }
            }
        }

        if (sceneMgr_.getInstanceCount() > 0 && sceneMgr_.getInstanceBuffer() != VK_NULL_HANDLE) {
            const MaterialId boxMat = matMgr_.getDefaultBoxMaterialId();
            VkPipeline boxPipe = matMgr_.getPipeline(boxMat, ctx_, pipeMgr_);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, boxPipe);
            VkDescriptorSet ds = matMgr_.getDescriptorSet(boxMat, imageIndex);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeMgr_.getBoxPipelineLayout(), 0, 1, &ds, 0, nullptr);
            VkBuffer bufs[] = { sceneMgr_.getCubeVertexBuffer(), sceneMgr_.getInstanceBuffer() };
            VkDeviceSize offs[] = { 0, 0 };
            vkCmdBindVertexBuffers(cb, 0, 2, bufs, offs);
            vkCmdBindIndexBuffer(cb, sceneMgr_.getCubeIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, sceneMgr_.getCubeIndexCount(), sceneMgr_.getInstanceCount(), 0, 0, 0);
        }

        vkCmdEndRenderPass(cb);

        // Create PiP texture only once
        if (!pipTextureCreated_) {
            pipTextureId_ = (ImTextureID)ImGui_ImplVulkan_AddTexture(
                fbMgr_.getPipSampler(),
                fbMgr_.getPipColorImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            pipTextureCreated_ = true;
        }

    } else if (pipTextureCreated_) {
        // No PiP this frame, but texture exists - keep it valid
    }

    std::array<VkClearValue, 2> clears{};
    if (ui_) {
        const ImVec4 cc = ui_->getClearColor();
        clears[0].color = { cc.x * cc.w, cc.y * cc.w, cc.z * cc.w, cc.w };
    } else {
        clears[0].color = { 0.f, 0.f, 0.f, 1.f };
    }
    clears[1].depthStencil = { 1.f, 0 };

    VkRenderPassBeginInfo rpi{};
    rpi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass        = rpMgr_.getMainRenderPass();
    rpi.framebuffer       = fbMgr_.getFramebuffer(imageIndex);
    rpi.renderArea.extent = swapChain_.getExtent();
    rpi.clearValueCount   = static_cast<uint32_t>(clears.size());
    rpi.pClearValues      = clears.data();

    TINYENGINE(cb, "Main Render Pass");
    {
    vkCmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    // 主管线已启用动态 viewport/scissor，每个 renderpass 开始时需显式设置。
    {
        const VkExtent2D ext = swapChain_.getExtent();
        VkViewport vp{ 0.f, 0.f, static_cast<float>(ext.width), static_cast<float>(ext.height), 0.f, 1.f };
        VkRect2D sc{ {0, 0}, ext };
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
    }

    // ── All model entities ────────────────────────────────────────────────────
    {
        TINYENGINE(cb, "Scene Geometry");
        const auto& allEnts = sceneMgr_.getModelEntities();
        if (!allEnts.empty()) {
            const MaterialId fallbackMat = matMgr_.isValid(sceneMgr_.getModelMaterialId())
                ? sceneMgr_.getModelMaterialId()
                : matMgr_.getDefaultMeshMaterialId();

            for (size_t ei = 0; ei < allEnts.size(); ++ei) {
                const auto& ent = allEnts[ei];
                const bool shotOutputView =
                    sequenceCameraActive_ || sequenceCaptureActive_;
                if (shotOutputView && ent.isCamera())
                    continue;
                if (!ent.visible || ent.indexCount == 0 || !ent.vertexBuffer || !ent.indexBuffer)
                    continue;

                VkBuffer vb = ent.vertexBuffer; VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
                vkCmdBindIndexBuffer(cb, ent.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                // Camera 实体的模型几何体需要叠加 modelRotationOffset（如 Y -90°）使显示朝向与预览一致
                glm::mat4 entModel = ent.transform.GetModelMatrix();
                const glm::mat4 offsetRot = glm::mat4_cast(ent.modelRotationOffset);
                entModel = entModel * offsetRot;
                PushConstants push{
                    entModel,
                    glm::mat4(glm::transpose(glm::inverse(glm::mat3(entModel))))
                };

                const MaterialId entityMaterial = matMgr_.isValid(ent.materialId)
                    ? ent.materialId : fallbackMat;
                auto drawRange = [&](MaterialId materialId, SkinBindingId skinBinding,
                                     uint32_t indexOffset, uint32_t indexCount) {
                    if (!matMgr_.isValid(materialId)) materialId = fallbackMat;
                    const bool skinned = matMgr_.isValidSkinBinding(skinBinding);
                    const VkPipeline pipeline = skinned
                        ? pipeMgr_.getSkinnedPipeline()
                        : matMgr_.getPipeline(materialId, ctx_, pipeMgr_);
                    const VkPipelineLayout layout = skinned
                        ? pipeMgr_.getSkinnedPipelineLayout()
                        : pipeMgr_.getMainPipelineLayout();
                    const VkDescriptorSet descriptorSet = skinned
                        ? matMgr_.getSkinDescriptorSet(skinBinding, imageIndex)
                        : matMgr_.getDescriptorSet(materialId, imageIndex);

                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(PushConstants), &push);
                    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            layout, 0, 1, &descriptorSet, 0, nullptr);
                    vkCmdDrawIndexed(cb, indexCount, 1, indexOffset, 0, 0);
                };

                if (ent.subMeshes.empty()) {
                    drawRange(entityMaterial, ent.skinBindingId, 0, ent.indexCount);
                } else {
                    for (size_t subMeshIndex = 0; subMeshIndex < ent.subMeshes.size(); ++subMeshIndex) {
                        const auto& sm = ent.subMeshes[subMeshIndex];
                        MaterialId smMat = entityMaterial;
                        if (sm.materialSlot >= 0 && sm.materialSlot < (int)ent.subMeshMaterials.size()
                            && ent.subMeshMaterials[sm.materialSlot] != 0u) {
                            const MaterialId slotMat = ent.subMeshMaterials[sm.materialSlot];
                            if (matMgr_.isValid(slotMat)) smMat = slotMat;
                        }
                        const SkinBindingId skinBinding = subMeshIndex < ent.subMeshSkinBindings.size()
                            ? static_cast<SkinBindingId>(ent.subMeshSkinBindings[subMeshIndex])
                            : kInvalidSkinBindingId;
                        drawRange(smMat, skinBinding, sm.indexOffset, sm.indexCount);
                    }
                }
            }
        }
    }

    // GPU-instanced boxes — all share the default box material (preserves instancing)
    if (sceneMgr_.getInstanceCount() > 0 && sceneMgr_.getInstanceBuffer() != VK_NULL_HANDLE) {
        TINYENGINE(cb, "Box Geometry");
        const MaterialId boxMat = matMgr_.getDefaultBoxMaterialId();
        VkPipeline boxPipe = matMgr_.getPipeline(boxMat, ctx_, pipeMgr_);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, boxPipe);
        VkDescriptorSet ds = matMgr_.getDescriptorSet(boxMat, imageIndex);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeMgr_.getBoxPipelineLayout(), 0, 1, &ds, 0, nullptr);
        VkBuffer bufs[] = { sceneMgr_.getCubeVertexBuffer(), sceneMgr_.getInstanceBuffer() };
        VkDeviceSize offs[] = { 0, 0 };
        vkCmdBindVertexBuffers(cb, 0, 2, bufs, offs);
        vkCmdBindIndexBuffer(cb, sceneMgr_.getCubeIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, sceneMgr_.getCubeIndexCount(), sceneMgr_.getInstanceCount(), 0, 0, 0);
    }

    // ImGui
    if (frameCapture_.shouldRenderUi() && ImGui::GetCurrentContext()) {
        ImDrawData* dd = ImGui::GetDrawData();
        if (dd && dd->Valid) {
            TINYENGINE(cb, "ImGui Overlay");
            ImGui_ImplVulkan_RenderDrawData(dd, cb);
        }
    }

    vkCmdEndRenderPass(cb);
    } // Main Render Pass scope

    frameCapture_.record(cb, swapChain_.getImages()[imageIndex], frameCount_);

    if (vkEndCommandBuffer(cb) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer!");
}

// ─── Swapchain recreation ─────────────────────────────────────────────────────

void Application::recreateSwapChain()
{
    const bool resumeSequenceCapture = sequenceCaptureActive_;
    if (resumeSequenceCapture) {
        ++sequenceCaptureSwapchainRebuilds_;
        sequenceCaptureStatus_ =
            "Swapchain rebuilding (recording frame is paused)";
    }

    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(window_, &w, &h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(ctx_.getDevice());
    if (resumeSequenceCapture)
        sequenceCaptureTarget_.destroy(ctx_);

    // ImGui Vulkan 后端重建会销毁 texture descriptor，PiP 在下一帧重新注册。
    pipTextureCreated_ = false;
    pipTextureId_ = (ImTextureID)0;

    pickSys_.destroy(ctx_, cmdMgr_);
    cmdMgr_.freeCommandBuffers(ctx_);
    descMgr_.destroy(ctx_);
    fbMgr_.destroy(ctx_);
    pipeMgr_.destroyPipelines(ctx_);
    rpMgr_.destroy(ctx_);
    swapChain_.destroy(ctx_);

    swapChain_.create(ctx_, window_);
    frameCapture_.configure(
        ctx_, swapChain_.supportsTransferSrc(), swapChain_.getImageFormat(),
        swapChain_.getExtent(),
        (std::filesystem::path(modelRegistry_.getResRoot()) / "bin" / "verify" / "captures").string());
    rpMgr_.create(ctx_, swapChain_);

    // Sync camera aspect ratio after swapchain recreate.
    {
        const VkExtent2D ext = swapChain_.getExtent();
        camera_.SetAspectRatio(static_cast<float>(ext.width), static_cast<float>(ext.height));
    }

    if (ui_) ui_->reloadImGuiVulkanAfterSwapchainRecreate(this);

    pipeMgr_.recreate(ctx_, rpMgr_,
                      ui_->vertexShaderPath, ui_->fragShaderPath,
                      ui_->boxVertShaderPath, ui_->boxFragShaderPath,
                      swapChain_.getExtent());

    fbMgr_.create(ctx_, swapChain_, rpMgr_);

    // 仅重建依赖 swapchain image 数量的材质 UBO/descriptor。
    // 材质 ID、纹理、SkinBinding、场景实体及其 GPU 模型缓冲全部原样保留。
    matMgr_.onSwapchainRecreate(ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_,
                                swapChain_.getImageCount());

    descMgr_.create(ctx_, swapChain_, pipeMgr_, bufMgr_);
    pickSys_.create(ctx_, cmdMgr_);
    cmdMgr_.allocateCommandBuffers(ctx_, swapChain_.getImageCount());
    cmdMgr_.resetImagesInFlight(swapChain_.getImageCount());

    if (resumeSequenceCapture && sequenceCaptureActive_) {
        std::string captureTargetError;
        if (!sequenceCaptureTarget_.create(
                ctx_, fbMgr_, rpMgr_,
                sequenceCaptureWidth_, sequenceCaptureHeight_,
                &captureTargetError)) {
            finishSequenceCapture(
                "Failed",
                "Cannot resume the fixed-resolution capture target after "
                "swapchain rebuild: " + captureTargetError);
        } else {
            sequenceCaptureStatus_ =
                sequenceCaptureWarmupFrameCount_
                        < sequenceCaptureWarmupTotalFrames_
                    ? "Pre-rolling"
                    : "Recording";
        }
    }
}

// ─── Picking ──────────────────────────────────────────────────────────────────

MaterialId Application::firstRenderedModelMaterialId() const
{
    // Walk submeshes in order; return the first material ID that is actually
    // bound and valid. For .obj (single submesh, slot -1) or unbound gltf
    // slots, fall back to modelMaterialId_.
    const auto& subs = sceneMgr_.getModelSubMeshes();
    for (const auto& sm : subs) {
        if (sm.materialSlot < 0) continue;
        const MaterialId id = sceneMgr_.getModelSubMeshMaterialId(sm.materialSlot);
        if (matMgr_.isValid(id)) return id;
    }
    return sceneMgr_.getModelMaterialId();
}

void Application::tryPickMainModel(float cx, float cy)
{
    mainModelSelected = false;
    pickedBoxEntityId = 0;

    int winW = 0, winH = 0, fbW = 0, fbH = 0;
    glfwGetWindowSize(window_, &winW, &winH);
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    if (winW <= 0 || winH <= 0 || fbW <= 0 || fbH <= 0) return;

    const float fx = cx * static_cast<float>(fbW) / static_cast<float>(winW);
    const float fy = cy * static_cast<float>(fbH) / static_cast<float>(winH);
    const int maxX = static_cast<int>(swapChain_.getExtent().width)  - 1;
    const int maxY = static_cast<int>(swapChain_.getExtent().height) - 1;
    if (maxX < 0 || maxY < 0) return;
    const int px = std::max(0, std::min(static_cast<int>(fx), maxX));
    const int py = std::max(0, std::min(static_cast<int>(fy), maxY));

    // Update pick UBO slot 0 with current camera before pick pass
    const glm::mat4 view = camera_.GetViewMatrix();
    const glm::mat4 proj = camera_.GetProjectionMatrix();
    // Pick 本身是同步路径；先等待上一帧，避免改写仍被 GPU 读取的 mapped UBO。
    vkDeviceWaitIdle(ctx_.getDevice());
    descMgr_.updateUniformBuffer(0, view, proj);
    // 蒙皮 pick pipeline 使用材质描述符集，需同步更新其 slot 0 相机 UBO。
    matMgr_.updateAllUBOs(0, view, proj);

    const uint32_t id = pickSys_.runPick(ctx_, rpMgr_, fbMgr_, pipeMgr_, matMgr_,
                                          descMgr_.getBoxDescriptorSet(0), 0,
                                          sceneMgr_, swapChain_.getExtent(),
                                          static_cast<uint32_t>(px),
                                          static_cast<uint32_t>(py));

    if (id == SceneManager::kPickIdNone) return;

    // 尝试匹配 ModelEntity（PickId = entityId 的低 32 位）
    if (auto* ent = sceneMgr_.getModelEntity(static_cast<uint64_t>(id))) {
        mainModelSelected = false;
        pickedBoxEntityId = 0;
        selectedCameraEntityId_ = 0;
        if (ui_) ui_->selectedEntityId_ = ent->entityId;
        selectedMaterialId = ent->materialId
            ? ent->materialId : firstRenderedModelMaterialId();
        // 标记是否为 Camera
        if (ent->isCamera()) {
            selectedCameraEntityId_ = ent->entityId;
        }
        // Entity 0 的兼容路径
        const auto& ents = sceneMgr_.getModelEntities();
        if (!ents.empty() && ent->entityId == ents[0].entityId)
            mainModelSelected = true;
        return;
    }

    if (id >= SceneManager::kPickIdBoxBase) {
        const uint32_t idx = id - SceneManager::kPickIdBoxBase;
        const auto& ids = sceneMgr_.getBoxRangeEntityIds();
        if (idx < ids.size()) {
            pickedBoxEntityId  = ids[idx];
            selectedMaterialId = sceneMgr_.getBoxMaterialId(pickedBoxEntityId);
        }
    }
}

void Application::tryBeginCameraFocusOnPick()
{
    glm::vec3 focus(0.f);
    float distance = 4.f;
    if (mainModelSelected) {
        const auto& entities = sceneMgr_.getModelEntities();
        if (entities.empty()) return;
        const auto& entity = entities.front();
        const glm::vec3 localExt = entity.localBoundsMax - entity.localBoundsMin;
        if (glm::length(localExt) < 1e-5f) {
            focus = entity.transform.position;
            distance = 5.f;
        } else {
            const glm::vec3 localCenter = 0.5f * (entity.localBoundsMin + entity.localBoundsMax);
            focus = glm::vec3(entity.transform.GetModelMatrix() * glm::vec4(localCenter, 1.f));
            const glm::vec3 scaledExt = glm::abs(entity.transform.scale) * localExt;
            distance = glm::max(3.f, glm::length(scaledExt) * 1.75f);
        }
    } else if (pickedBoxEntityId != 0) {
        focus    = sceneMgr_.getBoxPosition(pickedBoxEntityId);
        distance = 2.5f;
    } else {
        return;
    }
    camera_.BeginSmoothFocus(focus, distance, 0.65f);
}

// ─── Material API ─────────────────────────────────────────────────────────────

void Application::setModelMaterial(MaterialId id)
{
    if (!matMgr_.isValid(id)) return;
    sceneMgr_.setModelMaterialId(id);
    if (mainModelSelected) selectedMaterialId = id;
}

void Application::setBoxMaterial(RenderEntityId eid, MaterialId id)
{
    if (!matMgr_.isValid(id)) return;
    sceneMgr_.setBoxMaterialId(eid, id);
    if (pickedBoxEntityId == eid) selectedMaterialId = id;
}

uint64_t Application::createCameraActor(const glm::vec3& position, const glm::quat& orientation)
{
    uint64_t entityId = sceneMgr_.createCameraEntity(position, orientation, ctx_, bufMgr_);
    selectedCameraEntityId_ = entityId;
    if (ui_) ui_->selectedEntityId_ = entityId;
    return entityId;
}

bool Application::ensureAnimationAssetForMeshAst(const std::string& meshAstRelPath,
                                                 const std::string& modelPathOrRel,
                                                 bool refreshRegistryAfterWrite,
                                                 uint64_t entityId)
{
    if (meshAstRelPath.empty())
        return false;

    // 解析目标实体 id：传入 0 时回退到第一个实体（兼容旧调用方）
    const auto& ents = sceneMgr_.getModelEntities();
    if (ents.empty()) return false;
    if (entityId == 0) entityId = ents.front().entityId;

    const std::filesystem::path resRoot(modelRegistry_.getResRoot().empty()
        ? std::string("res")
        : modelRegistry_.getResRoot());
    AnimationAssetLoader::setResRoot(resRoot.string());
    const std::filesystem::path meshAstAbs = resRoot / stripResPrefix(meshAstRelPath);

    std::ifstream meshIn(meshAstAbs);
    if (!meshIn.is_open())
        return false;

    json meshJson;
    try {
        meshIn >> meshJson;
    } catch (const std::exception& e) {
        std::cerr << "[AnimationAsset] mesh ast parse failed (" << meshAstAbs.string()
                  << "): " << e.what() << "\n";
        return false;
    }

    // 如果传入的 .ast 文件没有 "animations" 数组（例如一个 .material.ast），
    // 则检查实体是否已有已知的 mesh .ast 路径，若有则以其为准重新读取。
    if (!meshJson.contains("animations") || !meshJson["animations"].is_array()) {
        if (auto* entity = sceneMgr_.getModelEntity(entityId)) {
            if (!entity->astRelPath.empty()) {
                const std::filesystem::path fallbackAst = resRoot / stripResPrefix(entity->astRelPath);
                if (fallbackAst != meshAstAbs) {
                    std::ifstream fallbackIn(fallbackAst);
                    if (fallbackIn.is_open()) {
                        try {
                            json fallbackJson;
                            fallbackIn >> fallbackJson;
                            if (fallbackJson.contains("animations") && fallbackJson["animations"].is_array()) {
                                meshJson = std::move(fallbackJson);
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "[AnimationAsset] fallback mesh ast parse failed ("
                                      << fallbackAst.string() << "): " << e.what() << "\n";
                        }
                    }
                }
            }
        }
    }

    std::string modelRel = stripResPrefix(modelPathOrRel);
    if (modelRel.empty() && meshJson.contains("model")) {
        const auto& model = meshJson["model"];
        if (model.is_string()) {
            modelRel = stripResPrefix(model.get<std::string>());
        } else if (model.is_object()) {
            modelRel = stripResPrefix(model.value("path", std::string{}));
        }
    }

    std::filesystem::path modelPath(modelRel);
    if (!modelPath.is_absolute())
        modelPath = resRoot / modelRel;

    std::vector<std::string> animAstPaths;
    if (meshJson.contains("animations") && meshJson["animations"].is_array()) {
        for (const auto& item : meshJson["animations"]) {
            if (item.is_string())
                animAstPaths.push_back(stripResPrefix(item.get<std::string>()));
        }
    }

    const std::string normalizedMeshAstRel = stripResPrefix(meshAstRelPath);
    const std::string baseName = defaultAnimBaseName(normalizedMeshAstRel);

    auto defaultBinaryRel = [&]() {
        const auto desired = resRoot / "bin" / "anim" / (baseName + ".anim.bin");
        return relativeToRes(desired, resRoot);
    };

    // 加载 mesh.ast 中引用的所有动画资产（支持 FBX/glTF），累积所有 clips
    std::vector<AnimationClip> mergedClips;
    std::shared_ptr<Skeleton> mergedSkeleton;
    std::vector<std::string> loadedPaths;
    std::unordered_set<std::string> mergedClipNames;
    bool anyLoaded = false;

    // 加载单个 .anim.ast 文件，累积到 mergedClips
    auto loadSingleAnim = [&](const std::string& astRel) -> bool {
        AnimationAsset animAsset;
        std::string animErr;
        if (AnimationAssetLoader::load(stripResPrefix(astRel), animAsset, &animErr)) {
            if (!anyLoaded) {
                mergedSkeleton = std::move(animAsset.skeleton);
                anyLoaded = true;
            }
            for (auto& clip : animAsset.clips) {
                const std::string originalName =
                    clip.name.empty() ? std::string("Clip") : clip.name;
                std::string uniqueName = originalName;
                for (size_t suffix = 1; mergedClipNames.count(uniqueName) != 0; ++suffix)
                    uniqueName = originalName + "_" + std::to_string(suffix);
                if (uniqueName != clip.name) {
                    std::cerr << "[AnimationAsset] duplicate clip name '" << clip.name
                              << "' from " << astRel << "; using runtime name '"
                              << uniqueName << "'\n";
                    clip.name = uniqueName;
                }
                mergedClipNames.insert(clip.name);
                mergedClips.push_back(std::move(clip));
            }
            loadedPaths.push_back(stripResPrefix(astRel));
            return true;
        }
        std::cerr << "[AnimationAsset] load failed (" << astRel << "): "
                  << animErr << "\n";
        return false;
    };

    for (const std::string& animAstRel : animAstPaths) {
        const std::filesystem::path animAstAbs = resRoot / animAstRel;
        json animJson;
        bool hasAnimMeta = false;
        {
            std::ifstream animIn(animAstAbs);
            if (animIn.is_open()) {
                try {
                    animIn >> animJson;
                    hasAnimMeta = true;
                } catch (const std::exception& e) {
                    std::cerr << "[AnimationAsset] anim ast parse failed ("
                              << animAstAbs.string() << "): " << e.what() << "\n";
                }
            }
        }

        if (hasAnimMeta && animJson.value("type", std::string{}) != "Anim")
            continue;

        const std::string rootModel = hasAnimMeta
            ? stripResPrefix(animJson.value("rootModel", std::string{}))
            : std::string{};
        if (!rootModel.empty() && rootModel != normalizedMeshAstRel) {
            std::cerr << "[AnimationAsset] rootModel mismatch: " << animAstRel
                      << " belongs to " << rootModel
                      << ", expected " << normalizedMeshAstRel << "\n";
            continue;
        }

        std::string binaryRel = hasAnimMeta
            ? stripResPrefix(animJson.value("binary", std::string{}))
            : std::string{};
        if (binaryRel.empty())
            binaryRel = defaultBinaryRel();

        if (hasAnimMeta && std::filesystem::exists(resRoot / binaryRel)) {
            if (loadSingleAnim(animAstRel))
                continue;
            std::cerr << "[AnimationAsset] existing binary was unreadable; regenerating "
                      << binaryRel << "\n";
        }

        // glTF 动画可自动生成；FBX 动画已由 importAnimationFbx 持久化，跳过生成
        const std::string extNow = toLowerCopy(modelPath.extension().string());
        if (extNow == ".gltf" || extNow == ".glb") {
            std::string saveErr;
            if (AnimationAssetLoader::saveFromGltf(modelPath.string(),
                                                    stripResPrefix(animAstRel),
                                                    stripResPrefix(binaryRel),
                                                    normalizedMeshAstRel,
                                                    &saveErr)) {
                loadSingleAnim(animAstRel);
            } else {
                std::cerr << "[AnimationAsset] generate failed (" << modelPath.string()
                          << "): " << saveErr << "\n";
            }
        }
    }

    // glTF 自动生成：无 animations 引用时尝试创建默认动画资产
    const std::string fileExtGl = toLowerCopy(modelPath.extension().string());
    if (fileExtGl == ".gltf" || fileExtGl == ".glb") {
        const std::string newAnimAstRel =
            (std::filesystem::path(normalizedMeshAstRel).parent_path() / (baseName + ".anim.ast")).generic_string();
        const std::string newBinaryRel = defaultBinaryRel();

        auto saveThenLoad = [&](const std::string& animAstRel, const std::string& binaryRel) {
            std::string saveErr;
            if (!AnimationAssetLoader::saveFromGltf(modelPath.string(),
                                                    stripResPrefix(animAstRel),
                                                    stripResPrefix(binaryRel),
                                                    normalizedMeshAstRel,
                                                    &saveErr)) {
                std::cerr << "[AnimationAsset] generate failed (" << modelPath.string()
                          << "): " << saveErr << "\n";
                return false;
            }
            return loadSingleAnim(animAstRel);
        };

        for (const std::string& animAstRel : animAstPaths) {
            const std::filesystem::path animAstAbs = resRoot / animAstRel;
            json animJson;
            bool hasAnimMeta = false;
            {
                std::ifstream animIn(animAstAbs);
                if (animIn.is_open()) {
                    try {
                        animIn >> animJson;
                        hasAnimMeta = true;
                    } catch (const std::exception& e) {
                        std::cerr << "[AnimationAsset] anim ast parse failed ("
                                  << animAstAbs.string() << "): " << e.what() << "\n";
                    }
                }
            }

            if (hasAnimMeta && animJson.value("type", std::string{}) != "Anim")
                continue;

            const std::string rootModel = hasAnimMeta
                ? stripResPrefix(animJson.value("rootModel", std::string{}))
                : std::string{};
            if (!rootModel.empty() && rootModel != normalizedMeshAstRel) {
                std::cerr << "[AnimationAsset] rootModel mismatch: " << animAstRel
                          << " belongs to " << rootModel
                          << ", expected " << normalizedMeshAstRel << "\n";
                continue;
            }

            std::string binaryRel = hasAnimMeta
                ? stripResPrefix(animJson.value("binary", std::string{}))
                : std::string{};
            if (binaryRel.empty())
                binaryRel = defaultBinaryRel();

            if (hasAnimMeta && !std::filesystem::exists(resRoot / binaryRel)) {
                std::cerr << "[AnimationAsset] binary not found; regenerating "
                          << binaryRel << "\n";
                saveThenLoad(animAstRel, binaryRel);
            }
        }

        if (!anyLoaded) {
            if (!saveThenLoad(newAnimAstRel, newBinaryRel))
                return false;

            if (!meshJson.contains("animations") || !meshJson["animations"].is_array())
                meshJson["animations"] = json::array();

            bool alreadyReferenced = false;
            for (const auto& item : meshJson["animations"]) {
                if (item.is_string() && stripResPrefix(item.get<std::string>()) == newAnimAstRel) {
                    alreadyReferenced = true;
                    break;
                }
            }
            if (!alreadyReferenced)
                meshJson["animations"].push_back(newAnimAstRel);

            std::ofstream meshOut(meshAstAbs);
            if (meshOut.is_open()) {
                meshOut << meshJson.dump(2) << "\n";
                if (refreshRegistryAfterWrite)
                    modelRegistry_.refresh();
            } else {
                std::cerr << "[AnimationAsset] failed to update mesh ast animations: "
                          << meshAstAbs.string() << "\n";
            }
        }
    }

    // 全部加载完成后，统一设置到 entity
    if (anyLoaded && mergedSkeleton && !mergedClips.empty()) {
        sceneMgr_.setEntityAnimationData(entityId, mergedSkeleton, std::move(mergedClips));
        if (auto* entity = sceneMgr_.getModelEntity(entityId)) {
            entity->animationAssetPath = loadedPaths.empty() ? std::string{} : loadedPaths.front();
            entity->animationAssetPaths = std::move(loadedPaths);
        }
    }
    return true;
}

bool Application::loadAndApplyMaterialAsset(const std::string& astRelPath)
{
    // Peek the asset first so we can do an optional mesh swap together with
    // the material change. MaterialAssetLoader::load only touches the JSON file
    // so the second internal load inside MaterialManager is cheap.
    MaterialAssetDesc peeked;
    const bool peekOk = MaterialAssetLoader::load(astRelPath, peeked, nullptr);

    const MaterialId mid = matMgr_.loadMaterialFromAsset(
        astRelPath, ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
    if (mid == kInvalidMaterialId) return false;

    // Optional mesh swap: .ast may carry a "model" field. Wait the GPU idle
    // before destroying the old vertex/index buffers since the in-flight
    // command buffers may still reference them.
    bool modelSwapped = false;
    if (peekOk && !peeked.modelPath.empty()) {
        vkDeviceWaitIdle(ctx_.getDevice());
        sceneMgr_.destroyModelBuffers(ctx_);
        try {
            sceneMgr_.loadModel(peeked.modelPath, mainModelTransform.position, bufMgr_);
            modelSwapped = true;
        } catch (const std::exception& ex) {
            std::cerr << "[MaterialAsset] model swap failed (" << peeked.modelPath
                      << "): " << ex.what() << "\n";
        }
    }

    sceneMgr_.setModelMaterialId(mid);

    // If the swapped-in model was a glTF, it will have dumped per-submesh .ast
    // files into modelAutoAstPaths_. Load and bind each one now so materials
    // are applied immediately without requiring a swapchain recreate.
    if (modelSwapped) {
        const auto& autoPaths = sceneMgr_.getModelAutoAstPaths();
        const auto& materialPaths = !peeked.subMaterialPaths.empty()
            ? peeked.subMaterialPaths
            : autoPaths;
        for (int slot = 0; slot < static_cast<int>(materialPaths.size()); ++slot) {
            if (materialPaths[slot].empty()) continue;
            const MaterialId smMid = matMgr_.loadMaterialFromAsset(
                materialPaths[slot], ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
            if (smMid != kInvalidMaterialId)
                sceneMgr_.setModelSubMeshMaterialId(slot, smMid);
        }

        // 确保 entity 记录了正确的 mesh .ast 路径，供 ensureAnimationAssetForMeshAst 中的回退逻辑使用
        {
            auto& ents = sceneMgr_.getModelEntities();
            if (!ents.empty() && ents[0].astRelPath.empty()) {
                const std::string stem = std::filesystem::path(astRelPath).stem().string();
                if (endsWith(stem, ".mesh")) {
                    ents[0].astRelPath = stripResPrefix(astRelPath);
                }
            }
        }

        // 若有骨骼数据，为实体的槽位材质创建蒙皮版本
        ensureAnimationAssetForMeshAst(astRelPath, peeked.modelPath, true);

        auto& ents = sceneMgr_.getModelEntities();
        if (!ents.empty() && ents[0].hasSkin_)
            convertModelMaterialsToSkinned();

        // 若 .ast 指定了 animController 路径，加载并应用到实体
        if (peekOk && !peeked.animControllerPath.empty() && !ents.empty()) {
            if (ents[0].animatorController.loadFromFile(peeked.animControllerPath))
                ents[0].animatorControllerPath = peeked.animControllerPath;
        }
    }

    if (mainModelSelected) selectedMaterialId = firstRenderedModelMaterialId();
    return true;
}

MaterialId Application::createMeshMaterial(const std::string& name,
                                           const std::string& albedoPath,
                                           const std::string& normalPath,
                                           const MaterialParams& params)
{
    return matMgr_.createMeshMaterial(name, albedoPath, normalPath, params,
                                      ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
}

MaterialId Application::createBoxMaterial(const std::string& name, const MaterialParams& params)
{
    return matMgr_.createBoxMaterial(name, params, ctx_, bufMgr_, pipeMgr_);
}

void Application::destroyMaterial(MaterialId id)
{
    vkDeviceWaitIdle(ctx_.getDevice());
    matMgr_.destroyMaterial(id, ctx_);
    convertModelMaterialsToSkinned();
}

bool Application::setMaterialAlbedo(MaterialId id, const std::string& path)
{
    std::filesystem::path resolved(path);
    if (!path.empty() && !resolved.is_absolute())
        resolved = std::filesystem::path(MaterialAssetLoader::getResRoot()) / stripResPrefix(path);
    std::string error;
    return matMgr_.setAlbedoPath(id, path.empty() ? std::string{} : resolved.lexically_normal().string(),
                                 ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_, &error);
}

bool Application::setMaterialNormal(MaterialId id, const std::string& path)
{
    std::filesystem::path resolved(path);
    if (!path.empty() && !resolved.is_absolute())
        resolved = std::filesystem::path(MaterialAssetLoader::getResRoot()) / stripResPrefix(path);
    std::string error;
    return matMgr_.setNormalPath(id, path.empty() ? std::string{} : resolved.lexically_normal().string(),
                                 ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_, &error);
}

// ─── Drag-Place Implementation ───────────────────────────────────────────────

glm::vec3 Application::screenToWorld(float mx, float my, float distance) const
{
    const VkExtent2D ext = swapChain_.getExtent();
    const float vpW = static_cast<float>(ext.width);
    const float vpH = static_cast<float>(ext.height);
    if (vpW <= 0.f || vpH <= 0.f) return camera_.Position;

    // NDC [-1, 1]（投影矩阵已含 Vulkan Y-flip，此处不再翻转）
    const float ndcX = (2.0f * mx / vpW) - 1.0f;
    const float ndcY = (2.0f * my / vpH) - 1.0f;

    const glm::mat4 invVP = glm::inverse(camera_.GetViewProjectionMatrix());
    const glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    if (glm::abs(nearPoint.w) < 1e-6f) return camera_.Position;
    const glm::vec3 nearWorld = glm::vec3(nearPoint) / nearPoint.w;

    const glm::vec3 rayDir = glm::normalize(nearWorld - camera_.Position);
    return camera_.Position + rayDir * distance;
}

uint64_t Application::placeRegisteredModel(const std::string& requestedAstRelPath,
                                            const ObjectTransform& transform)
{
    const std::string astRelPath = stripResPrefix(requestedAstRelPath);
    const ModelAsset* asset = modelRegistry_.findByPath(astRelPath);
    if (!asset)
        throw std::runtime_error("asset is not registered: " + astRelPath);
    if (asset->modelRelPath.empty() || asset->type == ModelType::Unknown
        || asset->astType == "Material" || asset->astType == "Anim") {
        throw std::runtime_error("asset is not a placeable model: " + astRelPath);
    }

    const std::filesystem::path fullPath =
        std::filesystem::path(modelRegistry_.getResRoot()) / stripResPrefix(asset->modelRelPath);
    if (!std::filesystem::is_regular_file(fullPath))
        throw std::runtime_error("registered model file is missing: " + asset->modelRelPath);

    const uint64_t entityId = sceneMgr_.createModelEntity(
        fullPath.lexically_normal().string(), transform.position, bufMgr_);
    auto* ent = sceneMgr_.getModelEntity(entityId);
    if (!ent)
        throw std::runtime_error("model loader did not create an entity");

    ent->astRelPath = asset->astRelPath;
    ent->displayName = asset->name;
    ent->transform = transform;

    MaterialAssetDesc entryDesc;
    const bool hasEntryDesc = MaterialAssetLoader::load(asset->astRelPath, entryDesc, nullptr);
    const auto& materialPaths = (hasEntryDesc && !entryDesc.subMaterialPaths.empty())
        ? entryDesc.subMaterialPaths
        : ent->autoAstPaths;

    if (!materialPaths.empty()) {
        if (ent->subMeshMaterials.size() < materialPaths.size())
            ent->subMeshMaterials.resize(materialPaths.size(), 0u);
        for (size_t slot = 0; slot < materialPaths.size(); ++slot) {
            const MaterialId materialId = matMgr_.loadMaterialFromAsset(
                materialPaths[slot], ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
            if (materialId != kInvalidMaterialId && slot < ent->subMeshMaterials.size())
                ent->subMeshMaterials[slot] = materialId;
        }
        if (!ent->subMeshMaterials.empty() && ent->subMeshMaterials[0] != 0u)
            ent->materialId = ent->subMeshMaterials[0];
    } else {
        const MaterialId materialId = matMgr_.loadMaterialFromAsset(
            asset->astRelPath, ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
        if (materialId != kInvalidMaterialId) {
            ent->materialId = materialId;
            if (!ent->subMeshes.empty())
                ent->subMeshMaterials.resize(ent->subMeshes.size(), materialId);
        }
    }

    if (hasEntryDesc)
        ensureAnimationAssetForMeshAst(asset->astRelPath, asset->modelRelPath, false, entityId);
    if (ent->hasSkin_)
        convertModelMaterialsToSkinned();

    const auto& entities = sceneMgr_.getModelEntities();
    if (!entities.empty() && entities.front().entityId == entityId)
        mainModelTransform = transform;
    return entityId;
}

void Application::beginDragPlace(uint64_t assetId)
{
    const ModelAsset* asset = modelRegistry_.findById(assetId);
    if (!asset) return;

    // Material 类型的资产不包含模型引用，禁止拖入场景
    if (asset->astType == "Material" || asset->astType == "Anim") {
        dragPlace.active   = true;  // 标记为 active 但无 entity，updateDragPlace/endDragPlace 不做实质操作
        dragPlace.assetId  = assetId;
        return;
    }

    dragPlace.assetId  = assetId;
    dragPlace.entityId = placeRegisteredModel(asset->astRelPath, ObjectTransform{});
    dragPlace.active   = true;
}

void Application::updateDragPlace(float mx, float my)
{
    if (!dragPlace.active || dragPlace.entityId == 0) return;
    const glm::vec3 pos = screenToWorld(mx, my, dragPlace.distance);
    sceneMgr_.setEntityTransform(dragPlace.entityId, ObjectTransform{pos});
}

void Application::endDragPlace()
{
    dragPlace.active   = false;
    dragPlace.entityId = 0;
    dragPlace.assetId  = 0;
}

void Application::deleteModelEntity(uint64_t entityId)
{
    vkDeviceWaitIdle(ctx_.getDevice());
    if (auto* entity = sceneMgr_.getModelEntity(entityId))
        destroyEntitySkinBindings(*entity);
    sceneMgr_.removeModelEntity(entityId, ctx_);
}

// ─── PNG Texture Loader (for UI thumbnails) ───────────────────────────────────

ImTextureID Application::createUITexture(const void* rgbaPixels, int w, int h, VkSampler& outSampler)
{
    if (!rgbaPixels || w <= 0 || h <= 0) return (ImTextureID)0;

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w * h * 4);

    // 1. Staging buffer
    VkBuffer stagingBuf{};
    VkDeviceMemory stagingMem{};
    {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = imageSize;
        bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx_.getDevice(), &bi, nullptr, &stagingBuf);

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(ctx_.getDevice(), stagingBuf, &req);
        VkMemoryAllocateInfo mi{};
        mi.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mi.allocationSize  = req.size;
        mi.memoryTypeIndex = ctx_.findMemoryType(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(ctx_.getDevice(), &mi, nullptr, &stagingMem);
        vkBindBufferMemory(ctx_.getDevice(), stagingBuf, stagingMem, 0);

        void* data = nullptr;
        vkMapMemory(ctx_.getDevice(), stagingMem, 0, imageSize, 0, &data);
        std::memcpy(data, rgbaPixels, imageSize);
        vkUnmapMemory(ctx_.getDevice(), stagingMem);
    }

    // 2. VkImage
    VkImage image{};
    VkDeviceMemory imageMem{};
    {
        VkImageCreateInfo ii{};
        ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType     = VK_IMAGE_TYPE_2D;
        ii.format        = VK_FORMAT_R8G8B8A8_SRGB;
        ii.extent        = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        ii.mipLevels     = 1;
        ii.arrayLayers   = 1;
        ii.samples       = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ii.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(ctx_.getDevice(), &ii, nullptr, &image);

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(ctx_.getDevice(), image, &req);
        VkMemoryAllocateInfo mi{};
        mi.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mi.allocationSize  = req.size;
        mi.memoryTypeIndex = ctx_.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(ctx_.getDevice(), &mi, nullptr, &imageMem);
        vkBindImageMemory(ctx_.getDevice(), image, imageMem, 0);
    }

    // 3. Copy staging → image with layout transitions
    VkCommandBuffer cb = cmdMgr_.beginSingleTimeCommands(ctx_);

    // Transition: undefined → transfer dst
    {
        VkImageMemoryBarrier bar{};
        bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image               = image;
        bar.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        bar.srcAccessMask       = 0;
        bar.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &bar);
    }

    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset       = { 0, 0, 0 };
    region.imageExtent       = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    vkCmdCopyBufferToImage(cb, stagingBuf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition: transfer dst → shader read
    {
        VkImageMemoryBarrier bar{};
        bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image               = image;
        bar.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        bar.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &bar);
    }

    cmdMgr_.endSingleTimeCommands(ctx_, cb);

    // Cleanup staging
    vkDestroyBuffer(ctx_.getDevice(), stagingBuf, nullptr);
    vkFreeMemory(ctx_.getDevice(), stagingMem, nullptr);

    // 4. VkImageView
    VkImageView view{};
    {
        VkImageViewCreateInfo vi{};
        vi.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image      = image;
        vi.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        vi.format     = VK_FORMAT_R8G8B8A8_SRGB;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(ctx_.getDevice(), &vi, nullptr, &view);
    }

    // 5. VkSampler
    {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy    = ctx_.getMaxAnisotropy();
        si.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        si.unnormalizedCoordinates = VK_FALSE;
        si.compareEnable   = VK_FALSE;
        si.mipLodBias      = 0.f;
        si.minLod          = 0.f;
        si.maxLod          = 0.f;
        vkCreateSampler(ctx_.getDevice(), &si, nullptr, &outSampler);
    }

    // 6. Register with ImGui (stores image+view+sampler internally, returns descriptor set)
    ImTextureID texId = (ImTextureID)ImGui_ImplVulkan_AddTexture(outSampler, view,
                                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Store view handle for cleanup (image and memory we'll track separately)
    // We use the view in ImGui's internal tracking. The id must remain valid.
    return texId;
}

ImTextureID Application::loadPNGTexture(const std::string& path, VkSampler& outSampler)
{
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!pixels) {
        std::cerr << "[Texture] Failed to load: " << path << "\n";
        return (ImTextureID)0;
    }
    ImTextureID tid = createUITexture(pixels, w, h, outSampler);
    stbi_image_free(pixels);
    return tid;
}

bool Application::saveScene(const std::string& path)
{
    // resRoot 已指向项目根/res/，save 直接写入唯一基准路径，无需双写。
    return SceneSerializer::save(path, sceneMgr_, matMgr_, camera_);
}

bool Application::loadScene(const std::string& path)
{
    vkDeviceWaitIdle(ctx_.getDevice());
    const std::string resRoot = modelRegistry_.getResRoot();
    const bool ok = SceneSerializer::load(path, *this, sceneMgr_, matMgr_,
                                          bufMgr_, ctx_, cmdMgr_, fbMgr_, pipeMgr_,
                                          camera_, resRoot);
    if (ok) {
        for (const auto& ent : sceneMgr_.getModelEntities()) {
            if (!ent.astRelPath.empty())
                ensureAnimationAssetForMeshAst(ent.astRelPath, "", false, ent.entityId);
        }
        convertModelMaterialsToSkinned();

        // 保留 SceneSerializer 从 .scene.json 中加载的 AnimatorController
        // （setEntityAnimationData 现在不会覆盖已有 controllerPath 的实体）

        // Reset UI state
        ui_->selectedEntityId_ = 0;
        selectedCameraEntityId_ = 0;
        sequenceCameraActive_ = false;
        sequencerPreviewPending_ = sequencerControlEnabled_;
        lastSequencerAnimatorEvalTime_ = -1.0;
        mainModelSelected  = false;
        pickedBoxEntityId  = 0;
        mainModelTransform = ObjectTransform{};
        // Sync entity 0 transform for ImGuizmo compatibility
        const auto& ents = sceneMgr_.getModelEntities();
        if (!ents.empty())
            mainModelTransform = ents[0].transform;
    }
    return ok;
}

// ─── Import Model ─────────────────────────────────────────────────────────────

bool Application::importModel(const std::string& sourcePath,
                                const std::string& subFolder,
                                std::string* outAnimFbxPath)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path srcPath = fs::absolute(sourcePath, ec);
    if (ec || !fs::is_regular_file(srcPath, ec)) {
        std::cerr << "[Import] source not found: " << sourcePath << "\n";
        return false;
    }

    const std::string stem = sanitizeAssetName(srcPath.stem().string());
    // 使用绝对路径，确保 fs::relative 在 resolveGltfImageRel 中能正确计算
    const std::string resRoot = std::filesystem::absolute(modelRegistry_.getResRoot()).string();
    std::string modelRelPath; // relative to res/

    auto toLower = [](const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return r;
    };

    const std::string lowerExt = toLower(srcPath.extension().string());

    const fs::path resRootPath(resRoot);
    const fs::path contentBase = makeUniquePath(resRootPath / makeContentRelDir(subFolder, stem));
    const std::string contentRelDir = relativeToRes(contentBase, resRootPath);

    auto writeMeshAst = [&](const std::string& astRelPath,
                            const std::vector<std::string>& materialPaths,
                            const std::vector<std::string>& animationPaths) {
        const fs::path astAbs = resRootPath / astRelPath;
        fs::create_directories(astAbs.parent_path(), ec);
        std::ofstream out(astAbs);
        if (!out.is_open()) {
            std::cerr << "[Import] cannot create .ast: " << astAbs << "\n";
            return false;
        }
        std::string displayName = stem;
        std::replace(displayName.begin(), displayName.end(), '_', ' ');
        out << "{\n"
            << "  \"name\": \"" << displayName << "\",\n"
            << "  \"type\": \"Mesh\",\n"
            << "  \"model\": \"" << modelRelPath << "\"";
        if (!materialPaths.empty()) {
            out << ",\n  \"materials\": [";
            for (size_t i = 0; i < materialPaths.size(); ++i) {
                if (i > 0) out << ", ";
                out << "\"" << materialPaths[i] << "\"";
            }
            out << "]";
        }
        if (!animationPaths.empty()) {
            out << ",\n  \"animations\": [";
            for (size_t i = 0; i < animationPaths.size(); ++i) {
                if (i > 0) out << ", ";
                out << "\"" << animationPaths[i] << "\"";
            }
            out << "]";
        }
        out << "\n}\n";
        return true;
    };

    if (lowerExt == ".obj") {
        const fs::path dst = makeUniquePath(resRootPath / "bin" / "mesh" / (stem + ".obj"));
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(srcPath, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "[Import] copy failed: " << ec.message() << "\n";
            return false;
        }
        modelRelPath = relativeToRes(dst, resRootPath);
        const std::string astRelPath = contentRelDir + "/" + stem + ".mesh.ast";
        if (!writeMeshAst(astRelPath, {}, {})) return false;
        std::cout << "[Import] imported " << sourcePath << " as " << modelRelPath
                  << " (" << astRelPath << ")\n";
        return true;
    }

    if (lowerExt == ".fbx") {
        FbxImportResult imported;
        std::string fbxError;

        // 先用 load() 尝试解析（可能包含网格/材质/骨架/动画）
        const bool meshOk = FbxImporter::load(srcPath.string(), imported, &fbxError);

        // Without-skin 动画 FBX：load() 可能因无网格返回 false，
        // 但内部已成功解析骨架和动画
        if (!imported.hasSkin && imported.skeleton && !imported.animationClips.empty()) {
            if (outAnimFbxPath) {
                *outAnimFbxPath = srcPath.string();
            }
            return false;
        }

        // 尝试 loadAnimationOnly 兜底（纯动画 FBX，无任何网格数据）
        if (!meshOk) {
            FbxAnimationOnlyResult animResult;
            std::string animError;
            if (FbxImporter::loadAnimationOnly(srcPath.string(), animResult, &animError)) {
                if (!animResult.animationClips.empty() && animResult.skeleton) {
                    if (outAnimFbxPath) {
                        *outAnimFbxPath = srcPath.string();
                    }
                    return false;
                }
            }
            std::cerr << "[Import] FBX parse failed: " << fbxError << "\n";
            return false;
        }

        const fs::path dst = makeUniquePath(resRootPath / "bin" / "mesh" / (stem + ".fbx"));
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(srcPath, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "[Import] copy failed: " << ec.message() << "\n";
            return false;
        }
        modelRelPath = relativeToRes(dst, resRootPath);

        auto persistTexture = [&](const FbxImportedTexture& texture,
                                  const std::string& materialName,
                                  const char* role) -> std::string {
            if (texture.filename.empty() && texture.embeddedContent.empty())
                return {};

            fs::path extension = fs::path(texture.filename).extension();
            if (extension.empty() && texture.embeddedContent.size() >= 4) {
                const auto& bytes = texture.embeddedContent;
                if (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
                    extension = ".png";
                else if (bytes[0] == 0xff && bytes[1] == 0xd8)
                    extension = ".jpg";
            }
            if (extension.empty()) extension = ".bin";

            const std::string textureStem = sanitizeAssetName(
                stem + "_" + materialName + "_" + role);
            const fs::path textureDst = makeUniquePath(
                resRootPath / "bin" / "texture" / stem / (textureStem + extension.string()));
            fs::create_directories(textureDst.parent_path(), ec);

            if (!texture.embeddedContent.empty()) {
                std::ofstream out(textureDst, std::ios::binary);
                if (!out.is_open()) return {};
                out.write(reinterpret_cast<const char*>(texture.embeddedContent.data()),
                          static_cast<std::streamsize>(texture.embeddedContent.size()));
                if (!out.good()) return {};
            } else {
                fs::path textureSource(texture.filename);
                if (!textureSource.is_absolute())
                    textureSource = srcPath.parent_path() / textureSource;
                textureSource = textureSource.lexically_normal();
                if (!fs::is_regular_file(textureSource, ec)) {
                    std::cerr << "[Import] FBX texture not found: " << textureSource << "\n";
                    return {};
                }
                fs::copy_file(textureSource, textureDst, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    std::cerr << "[Import] texture copy failed: " << ec.message() << "\n";
                    return {};
                }
            }
            return relativeToRes(textureDst, resRootPath);
        };

        std::vector<std::string> materialPaths;
        for (size_t i = 0; i < imported.materials.size(); ++i) {
            const FbxImportedMaterial& material = imported.materials[i];
            const std::string materialName = sanitizeAssetName(
                material.name.empty() ? ("material_" + std::to_string(i)) : material.name);
            const std::string albedo = persistTexture(material.albedoTexture, materialName, "albedo");
            const std::string normal = persistTexture(material.normalTexture, materialName, "normal");
            const std::string emissive = persistTexture(material.emissiveTexture, materialName, "emissive");

            json materialJson;
            materialJson["name"] = material.name;
            materialJson["type"] = "Material";
            materialJson["params"] = {
                {"baseColor", {material.baseColor.r, material.baseColor.g,
                               material.baseColor.b, material.baseColor.a}},
                {"roughness", material.roughness},
                {"metallic", material.metallic},
                {"emissiveIntensity", glm::length(glm::vec3(material.emissiveColor)) > 1e-6f ? 1.f : 0.f},
                {"emissiveColor", {material.emissiveColor.r, material.emissiveColor.g,
                                    material.emissiveColor.b, material.emissiveColor.a}}
            };
            materialJson["textures"] = json::object();
            if (!albedo.empty()) materialJson["textures"]["albedo"] = albedo;
            if (!normal.empty()) materialJson["textures"]["normal"] = normal;
            if (!emissive.empty()) materialJson["textures"]["emissive"] = emissive;

            const fs::path materialAbs = makeUniquePath(
                resRootPath / contentRelDir / (materialName + ".material.ast"));
            fs::create_directories(materialAbs.parent_path(), ec);
            std::ofstream materialOut(materialAbs);
            if (!materialOut.is_open()) {
                std::cerr << "[Import] cannot create material .ast: " << materialAbs << "\n";
                return false;
            }
            materialOut << materialJson.dump(2) << '\n';
            materialPaths.push_back(relativeToRes(materialAbs, resRootPath));
        }

        const std::string meshAstRel = contentRelDir + "/" + stem + ".mesh.ast";
        std::vector<std::string> animationPaths;
        if (imported.skeleton && !imported.animationClips.empty()) {
            const std::string animAstRel = contentRelDir + "/" + stem + ".anim.ast";
            const fs::path animBinAbs = makeUniquePath(
                resRootPath / "bin" / "anim" / (stem + ".anim.bin"));
            const std::string animBinRel = relativeToRes(animBinAbs, resRootPath);
            std::string animError;
            AnimationAssetLoader::setResRoot(resRootPath.string());
            if (AnimationAssetLoader::save(animAstRel, animBinRel, meshAstRel,
                                           *imported.skeleton, imported.animationClips,
                                           &animError)) {
                animationPaths.push_back(animAstRel);
            } else {
                std::cerr << "[Import] FBX animation asset generation failed: "
                          << animError << "\n";
            }
        }

        if (!writeMeshAst(meshAstRel, materialPaths, animationPaths)) return false;
        std::cout << "[Import] imported FBX " << sourcePath << " as " << modelRelPath
                  << " (" << meshAstRel << ")\n";
        return true;
    }

    if (lowerExt == ".gltf" || lowerExt == ".glb") {
        const fs::path srcDir = srcPath.parent_path();
        bool hasSidecars = false;
        if (lowerExt == ".gltf") {
            for (const auto& entry : fs::directory_iterator(srcDir, ec)) {
                if (ec) break;
                const std::string name = entry.path().filename().string();
                if (toLower(name) == toLower(srcPath.filename().string()))
                    continue;
                if (entry.is_regular_file(ec) && toLower(name).ends_with(".bin")) {
                    hasSidecars = true;
                    break;
                }
                if (entry.is_directory(ec) && toLower(name) == "textures") {
                    hasSidecars = true;
                    break;
                }
            }
        }

        fs::path dstPath;
        if (hasSidecars) {
            const fs::path dstDir = makeUniquePath(resRootPath / "bin" / "mesh" / stem);
            dstPath = dstDir / srcPath.filename();
            fs::create_directories(dstDir, ec);
            for (const auto& entry : fs::recursive_directory_iterator(srcDir, ec)) {
                if (ec) break;
                const fs::path rel = fs::relative(entry.path(), srcDir, ec);
                const fs::path dp = dstDir / rel;
                if (entry.is_directory(ec)) {
                    fs::create_directories(dp, ec);
                } else {
                    fs::create_directories(dp.parent_path(), ec);
                    fs::copy_file(entry.path(), dp, fs::copy_options::overwrite_existing, ec);
                }
            }
            if (ec) {
                std::cerr << "[Import] directory copy failed: " << ec.message() << "\n";
                return false;
            }
        } else {
            dstPath = makeUniquePath(resRootPath / "bin" / "mesh" / (stem + lowerExt));
            fs::create_directories(dstPath.parent_path(), ec);
            fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::cerr << "[Import] copy failed: " << ec.message() << "\n";
                return false;
            }
        }
        modelRelPath = relativeToRes(dstPath, resRootPath);

        std::vector<std::string> materialPaths;
        {
            cgltf_options opts{};
            cgltf_data* data = nullptr;
            if (cgltf_parse_file(&opts, dstPath.string().c_str(), &data) == cgltf_result_success && data) {
                if (cgltf_load_buffers(&opts, data, dstPath.string().c_str()) == cgltf_result_success) {
                    const fs::path gltfDir = dstPath.parent_path();
                    int globalPrimIndex = 0;
                    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
                        const cgltf_mesh& mesh = data->meshes[mi];
                        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi, ++globalPrimIndex) {
                            const cgltf_primitive& prim = mesh.primitives[pi];
                            if (!prim.material) continue;
                            const std::string astRel = SceneManager::dumpGltfMaterialAst(
                                prim.material, stem, globalPrimIndex,
                                gltfDir.string(), resRoot,
                                contentRelDir.substr(std::string("content/").size()));
                            if (!astRel.empty())
                                materialPaths.push_back(astRel);
                        }
                    }
                }
                cgltf_free(data);
            }
        }

        const std::string meshAstRel = contentRelDir + "/" + stem + ".mesh.ast";
        std::vector<std::string> animationPaths;
        {
            const std::string animAstRel = contentRelDir + "/" + stem + ".anim.ast";
            const fs::path animBinPath = makeUniquePath(resRootPath / "bin" / "anim" / (stem + ".anim.bin"));
            const std::string animBinRel = relativeToRes(animBinPath, resRootPath);
            std::string animErr;
            AnimationAssetLoader::setResRoot(resRootPath.string());
            if (AnimationAssetLoader::saveFromGltf(dstPath.string(), animAstRel, animBinRel,
                                                   meshAstRel, &animErr)) {
                animationPaths.push_back(animAstRel);
            } else if (!animErr.empty()) {
                std::cout << "[Import] no animation asset generated: " << animErr << "\n";
            }
        }

        if (!writeMeshAst(meshAstRel, materialPaths, animationPaths)) return false;
        std::cout << "[Import] imported " << sourcePath << " as " << modelRelPath
                  << " (" << meshAstRel << ")\n";
        return true;
    }

    std::cerr << "[Import] unsupported format: " << srcPath.extension() << "\n";
    return false;

    if (lowerExt == ".obj") {
        const fs::path dst = fs::path(resRoot) / "models" / (stem + ".obj");
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(srcPath, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "[Import] copy failed: " << ec.message() << "\n";
            return false;
        }
        modelRelPath = "models/" + stem + ".obj";

    } else if (lowerExt == ".gltf" || lowerExt == ".glb") {
        const fs::path srcDir = srcPath.parent_path();

        // 判断是否需要拷贝整个目录（存在配套 .bin 或 textures/ 子目录）
        bool hasSidecars = false;
        if (lowerExt == ".gltf") {
            for (const auto& entry : fs::directory_iterator(srcDir, ec)) {
                if (ec) break;
                const std::string name = entry.path().filename().string();
                if (toLower(name) == toLower(srcPath.filename().string()))
                    continue;
                if (entry.is_regular_file(ec) && toLower(name).ends_with(".bin")) {
                    hasSidecars = true; break;
                }
                if (entry.is_directory(ec) && toLower(name) == "textures") {
                    hasSidecars = true; break;
                }
            }
        }

        fs::path dstPath; // 拷贝目标路径（glTF 文件在 res/ 下的绝对路径）

        if (hasSidecars) {
            const fs::path dstDir = fs::path(resRoot) / "models" / stem;
            dstPath = dstDir / srcPath.filename();
            fs::create_directories(dstDir, ec);
            for (const auto& entry : fs::recursive_directory_iterator(srcDir, ec)) {
                if (ec) break;
                const fs::path rel = fs::relative(entry.path(), srcDir, ec);
                const fs::path dp = dstDir / rel;
                if (entry.is_directory(ec)) {
                    fs::create_directories(dp, ec);
                } else {
                    fs::create_directories(dp.parent_path(), ec);
                    fs::copy_file(entry.path(), dp,
                                  fs::copy_options::overwrite_existing, ec);
                }
            }
            if (ec) {
                std::cerr << "[Import] directory copy failed: " << ec.message() << "\n";
                return false;
            }
            modelRelPath = "models/" + stem + "/" + srcPath.filename().string();
        } else {
            dstPath = fs::path(resRoot) / "models" / (stem + lowerExt);
            fs::create_directories(dstPath.parent_path(), ec);
            fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::cerr << "[Import] copy failed: " << ec.message() << "\n";
                return false;
            }
            modelRelPath = "models/" + stem + lowerExt;
        }

        // ── 预解析 glTF 材质，生成详细 .ast ────────────────────────────
        std::vector<std::string> subMaterials;
        {
            cgltf_options opts{};
            cgltf_data*   data = nullptr;
            if (cgltf_parse_file(&opts, dstPath.string().c_str(), &data) == cgltf_result_success && data) {
                if (cgltf_load_buffers(&opts, data, dstPath.string().c_str()) == cgltf_result_success) {
                    const fs::path gltfDir = dstPath.parent_path();
                    int globalPrimIndex = 0;
                    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
                        const cgltf_mesh& mesh = data->meshes[mi];
                        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi, ++globalPrimIndex) {
                            const cgltf_primitive& prim = mesh.primitives[pi];
                            if (prim.material) {
                                const std::string astRel = SceneManager::dumpGltfMaterialAst(
                                    prim.material, stem, globalPrimIndex,
                                    gltfDir.string(), resRoot, subFolder);
                                if (!astRel.empty())
                                    subMaterials.push_back(astRel);
                            }
                        }
                    }
                    std::cout << "[Import] pre-generated " << subMaterials.size()
                              << " material .ast file(s) for " << stem << "\n";
                }
                cgltf_free(data);
            }
        }

        // ── 创建入口 .ast（含 subMaterials）────────────────────────────
        const std::string astRelDir  = subFolder.empty() ? "materials" : ("materials/" + subFolder);
        const std::string astRelPath = astRelDir + "/" + stem + ".ast";
        const fs::path astAbs = fs::path(resRoot) / astRelPath;
        fs::create_directories(astAbs.parent_path(), ec);

        std::ofstream out(astAbs);
        if (!out.is_open()) {
            std::cerr << "[Import] cannot create .ast: " << astAbs << "\n";
            return false;
        }
        std::string displayName = stem;
        std::replace(displayName.begin(), displayName.end(), '_', ' ');
        out << "{\n"
            << "  \"name\": \"" << displayName << "\",\n"
            << "  \"type\": \"Mesh\",\n"
            << "  \"model\": \"" << modelRelPath << "\"";
        if (!subMaterials.empty()) {
            out << ",\n  \"subMaterials\": [";
            for (size_t i = 0; i < subMaterials.size(); ++i) {
                if (i > 0) out << ", ";
                out << "\"" << subMaterials[i] << "\"";
            }
            out << "]";
        }
        out << "\n}\n";

        std::cout << "[Import] imported " << sourcePath << " as " << modelRelPath
                  << " (" << astRelPath << ")\n";
        return true;
    } else {
        std::cerr << "[Import] unsupported format: " << srcPath.extension() << "\n";
        return false;
    }

    // 创建入口 .ast 文件（.obj 格式）
    const std::string astRelDir  = subFolder.empty() ? "materials" : ("materials/" + subFolder);
    const std::string astRelPath = astRelDir + "/" + stem + ".ast";
    const fs::path astAbs = fs::path(resRoot) / astRelPath;
    fs::create_directories(astAbs.parent_path(), ec);

    std::ofstream out(astAbs);
    if (!out.is_open()) {
        std::cerr << "[Import] cannot create .ast: " << astAbs << "\n";
        return false;
    }
    std::string displayName = stem;
    std::replace(displayName.begin(), displayName.end(), '_', ' ');
    out << "{\n"
        << "  \"name\": \"" << displayName << "\",\n"
        << "  \"type\": \"Mesh\",\n"
        << "  \"model\": \"" << modelRelPath << "\"\n"
        << "}\n";

    std::cout << "[Import] imported " << sourcePath << " as " << modelRelPath
              << " (" << astRelPath << ")\n";
    return true;
}

// ─── Import Animation FBX ─────────────────────────────────────────────────────

bool Application::importAnimationFbx(const std::string& fbxPath,
                                     const std::string& targetMeshAstRelPath,
                                     AnimationImportReport* report)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (report)
        *report = {};

    if (fbxPath.empty() || targetMeshAstRelPath.empty()) {
        std::cerr << "[ImportAnim] empty path\n";
        return false;
    }

    // 1. Parse without-skin FBX for skeleton + clips
    FbxAnimationOnlyResult animResult;
    std::string animErr;
    if (!FbxImporter::loadAnimationOnly(fbxPath, animResult, &animErr)) {
        std::cerr << "[ImportAnim] failed to parse animation FBX: " << animErr << "\n";
        return false;
    }
    if (animResult.animationClips.empty()) {
        std::cerr << "[ImportAnim] animation FBX has no clips\n";
        return false;
    }

    const std::string resRoot = std::filesystem::absolute(modelRegistry_.getResRoot()).string();
    const fs::path resRootPath(resRoot);
    AnimationAssetLoader::setResRoot(resRoot);

    // 2. Load target mesh's skeleton for retargeting
    const fs::path meshAstAbs = resRootPath / stripResPrefix(targetMeshAstRelPath);
    std::ifstream meshIn(meshAstAbs);
    if (!meshIn.is_open()) {
        std::cerr << "[ImportAnim] target mesh .ast not found: " << meshAstAbs << "\n";
        return false;
    }

    json meshJson;
    try {
        meshIn >> meshJson;
    } catch (const std::exception& e) {
        std::cerr << "[ImportAnim] mesh ast parse failed: " << e.what() << "\n";
        return false;
    }

    std::shared_ptr<Skeleton> dstSkeleton;
    std::vector<std::string> existingAnimAsts;
    {
        if (meshJson.contains("animations") && meshJson["animations"].is_array()) {
            for (const auto& item : meshJson["animations"]) {
                if (item.is_string())
                    existingAnimAsts.push_back(stripResPrefix(item.get<std::string>()));
            }
        }

        // Try loading skeleton from existing .anim.ast first
        for (const auto& animAstRel : existingAnimAsts) {
            AnimationAsset loaded;
            if (AnimationAssetLoader::load(animAstRel, loaded, nullptr) && loaded.skeleton) {
                dstSkeleton = std::move(loaded.skeleton);
                std::cout << "[ImportAnim] loaded target skeleton from " << animAstRel
                          << " (" << dstSkeleton->bones.size() << " bones)\n";
                break;
            }
        }

        // Fallback: load from the original mesh FBX
        if (!dstSkeleton && meshJson.contains("model")) {
            std::string modelRel;
            const auto& model = meshJson["model"];
            if (model.is_string())
                modelRel = stripResPrefix(model.get<std::string>());
            else if (model.is_object())
                modelRel = stripResPrefix(model.value("path", std::string{}));

            if (!modelRel.empty()) {
                const fs::path modelPath = modelRel.find('/') == std::string::npos
                    ? resRootPath / modelRel
                    : fs::path(modelRel);
                const std::string ext = toLowerCopy(modelPath.extension().string());
                if (ext == ".fbx") {
                    FbxImportResult fullResult;
                    std::string fbxLoadErr;
                    if (FbxImporter::load(modelPath.string(), fullResult, &fbxLoadErr)) {
                        dstSkeleton = std::move(fullResult.skeleton);
                        if (dstSkeleton)
                            std::cout << "[ImportAnim] loaded target skeleton from original FBX ("
                                      << dstSkeleton->bones.size() << " bones)\n";
                    } else {
                        std::cerr << "[ImportAnim] failed to load original FBX for skeleton: "
                                  << fbxLoadErr << "\n";
                    }
                } else {
                    std::cerr << "[ImportAnim] cannot extract skeleton from non-FBX model: "
                              << modelRel << "\n";
                }
            }
        }
    }

    if (!dstSkeleton) {
        std::cerr << "[ImportAnim] failed to obtain target skeleton\n";
        return false;
    }

    // 3. Retarget: remap bone indices from source skeleton to target skeleton
    const auto& srcSkeleton = *animResult.skeleton;
    auto retargetResult = AnimationRetargeter::retargetClips(
        animResult.animationClips, srcSkeleton, *dstSkeleton);

    if (retargetResult.retargetedClips.empty()) {
        std::cerr << "[ImportAnim] retargeting failed: no bones matched between skeletons\n";
        return false;
    }

    std::cout << "[ImportAnim] retargeting complete: " << retargetResult.matchedCount
              << " bones matched, " << retargetResult.unmatchedBones.size() << " unmatched\n";

    // 4. Generate paths and save retargeted animation
    const std::string baseName = stripResPrefix(targetMeshAstRelPath);
    const std::string animName = fs::path(baseName).stem().string();

    // Determine subfolder within content/ (e.g. "characters" from "content/characters/char1.mesh.ast")
    std::string animSubFolder;
    {
        const fs::path bp(baseName);
        if (bp.has_parent_path()) {
            const fs::path parent = bp.parent_path();
            const std::string parentStr = parent.generic_string();
            const std::string contentPrefix = "content/";
            if (parentStr.rfind(contentPrefix, 0) == 0)
                animSubFolder = parentStr.substr(contentPrefix.size());
            else if (parentStr != "content" && parentStr != ".")
                animSubFolder = parentStr;
        }
    }
    const std::string contentPrefix = animSubFolder.empty()
        ? "content"
        : (std::string("content/") + animSubFolder);

    // Derive a unique name from the source FBX
    const std::string clipStem = sanitizeAssetName(fs::path(fbxPath).stem().string());

    // Clip names are controller/Sequencer identifiers, so keep them unique across
    // every animation asset already linked by the target mesh. Persist the chosen
    // names in the asset rather than relying only on a runtime alias.
    std::unordered_set<std::string> existingClipNames;
    for (const auto& astRel : existingAnimAsts) {
        AnimationAsset existingAsset;
        if (!AnimationAssetLoader::load(astRel, existingAsset, nullptr))
            continue;
        for (const auto& clip : existingAsset.clips)
            existingClipNames.insert(clip.name);
    }
    for (auto& clip : retargetResult.retargetedClips) {
        std::string base = clip.name.empty() ? clipStem : clip.name;
        if (existingClipNames.count(base) != 0)
            base = clipStem.empty() ? std::string("ImportedClip") : clipStem;
        std::string uniqueName = base;
        for (size_t suffix = 1; existingClipNames.count(uniqueName) != 0; ++suffix)
            uniqueName = base + "_" + std::to_string(suffix);
        clip.name = std::move(uniqueName);
        existingClipNames.insert(clip.name);
    }

    // Check for clip name conflicts with existing animation assets
    std::string animStem = animName + "_" + clipStem;
    {
        bool conflict = false;
        do {
            conflict = false;
            for (const auto& astRel : existingAnimAsts) {
                if (astRel.find(animStem + ".anim.ast") != std::string::npos) {
                    animStem += "_alt";
                    conflict = true;
                    break;
                }
            }
        } while (conflict);
    }

    const std::string animAstRel = contentPrefix + "/" + animStem + ".anim.ast";
    const std::string animBinRel = "bin/anim/" + animStem + ".anim.bin";
    const std::string normalizedMeshAstRel = stripResPrefix(targetMeshAstRelPath);

    std::string saveErr;
    if (!AnimationAssetLoader::save(animAstRel, animBinRel, normalizedMeshAstRel,
                                    *dstSkeleton, retargetResult.retargetedClips, &saveErr)) {
        std::cerr << "[ImportAnim] failed to save animation asset: " << saveErr << "\n";
        return false;
    }

    // 5. Update target mesh .ast to reference the new animation
    if (!meshJson.contains("animations") || !meshJson["animations"].is_array())
        meshJson["animations"] = json::array();

    meshJson["animations"].push_back(animAstRel);

    std::ofstream meshOut(meshAstAbs);
    if (meshOut.is_open()) {
        meshOut << meshJson.dump(2) << "\n";
        meshOut.close();
        if (!meshOut) {
            std::cerr << "[ImportAnim] failed to finalize mesh .ast update: "
                      << meshAstAbs << "\n";
            return false;
        }
    } else {
        std::cerr << "[ImportAnim] failed to update mesh .ast: " << meshAstAbs << "\n";
        return false;
    }

    modelRegistry_.refresh();

    // 6. Reload the target mesh's complete animation collection for every scene
    // instance. Normalization handles res/ prefixes, slash direction and case.
    auto normalizedAssetKey = [&](std::string path) {
        fs::path parsed(path);
        if (parsed.is_absolute()) {
            std::error_code relativeError;
            const fs::path relative = fs::relative(parsed, resRootPath, relativeError);
            if (!relativeError)
                path = relative.generic_string();
        }
        path = normalizeSequenceBindingPath(std::move(path));
        if (path.rfind("res/", 0) == 0)
            path.erase(0, 4);
        return path;
    };
    const std::string targetAssetKey = normalizedAssetKey(normalizedMeshAstRel);
    std::vector<uint64_t> matchingEntityIds;
    for (const auto& ent : sceneMgr_.getModelEntities()) {
        if (normalizedAssetKey(ent.astRelPath) == targetAssetKey)
            matchingEntityIds.push_back(ent.entityId);
    }

    size_t refreshedEntityCount = 0;
    size_t totalClipCount = 0;
    for (const uint64_t entityId : matchingEntityIds) {
        auto* entity = sceneMgr_.getModelEntity(entityId);
        const uint64_t previousRevision =
            entity ? entity->animationDataRevision : 0;
        ensureAnimationAssetForMeshAst(
            normalizedMeshAstRel, "", false, entityId);
        entity = sceneMgr_.getModelEntity(entityId);
        if (!entity || entity->animationDataRevision == previousRevision)
            continue;

        invalidateAnimationData(*entity);
        ++refreshedEntityCount;
        totalClipCount = entity->animationClips.size();
        std::cout << "[ImportAnim] refreshed entity " << entity->entityId
                  << " (" << entity->displayName << ") with "
                  << entity->animationClips.size() << " total clip(s)\n";
    }

    if (report) {
        report->assetPath = animAstRel;
        report->refreshedEntityCount = refreshedEntityCount;
        report->totalClipCount = totalClipCount;
        report->importedClipNames.reserve(retargetResult.retargetedClips.size());
        for (const auto& clip : retargetResult.retargetedClips)
            report->importedClipNames.push_back(clip.name);
    }

    std::cout << "[ImportAnim] imported " << fbxPath << " → " << animAstRel
              << " (linked to " << normalizedMeshAstRel << ")\n";
    return true;
}

// ─── Input ────────────────────────────────────────────────────────────────────

void Application::processInput(GLFWwindow* w)
{
    static bool fKeyWasDown = false;
    const bool fKeyDown = glfwGetKey(w, GLFW_KEY_F) == GLFW_PRESS;
    const bool imguiKb  = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
    if (!imguiKb && fKeyDown && !fKeyWasDown)
        tryBeginCameraFocusOnPick();
    fKeyWasDown = fKeyDown;

    // 1/2/3 切换 ImGuizmo 操作模式，4 切换世界/本地坐标系
    if (!imguiKb) {
        static bool key1WasDown = false, key2WasDown = false, key3WasDown = false, key4WasDown = false;
        const bool k1 = glfwGetKey(w, GLFW_KEY_1) == GLFW_PRESS;
        const bool k2 = glfwGetKey(w, GLFW_KEY_2) == GLFW_PRESS;
        const bool k3 = glfwGetKey(w, GLFW_KEY_3) == GLFW_PRESS;
        const bool k4 = glfwGetKey(w, GLFW_KEY_4) == GLFW_PRESS;
        if (k1 && !key1WasDown) ui_->gizmoOperation_ = 7;   // ImGuizmo::TRANSLATE
        if (k2 && !key2WasDown) ui_->gizmoOperation_ = 120; // ImGuizmo::ROTATE
        if (k3 && !key3WasDown) ui_->gizmoOperation_ = 896; // ImGuizmo::SCALE
        if (k4 && !key4WasDown) ui_->gizmoLocal_ = !ui_->gizmoLocal_; // 切换 WORLD/LOCAL
        key1WasDown = k1; key2WasDown = k2; key3WasDown = k3; key4WasDown = k4;
    }

    const bool manualCameraInput =
        !imguiKb && !sequenceCameraActive_ && !sequenceCaptureActive_;
    if (manualCameraInput) {
        camera_.speedZ = static_cast<float>(glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS)
                       - static_cast<float>(glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS);
        camera_.speedX = static_cast<float>(glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS)
                       - static_cast<float>(glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS);
        // UE-style vertical flight: E rises and Q descends along world up.
        camera_.speedY = static_cast<float>(glfwGetKey(w, GLFW_KEY_E) == GLFW_PRESS)
                       - static_cast<float>(glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS);
    } else {
        camera_.speedX = 0.f;
        camera_.speedY = 0.f;
        camera_.speedZ = 0.f;
    }
}

// ─── UIManager delegation ─────────────────────────────────────────────────────

glm::mat4 Application::getSceneViewMatrix()
{
    return camera_.GetViewMatrix();
}

glm::mat4 Application::getSceneProjMatrixForImGuizmo()
{
    return camera_.GetProjectionMatrixNoFlip();
}

Application::RenderEntityId Application::addBox(const glm::vec3& pos)
{
    RenderEntityId eid = sceneMgr_.addBox(pos, ctx_, bufMgr_);
    sceneMgr_.setBoxMaterialId(eid, matMgr_.getDefaultBoxMaterialId());
    return eid;
}

bool Application::removeBox(RenderEntityId id)
{
    return sceneMgr_.removeBox(id, ctx_, bufMgr_);
}

glm::vec3 Application::getBoxPosition(RenderEntityId id) const
{
    return sceneMgr_.getBoxPosition(id);
}

void Application::setBoxPosition(RenderEntityId id, const glm::vec3& pos)
{
    sceneMgr_.setBoxPosition(id, pos, ctx_, bufMgr_);
}

// ─── Cleanup ──────────────────────────────────────────────────────────────────

void Application::cleanUp()
{
    vkDeviceWaitIdle(ctx_.getDevice());

    // 停止 MCP IPC 服务
    if (ipc_) {
        ipc_->stop();
        ipc_.reset();
    }

    thumbnailRenderer_.destroy(ctx_, matMgr_, sceneMgr_);
    sequenceCaptureTarget_.destroy(ctx_);
    frameCapture_.destroy(ctx_);
    pickSys_.destroy(ctx_, cmdMgr_);
    sceneMgr_.destroy(ctx_);
    matMgr_.destroy(ctx_);
    descMgr_.destroy(ctx_);
    pipeMgr_.destroy(ctx_);
    fbMgr_.destroy(ctx_);
    cmdMgr_.destroy(ctx_);
    rpMgr_.destroy(ctx_);
    swapChain_.destroy(ctx_);
    ctx_.destroy();

    glfwDestroyWindow(window_);
    glfwTerminate();
    delete ui_;
    ui_ = nullptr;
}
