#include "Application.hpp"
#include "Animation/AnimationAssetLoader.hpp"
#include "Animation/AnimationRetargeter.hpp"
#include "FbxImporter.hpp"
#include "MaterialAssetLoader.hpp"
#include "SceneSerializer.hpp"
#include "TinyEngineDebug.hpp"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include "cgltf.h"
#include <stdexcept>
#include <vector>

// STB_IMAGE_IMPLEMENTATION defined globally; only TextureManager.cpp implements it
#undef STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
    if (!app || !app->rightMouseDown_ || app->sequenceCameraActive_) return;
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

void Application::run()
{
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

        if (!camera_.IsSmoothFocusActive())
            camera_.UpdataCameraPosition(dt > 0.f ? dt : 1.f / 240.f);
        camera_.UpdateSmoothFocus(dt > 0.f ? dt : 1.f / 240.f);

        // ── Camera Path 录制 ──────────────────────────────────────────────────
        if (recordingCameraPath_) {
            const double realDt = dt > 0.0 ? static_cast<double>(dt) : (1.0 / 240.0);
            recordTimer_ += realDt;
            recordingTime_ += realDt;

            if (recordTimer_ >= recordInterval_) {
                CameraKeyframe kf;
                kf.time = recordingTime_;
                kf.position = camera_.Position;
                kf.orientation = camera_.GetOrientation();
                kf.fovDeg = camera_.FovDeg;
                recordingPath_.keyframes.push_back(std::move(kf));
                recordTimer_ = 0.0;
            }
        }

        ui_->prepareFrame();
        drawFrame(dt);

        if (ui_->refreshVulkanShader()) {
            recreateSwapChain();
        }
        ui_->setRefreshVulkanStatus(false);
    }

    ui_->cleanUp();
    cleanUp();
}

// ─── Skinned material conversion ──────────────────────────────────────────────

void Application::beginCameraPathRecording(const std::string& pathName)
{
    recordingCameraPath_ = true;
    recordingPath_ = CameraPath{};
    recordingPath_.name = pathName;
    recordingPath_.interpolation = 0; // CatmullRom
    recordingTime_ = 0.0;
    recordTimer_ = 0.0;
}

void Application::endCameraPathRecording()
{
    recordingCameraPath_ = false;
    // 确保最后一个关键帧也记录了
    CameraKeyframe kf;
    kf.time = recordingTime_;
    kf.position = camera_.Position;
    kf.orientation = camera_.GetOrientation();
    kf.fovDeg = camera_.FovDeg;

    if (recordingPath_.keyframes.empty() ||
        recordingPath_.keyframes.back().time < kf.time - 0.001) {
        recordingPath_.keyframes.push_back(std::move(kf));
    }

    std::cout << "[CameraPath] Recording finished: " << recordingPath_.name
              << " (" << recordingPath_.keyframes.size() << " keyframes, "
              << recordingTime_ << "s)\n";
}

void Application::convertModelMaterialsToSkinned()
{
    for (auto& ent : sceneMgr_.getModelEntities()) {
        if (!ent.hasSkin_) continue;

        auto convert = [&](MaterialId srcId) -> MaterialId {
            if (srcId == kInvalidMaterialId || !matMgr_.isValid(srcId))
                return kInvalidMaterialId;
            if (matMgr_.hasSkinning(srcId)) return srcId;
            return matMgr_.createSkinnedMaterialFrom(
                srcId, ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
        };

        // 逐槽位克隆 — 保留每个 sub-mesh 的原始纹理和参数
        for (size_t i = 0; i < ent.subMeshMaterials.size(); ++i) {
            if (ent.subMeshMaterials[i] == 0u) continue;
            const MaterialId newId = convert(ent.subMeshMaterials[i]);
            if (newId != kInvalidMaterialId)
                ent.subMeshMaterials[i] = newId;
        }

        // 实体回退材质也转换为蒙皮版本
        const MaterialId newId = convert(ent.materialId);
        if (newId != kInvalidMaterialId)
            ent.materialId = newId;

        std::cout << "[tinyEngine] converted materials to skinned for entity "
                  << ent.entityId << " (" << ent.subMeshMaterials.size()
                  << " slots)\n";
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

    const glm::mat4 view = camera_.GetViewMatrix();
    const glm::mat4 proj = camera_.GetProjectionMatrix();
    matMgr_.updateAllUBOs(imageIndex, view, proj);

    // ── Sequencer 驱动 ───────────────────────────────────────────────────────
    sequenceCameraActive_ = false;
    bool seqHasAnimTrack = false;
    {
        const auto* seq = seqPlayer_.currentSequence();
        bool hasCameraTrack = false;
        if (seq) {
            for (const auto& t : seq->tracks) {
                if (t.type == TrackType::CameraPath) { hasCameraTrack = true; break; }
            }
            for (const auto& t : seq->tracks) {
                if (t.type == TrackType::AnimationClip && !t.animClips.empty()) {
                    seqHasAnimTrack = true; break;
                }
            }
        }

        SequencePlayer::FrameCallbacks seqCallbacks;
        seqCallbacks.onAnimClipEval = [&](double localT, const std::string& clipName,
                                           double clipOffset, double playSpeed) {
            for (auto& ent : sceneMgr_.getModelEntities()) {
                if (!ent.hasSkin_ || !ent.skeleton || ent.skeleton->bones.empty())
                    continue;
                for (int ci = 0; ci < static_cast<int>(ent.animationClips.size()); ++ci) {
                    if (ent.animationClips[ci].name == clipName) {
                        ent.previewClipIndex = ci;
                        double t = localT + clipOffset;
                        if (ent.animationClips[ci].duration > 0.f)
                            t = std::fmod(t, static_cast<double>(ent.animationClips[ci].duration));
                        ent.previewTime = static_cast<float>(t);
                        ent.previewSpeed = static_cast<float>(playSpeed);
                        break;
                    }
                }
            }
        };

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

        seqCallbacks.onEvent = [&](const std::string& eventName) {
            std::cout << "[Sequencer] Event: " << eventName << "\n";
            for (auto& ent : sceneMgr_.getModelEntities()) {
                if (ent.animatorController.hasStates()) {
                    AnimatorEvent ev;
                    ev.type = AnimatorEvent::Type::SetTrigger;
                    ev.paramName = eventName;
                    ent.animatorController.dispatchEvent(ev);
                }
            }
        };

        seqPlayer_.update(dt, seqCallbacks);
    }

    // Sequencer 未播放 AnimationClip 轨道时，恢复所有实体到正常模式
    if (!seqHasAnimTrack || !seqPlayer_.isPlaying()) {
        for (auto& ent : sceneMgr_.getModelEntities()) {
            ent.previewClipIndex = -1;
        }
    }

    // 逐实体动画采样与骨骼矩阵上传：每个实体使用自己的 skeleton/clips，
    // 不再依赖全局单例，支持多骨架动画实体同场景。
    std::vector<MaterialId> updatedMaterials;
    auto updateSkinMaterial = [&](MaterialId id, const std::vector<glm::mat4>& palette) {
        if (id == kInvalidMaterialId || !matMgr_.isValid(id)) return;
        if (std::find(updatedMaterials.begin(), updatedMaterials.end(), id) != updatedMaterials.end())
            return;
        matMgr_.updateBoneMatrices(id, imageIndex, palette);
        updatedMaterials.push_back(id);
    };

    for (auto& ent : sceneMgr_.getModelEntities()) {
        if (!ent.hasSkin_ || !ent.skeleton || ent.skeleton->bones.empty())
            continue;

        const auto& skeleton = ent.skeleton;
        const auto& clips = ent.animationClips;

        AnimatorController::BlendCommand blendCommand;
        if (ent.previewClipIndex >= 0 && ent.previewClipIndex < static_cast<int>(clips.size())) {
            // ── 预览模式：绕开状态机，直接播放单个 clip ──
            const AnimationClip* clip = &clips[ent.previewClipIndex];
            ent.previewTime += dt * ent.previewSpeed;
            if (clip->duration > 0.f)
                ent.previewTime = std::fmod(ent.previewTime, clip->duration);
            blendCommand.clipA = clip;
            blendCommand.timeA = ent.previewTime;
        } else {
            // A4 已采用每实体动画状态；Controller 同样按实体保存，避免不同模型互相覆盖。
            if (!ent.animatorController.hasStates() && !clips.empty()) {
                ent.animatorController.configureFromClips(clips);
            }
            blendCommand = ent.animatorController.update(dt, clips);
        }

        std::vector<glm::mat4> localTransforms(skeleton->bones.size());
        for (size_t i = 0; i < skeleton->bones.size(); ++i) {
            const glm::mat4& bindTransform = skeleton->bones[i].localBindTransform;
            if (!blendCommand.clipA) {
                localTransforms[i] = bindTransform;
                continue;
            }

            BoneLocalTransform pose = blendCommand.clipA->evaluateBoneLocalTransformParts(
                static_cast<int>(i), blendCommand.timeA, bindTransform);
            if (blendCommand.clipB) {
                const BoneLocalTransform target =
                    blendCommand.clipB->evaluateBoneLocalTransformParts(
                        static_cast<int>(i), blendCommand.timeB, bindTransform);
                const float weight = std::clamp(blendCommand.blendWeight, 0.f, 1.f);
                pose.translation = glm::mix(pose.translation, target.translation, weight);
                pose.rotation = glm::normalize(glm::slerp(pose.rotation, target.rotation, weight));
                pose.scale = glm::mix(pose.scale, target.scale, weight);
            }
            localTransforms[i] = pose.toMatrix();
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
            updateSkinMaterial(ent.materialId, makeSkinPalette(0));
            continue;
        }

        for (const auto& sm : ent.subMeshes) {
            MaterialId matId = ent.materialId;
            if (sm.materialSlot >= 0
                && sm.materialSlot < static_cast<int>(ent.subMeshMaterials.size())
                && ent.subMeshMaterials[sm.materialSlot] != kInvalidMaterialId) {
                matId = ent.subMeshMaterials[sm.materialSlot];
            }
            updateSkinMaterial(matId, makeSkinPalette(sm.skinIndex));
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
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Application::recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex)
{
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin recording command buffer!");

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
    
    if (renderPipForCamera && pipCameraPtr) {
        constexpr uint32_t pw = FramebufferManager::kPipWidth;
        constexpr uint32_t ph = FramebufferManager::kPipHeight;

        VkClearValue pipClears[2]{};
        pipClears[0].color = { {0.1f, 0.1f, 0.1f, 1.f} };
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

            // 跳过当前预览的 Camera 实体本身：PiP 相机位于该 Actor 原点，
            // 若绘制 Camera 模型会从内部遮挡整个预览画面。
            if (renderPipForCamera && ent.entityId == selectedCameraEntityId_)
                continue;

            VkBuffer vb = ent.vertexBuffer; VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
            vkCmdBindIndexBuffer(cb, ent.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            // Camera 实体的模型几何体需要叠加 modelRotationOffset（如 Y -90°）使显示朝向与预览一致
            glm::mat4 entModel = ent.transform.GetModelMatrix();
            const glm::mat4 offsetRot = glm::mat4_cast(ent.modelRotationOffset);
            entModel = entModel * offsetRot;
            PushConstants push = (ei == 0)
                ? PushConstants{ mainModelTransform.GetModelMatrix(), mainModelTransform.GetNormalMatrix() }
                : PushConstants{ entModel, glm::mat4(glm::transpose(glm::inverse(glm::mat3(entModel)))) };

            const MaterialId useMat = matMgr_.isValid(ent.materialId) ? ent.materialId : fallbackMat;
            VkPipeline pipe = matMgr_.getPipeline(useMat, ctx_, pipeMgr_);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            VkDescriptorSet ds = matMgr_.getPipDescriptorSet(useMat, imageIndex);
            const bool isSkinned = (pipe == pipeMgr_.getSkinnedPipeline());
            VkPipelineLayout pipeLayout = isSkinned
                ? pipeMgr_.getSkinnedPipelineLayout()
                : pipeMgr_.getMainPipelineLayout();
            vkCmdPushConstants(cb, pipeLayout,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeLayout, 0, 1, &ds, 0, nullptr);

            if (ent.subMeshes.empty()) {
                vkCmdDrawIndexed(cb, ent.indexCount, 1, 0, 0, 0);
            } else {
                for (const auto& sm : ent.subMeshes) {
                    MaterialId smMat = useMat;
                    if (sm.materialSlot >= 0 && sm.materialSlot < (int)ent.subMeshMaterials.size()
                        && ent.subMeshMaterials[sm.materialSlot] != 0u) {
                        const MaterialId slotMat = ent.subMeshMaterials[sm.materialSlot];
                        if (matMgr_.isValid(slotMat)) smMat = slotMat;
                    }
                    if (smMat != useMat) {
                        pipe = matMgr_.getPipeline(smMat, ctx_, pipeMgr_);
                        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                        ds = matMgr_.getPipDescriptorSet(smMat, imageIndex);
                        const bool smIsSkinned = (pipe == pipeMgr_.getSkinnedPipeline());
                        VkPipelineLayout smPipeLayout = smIsSkinned
                            ? pipeMgr_.getSkinnedPipelineLayout()
                            : pipeMgr_.getMainPipelineLayout();
                        vkCmdPushConstants(cb, smPipeLayout,
                                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
                        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                smPipeLayout, 0, 1, &ds, 0, nullptr);
                    }
                    vkCmdDrawIndexed(cb, sm.indexCount, 1, sm.indexOffset, 0, 0);
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
                if (!ent.visible || ent.indexCount == 0 || !ent.vertexBuffer || !ent.indexBuffer)
                    continue;

                VkBuffer vb = ent.vertexBuffer; VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
                vkCmdBindIndexBuffer(cb, ent.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                // Entity 0 使用 mainModelTransform，其他实体使用自身的 transform
                // Camera 实体的模型几何体需要叠加 modelRotationOffset（如 Y -90°）使显示朝向与预览一致
                glm::mat4 entModel = ent.transform.GetModelMatrix();
                const glm::mat4 offsetRot = glm::mat4_cast(ent.modelRotationOffset);
                entModel = entModel * offsetRot;
                PushConstants push = (ei == 0)
                    ? PushConstants{ mainModelTransform.GetModelMatrix(), mainModelTransform.GetNormalMatrix() }
                    : PushConstants{ entModel, glm::mat4(glm::transpose(glm::inverse(glm::mat3(entModel)))) };

                const MaterialId useMat = matMgr_.isValid(ent.materialId) ? ent.materialId : fallbackMat;
                VkPipeline pipe = matMgr_.getPipeline(useMat, ctx_, pipeMgr_);
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                VkDescriptorSet ds = matMgr_.getDescriptorSet(useMat, imageIndex);
                const bool isSkinned = (pipe == pipeMgr_.getSkinnedPipeline());
                VkPipelineLayout pipeLayout = isSkinned
                    ? pipeMgr_.getSkinnedPipelineLayout()
                    : pipeMgr_.getMainPipelineLayout();
                vkCmdPushConstants(cb, pipeLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeLayout, 0, 1, &ds, 0, nullptr);

                if (ent.subMeshes.empty()) {
                    vkCmdDrawIndexed(cb, ent.indexCount, 1, 0, 0, 0);
                } else {
                    for (const auto& sm : ent.subMeshes) {
                        MaterialId smMat = useMat;
                        if (sm.materialSlot >= 0 && sm.materialSlot < (int)ent.subMeshMaterials.size()
                            && ent.subMeshMaterials[sm.materialSlot] != 0u) {
                            const MaterialId slotMat = ent.subMeshMaterials[sm.materialSlot];
                            if (matMgr_.isValid(slotMat)) smMat = slotMat;
                        }
                        if (smMat != useMat) {
                            pipe = matMgr_.getPipeline(smMat, ctx_, pipeMgr_);
                            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                            ds = matMgr_.getDescriptorSet(smMat, imageIndex);
                            const bool smIsSkinned = (pipe == pipeMgr_.getSkinnedPipeline());
                            VkPipelineLayout smPipeLayout = smIsSkinned
                                ? pipeMgr_.getSkinnedPipelineLayout()
                                : pipeMgr_.getMainPipelineLayout();
                            vkCmdPushConstants(cb, smPipeLayout,
                                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push);
                            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                    smPipeLayout, 0, 1, &ds, 0, nullptr);
                        }
                        vkCmdDrawIndexed(cb, sm.indexCount, 1, sm.indexOffset, 0, 0);
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
    if (ImGui::GetCurrentContext()) {
        ImDrawData* dd = ImGui::GetDrawData();
        if (dd && dd->Valid) {
            TINYENGINE(cb, "ImGui Overlay");
            ImGui_ImplVulkan_RenderDrawData(dd, cb);
        }
    }

    vkCmdEndRenderPass(cb);
    } // Main Render Pass scope

    if (vkEndCommandBuffer(cb) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer!");
}

// ─── Swapchain recreation ─────────────────────────────────────────────────────

void Application::recreateSwapChain()
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(window_, &w, &h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(ctx_.getDevice());

    // recreateSwapChain 会调用 ImGui_ImplVulkan_Shutdown 销毁所有 ImGui texture，
    // 必须重置 pipTextureCreated_，否则下一帧 PiP 会使用已失效的旧 texture 句柄。
    pipTextureCreated_ = false;

    pickSys_.destroy(ctx_, cmdMgr_);
    cmdMgr_.freeCommandBuffers(ctx_);
    descMgr_.destroy(ctx_);
    matMgr_.destroy(ctx_);
    fbMgr_.destroy(ctx_);
    pipeMgr_.destroyPipelines(ctx_);
    rpMgr_.destroy(ctx_);
    swapChain_.destroy(ctx_);

    swapChain_.create(ctx_, window_);
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

    sceneMgr_.destroyModelBuffers(ctx_);
    sceneMgr_.loadModel(ui_->modelPath, glm::vec3(0.f), bufMgr_);
    mainModelTransform = ObjectTransform{};
    mainModelSelected  = false;
    pickedBoxEntityId  = 0;

    matMgr_.init(ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_,
                 swapChain_.getImageCount(), ui_->texturePath);
    sceneMgr_.setModelMaterialId(matMgr_.getDefaultMeshMaterialId());

    // Re-apply the asset material after swapchain recreate.
    {
        const MaterialId mid = matMgr_.loadMaterialFromAsset(
            "materials/mainmodel.ast", ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
        if (mid != kInvalidMaterialId) sceneMgr_.setModelMaterialId(mid);
    }

    // Re-apply auto-dumped glTF .ast files per SubMesh slot.
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

    descMgr_.create(ctx_, swapChain_, pipeMgr_, bufMgr_);
    pickSys_.create(ctx_, cmdMgr_);
    cmdMgr_.allocateCommandBuffers(ctx_, swapChain_.getImageCount());
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
        const glm::vec3 ext = sceneMgr_.getModelBoundsMax() - sceneMgr_.getModelBoundsMin();
        if (glm::length(ext) < 1e-5f) { focus = mainModelTransform.position; distance = 5.f; }
        else {
            focus    = 0.5f * (sceneMgr_.getModelBoundsMin() + sceneMgr_.getModelBoundsMax())
                       + mainModelTransform.position;
            distance = glm::max(3.f, glm::length(ext) * 1.75f);
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
            for (auto& clip : animAsset.clips)
                mergedClips.push_back(std::move(clip));
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
    matMgr_.destroyMaterial(id, ctx_);
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

    // 使用 .ast 中的 modelRelPath 拼接模型文件完整路径
    const std::string fullPath = modelRegistry_.getResRoot() + "/" + asset->modelRelPath;

    dragPlace.assetId  = assetId;
    dragPlace.entityId = sceneMgr_.createModelEntity(fullPath, glm::vec3(0.f), bufMgr_);
    auto* ent = sceneMgr_.getModelEntity(dragPlace.entityId);
    if (!ent) { dragPlace.active = true; return; }

    // 记录 .ast 资产路径，供 SceneSerializer 保存
    ent->astRelPath = asset->astRelPath;

    MaterialAssetDesc entryDesc;
    const bool hasEntryDesc = MaterialAssetLoader::load(asset->astRelPath, entryDesc, nullptr);
    const auto& materialPaths = (hasEntryDesc && !entryDesc.subMaterialPaths.empty())
        ? entryDesc.subMaterialPaths
        : ent->autoAstPaths;

    // glTF 入口 .ast 可显式声明 subMaterials；否则回退到 loadModelFromGltf 自动生成的路径。
    if (!materialPaths.empty()) {
        if (ent->subMeshMaterials.size() < materialPaths.size())
            ent->subMeshMaterials.resize(materialPaths.size(), 0u);
        for (size_t slot = 0; slot < materialPaths.size(); ++slot) {
            const MaterialId mid = matMgr_.loadMaterialFromAsset(
                materialPaths[slot], ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
            if (mid != kInvalidMaterialId && slot < ent->subMeshMaterials.size()) {
                ent->subMeshMaterials[slot] = mid;
            }
        }
        // 把 slot 0 的材质设为整个实体的回退材质
        if (!ent->subMeshMaterials.empty() && ent->subMeshMaterials[0] != 0u)
            ent->materialId = ent->subMeshMaterials[0];
    } else {
        // 无 auto-generated .ast（如 .obj 文件）：加载入口 .ast
        const MaterialId mid = matMgr_.loadMaterialFromAsset(
            asset->astRelPath, ctx_, cmdMgr_, bufMgr_, fbMgr_, pipeMgr_);
        if (mid != kInvalidMaterialId) {
            ent->materialId = mid;
            if (!ent->subMeshes.empty()) {
                ent->subMeshMaterials.resize(ent->subMeshes.size(), mid);
            }
        }
    }

    // 若有骨骼数据，为实体的槽位材质创建蒙皮版本
    if (hasEntryDesc)
        ensureAnimationAssetForMeshAst(asset->astRelPath, asset->modelRelPath, false);

    if (ent->hasSkin_)
        convertModelMaterialsToSkinned();

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

        // Reset UI state
        ui_->selectedEntityId_ = 0;
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
                                     const std::string& targetMeshAstRelPath)
{
    namespace fs = std::filesystem;
    std::error_code ec;

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
    } else {
        std::cerr << "[ImportAnim] failed to update mesh .ast: " << meshAstAbs << "\n";
        return false;
    }

    modelRegistry_.refresh();

    // 6. If target entity is already loaded in the scene, apply the animation
    const auto& ents = sceneMgr_.getModelEntities();
    for (const auto& ent : ents) {
        if (ent.astRelPath == targetMeshAstRelPath) {
            sceneMgr_.setEntityAnimationData(ent.entityId, dstSkeleton,
                                             retargetResult.retargetedClips);
            std::cout << "[ImportAnim] applied animation to entity " << ent.entityId
                      << " (" << ent.displayName << ")\n";
            break;
        }
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

    camera_.speedZ = (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS)  ?  1.f
                   : (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS)  ? -1.f : 0.f;
    camera_.speedX = (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS)  ? -1.f
                   : (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS)  ?  1.f : 0.f;
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

    thumbnailRenderer_.destroy(ctx_, matMgr_, sceneMgr_);
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
