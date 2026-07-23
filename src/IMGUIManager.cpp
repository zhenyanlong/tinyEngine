#include "IMGUIManager.hpp"
#include "Application.hpp"
#include "Animation/AnimationAssetLoader.hpp"
#include "ImGuizmo.h"
#include "nlohmann/json.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <commdlg.h>
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace {

bool isAnimatorControllerAsset(const std::filesystem::path& path)
{
    const std::string filename = path.filename().string();
    constexpr const char* suffix = ".animctrl.json";
    constexpr size_t suffixLength = 14;
    return filename.size() >= suffixLength
        && filename.compare(filename.size() - suffixLength, suffixLength, suffix) == 0;
}

bool isControllerCompatibleWithClips(const std::filesystem::path& path,
                                     const std::vector<AnimationClip>& clips)
{
    AnimatorController candidate;
    if (!candidate.loadFromFile(path.string())) return false;

    for (const auto& state : candidate.states()) {
        auto clipExists = [&](const std::string& clipName) {
            return !clipName.empty()
                && std::any_of(clips.begin(), clips.end(), [&](const AnimationClip& clip) {
                    return clip.name == clipName;
                });
        };
        if (state.motionType == StateMotionType::BlendSpace1D) {
            if (state.blendSamples.empty()) return false;
            const bool hasFloatParameter = std::any_of(
                candidate.params().begin(), candidate.params().end(),
                [&](const AnimatorParam& param) {
                    return param.name == state.blendParameter
                        && param.type == AnimatorParam::Type::Float;
                });
            if (!hasFloatParameter) return false;
            std::vector<float> positions;
            for (const auto& sample : state.blendSamples) {
                if (!clipExists(sample.clipName)) return false;
                if (!std::isfinite(sample.position)) return false;
                positions.push_back(sample.position);
            }
            std::sort(positions.begin(), positions.end());
            for (size_t i = 1; i < positions.size(); ++i) {
                if (std::abs(positions[i] - positions[i - 1]) <= 1e-5f)
                    return false;
            }
        } else if (!clipExists(state.clipName)) {
            return false;
        }
    }
    return true;
}

bool validateAnimatorController(const AnimatorController& controller,
                                const std::vector<AnimationClip>& clips,
                                std::string& error)
{
    auto clipExists = [&](const std::string& clipName) {
        return !clipName.empty()
            && std::any_of(clips.begin(), clips.end(), [&](const AnimationClip& clip) {
                return clip.name == clipName;
            });
    };
    auto floatParamExists = [&](const std::string& name) {
        return std::any_of(
            controller.params().begin(), controller.params().end(),
            [&](const AnimatorParam& param) {
                return param.name == name && param.type == AnimatorParam::Type::Float;
            });
    };

    std::set<std::string> stateNames;
    for (const auto& state : controller.states()) {
        if (state.name.empty() || !stateNames.insert(state.name).second) {
            error = "State names must be non-empty and unique";
            return false;
        }
        if (!std::isfinite(state.playRate) || state.playRate < 0.f) {
            error = "State '" + state.name + "' has an invalid play rate";
            return false;
        }
        if (state.motionType == StateMotionType::SingleClip) {
            if (!clipExists(state.clipName)) {
                error = "State '" + state.name + "' references a missing clip";
                return false;
            }
            continue;
        }

        if (!floatParamExists(state.blendParameter)) {
            error = "BlendSpace state '" + state.name
                  + "' requires an existing Float parameter";
            return false;
        }
        if (state.blendSamples.empty()) {
            error = "BlendSpace state '" + state.name + "' has no samples";
            return false;
        }
        std::vector<float> positions;
        positions.reserve(state.blendSamples.size());
        for (const auto& sample : state.blendSamples) {
            if (!clipExists(sample.clipName) || !std::isfinite(sample.position)) {
                error = "BlendSpace state '" + state.name
                      + "' has an invalid sample";
                return false;
            }
            positions.push_back(sample.position);
        }
        std::sort(positions.begin(), positions.end());
        for (size_t i = 1; i < positions.size(); ++i) {
            if (std::abs(positions[i] - positions[i - 1]) <= 1e-5f) {
                error = "BlendSpace state '" + state.name
                      + "' has duplicate sample positions";
                return false;
            }
        }
    }
    return true;
}

std::vector<std::string> scanCompatibleAnimatorControllers(
    const std::filesystem::path& resRoot,
    const std::vector<AnimationClip>& clips)
{
    std::vector<std::string> result;
    const std::filesystem::path root = resRoot / "animators";
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return result;

    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !isAnimatorControllerAsset(it->path())) continue;
        if (isControllerCompatibleWithClips(it->path(), clips))
            result.push_back(it->path().lexically_normal().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string makeAnimatorControllerStem(const SceneManager::ModelEntity& entity)
{
    std::string stem = entity.displayName;
    if (stem.empty() && !entity.astRelPath.empty())
        stem = std::filesystem::path(entity.astRelPath).stem().string();
    if (stem.empty()) stem = "NewAnimController";

    for (char& ch : stem) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (!std::isalnum(value) && ch != '-' && ch != '_') ch = '_';
    }
    return stem;
}

std::filesystem::path makeUniqueAnimatorControllerPath(
    const std::filesystem::path& directory,
    const std::string& stem)
{
    std::filesystem::path candidate = directory / (stem + ".animctrl.json");
    std::error_code ec;
    for (int suffix = 1; std::filesystem::exists(candidate, ec); ++suffix) {
        ec.clear();
        candidate = directory / (stem + "_" + std::to_string(suffix) + ".animctrl.json");
    }
    return candidate;
}

std::string trimCopy(const std::string& value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

} // namespace

/** @brief ImGui Vulkan 后端的错误回调：非 0 打印并中止（教学：发布版可改为日志） */
static void check_vk_result(VkResult err)
{
    if (err == 0)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

#ifdef _WIN32
/** @brief 打开 Windows 原生文件选择对话框，返回选中的文件路径（UTF-8） */
static std::string openFileDialog(GLFWwindow* window)
{
    wchar_t fileBuf[1024]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = glfwGetWin32Window(window);
    ofn.lpstrFilter = L"3D Models (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = sizeof(fileBuf) / sizeof(wchar_t);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"fbx";

    if (GetOpenFileNameW(&ofn)) {
        int len = WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string result(static_cast<size_t>(len) - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, &result[0], len, nullptr, nullptr);
        return result;
    }
    return {};
}
#endif

/**
 * @brief 返回项目根目录（包含 CMakeLists.txt 和 res/ 的目录）。
 *
 * 以前返回 exe 所在目录，依赖 CMake post-build 把 res/ 复制到 exe 旁，
 * 导致场景/资产保存后被源 res/ 覆盖。现在从 exe 路径向上查找项目根，
 * 所有 res 读写都以项目根/res/ 为唯一基准，彻底消除双份 res 问题。
 */
static std::string applicationResourceRoot()
{
    namespace fs = std::filesystem;
#ifdef _WIN32
    char path[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) != 0) {
        fs::path p(path);
        // 从 exe 所在目录向上查找项目根：同时含 CMakeLists.txt 和 res/ 的目录
        for (fs::path probe = p.parent_path(); probe.has_parent_path(); probe = probe.parent_path()) {
            std::error_code ec;
            if (fs::is_regular_file(probe / "CMakeLists.txt", ec)
                && fs::is_directory(probe / "res", ec)) {
                return probe.string();
            }
        }
        return p.parent_path().string();
    }
#endif
    return fs::current_path().string();
}

/** @brief 在目录中按主文件名与若干 fallback 查找第一个存在的模型文件 */
static std::string resolveModelInDir(const std::string& dirWithSlash, const char* primaryFile)
{
	const char* fallbacks[] = { primaryFile, "01.obj", "no_material.obj" };
	std::filesystem::path dir(dirWithSlash);
	std::error_code ec;
	for (const char* name : fallbacks) {
		const auto f = dir / name;
		if (std::filesystem::is_regular_file(f, ec))
			return f.string();
	}
	return (dir / primaryFile).string();
}

/** @brief 在目录中按主文件名与教学用示例贴图列表查找纹理路径 */
static std::string resolveTextureInDir(const std::string& dirWithSlash, const char* primaryFile)
{
	const char* fallbacks[] = {
		primaryFile,
		"cyber_room.png",
		"fantasy_game_inn.png",
		"viking_room.png",
		"texture.jpg",
	};
	std::filesystem::path dir(dirWithSlash);
	std::error_code ec;
	for (const char* name : fallbacks) {
		const auto f = dir / name;
		if (std::filesystem::is_regular_file(f, ec))
			return f.string();
	}
	return (dir / primaryFile).string();
}

/** @brief 见 IMGUIManager.hpp：用当前 VulkanRender 的队列与 RenderPass 初始化 ImGui_ImplVulkan */
void UIManager::initImGuiVulkanBackend()
{
	IM_ASSERT(vulkanRender != nullptr);
	QueueFamily = vulkanRender->getGraphicsQueueFamily();
	Queue = vulkanRender->getGraphicsQueue();

	ImGui_ImplVulkan_InitInfo init_info{};
	init_info.Instance = Instance;
	init_info.PhysicalDevice = PhysicalDevice;
	init_info.Device = Device;
	init_info.QueueFamily = QueueFamily;
	init_info.Queue = Queue;
	init_info.PipelineCache = PipelineCache;
	init_info.DescriptorPoolSize = 1024;
	init_info.RenderPass = vulkanRender->getMainRenderPass();
	init_info.Subpass = 0;
	init_info.MinImageCount = 2;
	init_info.ImageCount = vulkanRender->getSwapChainImageCount();
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.Allocator = Allocator;
	init_info.CheckVkResultFn = check_vk_result;
	ImGui_ImplVulkan_Init(&init_info);
}

/** @brief 见 IMGUIManager.hpp：CreateContext、GLFW/Vulkan 后端、暗色主题 */
void UIManager::initIMGUI()
{
	IM_ASSERT(vulkanRender != nullptr);
	window = vulkanRender->getMainWindow();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	g_io = io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForVulkan(window, true);
	initImGuiVulkanBackend();

	// 加载缩略图占位图标
	loadThumbnailIcon();
}

/** @brief 见 IMGUIManager.hpp：swapchain 重建后重绑 ImGui 与新的 RenderPass */
void UIManager::reloadImGuiVulkanAfterSwapchainRecreate(Application* app)
{
	vulkanRender = app;
	ImGui_ImplVulkan_Shutdown();
	// ImGui Vulkan 后端拥有这些 ImTextureID 对应的 descriptor set。
	// 后端重建后旧 ID 全部失效；清空缓存并按需重新注册，避免 resize 后使用悬空 descriptor。
	if (thumbIconSampler_ != VK_NULL_HANDLE) {
		vkDestroySampler(Device, thumbIconSampler_, nullptr);
		thumbIconSampler_ = VK_NULL_HANDLE;
	}
	for (const auto& [assetId, sampler] : thumbSamplers_) {
		if (sampler != VK_NULL_HANDLE)
			vkDestroySampler(Device, sampler, nullptr);
	}
	thumbnailIcon_ = (ImTextureID)0;
	thumbCache_.clear();
	thumbSamplers_.clear();
	initImGuiVulkanBackend();
	loadThumbnailIcon();
}

/** @brief 见 IMGUIManager.hpp：保存 Instance 与分配器供后续 Init */
void UIManager::setVulkanInstance(const VkInstance& instance, VkAllocationCallbacks* allocator)
{
	Instance = instance;
	Allocator = allocator;
}

/** @brief 见 IMGUIManager.hpp：保存 PhysicalDevice 与 Device */
void UIManager::setPhysicalDevice(const VkDevice& device, const VkPhysicalDevice& physicalDevice)
{
	PhysicalDevice = physicalDevice;
	Device = device;
}

/** @brief 见 IMGUIManager.hpp：设置是否请求刷新 Vulkan（如改 shader 路径） */
void UIManager::setRefreshVulkanStatus(bool status)
{
    refreshVulkanRender = status;
}

/** @brief 见 IMGUIManager.hpp：由 vert 路径派生 box 顶点 shader 路径 */
void UIManager::syncBoxVertShaderPathFromVert()
{
    boxVertShaderPath = vertexShaderPath;
    static const char kVert[] = "vert.spv";
    const size_t pos = boxVertShaderPath.rfind(kVert);
    if (pos != std::string::npos)
        boxVertShaderPath.replace(pos, sizeof(kVert) - 1, "box_vert.spv");
}

/** @brief 见 IMGUIManager.hpp：由 frag 路径派生 box 片段 shader 路径 */
void UIManager::syncBoxFragShaderPathFromFrag()
{
    boxFragShaderPath = fragShaderPath;
    static const char kFrag[] = "frag.spv";
    const size_t pos = boxFragShaderPath.rfind(kFrag);
    if (pos != std::string::npos)
        boxFragShaderPath.replace(pos, sizeof(kFrag) - 1, "box.spv");
}

/** @brief 见 IMGUIManager.hpp：填充默认资源目录与当前 cyber_room 示例路径 */
void UIManager::setModelDefaultPath()
{
	const std::string root = applicationResourceRoot();
	snprintf(VertexShaderPath, sizeof(VertexShaderPath), "%s/res/shaders/", root.c_str());
	snprintf(FragShdaerPath, sizeof(FragShdaerPath), "%s/res/shaders/", root.c_str());
	snprintf(ModelPath, sizeof(ModelPath), "%s/res/models/", root.c_str());
	snprintf(TexturePath, sizeof(TexturePath), "%s/res/textures/", root.c_str());
	snprintf(currentVertexShaderPath, sizeof(currentVertexShaderPath), "%svert.spv", VertexShaderPath);
	snprintf(currentFragShdaerPath, sizeof(currentFragShdaerPath), "%sfrag.spv", FragShdaerPath);

	const std::string modelResolved = resolveModelInDir(ModelPath, "cyber_room.obj");
	snprintf(currentModelPath, sizeof(currentModelPath), "%s", modelResolved.c_str());
	const std::string texResolved = resolveTextureInDir(TexturePath, "cyber_room.png");
	snprintf(currentTexturePath, sizeof(currentTexturePath), "%s", texResolved.c_str());

	vertexShaderPath = currentVertexShaderPath;
	fragShaderPath = currentFragShdaerPath;
	syncBoxFragShaderPathFromFrag();
	syncBoxVertShaderPathFromVert();
	modelPath = modelResolved;
	texturePath = texResolved;
}

/** @brief 见 IMGUIManager.hpp：返回是否需要 recreateSwapChain / 重载管线 */
bool UIManager::refreshVulkanShader()
{
    return refreshVulkanRender;
}

/** @brief 见 IMGUIManager.hpp：每帧 NewFrame、操作面板、ImGuizmo 平移、ImGui::Render */
void UIManager::prepareFrame()
{
	if (window == nullptr) {
		return;
	}

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();

    ImGui::Begin("tinyEngineOperationWindow"); 
    std::string prompt= R"(
        You can move by pressing w, a, s, d. 
        You can press and hold the right mouse button to rotate the view.
    )";
    ImGui::Text(prompt.c_str());              

    ImGui::Checkbox("Content Browser", &showContentBrowser_);
    ImGui::Checkbox("Animator", &showAnimatorPanel_);
    ImGui::Checkbox("Sequencer", &showSequencerPanel_);

    if (ImGui::Button("New Camera")) {
        if (vulkanRender) {
            Camera& cam = vulkanRender->getCamera();
            vulkanRender->createCameraActor(cam.GetWorldPosition(), cam.GetOrientation());
        }
    }

    ImGui::Checkbox("PiP Camera", &showPipWindow_);

    ImGui::ColorEdit3("clear color", (float*)&clear_color); 

    // 创建一个静态变量来存储选中项的索引
    static int selectedIndex = 0;

    // 定义下拉选项的内容
    const char* items[] = { "cyberRoom", "fantasyGameInn", "vikingRoom"};
    const int itemCount = IM_ARRAYSIZE(items);

    // 创建下拉选项框
    ImGui::Combo("drop-down box", &selectedIndex, items, itemCount);
    if (selectedIndex != currentIndex) {
        switch (selectedIndex)
        {
        case 0:
            snprintf(currentVertexShaderPath, sizeof(currentVertexShaderPath), "%svert.spv", VertexShaderPath);
            snprintf(currentFragShdaerPath, sizeof(currentFragShdaerPath), "%sfrag.spv", FragShdaerPath);
            {
                const std::string mr = resolveModelInDir(ModelPath, "cyber_room.obj");
                snprintf(currentModelPath, sizeof(currentModelPath), "%s", mr.c_str());
                const std::string tr = resolveTextureInDir(TexturePath, "cyber_room.png");
                snprintf(currentTexturePath, sizeof(currentTexturePath), "%s", tr.c_str());
            }
            setRefreshVulkanStatus(true);
            break;
        case 1:
            snprintf(currentVertexShaderPath, sizeof(currentVertexShaderPath), "%svert.spv", VertexShaderPath);
            snprintf(currentFragShdaerPath, sizeof(currentFragShdaerPath), "%sfrag.spv", FragShdaerPath);
            {
                const std::string mr = resolveModelInDir(ModelPath, "fantasy_game_inn.obj");
                snprintf(currentModelPath, sizeof(currentModelPath), "%s", mr.c_str());
                const std::string tr = resolveTextureInDir(TexturePath, "fantasy_game_inn.png");
                snprintf(currentTexturePath, sizeof(currentTexturePath), "%s", tr.c_str());
            }
            setRefreshVulkanStatus(true);
            break;
        case 2:
            snprintf(currentVertexShaderPath, sizeof(currentVertexShaderPath), "%svert.spv", VertexShaderPath);
            snprintf(currentFragShdaerPath, sizeof(currentFragShdaerPath), "%sfrag.spv", FragShdaerPath);
            {
                const std::string mr = resolveModelInDir(ModelPath, "viking_room.obj");
                snprintf(currentModelPath, sizeof(currentModelPath), "%s", mr.c_str());
                const std::string tr = resolveTextureInDir(TexturePath, "viking_room.png");
                snprintf(currentTexturePath, sizeof(currentTexturePath), "%s", tr.c_str());
            }
            setRefreshVulkanStatus(true);
            break;
        default:
            break;
        }
        vertexShaderPath = currentVertexShaderPath;
        fragShaderPath = currentFragShdaerPath;
        syncBoxFragShaderPathFromFrag();
        syncBoxVertShaderPathFromVert();
        modelPath = currentModelPath;
        texturePath = currentTexturePath;
        currentIndex = selectedIndex;
    }

    // ── Material Asset (.ast) Picker ─────────────────────────────────
    if (vulkanRender) {
        ImGui::Separator();
        ImGui::Text("Material Assets (.ast)");

        if (!astAssetScanned_) scanMaterialAssets();

        if (astAssetFiles_.empty()) {
            ImGui::TextDisabled("(no .ast files in res/content)");
            if (ImGui::Button("Rescan##ast")) scanMaterialAssets();
        } else {
            std::vector<const char*> items;
            items.reserve(astAssetFiles_.size());
            for (const auto& s : astAssetFiles_) items.push_back(s.c_str());
            if (astAssetIndex_ < 0 || astAssetIndex_ >= static_cast<int>(items.size()))
                astAssetIndex_ = 0;
            ImGui::Combo("##astpick", &astAssetIndex_, items.data(), static_cast<int>(items.size()));

            if (ImGui::Button("Apply##ast")) {
                const std::string rel = astAssetFiles_[astAssetIndex_];
                if (vulkanRender->loadAndApplyMaterialAsset(rel)) {
                    astStatusMsg_ = std::string("Loaded: ") + astAssetFiles_[astAssetIndex_];
                } else {
                    astStatusMsg_ = std::string("Failed: ") + astAssetFiles_[astAssetIndex_];
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Rescan##ast")) scanMaterialAssets();

            if (!astStatusMsg_.empty()) {
                ImGui::TextWrapped("%s", astStatusMsg_.c_str());
            }
        }
    }

    if (vulkanRender) {
        ImGui::Separator();
        ImGui::Text("Main model (scene mesh)");
        ImGui::Text("Pick: left-click mesh or box in this window.");
        ImGui::Text("Main model selected: %s", vulkanRender->mainModelSelected ? "yes" : "no");
        ImGui::Text("Picked box entity: %llu",
            static_cast<unsigned long long>(vulkanRender->pickedBoxEntityId));
        if (vulkanRender->mainModelSelected) {
            ImGui::TextUnformatted("Move main: drag RGB arrows in 3D view.");
        }
        else if (vulkanRender->pickedBoxEntityId != 0) {
            ImGui::TextUnformatted("Move box: drag RGB arrows in 3D view.");
        }

        // ── Scene Save / Load ────────────────────────────────────────────
        ImGui::Separator();
        ImGui::Text("Scene Persistence");
        static char sceneNameBuf[128] = "default";
        ImGui::InputText("Scene Name", sceneNameBuf, sizeof(sceneNameBuf));
        if (ImGui::Button("Save Scene")) {
            const std::string path = vulkanRender->getResRoot() + "/scenes/"
                + sceneNameBuf + ".scene.json";
            // Ensure directory exists
            std::filesystem::create_directories(
                std::filesystem::path(vulkanRender->getResRoot()) / "scenes");
            if (vulkanRender->saveScene(path))
                astStatusMsg_ = std::string("Saved: ") + sceneNameBuf + ".scene.json";
            else
                astStatusMsg_ = std::string("Save failed: ") + sceneNameBuf + ".scene.json";
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Scene")) {
            const std::string path = vulkanRender->getResRoot() + "/scenes/"
                + sceneNameBuf + ".scene.json";
            if (vulkanRender->loadScene(path))
                astStatusMsg_ = std::string("Loaded: ") + sceneNameBuf + ".scene.json";
            else
                astStatusMsg_ = std::string("Load failed: ") + sceneNameBuf + ".scene.json";
        }
    }

    ImGui::Separator();
    ImGui::Text("Box Entity");
    ImGui::InputFloat3("Box Position", boxPosition);
    if (ImGui::Button("Add Box")) {
        if (vulkanRender) {
            lastAddedBoxId = vulkanRender->addBox(glm::vec3(boxPosition[0], boxPosition[1], boxPosition[2]));
            deleteBoxId = lastAddedBoxId;
        }
    }
    ImGui::Text("Last Added ID: %llu", static_cast<unsigned long long>(lastAddedBoxId));
    ImGui::InputScalar("Delete Box ID", ImGuiDataType_U64, &deleteBoxId);
    if (ImGui::Button("Delete Box")) {
        if (vulkanRender) {
            vulkanRender->removeBox(static_cast<Application::RenderEntityId>(deleteBoxId));
        }
    }

    // ── Material Panel ────────────────────────────────────────────────────────
    if (vulkanRender) {
        ImGui::Separator();
        ImGui::Text("Materials");

        MaterialManager& matMgr = vulkanRender->getMaterialManager();
        const auto& allIds = matMgr.getAllMaterialIds();

        // Sync editing selection to the picked object's material
        if (vulkanRender->selectedMaterialId != kInvalidMaterialId &&
            matMgr.isValid(vulkanRender->selectedMaterialId))
        {
            editingMaterialId_ = vulkanRender->selectedMaterialId;
        }
        if (!matMgr.isValid(editingMaterialId_) && !allIds.empty())
            editingMaterialId_ = allIds.front();

        // Material list
        if (ImGui::BeginListBox("##materials", ImVec2(-1, 80.f))) {
            for (MaterialId id : allIds) {
                const bool sel = (id == editingMaterialId_);
                char label[256];
                const MaterialType matType = matMgr.getMaterialType(id);
                const char* typeName = matType == MaterialType::Mesh ? "Mesh"
                    : (matType == MaterialType::Box ? "Box" : "Material");
                snprintf(label, sizeof(label), "[%u] %s (%s)",
                         id, matMgr.getMaterialName(id).c_str(), typeName);
                if (ImGui::Selectable(label, sel)) {
                    editingMaterialId_ = id;
                    // Sync texture path buffers to this material
                    const std::string& ap = matMgr.getAlbedoPath(id);
                    const std::string& np = matMgr.getNormalPath(id);
                    snprintf(matAlbedoPath_, sizeof(matAlbedoPath_), "%s", ap.c_str());
                    snprintf(matNormalPath_, sizeof(matNormalPath_), "%s", np.c_str());
                    texturePathMaterialId_ = id;
                    textureStatusMsg_.clear();
                }
            }
            ImGui::EndListBox();
        }

        // Create new material
        if (ImGui::Button("+ Mesh Material")) {
            MaterialId newId = vulkanRender->createMeshMaterial("New Mesh", "", "", MaterialParams{});
            editingMaterialId_ = newId;
            matAlbedoPath_[0] = '\0';
            matNormalPath_[0] = '\0';
            texturePathMaterialId_ = newId;
            textureStatusMsg_.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Box Material")) {
            MaterialId newId = vulkanRender->createBoxMaterial("New Box", MaterialParams{});
            editingMaterialId_ = newId;
        }
    }

    ImGui::End();

    if (showContentBrowser_)
        drawContentBrowser();

    if (showOutliner_)
		drawSceneOutliner();

	if (showPropertiesPanel_)
		drawPropertiesPanel();

	if (showAnimatorPanel_)
        drawAnimatorPanel();

    bool shouldShowPip = showPipWindow_;
    if (vulkanRender && vulkanRender->selectedCameraEntityId_ != 0) {
        shouldShowPip = true;
    }
    
    if (shouldShowPip && vulkanRender)
        drawPipWindow();

    if (showSequencerPanel_ && vulkanRender)
        drawSequencerPanel();

	if (vulkanRender != nullptr) {
		ImGuiIO& io = ImGui::GetIO();
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
		ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
		glm::mat4 view = vulkanRender->getSceneViewMatrix();
		glm::mat4 proj = vulkanRender->getSceneProjMatrixForImGuizmo();

		ImGuizmo::OPERATION op = static_cast<ImGuizmo::OPERATION>(gizmoOperation_);
		const ImGuizmo::MODE gizmoMode = gizmoLocal_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

		// 优先对选中的非 Box 实体操作 ImGuizmo
		if (selectedEntityId_ != 0) {
			auto* ent = vulkanRender->getSceneManager().getModelEntity(selectedEntityId_);
			if (ent) {
				glm::mat4 model = ent->transform.GetModelMatrix();
				ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, gizmoMode,
					glm::value_ptr(model), nullptr, nullptr);
				// 从修改后的矩阵分解 TRS
				ent->transform.position = glm::vec3(model[3]);
				ent->transform.scale = glm::vec3(
					glm::length(glm::vec3(model[0])),
					glm::length(glm::vec3(model[1])),
					glm::length(glm::vec3(model[2])));
				glm::mat3 rot;
				rot[0] = glm::vec3(model[0]) / ent->transform.scale.x;
				rot[1] = glm::vec3(model[1]) / ent->transform.scale.y;
				rot[2] = glm::vec3(model[2]) / ent->transform.scale.z;
				ent->transform.rotation = glm::quat_cast(rot);
				vulkanRender->getSceneManager().setEntityTransform(ent->entityId, ent->transform);
			}
		}
		else if (vulkanRender->mainModelSelected) {
			auto& entities = vulkanRender->getSceneManager().getModelEntities();
			if (!entities.empty()) {
				auto& transform = entities.front().transform;
				glm::mat4 model = transform.GetModelMatrix();
				ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, gizmoMode,
					glm::value_ptr(model), nullptr, nullptr);
				transform.position = glm::vec3(model[3]);
				transform.scale = glm::vec3(
					glm::length(glm::vec3(model[0])),
					glm::length(glm::vec3(model[1])),
					glm::length(glm::vec3(model[2])));
				glm::mat3 rot;
				rot[0] = glm::vec3(model[0]) / transform.scale.x;
				rot[1] = glm::vec3(model[1]) / transform.scale.y;
				rot[2] = glm::vec3(model[2]) / transform.scale.z;
				transform.rotation = glm::quat_cast(rot);
				vulkanRender->mainModelTransform = transform;
			}
		}
		else if (vulkanRender->pickedBoxEntityId != 0) {
			glm::vec3 boxPos = vulkanRender->getBoxPosition(vulkanRender->pickedBoxEntityId);
			glm::mat4 model = glm::translate(glm::mat4(1.0f), boxPos);
			ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, gizmoMode,
				glm::value_ptr(model), nullptr, nullptr);
			vulkanRender->setBoxPosition(vulkanRender->pickedBoxEntityId, glm::vec3(model[3]));
		}
	}

    // ── PiP Overlay on main viewport (right-bottom corner) ───────────────────
    if (vulkanRender && vulkanRender->selectedCameraEntityId_ != 0) {
        ImTextureID pipTex = vulkanRender->getPipTextureId();
        if (pipTex != (ImTextureID)0) {
            ImGuiIO& io = ImGui::GetIO();
            const float pipW = io.DisplaySize.x * 0.25f;
            const float pipH = io.DisplaySize.y * 0.25f;
            const float offsetX = io.DisplaySize.x - pipW - 10;
            const float offsetY = io.DisplaySize.y - pipH - 10;

            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            drawList->AddImage(
                pipTex,
                ImVec2(offsetX, offsetY),
                ImVec2(offsetX + pipW, offsetY + pipH),
                ImVec2(0, 0),
                ImVec2(1, 1),
                IM_COL32(255, 255, 255, 255)
            );
            drawList->AddRect(
                ImVec2(offsetX, offsetY),
                ImVec2(offsetX + pipW, offsetY + pipH),
                IM_COL32(100, 200, 255, 200),
                0.0f,
                0,
                2.0f
            );
        }
    }

    ImGui::Render();
}

/** @brief 见 IMGUIManager.hpp：Shutdown ImGui Vulkan/GLFW 并 DestroyContext */
void UIManager::cleanUp()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

/** @brief 加载 res/icons/model.png 作为共享缩略图图标 */
void UIManager::loadThumbnailIcon()
{
    if (!vulkanRender) return;
    const std::string iconPath =
        (std::filesystem::path(applicationResourceRoot()) / "res" / "icons" / "model.png").string();
    thumbnailIcon_ = vulkanRender->loadPNGTexture(iconPath, thumbIconSampler_);
}

/** @brief 扫描 res/materials/ 下的 .ast 文件填充 astAssetFiles_ */
void UIManager::scanMaterialAssets()
{
    astAssetFiles_.clear();
    const std::filesystem::path resRoot =
        std::filesystem::path(applicationResourceRoot()) / "res";
    std::filesystem::path matDir = resRoot / "content";
    std::error_code ec;
    if (!std::filesystem::is_directory(matDir, ec))
        matDir = resRoot / "materials";
    if (std::filesystem::is_directory(matDir, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(matDir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".ast") {
                bool include = true;
                std::ifstream f(entry.path());
                if (f.is_open()) {
                    try {
                        nlohmann::json j;
                        f >> j;
                        include = j.value("type", std::string("Mesh")) != "Anim";
                    } catch (...) {
                        include = true;
                    }
                }
                if (include) {
                    const auto rel = std::filesystem::relative(entry.path(), resRoot, ec);
                    if (!ec) astAssetFiles_.push_back(rel.generic_string());
                }
            }
        }
        std::sort(astAssetFiles_.begin(), astAssetFiles_.end());
    }
    astAssetScanned_ = true;
    if (astAssetIndex_ >= static_cast<int>(astAssetFiles_.size()))
        astAssetIndex_ = 0;
}

// ─── Content Browser ──────────────────────────────────────────────────────

/** @brief 绘制 Content Browser 面板：通过 ModelRegistry 展示模型文件 */
void UIManager::drawContentBrowser()
{
    ModelRegistry* reg = vulkanRender ? &vulkanRender->getModelRegistry() : nullptr;

    ImGui::SetNextWindowSize(ImVec2(460, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Content Browser", &showContentBrowser_)) {
        ImGui::End();
        return;
    }

    // ── 左侧文件夹列表 ──────────────────────────────────────────────────
    ImGui::BeginChild("##cbFolders", ImVec2(130, 0), true);
    ImGui::TextUnformatted("Folders");
    ImGui::Separator();

    // "Root" (/) 按钮
    if (ImGui::Selectable("Content", currentFolder_.empty()))
        currentFolder_.clear();

    if (reg) {
        std::set<std::string> folderSet;
        for (const auto& folder : reg->getFolders()) {
            std::filesystem::path path(folder);
            std::string prefix;
            for (const auto& part : path) {
                if (!prefix.empty()) prefix += "/";
                prefix += part.generic_string();
                folderSet.insert(prefix);
            }
        }

        std::function<void(const std::string&)> drawTree =
            [&](const std::string& parent) {
                for (const auto& folder : folderSet) {
                    const std::filesystem::path folderPath(folder);
                    if (folderPath.parent_path().generic_string() != parent)
                        continue;

                    const std::string childPrefix = folder + "/";
                    bool hasChild = false;
                    for (const auto& candidate : folderSet) {
                        if (candidate.rfind(childPrefix, 0) == 0) {
                            hasChild = true;
                            break;
                        }
                    }

                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                             | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (!hasChild) flags |= ImGuiTreeNodeFlags_Leaf;
                    if (currentFolder_ == folder) flags |= ImGuiTreeNodeFlags_Selected;

                    const std::string label = folderPath.filename().generic_string();
                    const bool open = ImGui::TreeNodeEx(folder.c_str(), flags, "%s", label.c_str());
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                        currentFolder_ = folder;
                    if (open) {
                        if (hasChild) drawTree(folder);
                        ImGui::TreePop();
                    }
                }
            };
        drawTree("");
    }

    ImGui::Separator();

    // 新建文件夹输入
    ImGui::InputTextWithHint("##newFolder", "New folder...", newFolderName_, sizeof(newFolderName_));
    if (ImGui::Button("Create##mkfolder")) {
        std::string name(newFolderName_);
        // trim whitespace
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);
        std::replace(name.begin(), name.end(), '\\', '/');
        while (!name.empty() && name.front() == '/') name.erase(name.begin());
        while (!name.empty() && name.back() == '/') name.pop_back();
        if (!name.empty() && vulkanRender) {
            const std::filesystem::path rel(name);
            if (!rel.is_absolute() && name.find("..") == std::string::npos) {
                // 在当前浏览的子文件夹下创建：currentFolder_ 为空则在 content/ 根
                std::string fullRel = currentFolder_.empty()
                    ? name
                    : (currentFolder_ + "/" + name);
                const std::string dirPath = vulkanRender->getResRoot() + "/content/" + fullRel;
                std::error_code ec;
                std::filesystem::create_directories(dirPath, ec);
                if (!ec) {
                    currentFolder_ = fullRel;
                    newFolderName_[0] = '\0';
                    if (reg) reg->refresh();
                }
            }
        }
    }

    ImGui::EndChild();
    ImGui::SameLine();

    // ── 右侧内容区 ──────────────────────────────────────────────────────
    ImGui::BeginChild("##cbContent", ImVec2(0, 0), false);

    // 当前路径面包屑
    if (currentFolder_.empty())
        ImGui::TextDisabled("/ content /");
    else
        ImGui::TextDisabled("/ content / %s /", currentFolder_.c_str());

    // 搜索框 + 刷新按钮 + 导入按钮
    ImGui::InputTextWithHint("##cbSearch", "Search...", contentBrowserSearch_, sizeof(contentBrowserSearch_));
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        // 清除缩略图缓存，让下次显示重新从磁盘加载新生成的 PNG
        thumbCache_.clear();
        thumbSamplers_.clear();
        if (reg) reg->refresh();
        if (vulkanRender) vulkanRender->refreshAllThumbnails();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import...")) {
#ifdef _WIN32
        if (vulkanRender) {
            const std::string selected = openFileDialog(vulkanRender->getMainWindow());
            if (!selected.empty()) {
                std::string animFbxPath;
                if (vulkanRender->importModel(selected, currentFolder_, &animFbxPath)) {
                    if (reg) reg->refresh();
                    vulkanRender->refreshAllThumbnails();
                } else if (!animFbxPath.empty()) {
                    // Without-skin 动画 FBX → 弹出 mesh 选择
                    importAnimFbxPath_ = animFbxPath;
                    meshPickerSelected_ = 0;
                    showMeshPicker_ = true;
                }
            }
        }
#else
        std::cerr << "[Import] not supported on this platform\n";
#endif
    }

    // 类型筛选下拉框
    const char* typeItems[] = { "All", "Mesh", "Box", "Material", "Anim" };
    {
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("##typeFilter", &typeFilterIdx_, typeItems, IM_ARRAYSIZE(typeItems));
    }

    // 过滤模型列表（限定当前文件夹 + 关键词）
    std::vector<const ModelAsset*> filteredRaw;
    if (reg)
        filteredRaw = reg->search(contentBrowserSearch_, currentFolder_);

    // 按类型筛选
    std::vector<const ModelAsset*> filtered;
    for (const auto* a : filteredRaw) {
        if (typeFilterIdx_ == 0) {
            filtered.push_back(a);
        } else {
            const char* want = typeItems[typeFilterIdx_];
            if (a->astType == want)
                filtered.push_back(a);
        }
    }

    ImGui::Separator();

    // 缩略图网格
    const float thumbSize = 72.f;
    const float cellWidth = thumbSize + 8.f;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));

    ImGui::BeginChild("##cbGrid", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    int n = 0;
    for (const auto* asset : filtered) {
        if (n > 0 && n % columns != 0)
            ImGui::SameLine();

        ImGui::BeginGroup();

        // 缩略图：优先专用纹理，其次共享占位图标，回退纯色方块
        ImTextureID texId = (ImTextureID)0;

        // 1. 尝试缓存命中
        auto cacheIt = thumbCache_.find(asset->id);
        if (cacheIt != thumbCache_.end()) {
            texId = cacheIt->second;
        }
        // 2. 尝试从磁盘加载专属缩略图
        else if (reg) {
            const std::string stem = std::filesystem::path(asset->astRelPath).stem().string();
            const std::string thumbPath = reg->getResRoot() + "/thumbnails/" + stem + ".png";
            if (std::filesystem::exists(thumbPath) && vulkanRender) {
                VkSampler sampler{};
                ImTextureID loaded = vulkanRender->loadPNGTexture(thumbPath, sampler);
                if (loaded) {
                    thumbCache_[asset->id] = loaded;
                    thumbSamplers_[asset->id] = sampler;
                    texId = loaded;
                }
            }
        }

        if (texId) {
            ImGui::ImageButton(("##thumb" + std::to_string(n)).c_str(),
                               texId, ImVec2(thumbSize, thumbSize));
        } else if (thumbnailIcon_) {
            ImGui::ImageButton(("##thumb" + std::to_string(n)).c_str(),
                               thumbnailIcon_, ImVec2(thumbSize, thumbSize));
        } else {
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImU32 col = IM_COL32(60, 90, 140, 255);
            ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + thumbSize, p0.y + thumbSize), col, 4.f);
            ImGui::InvisibleButton(("##thumb" + std::to_string(n)).c_str(), ImVec2(thumbSize, thumbSize));
        }

        // 拖拽源：Material 类型资产（无 model 引用）不参与拖拽放置
        if (asset->astType != "Material" && asset->astType != "Anim") {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                uint64_t id = asset->id;
                ImGui::SetDragDropPayload("MODEL_ASSET", &id, sizeof(uint64_t));
                ImGui::Text("%s", asset->name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        const ImVec4 typeColor =
            asset->astType == "Mesh" ? ImVec4(0.35f, 0.72f, 1.00f, 1.0f) :
            asset->astType == "Anim" ? ImVec4(1.00f, 0.72f, 0.30f, 1.0f) :
            asset->astType == "Material" ? ImVec4(0.55f, 0.88f, 0.55f, 1.0f) :
            ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
        ImGui::TextColored(typeColor, "%s", asset->astType.c_str());

        // 文件名
        const std::string displayName = asset->name.size() > 16
            ? asset->name.substr(0, 14) + ".." : asset->name;
        ImGui::TextWrapped("%s", displayName.c_str());

        ImGui::EndGroup();
        ++n;
    }

    ImGui::EndChild();

    // 状态栏
    ImGui::Separator();

    if (vulkanRender) {
        ImGui::SliderFloat("Place Distance", &placementDistance_, 1.0f, 50.0f);
        vulkanRender->dragPlace.distance = placementDistance_;
    }

    if (reg)
        ImGui::Text("Assets: %zu", reg->size(currentFolder_));
    else
        ImGui::TextDisabled("(unavailable)");

    ImGui::EndChild();
    ImGui::End();

    // ── Import Anim: 目标 mesh 选择弹窗 ──────────────────────────────────
    if (showMeshPicker_ && reg && !importAnimFbxPath_.empty()) {
        ImGui::OpenPopup("Select Target Mesh");
        ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Select Target Mesh", &showMeshPicker_,
                                   ImGuiWindowFlags_None)) {
            ImGui::Text("Animation FBX: %s", importAnimFbxPath_.c_str());
            ImGui::Separator();
            ImGui::Text("Select the mesh asset to bind this animation to:");
            ImGui::Separator();

            // Collect Mesh-type assets
            const auto& all = reg->getAll();
            std::vector<const ModelAsset*> meshes;
            for (const auto& a : all) {
                if (a.astType == "Mesh")
                    meshes.push_back(&a);
            }

            if (meshes.empty()) {
                ImGui::TextDisabled("No mesh assets found. Import a mesh first.");
            } else {
                ImGui::BeginChild("##meshPickerList", ImVec2(0, -50), true);
                for (int i = 0; i < static_cast<int>(meshes.size()); ++i) {
                    if (ImGui::Selectable(meshes[i]->name.c_str(), i == meshPickerSelected_))
                        meshPickerSelected_ = i;
                }
                ImGui::EndChild();

                if (ImGui::Button("Import", ImVec2(120, 0))) {
                    const std::string& targetAst = meshes[meshPickerSelected_]->astRelPath;
                    if (vulkanRender) {
                        const bool ok = vulkanRender->importAnimationFbx(
                            importAnimFbxPath_, targetAst);
                        if (ok) {
                            if (reg) reg->refresh();
                        }
                    }
                    showMeshPicker_ = false;
                    importAnimFbxPath_.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    showMeshPicker_ = false;
                    importAnimFbxPath_.clear();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }
}

// ─── Scene Outliner & Properties ────────────────────────────────────────────

void UIManager::drawSceneOutliner()
{
    ImGui::SetNextWindowSize(ImVec2(260, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Outliner", &showOutliner_)) {
        ImGui::End();
        return;
    }

    if (!vulkanRender) {
        ImGui::TextDisabled("(unavailable)");
        ImGui::End();
        return;
    }

    auto& scene = vulkanRender->getSceneManager();

    ImGui::Text("Entities");
    ImGui::Separator();

    // 模型实体列表
    const auto& ents = scene.getModelEntities();
    for (size_t i = 0; i < ents.size(); ++i) {
        const auto& ent = ents[i];
        
        if (ent.isCamera()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        }
        
        const char* name = ent.displayName.empty() ? "(unnamed)" : ent.displayName.c_str();
        char label[256];
        snprintf(label, sizeof(label), "%s##ent%llu", name,
                 static_cast<unsigned long long>(ent.entityId));

        if (ImGui::Selectable(label, selectedEntityId_ == ent.entityId)) {
            selectedEntityId_ = ent.entityId;
            vulkanRender->mainModelSelected = (i == 0);
            vulkanRender->pickedBoxEntityId = 0;
            vulkanRender->selectedCameraEntityId_ = ent.isCamera() ? ent.entityId : 0;
        }
        
        if (ent.isCamera()) {
            ImGui::PopStyleColor();
        }
    }

    // Box 实例列表
    const auto& boxes = scene.getBoxes();
    if (!boxes.empty()) {
        ImGui::Separator();
        ImGui::Text("Boxes");
        for (const auto& kv : boxes) {
            char label[64];
            snprintf(label, sizeof(label), "Box_%llu##box%llu",
                     static_cast<unsigned long long>(kv.first),
                     static_cast<unsigned long long>(kv.first));

            if (ImGui::Selectable(label, selectedEntityId_ == 0
                                         && vulkanRender->pickedBoxEntityId == kv.first)) {
                selectedEntityId_ = 0;
                vulkanRender->mainModelSelected = false;
                vulkanRender->pickedBoxEntityId = kv.first;
            }
        }
    }

    // 底部操作按钮
    ImGui::Separator();
    if (ImGui::Button("Delete Selected")) {
        if (selectedEntityId_ != 0 && vulkanRender) {
            vulkanRender->deleteModelEntity(selectedEntityId_);
            selectedEntityId_ = 0;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Box")) {
        if (vulkanRender) {
            auto eid = vulkanRender->addBox(glm::vec3(0.f));
            vulkanRender->pickedBoxEntityId = eid;
            selectedEntityId_ = 0;
        }
    }

    ImGui::End();
}

// ─── PiP (Picture-in-Picture) Window ────────────────────────────────────────

void UIManager::drawPipWindow()
{
    ImGui::SetNextWindowSize(ImVec2(360, 300), ImGuiCond_FirstUseEver);
    bool windowOpen = true;
    if (!ImGui::Begin("PiP Camera", &windowOpen)) {
        if (vulkanRender) vulkanRender->setPipActive(false);
        ImGui::End();
        return;
    }
    
    if (!windowOpen) {
        showPipWindow_ = false;
    }

    if (!vulkanRender) {
        ImGui::TextDisabled("(unavailable)");
        ImGui::End();
        return;
    }

    vulkanRender->setPipActive(true);

    auto& pipCam = vulkanRender->getPipCamera();
    
    if (vulkanRender->selectedCameraEntityId_ != 0) {
        auto* camEnt = vulkanRender->getSceneManager().getModelEntity(vulkanRender->selectedCameraEntityId_);
        if (camEnt && camEnt->isCamera()) {
            ImGui::Text("Camera Actor: %s", camEnt->displayName.c_str());
            ImGui::Separator();
            
            ImGui::DragFloat("FOV", &camEnt->cameraData.fovDeg, 1.0f, 10.0f, 120.0f);
            ImGui::DragFloat("Near Plane", &camEnt->cameraData.nearPlane, 0.01f, 0.01f, 10.0f);
            ImGui::DragFloat("Far Plane", &camEnt->cameraData.farPlane, 1.0f, 10.0f, 1000.0f);
            ImGui::Checkbox("Preview Enabled", &camEnt->cameraPreviewEnabled);
            
            ImGui::Separator();
            ImGui::Text("Use Gizmo to move/rotate");
        } else {
            ImGui::TextDisabled("Camera entity not found");
        }
    } else {
        ImGui::Text("PiP Camera Properties");
        ImGui::Separator();

        if (ImGui::Button("Sync from Main Camera")) {
            pipCam.syncFromMainCamera(vulkanRender->getCamera());
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Default")) {
            pipCam.position    = glm::vec3(0.f, 0.f, 5.f);
            pipCam.orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
            pipCam.fovDeg      = 45.f;
            pipCam.aspectRatio = static_cast<float>(pipWindowWidth_) / static_cast<float>(pipWindowHeight_);
        }

        float pos[3] = { pipCam.position.x, pipCam.position.y, pipCam.position.z };
        if (ImGui::DragFloat3("Position", pos, 0.1f))
            pipCam.position = glm::vec3(pos[0], pos[1], pos[2]);

        glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(pipCam.orientation));
        float eulerArr[3] = { eulerAngles.x, eulerAngles.y, eulerAngles.z };
        if (ImGui::DragFloat3("Rotation (deg)", eulerArr, 1.0f)) {
            glm::vec3 rad = glm::radians(glm::vec3(eulerArr[0], eulerArr[1], eulerArr[2]));
            pipCam.orientation = glm::quat(rad);
        }

        ImGui::DragFloat("FOV", &pipCam.fovDeg, 1.0f, 10.0f, 120.0f);

        const float fixedAspect = static_cast<float>(pipWindowWidth_) / static_cast<float>(pipWindowHeight_);
        pipCam.aspectRatio = fixedAspect;
        ImGui::Text("Aspect (locked to framebuffer): %.3f", fixedAspect);
    }

    ImGui::Separator();
    ImGui::Text("Resolution: %dx%d", pipWindowWidth_, pipWindowHeight_);

    ImTextureID texId = vulkanRender->getPipTextureId();
    if (texId != (ImTextureID)0) {
        const float aspect = pipCam.aspectRatio;
        const float availW = ImGui::GetContentRegionAvail().x;
        const float availH = ImGui::GetContentRegionAvail().y;
        float imgW = availW;
        float imgH = imgW / aspect;
        if (imgH > availH) {
            imgH = availH;
            imgW = imgH * aspect;
        }

        const ImVec2 imgSize(imgW, imgH);
        ImGui::Image(texId, imgSize,
                     ImVec2(0, 0), ImVec2(1, 1),
                     ImVec4(1, 1, 1, 1),
                     ImVec4(0.5f, 0.5f, 0.5f, 1.f));
    } else {
        ImGui::TextDisabled("(PiP texture not ready yet)");
    }

    ImGui::End();
}

// ─── Sequencer Panel ─────────────────────────────────────────────────────────

void UIManager::scanSequenceAssets()
{
    sequenceAssets_.clear();
    sequenceAssetSelected_ = -1;
    if (!vulkanRender) return;

    const std::filesystem::path root =
        std::filesystem::path(vulkanRender->getResRoot()) / "sequences";
    std::error_code errorCode;
    if (!std::filesystem::is_directory(root, errorCode)) return;

    constexpr const char* suffix = ".seq.json";
    constexpr size_t suffixLength = 9;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(root, options, errorCode);
    const std::filesystem::recursive_directory_iterator end;
    while (it != end) {
        if (errorCode) {
            errorCode.clear();
            it.increment(errorCode);
            continue;
        }

        const auto path = it->path();
        if (it->is_regular_file(errorCode)) {
            std::string filename = path.filename().string();
            std::string lowerFilename = filename;
            std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowerFilename.size() >= suffixLength
                && lowerFilename.compare(lowerFilename.size() - suffixLength,
                                         suffixLength, suffix) == 0) {
                SequenceAssetEntry entry;
                entry.absolutePath = path.string();
                std::error_code relativeError;
                entry.relativePath = std::filesystem::relative(path, root, relativeError).generic_string();
                if (relativeError) entry.relativePath = filename;

                Sequence preview;
                entry.valid = SequenceAssetLoader::loadSequence(entry.absolutePath, preview, &entry.error);
                entry.displayName = entry.valid && !preview.name.empty()
                    ? preview.name
                    : filename.substr(0, filename.size() - suffixLength);
                if (entry.valid) {
                    entry.duration = preview.totalDuration > 0.0
                        ? preview.totalDuration : preview.computeTotalDuration();
                    entry.trackCount = preview.tracks.size();
                }
                sequenceAssets_.push_back(std::move(entry));
            }
        }
        errorCode.clear();
        it.increment(errorCode);
    }

    std::sort(sequenceAssets_.begin(), sequenceAssets_.end(),
              [](const SequenceAssetEntry& lhs, const SequenceAssetEntry& rhs) {
                  return lhs.relativePath < rhs.relativePath;
              });
}

void UIManager::drawSequencerPanel()
{
    ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Sequencer", &showSequencerPanel_)) {
        ImGui::End();
        return;
    }

    if (!vulkanRender) {
        ImGui::TextDisabled("(unavailable)");
        ImGui::End();
        return;
    }

    auto& seqPlayer = vulkanRender->getSequencePlayer();
    Sequence& sequence = vulkanRender->getCurrentSequence();

    const double totalDuration = sequence.totalDuration > 0.0
        ? sequence.totalDuration
        : sequence.computeTotalDuration();
    const double currentTime = seqPlayer.currentTime();

    static int selectedTrackIdx = -1;
    static int selectedShotKeyIdx = -1;
    static int selectedClipType = -1;
    static int selectedClipIdx = -1;
    static char seqNameBuf[256] = "NewSequence";
    static bool loopPlayback = false;
    static float zoomLevel = 1.0f;
    constexpr int kCameraShotTrackSelection = -2;

    const float timelineHeight = 30.0f;
    const float trackLabelWidth = 180.0f;
    const float trackHeight = 50.0f;
    const float pixelsPerSecond = 100.0f * zoomLevel;



    ImGui::Text("Sequencer");
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    ImGui::SetNextItemWidth(150);
    ImGui::InputText("##seqName", seqNameBuf, sizeof(seqNameBuf));
    ImGui::Separator();

    bool sequencerControl = vulkanRender->isSequencerControlEnabled();
    ImGui::BeginDisabled(vulkanRender->isSequenceCaptureActive());
    if (ImGui::Checkbox("Sequencer Control", &sequencerControl)) {
        vulkanRender->setSequencerControlEnabled(sequencerControl);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "When enabled, Sequencer tracks may drive bound scene objects.\n"
            "When disabled, the playhead can still move without changing the scene.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled(sequencerControl
        ? "(scene output enabled)"
        : "(transport only; scene output disabled)");
    ImGui::Separator();

    if (seqPlayer.isPlaying()) {
        if (ImGui::Button("Pause"))
            seqPlayer.pause();
    } else {
        if (ImGui::Button("Play"))
            seqPlayer.play(loopPlayback);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
        seqPlayer.stop();
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &loopPlayback);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::DragFloat("Zoom", &zoomLevel, 0.1f, 0.1f, 5.0f, "%.1fx");
    ImGui::SameLine();
    ImGui::Text("Time: %.2f / %.2f s", currentTime, totalDuration);
    ImGui::SameLine();

    static char seqDurationBuf[16];
    snprintf(seqDurationBuf, sizeof(seqDurationBuf), "%.1f", sequence.totalDuration > 0.0 ? sequence.totalDuration : totalDuration);
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputText("Dur##seqDur", seqDurationBuf, sizeof(seqDurationBuf))) {
        double newDur = atof(seqDurationBuf);
        if (newDur > 0.0) sequence.totalDuration = newDur;
    }
    ImGui::SameLine();

    if (ImGui::Button("Load##seq")) {
        sequenceAssetSearch_[0] = '\0';
        scanSequenceAssets();
        ImGui::OpenPopup("Load Sequence");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save##seq")) {
        sequence.name = seqNameBuf;
        std::string path = vulkanRender->getResRoot() + "/sequences/" + std::string(seqNameBuf) + ".seq.json";
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        if (SequenceAssetLoader::saveSequence(path, sequence)) {
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Saved sequence: %s", seqNameBuf);
        }
    }

    ImGui::SetNextWindowSize(ImVec2(680.f, 430.f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Load Sequence", nullptr, ImGuiWindowFlags_None)) {
        ImGui::SetNextItemWidth(-90.f);
        if (ImGui::InputTextWithHint("##sequenceSearch", "Search sequence name or path...",
                                     sequenceAssetSearch_, sizeof(sequenceAssetSearch_))) {
            sequenceAssetSelected_ = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) scanSequenceAssets();
        ImGui::Separator();

        std::string search = sequenceAssetSearch_;
        std::transform(search.begin(), search.end(), search.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool requestLoad = false;
        ImGui::BeginChild("##sequenceAssetList", ImVec2(0.f, -58.f), true);
        if (sequenceAssets_.empty()) {
            ImGui::TextDisabled("No .seq.json files found under res/sequences.");
        }
        for (int index = 0; index < static_cast<int>(sequenceAssets_.size()); ++index) {
            auto& entry = sequenceAssets_[index];
            std::string searchable = entry.displayName + " " + entry.relativePath;
            std::transform(searchable.begin(), searchable.end(), searchable.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!search.empty() && searchable.find(search) == std::string::npos) continue;

            ImGui::PushID(index);
            if (!entry.valid)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.35f, 0.35f, 1.f));
            const bool activated = ImGui::Selectable(
                entry.displayName.c_str(), sequenceAssetSelected_ == index,
                ImGuiSelectableFlags_AllowDoubleClick, ImVec2(220.f, 0.f));
            if (!entry.valid) ImGui::PopStyleColor();
            if (activated) {
                sequenceAssetSelected_ = index;
                requestLoad = entry.valid && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            }
            ImGui::SameLine();
            if (entry.valid) {
                ImGui::TextDisabled("%s   %.2fs   %zu tracks",
                                    entry.relativePath.c_str(), entry.duration, entry.trackCount);
            } else {
                ImGui::TextDisabled("%s   invalid", entry.relativePath.c_str());
            }
            if (ImGui::IsItemHovered() && !entry.valid && !entry.error.empty()) {
                ImGui::BeginTooltip();
                ImGui::TextWrapped("%s", entry.error.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        const bool canLoad = sequenceAssetSelected_ >= 0
            && sequenceAssetSelected_ < static_cast<int>(sequenceAssets_.size())
            && sequenceAssets_[sequenceAssetSelected_].valid;
        ImGui::BeginDisabled(!canLoad);
        if (ImGui::Button("Load Selected")) requestLoad = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

        if (requestLoad && canLoad) {
            auto& entry = sequenceAssets_[sequenceAssetSelected_];
            std::string loadError;
            Application::SequenceBindingReport bindingReport;
            if (vulkanRender->loadSequenceAsset(entry.absolutePath, &loadError,
                                                &bindingReport)) {
                const std::string loadedName = sequence.name.empty()
                    ? entry.displayName : sequence.name;
                snprintf(seqNameBuf, sizeof(seqNameBuf), "%s", loadedName.c_str());
                selectedTrackIdx = -1;
                selectedShotKeyIdx = -1;
                selectedClipType = -1;
                selectedClipIdx = -1;
                selectedKeyframeIdx_ = -1;
                sequencerEditTime_ = 0.0;
                sequencerEditTimeSet_ = false;
                seqTimelineScrollY_ = 0.f;
                loopPlayback = false;
                sequenceRebindTrackIndices_ = std::move(bindingReport.unresolvedTrackIndices);
                sequenceRebindEntityIds_.assign(sequenceRebindTrackIndices_.size(), uint64_t(0));
                sequenceRebindPopupPending_ = !sequenceRebindTrackIndices_.empty();
                const size_t unresolvedTotal =
                    sequenceRebindTrackIndices_.size()
                    + bindingReport.unresolvedCameraShotKeyIndices.size();
                if (sequenceRebindPopupPending_ || unresolvedTotal > 0) {
                    snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                             "Loaded %s: rebound %zu track(s), %zu shot key(s); %zu target(s) unresolved",
                             entry.relativePath.c_str(), bindingReport.reboundTrackCount,
                             bindingReport.reboundCameraShotKeyCount, unresolvedTotal);
                } else {
                    snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                             "Loaded %s: rebound %zu track(s), %zu shot key(s)",
                             entry.relativePath.c_str(), bindingReport.reboundTrackCount,
                             bindingReport.reboundCameraShotKeyCount);
                }
                ImGui::CloseCurrentPopup();
            } else {
                entry.valid = false;
                entry.error = loadError;
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Load failed: %s", loadError.c_str());
            }
        }
        ImGui::EndPopup();
    }

    if (sequenceRebindPopupPending_) {
        ImGui::OpenPopup("Rebind Sequence Tracks");
        sequenceRebindPopupPending_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(620.f, 360.f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rebind Sequence Tracks", nullptr, ImGuiWindowFlags_None)) {
        ImGui::TextWrapped("Some tracks no longer identify a unique scene entity. Select a target for each track.");
        ImGui::Separator();

        const auto& entities = vulkanRender->getSceneManager().getModelEntities();
        bool allResolved = !sequenceRebindTrackIndices_.empty();
        ImGui::BeginChild("##sequenceRebindList", ImVec2(0.f, -48.f), true);
        for (size_t issueIndex = 0; issueIndex < sequenceRebindTrackIndices_.size(); ++issueIndex) {
            const size_t trackIndex = sequenceRebindTrackIndices_[issueIndex];
            if (trackIndex >= sequence.tracks.size()) {
                allResolved = false;
                continue;
            }
            ImGui::PushID(static_cast<int>(issueIndex));
            ImGui::TextWrapped("%s", sequence.tracks[trackIndex].name.c_str());

            const uint64_t selectedId = sequenceRebindEntityIds_[issueIndex];
            const char* preview = "(select entity)";
            for (const auto& entity : entities) {
                if (entity.entityId == selectedId) {
                    preview = entity.displayName.c_str();
                    break;
                }
            }
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::BeginCombo("##targetEntity", preview)) {
                for (const auto& entity : entities) {
                    const bool selected = entity.entityId == selectedId;
                    const std::string label = entity.displayName.empty()
                        ? "Entity " + std::to_string(entity.entityId)
                        : entity.displayName + "  [" + std::to_string(entity.entityId) + "]";
                    if (ImGui::Selectable(label.c_str(), selected))
                        sequenceRebindEntityIds_[issueIndex] = entity.entityId;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (!vulkanRender->getSceneManager().getModelEntity(
                    sequenceRebindEntityIds_[issueIndex])) {
                allResolved = false;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::BeginDisabled(!allResolved);
        if (ImGui::Button("Apply Bindings")) {
            bool applied = true;
            for (size_t i = 0; i < sequenceRebindTrackIndices_.size(); ++i) {
                applied = vulkanRender->bindSequenceTrack(sequenceRebindTrackIndices_[i],
                                                           sequenceRebindEntityIds_[i]) && applied;
            }
            if (applied) {
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Rebound %zu sequence track target(s)",
                         sequenceRebindTrackIndices_.size());
                sequenceRebindTrackIndices_.clear();
                sequenceRebindEntityIds_.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Keep Unresolved")) {
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                     "%zu sequence track target(s) remain unresolved",
                     sequenceRebindTrackIndices_.size());
            sequenceRebindTrackIndices_.clear();
            sequenceRebindEntityIds_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // ── 计算每条轨道的行高（左右两列共用） ────────────────────────────
    // 当前 .seq.json 没有 parentTrackId；一个 Group 的子轨道定义为其后直到
    // 下一个 Group 之前的连续范围。左右两列和删除逻辑必须使用同一层级规则。
    auto isSubTrack = [&](int ti) -> bool {
        if (sequence.tracks[ti].type == TrackType::Group)
            return false;
        for (int previous = ti - 1; previous >= 0; --previous) {
            if (sequence.tracks[previous].type == TrackType::Group)
                return true;
        }
        return false;
    };
    auto isParentTrack = [&](int ti) -> bool {
        const auto& track = sequence.tracks[ti];
        return track.type == TrackType::Group;
    };
    auto getRowHeight = [&](int ti) -> float {
        return isParentTrack(ti) ? trackHeight * 0.5f : trackHeight;
    };

    // ── 双列布局：左标签列（固定，不随水平滚动移动）+ 右时间线列（水平滚动） ──
    // 解决单列布局中同一 Y 行混用 InvisibleButton(标签)+Dummy(时间线) 导致的
    // hover/点击不稳定（根因：同行多交互 item 抢占 HoveredId，且标签列随滚动滑走），
    // 同时让刻度尺与关键帧共用同一 contentOriginX，根除对齐漂移（Bug 1 + Bug 2）。
    static float parameterPanelHeight = 240.0f;
    constexpr float panelSplitterHeight = 8.0f;
    const float availablePanelHeight = ImGui::GetContentRegionAvail().y;
    const float minimumTimelineHeight =
        std::min(140.0f, availablePanelHeight * 0.40f);
    const float minimumParameterHeight =
        std::min(180.0f, availablePanelHeight * 0.45f);
    const float maximumParameterHeight = std::max(
        minimumParameterHeight,
        availablePanelHeight - minimumTimelineHeight - panelSplitterHeight);
    parameterPanelHeight = std::clamp(
        parameterPanelHeight,
        minimumParameterHeight,
        maximumParameterHeight);
    const float editorHeight = std::max(
        80.0f,
        availablePanelHeight - parameterPanelHeight - panelSplitterHeight);
    const float labelColWidth = trackLabelWidth;
    const float timeColWidth  = std::max(ImGui::GetContentRegionAvail().x - labelColWidth, 1.0f);

    // 两列共用同一套行高，保证垂直像素偏移一致（滚动同步的前提）
    float tracksContentHeight = trackHeight;
    for (int ti = 0; ti < static_cast<int>(sequence.tracks.size()); ++ti)
        tracksContentHeight += getRowHeight(ti);

    // ── 左列：轨道标签（无水平滚动，垂直滚动跟随右列） ───────────────
    ImGui::BeginChild("##seqLabels", ImVec2(labelColWidth, editorHeight), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // 跟随右列上一帧的垂直滚动位置（1 帧延迟，用户不可见）
    ImGui::SetScrollY(seqTimelineScrollY_);
    {
        ImDrawList* lblDL = ImGui::GetWindowDrawList();
        // 顶部留白对齐刻度尺行
        ImGui::Dummy(ImVec2(labelColWidth, timelineHeight));
        // 刻度尺底部对齐分隔线
        ImVec2 sp = ImGui::GetCursorScreenPos();
        lblDL->AddLine(ImVec2(sp.x, sp.y), ImVec2(sp.x + labelColWidth, sp.y), IM_COL32(80, 80, 80, 255));
        // 行 Y 独立累加，与右列 ##seqTimeline 严格一致：
        // Selectable 自然推进会叠加 Style.ItemSpacing.y（默认 8px），逐行比右列多出该间距，
        // 轨道越多累积偏移越大。显式 SetCursorScreenPos 定位可绕过 ItemSpacing。
        float lblRowCursorY = sp.y;
        const float lblContentX = sp.x;

        bool shotBindingMissing = false;
        if (sequence.cameraShotTrack) {
            for (const auto& key : sequence.cameraShotTrack->keyframes) {
                const auto* entity =
                    vulkanRender->getSceneManager().getModelEntity(key.cameraEntityId);
                if (!entity || !entity->isCamera()) {
                    shotBindingMissing = true;
                    break;
                }
            }
        }
        ImGui::PushID("CameraShotGlobalLabel");
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.45f, 0.28f, 0.08f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.55f, 0.38f, 0.12f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.65f, 0.42f, 0.12f, 0.7f));
        ImGui::SetCursorScreenPos(ImVec2(lblContentX, lblRowCursorY));
        if (ImGui::Selectable(
                "##cameraShotTrack",
                selectedTrackIdx == kCameraShotTrackSelection, 0,
                ImVec2(labelColWidth, trackHeight))) {
            selectedTrackIdx = kCameraShotTrackSelection;
            selectedShotKeyIdx = -1;
            selectedClipType = -1;
            selectedClipIdx = -1;
            selectedKeyframeIdx_ = -1;
        }
        ImGui::PopStyleColor(3);
        {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImU32 color = shotBindingMissing
                ? IM_COL32(255, 64, 64, 255)
                : IM_COL32(245, 174, 68, 255);
            lblDL->AddText(ImVec2(mn.x + 4.f, mn.y + 5.f), color,
                           "Camera / Shots  [Global]");
            if (shotBindingMissing) {
                lblDL->AddText(
                    ImVec2(mn.x + 4.f, mn.y + 5.f + ImGui::GetTextLineHeight()),
                    color, "Camera Missing");
            }
        }
        ImGui::PopID();
        lblRowCursorY += trackHeight;

        for (int ti = 0; ti < static_cast<int>(sequence.tracks.size()); ++ti) {
            const auto& track = sequence.tracks[ti];
            bool parent = isParentTrack(ti);
            bool sub = isSubTrack(ti);
            bool selected = (ti == selectedTrackIdx);
            float rowH = getRowHeight(ti);

            ImVec4 trackColor = (track.type == TrackType::Group) ? ImVec4(0.65f, 0.65f, 0.65f, 1.0f) :
                                (track.type == TrackType::AnimationClip) ? ImVec4(0.3f, 0.6f, 0.9f, 1.0f) :
                                (track.type == TrackType::CameraPath) ? ImVec4(0.9f, 0.6f, 0.3f, 1.0f) :
                                (track.type == TrackType::TransformTween) ? ImVec4(0.4f, 0.4f, 0.4f, 1.0f) :
                                (track.type == TrackType::TransformKeyframe) ? ImVec4(0.9f, 0.9f, 0.3f, 1.0f) :
                                (track.type == TrackType::AnimatorKeyframe) ? ImVec4(0.9f, 0.3f, 0.9f, 1.0f) :
                                ImVec4(0.9f, 0.3f, 0.3f, 1.0f);

            // 用 Selectable 提供可靠的 hover/click，替代 InvisibleButton+Dummy 同行混用
            ImGui::PushID(ti + 100000);
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.4f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.3f, 0.5f, 0.8f, 0.6f));
            // 显式定位到累加 Y，绕过 ItemSpacing，与右列每行 Y 完全对齐
            ImGui::SetCursorScreenPos(ImVec2(lblContentX, lblRowCursorY));
            if (ImGui::Selectable("##trackLbl", selected, 0,
                                  ImVec2(labelColWidth, rowH))) {
                selectedTrackIdx = ti;
                selectedClipType = -1;
                selectedClipIdx = -1;
                selectedKeyframeIdx_ = -1;
            }
            ImGui::PopStyleColor(3);
            // 在 Selectable 上叠加居中文字（保持原视觉风格）
            {
                ImVec2 mn = ImGui::GetItemRectMin();
                bool targetMissing = false;
                if (track.type == TrackType::TransformKeyframe)
                    targetMissing = !vulkanRender->getSceneManager().getModelEntity(track.keyframeTrack.targetEntityId);
                else if (track.type == TrackType::AnimatorKeyframe)
                    targetMissing = !vulkanRender->getSceneManager().getModelEntity(track.animatorTrack.targetEntityId);
                ImU32 txtCol = ImGui::ColorConvertFloat4ToU32(
                    targetMissing ? ImVec4(1.f, 0.25f, 0.25f, 1.f)
                                  : (parent ? ImVec4(1, 1, 1, 0.6f) : trackColor));
                float tx = mn.x + 4.0f + (sub ? 12.0f : 0.0f);
                float ty = targetMissing ? mn.y + 5.f
                                         : mn.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                lblDL->AddText(ImVec2(tx, ty), txtCol, track.name.c_str());
                if (targetMissing) {
                    lblDL->AddText(ImVec2(tx, ty + ImGui::GetTextLineHeight()), txtCol,
                                   "Target Missing");
                }
            }
            ImGui::PopID();
            lblRowCursorY += rowH;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ── 右列：时间线（水平滚动 + 垂直滚动，作为垂直滚动主控） ─────────
    ImGui::BeginChild("##seqTimeline", ImVec2(timeColWidth, editorHeight), true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float timelineWidth = std::max(static_cast<float>(totalDuration * pixelsPerSecond + 200.0),
                                         ImGui::GetContentRegionAvail().x);
    // contentOriginX 每帧在时间线窗口内取值，天然含水平滚动偏移；
    // 刻度尺与所有轨道行共用同一原点，根除对齐漂移
    const float contentOriginX = ImGui::GetCursorScreenPos().x;
    auto timeToX = [&](double t) { return contentOriginX + static_cast<float>(t) * pixelsPerSecond; };

    // ── 刻度尺行 ──────────────────────────────────────────────────────
    {
        ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const double rulerDuration = std::max(totalDuration, 0.0);
        const int lastWholeSecond =
            static_cast<int>(std::floor(rulerDuration));
        for (int sec = 0; sec <= lastWholeSecond; ++sec) {
            float x = timeToX(static_cast<double>(sec));
            drawList->AddLine(ImVec2(x, rowPos.y), ImVec2(x, rowPos.y + timelineHeight), IM_COL32(100, 100, 100, 255));
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", sec);
            drawList->AddText(ImVec2(x + 2, rowPos.y + 2), IM_COL32(200, 200, 200, 255), buf);
        }

        const float rulerSpan = timelineHeight + tracksContentHeight;
        const bool hasFractionalEnd =
            rulerDuration - static_cast<double>(lastWholeSecond) > 1e-6;
        if (hasFractionalEnd) {
            const float endX = timeToX(rulerDuration);
            drawList->AddLine(
                ImVec2(endX, rowPos.y),
                ImVec2(endX, rowPos.y + rulerSpan),
                IM_COL32(130, 130, 130, 220), 1.0f);

            char durationLabel[32];
            snprintf(durationLabel, sizeof(durationLabel), "%.3f", rulerDuration);
            std::string trimmedLabel = durationLabel;
            while (!trimmedLabel.empty() && trimmedLabel.back() == '0')
                trimmedLabel.pop_back();
            if (!trimmedLabel.empty() && trimmedLabel.back() == '.')
                trimmedLabel.pop_back();
            drawList->AddText(
                ImVec2(endX + 2.f, rowPos.y + 2.f),
                IM_COL32(220, 220, 220, 255), trimmedLabel.c_str());
        }
        if (totalDuration > 0.0) {
            float playheadX = timeToX(currentTime);
            drawList->AddLine(ImVec2(playheadX, rowPos.y), ImVec2(playheadX, rowPos.y + rulerSpan), IM_COL32(255, 50, 50, 255), 2.0f);
        }
        if (sequencerEditTimeSet_) {
            float editX = timeToX(sequencerEditTime_);
            drawList->AddLine(ImVec2(editX, rowPos.y), ImVec2(editX, rowPos.y + rulerSpan), IM_COL32(255, 255, 0, 200), 1.0f);
        }

        // 刻度尺拖动区域：激活后即使鼠标离开 ruler 仍由 ImGui 保持捕获。
        ImGui::InvisibleButton("##timelineRuler", ImVec2(timelineWidth, timelineHeight));
        if (ImGui::IsItemActivated() && seqPlayer.isPlaying())
            seqPlayer.pause();
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            float mouseX = ImGui::GetMousePos().x - contentOriginX;
            const double dragTime = mouseX / pixelsPerSecond;
            sequencerEditTime_ = std::clamp(dragTime, 0.0, std::max(totalDuration, 0.0));
            sequencerEditTimeSet_ = true;
            seqPlayer.seek(sequencerEditTime_);
            vulkanRender->requestSequencerPreview();
        }
    }

    // ── 分隔线 ────────────────────────────────────────────────────────
    {
        ImVec2 sp = ImGui::GetCursorScreenPos();
        drawList->AddLine(ImVec2(contentOriginX, sp.y), ImVec2(contentOriginX + timelineWidth, sp.y), IM_COL32(80, 80, 80, 255));
    }

    // 行 Y 用独立累加偏移决定，不依赖 GetCursorScreenPos()：
    // clip/keyframe 的 SetCursorScreenPos+Button 会把 cursor 上移，若用 GetCursorScreenPos
    // 取下一行起点会逐行上移错位。此处显式累加，每行 Dummy 也显式定位。
    float rowCursorY = ImGui::GetCursorScreenPos().y;

    {
        const ImVec2 contentPos(contentOriginX, rowCursorY);
        if (selectedTrackIdx == kCameraShotTrackSelection) {
            drawList->AddRectFilled(
                contentPos,
                ImVec2(contentPos.x + timelineWidth, contentPos.y + trackHeight),
                IM_COL32(90, 62, 24, 110));
        }
        ImGui::SetCursorScreenPos(contentPos);
        ImGui::Dummy(ImVec2(timelineWidth, trackHeight));
        rowCursorY += trackHeight;

        if (sequence.cameraShotTrack) {
            const auto& keys = sequence.cameraShotTrack->keyframes;
            for (int keyIndex = 0;
                 keyIndex < static_cast<int>(keys.size());
                 ++keyIndex) {
                const auto& key = keys[keyIndex];
                const float keyX =
                    contentPos.x + static_cast<float>(key.time * pixelsPerSecond);
                constexpr float keySize = 8.f;
                const ImVec2 keyMin(
                    keyX - keySize,
                    contentPos.y + trackHeight * 0.5f - keySize);
                const ImVec2 keyMax(
                    keyX + keySize,
                    contentPos.y + trackHeight * 0.5f + keySize);
                const bool keySelected =
                    selectedTrackIdx == kCameraShotTrackSelection
                    && selectedShotKeyIdx == keyIndex;
                const auto* cameraEntity =
                    vulkanRender->getSceneManager().getModelEntity(key.cameraEntityId);
                const bool validCamera = cameraEntity && cameraEntity->isCamera();
                const ImU32 keyColor = !validCamera
                    ? IM_COL32(255, 60, 60, 255)
                    : keySelected
                        ? IM_COL32(255, 255, 255, 255)
                        : IM_COL32(245, 174, 68, 235);
                drawList->AddQuadFilled(
                    ImVec2(keyX, keyMin.y),
                    ImVec2(keyMax.x, contentPos.y + trackHeight * 0.5f),
                    ImVec2(keyX, keyMax.y),
                    ImVec2(keyMin.x, contentPos.y + trackHeight * 0.5f),
                    keyColor);

                const float segmentEndX = keyIndex + 1 < static_cast<int>(keys.size())
                    ? contentPos.x
                        + static_cast<float>(keys[keyIndex + 1].time * pixelsPerSecond)
                    : contentPos.x + timelineWidth;
                drawList->AddLine(
                    ImVec2(keyX + keySize, contentPos.y + trackHeight * 0.5f),
                    ImVec2(segmentEndX, contentPos.y + trackHeight * 0.5f),
                    IM_COL32(220, 145, 45, 150), 2.f);
                const std::string cameraLabel = validCamera
                    ? cameraEntity->displayName
                    : (key.targetDisplayName.empty()
                        ? "Camera Missing" : key.targetDisplayName + " (Missing)");
                drawList->AddText(
                    ImVec2(keyX + keySize + 3.f, contentPos.y + 5.f),
                    keyColor, cameraLabel.c_str());

                ImGui::SetCursorScreenPos(keyMin);
                ImGui::PushID(keyIndex + 700000);
                ImGui::InvisibleButton(
                    "##cameraShotKey",
                    ImVec2(keySize * 2.f, keySize * 2.f));
                if (ImGui::IsItemClicked()) {
                    selectedTrackIdx = kCameraShotTrackSelection;
                    selectedShotKeyIdx = keyIndex;
                    selectedClipType = -1;
                    selectedClipIdx = -1;
                    selectedKeyframeIdx_ = -1;
                    sequencerEditTime_ = key.time;
                    sequencerEditTimeSet_ = true;
                    seqPlayer.seek(key.time);
                    vulkanRender->requestSequencerPreview();
                }
                ImGui::PopID();
            }
        }
    }

    // ── 每条轨道一行 ──────────────────────────────────────────────────
    for (int ti = 0; ti < static_cast<int>(sequence.tracks.size()); ++ti) {
        const auto& track = sequence.tracks[ti];
        ImGui::PushID(ti);

        float rowH = getRowHeight(ti);
        bool parent = isParentTrack(ti);
        bool selected = (ti == selectedTrackIdx);

        // contentPos.y 由独立累加偏移决定，不受上一行 clip/keyframe 挪动 cursor 的影响
        ImVec2 contentPos(contentOriginX, rowCursorY);

        // 选中行高亮背景（时间线列）
        if (selected) {
            drawList->AddRectFilled(contentPos, ImVec2(contentPos.x + timelineWidth, contentPos.y + rowH),
                                    IM_COL32(60, 60, 90, 100));
        }

        // Dummy 占位该行时间线区域（定义滚动内容尺寸）；cursor 显式定位到行起点，
        // 随后累加 rowH，使下一行 Y 与 clip/keyframe 的 cursor 漂移完全解耦
        ImGui::SetCursorScreenPos(ImVec2(contentOriginX, rowCursorY));
        ImGui::Dummy(ImVec2(timelineWidth, rowH));
        rowCursorY += rowH;

        ImVec4 clipColor = (track.type == TrackType::Group) ? ImVec4(0.5f, 0.5f, 0.5f, 0.8f) :
                           (track.type == TrackType::AnimationClip) ? ImVec4(0.3f, 0.6f, 0.9f, 0.8f) :
                           (track.type == TrackType::CameraPath) ? ImVec4(0.9f, 0.6f, 0.3f, 0.8f) :
                           (track.type == TrackType::TransformTween) ? ImVec4(0.4f, 0.4f, 0.4f, 0.8f) :
                           (track.type == TrackType::TransformKeyframe) ? ImVec4(0.9f, 0.9f, 0.3f, 0.8f) :
                           (track.type == TrackType::AnimatorKeyframe) ? ImVec4(0.9f, 0.3f, 0.9f, 0.8f) :
                           ImVec4(0.9f, 0.3f, 0.3f, 0.8f);

        // 父轨道不绘制内容
        if (parent) {
            ImGui::PopID();
            continue;
        }

        // 绘制 clip / keyframe
        auto drawClip = [&](const SequenceClipBase& clip, int clipIdx, int clipType) {
            float startX = static_cast<float>(clip.startTime * pixelsPerSecond);
            float width = static_cast<float>(clip.duration * pixelsPerSecond);
            ImVec2 clipMin(contentPos.x + startX, contentPos.y + 5);
            ImVec2 clipMax(contentPos.x + startX + width, contentPos.y + rowH - 5);

            ImGui::SetCursorScreenPos(clipMin);
            char clipLabel[256];
            snprintf(clipLabel, sizeof(clipLabel), "%s##clip%d_%d", clip.name.c_str(), ti, clipIdx);

            ImGui::PushStyleColor(ImGuiCol_Button, clipColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(clipColor.x + 0.1f, clipColor.y + 0.1f, clipColor.z + 0.1f, clipColor.w));
            bool clicked = ImGui::Button(clipLabel, ImVec2(width, rowH - 10));
            ImGui::PopStyleColor(2);

            if (clicked) {
                selectedTrackIdx = ti;
                selectedClipType = clipType;
                selectedClipIdx = clipIdx;
            }
            if (selectedTrackIdx == ti && selectedClipType == clipType && selectedClipIdx == clipIdx) {
                drawList->AddRect(clipMin, clipMax, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
            }
        };

        if (track.type == TrackType::AnimationClip) {
            for (int ci = 0; ci < static_cast<int>(track.animClips.size()); ++ci)
                drawClip(track.animClips[ci], ci, 0);
        } else if (track.type == TrackType::CameraPath) {
            for (int ci = 0; ci < static_cast<int>(track.cameraPathClips.size()); ++ci)
                drawClip(track.cameraPathClips[ci], ci, 1);
        } else if (track.type == TrackType::TransformTween) {
            for (int ci = 0; ci < static_cast<int>(track.tweenClips.size()); ++ci)
                drawClip(track.tweenClips[ci], ci, 2);
        } else if (track.type == TrackType::TransformKeyframe) {
            for (int ci = 0; ci < static_cast<int>(track.keyframeTrack.keyframes.size()); ++ci) {
                const auto& kf = track.keyframeTrack.keyframes[ci];
                float kfX = contentPos.x + static_cast<float>(kf.time * pixelsPerSecond);
                float kfSize = 8.0f;
                ImVec2 kfMin(kfX - kfSize, contentPos.y + rowH * 0.5f - kfSize);
                ImVec2 kfMax(kfX + kfSize, contentPos.y + rowH * 0.5f + kfSize);

                bool isSelected = (selectedTrackIdx == ti && selectedKeyframeIdx_ == ci);
                ImU32 kfColor = isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 220, 80, 220);

                drawList->AddRectFilled(kfMin, kfMax, kfColor, 2.0f);
                drawList->AddRect(kfMin, kfMax, IM_COL32(100, 100, 50, 200), 2.0f);

                if (ci > 0) {
                    float prevKfX = contentPos.x + static_cast<float>(track.keyframeTrack.keyframes[ci - 1].time * pixelsPerSecond);
                    drawList->AddLine(ImVec2(prevKfX + kfSize, contentPos.y + rowH * 0.5f),
                                      ImVec2(kfX - kfSize, contentPos.y + rowH * 0.5f),
                                      IM_COL32(180, 180, 80, 100), 1.0f);
                }

                ImGui::SetCursorScreenPos(kfMin);
                ImGui::InvisibleButton(("##kf" + std::to_string(ci)).c_str(), ImVec2(kfSize * 2, kfSize * 2));
                if (ImGui::IsItemClicked()) {
                    selectedTrackIdx = ti;
                    selectedKeyframeIdx_ = ci;
                    selectedClipType = -1;
                    selectedClipIdx = -1;
                    sequencerEditTime_ = kf.time;
                    sequencerEditTimeSet_ = true;
                    seqPlayer.seek(kf.time);
                    vulkanRender->requestSequencerPreview();
                }
            }
        } else if (track.type == TrackType::AnimatorKeyframe) {
            for (int ci = 0; ci < static_cast<int>(track.animatorTrack.keyframes.size()); ++ci) {
                const auto& kf = track.animatorTrack.keyframes[ci];
                float kfX = contentPos.x + static_cast<float>(kf.time * pixelsPerSecond);
                float kfSize = 8.0f;
                ImVec2 kfMin(kfX - kfSize, contentPos.y + rowH * 0.5f - kfSize);
                ImVec2 kfMax(kfX + kfSize, contentPos.y + rowH * 0.5f + kfSize);

                bool isSelected = (selectedTrackIdx == ti && selectedKeyframeIdx_ == ci);
                ImU32 kfColor = isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 120, 255, 220);

                drawList->AddRectFilled(kfMin, kfMax, kfColor, 2.0f);
                drawList->AddRect(kfMin, kfMax, IM_COL32(100, 50, 100, 200), 2.0f);

                if (ci > 0) {
                    float prevKfX = contentPos.x + static_cast<float>(track.animatorTrack.keyframes[ci - 1].time * pixelsPerSecond);
                    drawList->AddLine(ImVec2(prevKfX + kfSize, contentPos.y + rowH * 0.5f),
                                      ImVec2(kfX - kfSize, contentPos.y + rowH * 0.5f),
                                      IM_COL32(180, 80, 180, 100), 1.0f);
                }

                ImGui::SetCursorScreenPos(kfMin);
                ImGui::InvisibleButton(("##akf" + std::to_string(ci)).c_str(), ImVec2(kfSize * 2, kfSize * 2));
                if (ImGui::IsItemClicked()) {
                    selectedTrackIdx = ti;
                    selectedKeyframeIdx_ = ci;
                    selectedClipType = -1;
                    selectedClipIdx = -1;
                    sequencerEditTime_ = kf.time;
                    sequencerEditTimeSet_ = true;
                    seqPlayer.seek(kf.time);
                    vulkanRender->requestSequencerPreview();
                }
            }
        } else if (track.type == TrackType::Event) {
            for (int ci = 0; ci < static_cast<int>(track.eventClips.size()); ++ci)
                drawClip(track.eventClips[ci], ci, 3);
        }

        ImGui::PopID();
    }

    if (sequencerEditTimeSet_ && !seqPlayer.isPlaying() &&
        std::abs(seqPlayer.currentTime() - sequencerEditTime_) > 0.001) {
        seqPlayer.seek(sequencerEditTime_);
        vulkanRender->requestSequencerPreview();
    }

    // 捕获时间线列垂直滚动，供左列标签下一帧跟随（双列滚动同步）
    seqTimelineScrollY_ = ImGui::GetScrollY();
    ImGui::EndChild();

    {
        const ImVec2 splitterStart = ImGui::GetCursorScreenPos();
        const float splitterWidth = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton(
            "##sequencerPanelSplitter",
            ImVec2(splitterWidth, panelSplitterHeight));
        const bool splitterHovered = ImGui::IsItemHovered();
        const bool splitterActive = ImGui::IsItemActive();
        if (splitterHovered || splitterActive)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (splitterActive) {
            parameterPanelHeight = std::clamp(
                parameterPanelHeight - ImGui::GetIO().MouseDelta.y,
                minimumParameterHeight,
                maximumParameterHeight);
        }

        const ImU32 splitterColor = splitterActive
            ? IM_COL32(120, 170, 230, 255)
            : splitterHovered
                ? IM_COL32(100, 130, 170, 255)
                : IM_COL32(70, 70, 70, 255);
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(
                splitterStart.x,
                splitterStart.y + panelSplitterHeight * 0.5f),
            ImVec2(
                splitterStart.x + splitterWidth,
                splitterStart.y + panelSplitterHeight * 0.5f),
            splitterColor, splitterActive ? 2.0f : 1.0f);
    }

    ImGui::BeginChild("##clipEditor", ImVec2(0, 0), true);

    if (selectedTrackIdx == kCameraShotTrackSelection) {
        ImGui::Text("Camera / Shot Track");
        ImGui::SameLine();
        ImGui::TextDisabled("(global, unique, hard cuts)");
        ImGui::TextDisabled(
            "A key selects the render Camera Actor until the next key. "
            "Animate that actor with its Transform track.");

        uint64_t selectedCameraId = vulkanRender->selectedCameraEntityId_;
        if (selectedCameraId == 0) {
            const auto* selectedEntity =
                vulkanRender->getSceneManager().getModelEntity(selectedEntityId_);
            if (selectedEntity && selectedEntity->isCamera())
                selectedCameraId = selectedEntity->entityId;
        }
        const auto* selectedCamera =
            vulkanRender->getSceneManager().getModelEntity(selectedCameraId);
        const bool canAddShotKey = selectedCamera && selectedCamera->isCamera();
        ImGui::BeginDisabled(!canAddShotKey);
        if (ImGui::Button("Add Shot Key at Playhead")) {
            if (!sequence.cameraShotTrack)
                sequence.cameraShotTrack.emplace();
            auto& keys = sequence.cameraShotTrack->keyframes;
            const double keyTime = sequencerEditTimeSet_
                ? sequencerEditTime_ : seqPlayer.currentTime();
            auto existing = std::find_if(
                keys.begin(), keys.end(),
                [&](const CameraShotKeyframe& key) {
                    return std::abs(key.time - keyTime) <= 1e-6;
                });
            CameraShotKeyframe key;
            key.time = keyTime;
            key.cameraEntityId = selectedCamera->entityId;
            key.targetDisplayName = selectedCamera->displayName;
            if (existing != keys.end())
                *existing = key;
            else
                keys.push_back(std::move(key));
            std::stable_sort(
                keys.begin(), keys.end(),
                [](const CameraShotKeyframe& a, const CameraShotKeyframe& b) {
                    return a.time < b.time;
                });
            for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
                if (std::abs(keys[i].time - keyTime) <= 1e-6
                    && keys[i].cameraEntityId == selectedCamera->entityId) {
                    selectedShotKeyIdx = i;
                    break;
                }
            }
            vulkanRender->requestSequencerPreview();
        }
        ImGui::EndDisabled();
        if (!canAddShotKey)
            ImGui::TextDisabled("Select a Camera Actor to add a shot key.");

        if (sequence.cameraShotTrack
            && selectedShotKeyIdx >= 0
            && selectedShotKeyIdx
                < static_cast<int>(sequence.cameraShotTrack->keyframes.size())) {
            auto& keys = sequence.cameraShotTrack->keyframes;
            auto& key = keys[selectedShotKeyIdx];
            ImGui::Separator();
            ImGui::Text("Shot Key [%d]", selectedShotKeyIdx);
            double keyTime = key.time;
            if (ImGui::DragScalar(
                    "Time##shotKey", ImGuiDataType_Double,
                    &keyTime, 0.1f, nullptr, nullptr, "%.3f")) {
                const uint64_t cameraId = key.cameraEntityId;
                key.time = std::max(0.0, keyTime);
                std::stable_sort(
                    keys.begin(), keys.end(),
                    [](const CameraShotKeyframe& a, const CameraShotKeyframe& b) {
                        return a.time < b.time;
                    });
                for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
                    if (keys[i].cameraEntityId == cameraId
                        && std::abs(keys[i].time - std::max(0.0, keyTime)) <= 1e-6) {
                        selectedShotKeyIdx = i;
                        break;
                    }
                }
                sequencerEditTime_ = std::max(0.0, keyTime);
                sequencerEditTimeSet_ = true;
                seqPlayer.seek(sequencerEditTime_);
                vulkanRender->requestSequencerPreview();
            }

            auto& currentKey = keys[selectedShotKeyIdx];
            const auto* boundCamera =
                vulkanRender->getSceneManager().getModelEntity(
                    currentKey.cameraEntityId);
            const char* cameraPreview =
                boundCamera && boundCamera->isCamera()
                ? boundCamera->displayName.c_str()
                : "Camera Missing";
            ImGui::SetNextItemWidth(260.f);
            if (ImGui::BeginCombo("Camera Actor##shotKey", cameraPreview)) {
                for (const auto& entity :
                     vulkanRender->getSceneManager().getModelEntities()) {
                    if (!entity.isCamera())
                        continue;
                    const bool selected =
                        entity.entityId == currentKey.cameraEntityId;
                    const std::string label = entity.displayName.empty()
                        ? "Camera " + std::to_string(entity.entityId)
                        : entity.displayName + "  ["
                            + std::to_string(entity.entityId) + "]";
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        vulkanRender->bindCameraShotKey(
                            static_cast<size_t>(selectedShotKeyIdx),
                            entity.entityId);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (!boundCamera || !boundCamera->isCamera()) {
                ImGui::TextColored(
                    ImVec4(1.f, 0.25f, 0.25f, 1.f),
                    "This shot key must be rebound before recording.");
            }

            if (ImGui::Button("Preview Shot Key")) {
                sequencerEditTime_ = currentKey.time;
                sequencerEditTimeSet_ = true;
                seqPlayer.seek(currentKey.time);
                vulkanRender->requestSequencerPreview();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Shot Key")) {
                keys.erase(keys.begin() + selectedShotKeyIdx);
                selectedShotKeyIdx = -1;
                vulkanRender->requestSequencerPreview();
            }
        }
        ImGui::Separator();
    }

    // ── Add Selected to Track → 自动判断子轨道 ────────────────────────
    if (vulkanRender->selectedCameraEntityId_ != 0 || selectedEntityId_ != 0) {
        uint64_t targetId = vulkanRender->selectedCameraEntityId_ != 0
            ? vulkanRender->selectedCameraEntityId_ : selectedEntityId_;

        auto* ent = vulkanRender->getSceneManager().getModelEntity(targetId);
        bool hasAnimator = ent && ent->animatorController.hasStates();

        if (ImGui::Button("Add Selected Entity Tracks")) {
            // 用 displayName 命名，回退到 entityId
            std::string entityName = (ent && !ent->displayName.empty())
                ? ent->displayName : "Entity_" + std::to_string(targetId);

            // 检查是否已存在：遍历已有 tracks，通过父轨道名称（entityName 匹配）判定
            // 父轨道是唯一为 entityName 的纯组织轨道，子轨道以 [T] entityName / [A] entityName 开头
            bool alreadyExists = false;
            for (const auto& t : sequence.tracks) {
                if (t.name == entityName) {
                    alreadyExists = true;
                    break;
                }
            }
            if (alreadyExists) {
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Entity '%s' already has tracks in this sequence", entityName.c_str());
            } else {

            auto addTrack = [&](const std::string& label, TrackType type, auto setup) {
                SequenceTrack t;
                t.name = label;
                t.type = type;
                setup(t);
                sequence.tracks.push_back(std::move(t));
            };

            // 父轨道：仅用作分组标签，不可编辑
            SequenceTrack parentTrack;
            parentTrack.name = entityName;
            parentTrack.type = TrackType::Group;
            sequence.tracks.push_back(std::move(parentTrack));

            // Transform 子轨道
            addTrack("[T] " + entityName + " Transform", TrackType::TransformKeyframe,
                     [&](SequenceTrack& t) {
                t.keyframeTrack.name = t.name;
                t.keyframeTrack.targetEntityId = targetId;
                if (ent) {
                    t.keyframeTrack.targetAstRelPath = ent->astRelPath;
                    t.keyframeTrack.targetDisplayName = ent->displayName;
                }
            });

            // 有 Animator 时额外创建 Animator 子轨道
            if (hasAnimator) {
                addTrack("[A] " + entityName + " Animator", TrackType::AnimatorKeyframe,
                         [&](SequenceTrack& t) {
                    t.animatorTrack.name = t.name;
                    t.animatorTrack.targetEntityId = targetId;
                    if (ent) {
                        t.animatorTrack.targetAstRelPath = ent->astRelPath;
                        t.animatorTrack.targetDisplayName = ent->displayName;
                    }
                });
            }

            selectedTrackIdx = static_cast<int>(sequence.tracks.size()) - 1;
            }
        }
    }

    // ── 统一 Add Keyframe：根据当前选中轨道类型自动判断 ────────────────
    ImGui::SameLine();
    if (sequencerEditTimeSet_) {
        bool canAddKeyframe = (selectedTrackIdx >= 0 &&
            selectedTrackIdx < static_cast<int>(sequence.tracks.size()) &&
            (sequence.tracks[selectedTrackIdx].type == TrackType::TransformKeyframe ||
             sequence.tracks[selectedTrackIdx].type == TrackType::AnimatorKeyframe));

        if (canAddKeyframe && ImGui::Button("Add Keyframe")) {
            auto& track = sequence.tracks[selectedTrackIdx];

            if (track.type == TrackType::TransformKeyframe) {
                auto* ent = vulkanRender->getSceneManager().getModelEntity(track.keyframeTrack.targetEntityId);
                if (ent) {
                    TransformKeyframe kf;
                    kf.time = sequencerEditTime_;
                    kf.position = ent->transform.position;
                    kf.rotation = ent->transform.rotation;
                    kf.scale = ent->transform.scale;

                    auto& kfs = track.keyframeTrack.keyframes;
                    kfs.push_back(std::move(kf));
                    std::sort(kfs.begin(), kfs.end(),
                              [](const TransformKeyframe& a, const TransformKeyframe& b) {
                                  return a.time < b.time;
                              });
                    for (int ri = 0; ri < static_cast<int>(kfs.size()); ++ri) {
                        if (std::abs(kfs[ri].time - sequencerEditTime_) < 0.001) {
                            selectedKeyframeIdx_ = ri;
                            break;
                        }
                    }
                    if (sequence.totalDuration <= 0.0) {
                        sequence.totalDuration = track.keyframeTrack.totalDuration() + 5.0;
                    }
                    snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                             "Added transform keyframe at %.2fs", sequencerEditTime_);
                }
            } else if (track.type == TrackType::AnimatorKeyframe) {
                AnimatorKeyframe kf;
                kf.time = sequencerEditTime_;
                auto& kfs = track.animatorTrack.keyframes;
                kfs.push_back(std::move(kf));
                std::sort(kfs.begin(), kfs.end(),
                          [](const AnimatorKeyframe& a, const AnimatorKeyframe& b) {
                              return a.time < b.time;
                          });
                for (int ri = 0; ri < static_cast<int>(kfs.size()); ++ri) {
                    if (std::abs(kfs[ri].time - sequencerEditTime_) < 0.001) {
                        selectedKeyframeIdx_ = ri;
                        break;
                    }
                }
                if (sequence.totalDuration <= 0.0) {
                    sequence.totalDuration = track.animatorTrack.totalDuration() + 5.0;
                }
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Added animator keyframe at %.2fs", sequencerEditTime_);
            }
        }
    }

    if (selectedTrackIdx >= 0 && selectedTrackIdx < static_cast<int>(sequence.tracks.size())) {
        auto& track = sequence.tracks[selectedTrackIdx];
        ImGui::Text("Track: %s", track.name.c_str());
        ImGui::SameLine();
        static char trackNameBuf[256];
        snprintf(trackNameBuf, sizeof(trackNameBuf), "%s", track.name.c_str());
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputText("##trackNameEdit", trackNameBuf, sizeof(trackNameBuf)))
            track.name = trackNameBuf;

        auto trackTypeLabel = [](TrackType type) {
            switch (type) {
            case TrackType::AnimationClip:     return "AnimationClip (Legacy, Disabled)";
            case TrackType::CameraPath:        return "CameraPath (Legacy)";
            case TrackType::TransformTween:    return "TransformTween (Deprecated)";
            case TrackType::TransformKeyframe: return "TransformKeyframe";
            case TrackType::AnimatorKeyframe:  return "AnimatorKeyframe";
            case TrackType::Event:             return "Event (Legacy, Disabled)";
            case TrackType::Group:             return "Group";
            }
            return "Unknown";
        };
        ImGui::SameLine();
        ImGui::TextDisabled("Type: %s (fixed)", trackTypeLabel(track.type));
        if (track.type == TrackType::AnimationClip || track.type == TrackType::Event)
            ImGui::TextDisabled("Legacy track retained for compatibility; runtime evaluation is disabled.");
        else if (track.type == TrackType::CameraPath)
            ImGui::TextDisabled(
                "Legacy playback compatibility only; new CameraPath clips cannot be created.");
        else if (track.type == TrackType::Group)
            ImGui::TextDisabled(
                "Group contains the following tracks up to the next Group.");

        ImGui::Separator();

        // ── Keyframe Track Editor ────────────────────────────────────
        if (track.type == TrackType::TransformKeyframe) {
            auto& kft = track.keyframeTrack;
            ImGui::Text("Target Entity ID: %llu",
                        static_cast<unsigned long long>(kft.targetEntityId));
            if (!vulkanRender->getSceneManager().getModelEntity(kft.targetEntityId))
                ImGui::TextColored(ImVec4(1.f, 0.25f, 0.25f, 1.f), "Target Missing");
            ImGui::Text("Keyframes: %zu", kft.keyframes.size());

            if (selectedKeyframeIdx_ >= 0
                && selectedKeyframeIdx_ < static_cast<int>(kft.keyframes.size()))
            {
                auto& kf = kft.keyframes[selectedKeyframeIdx_];
                ImGui::Separator();
                ImGui::Text("Keyframe [%d]", selectedKeyframeIdx_);

                double kfTime = kf.time;
                if (ImGui::DragScalar("Time", ImGuiDataType_Double, &kfTime, 0.1f)) {
                    kf.time = kfTime;
                    std::sort(kft.keyframes.begin(), kft.keyframes.end(),
                              [](const TransformKeyframe& a, const TransformKeyframe& b) {
                                  return a.time < b.time;
                              });
                    for (int ri = 0; ri < static_cast<int>(kft.keyframes.size()); ++ri) {
                        if (kft.keyframes[ri].time == kf.time) {
                            selectedKeyframeIdx_ = ri;
                            break;
                        }
                    }
                }

                float pos[3] = { kf.position.x, kf.position.y, kf.position.z };
                if (ImGui::DragFloat3("Position", pos, 0.1f))
                    kf.position = glm::vec3(pos[0], pos[1], pos[2]);

                glm::vec3 euler = glm::degrees(glm::eulerAngles(kf.rotation));
                float rot[3] = { euler.x, euler.y, euler.z };
                if (ImGui::DragFloat3("Rotation", rot, 1.0f))
                    kf.rotation = glm::quat(glm::radians(glm::vec3(rot[0], rot[1], rot[2])));

                float scale[3] = { kf.scale.x, kf.scale.y, kf.scale.z };
                if (ImGui::DragFloat3("Scale", scale, 0.05f, 0.01f, 100.0f))
                    kf.scale = glm::vec3(scale[0], scale[1], scale[2]);

                const char* easeNames[] = { "Linear", "SmoothStep", "EaseIn", "EaseOut", "EaseInOut", "Cubic", "Exponential" };
                int easeIdx = static_cast<int>(kf.easeToNext);
                if (ImGui::Combo("Ease to Next", &easeIdx, easeNames, IM_ARRAYSIZE(easeNames)))
                    kf.easeToNext = static_cast<TweenEase>(easeIdx);

                ImGui::Separator();
                if (ImGui::Button("Delete Keyframe")) {
                    kft.keyframes.erase(kft.keyframes.begin() + selectedKeyframeIdx_);
                    selectedKeyframeIdx_ = -1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Update from Entity")) {
                    auto* ent = vulkanRender->getSceneManager().getModelEntity(kft.targetEntityId);
                    if (ent) {
                        kf.position = ent->transform.position;
                        kf.rotation = ent->transform.rotation;
                        kf.scale = ent->transform.scale;
                        snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                                 "Updated keyframe [%d] from entity transform", selectedKeyframeIdx_);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Preview")) {
                    sequencerEditTime_ = kf.time;
                    sequencerEditTimeSet_ = true;
                    seqPlayer.seek(kf.time);
                    vulkanRender->requestSequencerPreview();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Delete All Keyframes")) {
                kft.keyframes.clear();
                selectedKeyframeIdx_ = -1;
            }
            ImGui::SameLine();
            if (sequencerEditTimeSet_ && ImGui::Button("Preview Here")) {
                vulkanRender->requestSequencerPreview();
            }
        }
        // ── Animator Keyframe Track Editor ────────────────────────────────
        if (track.type == TrackType::AnimatorKeyframe) {
            auto& akt = track.animatorTrack;
            ImGui::Text("Target Entity ID: %llu",
                        static_cast<unsigned long long>(akt.targetEntityId));
            if (!vulkanRender->getSceneManager().getModelEntity(akt.targetEntityId))
                ImGui::TextColored(ImVec4(1.f, 0.25f, 0.25f, 1.f), "Target Missing");

            // 初始状态：从 Controller states 列表中选择，避免手动输入拼写错误
            // 通过 targetEntityId 查找实体以获取其 AnimatorController 的 states 列表
            {
                auto* entityForCtrl = vulkanRender->getSceneManager().getModelEntity(akt.targetEntityId);
                std::vector<std::string> stateNames;
                int selState = -1;
                if (entityForCtrl && entityForCtrl->animatorController.hasStates()) {
                    const auto& ctrlStates = entityForCtrl->animatorController.states();
                    for (int si = 0; si < static_cast<int>(ctrlStates.size()); ++si) {
                        stateNames.push_back(ctrlStates[si].name);
                        if (ctrlStates[si].name == akt.initialState)
                            selState = si;
                    }
                }
                // 添加一个空选项（表示不指定 initialState，使用控制器默认状态）
                stateNames.insert(stateNames.begin(), "(default)");
                int comboIdx = (selState >= 0) ? selState + 1 : 0;
                ImGui::SetNextItemWidth(150);
                if (ImGui::Combo("Initial State", &comboIdx, [](void* data, int idx, const char** out) -> bool {
                        auto* names = static_cast<std::vector<std::string>*>(data);
                        if (idx < 0 || idx >= static_cast<int>(names->size())) return false;
                        *out = (*names)[idx].c_str();
                        return true;
                    }, &stateNames, static_cast<int>(stateNames.size())))
                {
                    akt.initialState = (comboIdx > 0) ? stateNames[comboIdx] : std::string();
                }
            }

            ImGui::Text("Keyframes: %zu", akt.keyframes.size());

            if (selectedKeyframeIdx_ >= 0
                && selectedKeyframeIdx_ < static_cast<int>(akt.keyframes.size()))
            {
                auto& kf = akt.keyframes[selectedKeyframeIdx_];
                ImGui::Separator();
                ImGui::Text("Animator Keyframe [%d] @ %.2fs", selectedKeyframeIdx_, kf.time);

                double kfTime = kf.time;
                if (ImGui::DragScalar("Time", ImGuiDataType_Double, &kfTime, 0.1f)) {
                    kf.time = kfTime;
                    std::sort(akt.keyframes.begin(), akt.keyframes.end(),
                              [](const AnimatorKeyframe& a, const AnimatorKeyframe& b) {
                                  return a.time < b.time;
                              });
                    for (int ri = 0; ri < static_cast<int>(akt.keyframes.size()); ++ri) {
                        if (std::abs(akt.keyframes[ri].time - kfTime) < 0.001) {
                            selectedKeyframeIdx_ = ri;
                            break;
                        }
                    }
                }

                // 事件列表
                int removeIdx = -1;
                for (int ei = 0; ei < static_cast<int>(kf.events.size()); ++ei) {
                    auto& ev = kf.events[ei];
                    ImGui::PushID(ei);
                    ImGui::Separator();
                    ImGui::Text("Event %d", ei);

                    // 参数名：从 Controller 参数列表中选择，自动填充类型
                    {
                        bool eventChanged = false;
                        auto* entityForParams = vulkanRender->getSceneManager().getModelEntity(akt.targetEntityId);
                        int selParam = -1;
                        std::vector<std::string> paramNames;
                        std::vector<AnimatorParam> ctrlParams;
                        if (entityForParams && entityForParams->animatorController.hasStates()) {
                            ctrlParams = entityForParams->animatorController.params();
                            for (int pi = 0; pi < static_cast<int>(ctrlParams.size()); ++pi) {
                                paramNames.push_back(ctrlParams[pi].name);
                                if (ctrlParams[pi].name == ev.paramName)
                                    selParam = pi;
                            }
                        }
                        // 添加一个自由输入选项（允许手动输入不在列表中的参数名）
                        paramNames.insert(paramNames.begin(), "(manual)");
                        int paramComboIdx = (selParam >= 0) ? selParam + 1 : 0;
                        ImGui::SetNextItemWidth(150);
                        if (ImGui::Combo("Param##ev", &paramComboIdx, [](void* data, int idx, const char** out) -> bool {
                                auto* names = static_cast<std::vector<std::string>*>(data);
                                if (idx < 0 || idx >= static_cast<int>(names->size())) return false;
                                *out = (*names)[idx].c_str();
                                return true;
                            }, &paramNames, static_cast<int>(paramNames.size())))
                        {
                            eventChanged = true;
                            if (paramComboIdx > 0) {
                                // 从列表中选择：自动设置 paramName 和对应的 type
                                ev.paramName = paramNames[paramComboIdx];
                                const auto& selectedParam = ctrlParams[paramComboIdx - 1];
                                switch (selectedParam.type) {
                                case AnimatorParam::Type::Float:
                                    ev.type = AnimatorEvent::Type::SetFloat; break;
                                case AnimatorParam::Type::Int:
                                    ev.type = AnimatorEvent::Type::SetInt; break;
                                case AnimatorParam::Type::Bool:
                                    ev.type = AnimatorEvent::Type::SetBool; break;
                                case AnimatorParam::Type::Trigger:
                                    ev.type = AnimatorEvent::Type::SetTrigger; break;
                                }
                            }
                            // 选择 "(manual)" 时不清除 paramName，保持原值
                        }

                        // 如果当前选中的是 manual 模式（参数名不在列表中），显示 InputText 供手动输入
                        if (selParam < 0 && !ev.paramName.empty()) {
                            ImGui::SameLine();
                            static char manualParamBuf[128];
                            snprintf(manualParamBuf, sizeof(manualParamBuf), "%s", ev.paramName.c_str());
                            ImGui::SetNextItemWidth(120);
                            if (ImGui::InputText("##manualParam", manualParamBuf, sizeof(manualParamBuf))) {
                                ev.paramName = manualParamBuf;
                                eventChanged = true;
                            }
                        } else if (paramNames.size() <= 1) {
                            // 没有可用参数列表时显示普通 InputText
                            static char paramBuf[128];
                            snprintf(paramBuf, sizeof(paramBuf), "%s", ev.paramName.c_str());
                            ImGui::SetNextItemWidth(150);
                            if (ImGui::InputText("Param##ev", paramBuf, sizeof(paramBuf))) {
                                ev.paramName = paramBuf;
                                eventChanged = true;
                            }
                        }

                        if (eventChanged && sequencerEditTimeSet_) {
                            seqPlayer.seek(sequencerEditTime_);
                            vulkanRender->requestSequencerPreview();
                        }
                    }

                    switch (ev.type) {
                    case AnimatorEvent::Type::SetFloat: {
                        bool valChanged = ImGui::DragFloat("Value##ev", &ev.floatValue, 0.1f);
                        {
                            const char* interpNames[] = { "Step", "Linear", "SmoothStep", "EaseIn", "EaseOut", "EaseInOut", "Cubic", "Exponential" };
                            int interpIdx = static_cast<int>(ev.interp);
                            if (ImGui::Combo("Interp##ev", &interpIdx, interpNames, IM_ARRAYSIZE(interpNames))) {
                                ev.interp = static_cast<ParamInterp>(interpIdx);
                                valChanged = true;
                            }
                        }
                        if (valChanged && sequencerEditTimeSet_) {
                            seqPlayer.seek(sequencerEditTime_);
                            vulkanRender->requestSequencerPreview();
                        }
                        break;
                    }
                    case AnimatorEvent::Type::SetInt: {
                        bool valChanged = ImGui::DragInt("Value##ev", &ev.intValue, 1);
                        {
                            const char* interpNames[] = { "Step", "Linear", "SmoothStep", "EaseIn", "EaseOut", "EaseInOut", "Cubic", "Exponential" };
                            int interpIdx = static_cast<int>(ev.interp);
                            if (ImGui::Combo("Interp##ev", &interpIdx, interpNames, IM_ARRAYSIZE(interpNames))) {
                                ev.interp = static_cast<ParamInterp>(interpIdx);
                                valChanged = true;
                            }
                        }
                        if (valChanged && sequencerEditTimeSet_) {
                            seqPlayer.seek(sequencerEditTime_);
                            vulkanRender->requestSequencerPreview();
                        }
                        break;
                    }
                    case AnimatorEvent::Type::SetBool:
                        if (ImGui::Checkbox("Value##ev", &ev.boolValue) && sequencerEditTimeSet_) {
                            seqPlayer.seek(sequencerEditTime_);
                            vulkanRender->requestSequencerPreview();
                        }
                        break;
                    case AnimatorEvent::Type::SetTrigger:
                        break;
                    }

                    if (ImGui::SmallButton("Remove##ev")) removeIdx = ei;
                    ImGui::PopID();
                }

                if (removeIdx >= 0) {
                    kf.events.erase(kf.events.begin() + removeIdx);
                    if (sequencerEditTimeSet_) {
                        seqPlayer.seek(sequencerEditTime_);
                        vulkanRender->requestSequencerPreview();
                    }
                }

                ImGui::Separator();
                if (ImGui::Button("+ Add Event")) {
                    AnimatorParamEvent newEv;
                    kf.events.push_back(std::move(newEv));
                    if (sequencerEditTimeSet_) {
                        seqPlayer.seek(sequencerEditTime_);
                        vulkanRender->requestSequencerPreview();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete Keyframe")) {
                    akt.keyframes.erase(akt.keyframes.begin() + selectedKeyframeIdx_);
                    selectedKeyframeIdx_ = -1;
                    if (sequencerEditTimeSet_) {
                        seqPlayer.seek(sequencerEditTime_);
                        vulkanRender->requestSequencerPreview();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Preview##akf")) {
                    sequencerEditTime_ = kf.time;
                    sequencerEditTimeSet_ = true;
                    seqPlayer.seek(kf.time);
                    vulkanRender->requestSequencerPreview();
                }
            }
        }
        // ── Old clip-based editor for non-keyframe tracks ────────────
        else if (selectedClipType >= 0 && selectedClipIdx >= 0) {
            ImGui::Text("Clip Properties");

            auto drawClipEditor = [&](SequenceClipBase& clip) {
                ImGui::SetNextItemWidth(200);
                static char clipNameBuf[256];
                snprintf(clipNameBuf, sizeof(clipNameBuf), "%s", clip.name.c_str());
                if (ImGui::InputText("Name##clip", clipNameBuf, sizeof(clipNameBuf)))
                    clip.name = clipNameBuf;
                double startTime = clip.startTime;
                if (ImGui::DragScalar("Start Time", ImGuiDataType_Double, &startTime, 0.1f))
                    clip.startTime = startTime;
                double duration = clip.duration;
                if (ImGui::DragScalar("Duration", ImGuiDataType_Double, &duration, 0.1f))
                    clip.duration = duration;
            };

            if (selectedClipType == 0 && selectedClipIdx < static_cast<int>(track.animClips.size())) {
                auto& clip = track.animClips[selectedClipIdx];
                drawClipEditor(clip);
                static char clipNameBuf[256];
                snprintf(clipNameBuf, sizeof(clipNameBuf), "%s", clip.clipName.c_str());
                ImGui::SetNextItemWidth(200);
                if (ImGui::InputText("Clip Name", clipNameBuf, sizeof(clipNameBuf)))
                    clip.clipName = clipNameBuf;
                double clipOffset = clip.clipOffset;
                if (ImGui::DragScalar("Clip Offset", ImGuiDataType_Double, &clipOffset, 0.1f))
                    clip.clipOffset = clipOffset;
                double playSpeed = clip.playSpeed;
                if (ImGui::DragScalar("Play Speed", ImGuiDataType_Double, &playSpeed, 0.1f))
                    clip.playSpeed = playSpeed;
            } else if (selectedClipType == 1 && selectedClipIdx < static_cast<int>(track.cameraPathClips.size())) {
                auto& clip = track.cameraPathClips[selectedClipIdx];
                drawClipEditor(clip);
                static char pathBuf[512];
                snprintf(pathBuf, sizeof(pathBuf), "%s", clip.pathAssetRelPath.c_str());
                ImGui::SetNextItemWidth(300);
                if (ImGui::InputText("Path Asset", pathBuf, sizeof(pathBuf)))
                    clip.pathAssetRelPath = pathBuf;
            } else if (selectedClipType == 2 && selectedClipIdx < static_cast<int>(track.tweenClips.size())) {
                auto& clip = track.tweenClips[selectedClipIdx];
                drawClipEditor(clip);
                float startPos[3] = { clip.startPosition.x, clip.startPosition.y, clip.startPosition.z };
                if (ImGui::DragFloat3("Start Position", startPos, 0.1f))
                    clip.startPosition = glm::vec3(startPos[0], startPos[1], startPos[2]);
                float endPos[3] = { clip.endPosition.x, clip.endPosition.y, clip.endPosition.z };
                if (ImGui::DragFloat3("End Position", endPos, 0.1f))
                    clip.endPosition = glm::vec3(endPos[0], endPos[1], endPos[2]);
                glm::vec3 startEuler = glm::degrees(glm::eulerAngles(clip.startRotation));
                float startRot[3] = { startEuler.x, startEuler.y, startEuler.z };
                if (ImGui::DragFloat3("Start Rotation", startRot, 1.0f))
                    clip.startRotation = glm::quat(glm::radians(glm::vec3(startRot[0], startRot[1], startRot[2])));
                glm::vec3 endEuler = glm::degrees(glm::eulerAngles(clip.endRotation));
                float endRot[3] = { endEuler.x, endEuler.y, endEuler.z };
                if (ImGui::DragFloat3("End Rotation", endRot, 1.0f))
                    clip.endRotation = glm::quat(glm::radians(glm::vec3(endRot[0], endRot[1], endRot[2])));
                float startScale[3] = { clip.startScale.x, clip.startScale.y, clip.startScale.z };
                if (ImGui::DragFloat3("Start Scale", startScale, 0.05f, 0.01f, 100.0f))
                    clip.startScale = glm::vec3(startScale[0], startScale[1], startScale[2]);
                float endScaleArr[3] = { clip.endScale.x, clip.endScale.y, clip.endScale.z };
                if (ImGui::DragFloat3("End Scale", endScaleArr, 0.05f, 0.01f, 100.0f))
                    clip.endScale = glm::vec3(endScaleArr[0], endScaleArr[1], endScaleArr[2]);
                const char* easeNames[] = { "Linear", "SmoothStep", "EaseIn", "EaseOut", "EaseInOut", "Cubic", "Exponential" };
                int easeIdx = static_cast<int>(clip.ease);
                if (ImGui::Combo("Ease", &easeIdx, easeNames, IM_ARRAYSIZE(easeNames)))
                    clip.ease = static_cast<TweenEase>(easeIdx);
            } else if (selectedClipType == 3 && selectedClipIdx < static_cast<int>(track.eventClips.size())) {
                auto& clip = track.eventClips[selectedClipIdx];
                drawClipEditor(clip);
                const char* eventTypeNames[] = { "SetFloat", "SetInt", "SetBool", "SetTrigger" };
                int evType = static_cast<int>(clip.event.type);
                ImGui::SetNextItemWidth(120);
                if (ImGui::Combo("Event Type", &evType, eventTypeNames, IM_ARRAYSIZE(eventTypeNames)))
                    clip.event.type = static_cast<AnimatorEvent::Type>(evType);
                static char paramBuf[128];
                snprintf(paramBuf, sizeof(paramBuf), "%s", clip.event.paramName.c_str());
                ImGui::SetNextItemWidth(200);
                if (ImGui::InputText("Param Name", paramBuf, sizeof(paramBuf)))
                    clip.event.paramName = paramBuf;
                switch (clip.event.type) {
                case AnimatorEvent::Type::SetFloat:
                    ImGui::DragFloat("Value", &clip.event.floatValue, 0.1f);
                    break;
                case AnimatorEvent::Type::SetInt:
                    ImGui::DragInt("Value", &clip.event.intValue, 1);
                    break;
                case AnimatorEvent::Type::SetBool:
                    ImGui::Checkbox("Value", &clip.event.boolValue);
                    break;
                case AnimatorEvent::Type::SetTrigger:
                    break;
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Delete Clip")) {
                if (selectedClipType == 0 && selectedClipIdx < static_cast<int>(track.animClips.size()))
                    track.animClips.erase(track.animClips.begin() + selectedClipIdx);
                else if (selectedClipType == 1 && selectedClipIdx < static_cast<int>(track.cameraPathClips.size()))
                    track.cameraPathClips.erase(track.cameraPathClips.begin() + selectedClipIdx);
                else if (selectedClipType == 2 && selectedClipIdx < static_cast<int>(track.tweenClips.size()))
                    track.tweenClips.erase(track.tweenClips.begin() + selectedClipIdx);
                else if (selectedClipType == 3 && selectedClipIdx < static_cast<int>(track.eventClips.size()))
                    track.eventClips.erase(track.eventClips.begin() + selectedClipIdx);
                selectedClipIdx = -1;
                selectedClipType = -1;
            }
        } else if (track.type != TrackType::TransformKeyframe
                   && track.type != TrackType::AnimatorKeyframe
                   && track.type != TrackType::Group) {
            ImGui::TextDisabled("Select a clip to edit properties");
        }

        if (track.type == TrackType::TransformTween) {
            ImGui::Separator();
            ImGui::Text("Add Clip");

            static double newClipStart = 0.0;
            static double newClipDuration = 1.0;
            static char newClipName[128] = "NewClip";

            ImGui::SetNextItemWidth(100);
            ImGui::DragScalar("Start##newClip", ImGuiDataType_Double, &newClipStart, 0.1f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::DragScalar("Duration##newClip", ImGuiDataType_Double, &newClipDuration, 0.1f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("Name##newClip", newClipName, sizeof(newClipName));

            if (ImGui::Button("Add Clip##addClip")) {
                SequenceClipBase base;
                base.name = newClipName;
                base.startTime = newClipStart;
                base.duration = newClipDuration;

                if (track.type == TrackType::AnimationClip) {
                    AnimTrackClip ac;
                    static_cast<SequenceClipBase&>(ac) = base;
                    track.animClips.push_back(std::move(ac));
                } else if (track.type == TrackType::CameraPath) {
                    CameraPathClip cc;
                    static_cast<SequenceClipBase&>(cc) = base;
                    track.cameraPathClips.push_back(std::move(cc));
                } else if (track.type == TrackType::TransformTween) {
                    TransformTweenClip tc;
                    static_cast<SequenceClipBase&>(tc) = base;
                    track.tweenClips.push_back(std::move(tc));
                } else if (track.type == TrackType::Event) {
                    EventClip ec;
                    static_cast<SequenceClipBase&>(ec) = base;
                    track.eventClips.push_back(std::move(ec));
                }
            }
        }

        // AnimatorKeyframe 轨道特殊处理：无 clip 添加，只有关键帧
        if (track.type == TrackType::AnimatorKeyframe) {
            // 关键帧通过上面的 Add Animator Keyframe 按钮添加
        }
    } else if (selectedTrackIdx != kCameraShotTrackSelection) {
        ImGui::TextDisabled("Select a track to edit");
    }

    ImGui::Separator();
    ImGui::Text("Track Actions");

    auto makeUniqueTrackName = [&](const std::string& base) {
        auto exists = [&](const std::string& candidate) {
            return std::any_of(sequence.tracks.begin(), sequence.tracks.end(),
                               [&](const SequenceTrack& track) {
                                   return track.name == candidate;
                               });
        };
        if (!exists(base))
            return base;
        for (int suffix = 2; ; ++suffix) {
            const std::string candidate = base + " " + std::to_string(suffix);
            if (!exists(candidate))
                return candidate;
        }
    };

    if (ImGui::Button("Add Group##addGroup")) {
        SequenceTrack newTrack;
        newTrack.name = makeUniqueTrackName("New Group");
        newTrack.type = TrackType::Group;
        sequence.tracks.push_back(std::move(newTrack));
        selectedTrackIdx = static_cast<int>(sequence.tracks.size()) - 1;
        selectedClipIdx = -1;
        selectedClipType = -1;
        selectedKeyframeIdx_ = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Select Camera / Shot Track##selectCameraShotTrack")) {
        selectedTrackIdx = kCameraShotTrackSelection;
        selectedShotKeyIdx = -1;
        selectedClipIdx = -1;
        selectedClipType = -1;
        selectedKeyframeIdx_ = -1;
    }
    ImGui::SameLine();

    static int pendingDeleteTrackIdx = -1;
    const bool canDeleteTrack =
        selectedTrackIdx >= 0
        && selectedTrackIdx < static_cast<int>(sequence.tracks.size());
    ImGui::BeginDisabled(!canDeleteTrack);
    if (ImGui::Button("Delete Selected##delTrack")) {
        pendingDeleteTrackIdx = selectedTrackIdx;
        ImGui::OpenPopup("Delete Sequencer Track");
    }
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Delete Sequencer Track", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool deleteTargetValid =
            pendingDeleteTrackIdx >= 0
            && pendingDeleteTrackIdx < static_cast<int>(sequence.tracks.size());
        if (!deleteTargetValid) {
            ImGui::TextDisabled("The selected track no longer exists.");
        } else {
            const auto& deleteTarget = sequence.tracks[pendingDeleteTrackIdx];
            int deleteEnd = pendingDeleteTrackIdx + 1;
            if (deleteTarget.type == TrackType::Group) {
                while (deleteEnd < static_cast<int>(sequence.tracks.size())
                       && sequence.tracks[deleteEnd].type != TrackType::Group) {
                    ++deleteEnd;
                }
                ImGui::TextWrapped(
                    "Delete group '%s' and its %d following child track(s)?",
                    deleteTarget.name.c_str(),
                    deleteEnd - pendingDeleteTrackIdx - 1);
            } else {
                ImGui::TextWrapped("Delete track '%s'?", deleteTarget.name.c_str());
            }
            ImGui::TextDisabled("This operation cannot be undone.");

            if (ImGui::Button("Delete", ImVec2(120.f, 0.f))) {
                const std::string deletedName = deleteTarget.name;
                const int deletedCount = deleteEnd - pendingDeleteTrackIdx;
                sequence.tracks.erase(
                    sequence.tracks.begin() + pendingDeleteTrackIdx,
                    sequence.tracks.begin() + deleteEnd);
                selectedTrackIdx = -1;
                selectedClipIdx = -1;
                selectedClipType = -1;
                selectedKeyframeIdx_ = -1;
                pendingDeleteTrackIdx = -1;
                vulkanRender->requestSequencerPreview();
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Deleted '%s' (%d track%s)",
                         deletedName.c_str(), deletedCount,
                         deletedCount == 1 ? "" : "s");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Cancel", ImVec2(120.f, 0.f))) {
            pendingDeleteTrackIdx = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    ImGui::Text("Sequence Recording");
    ImGui::TextDisabled(
        "Fixed-timestep PNG capture at the current framebuffer resolution. "
        "The Camera / Shot track selects each recorded view.");
    static double captureStartTime = 0.0;
    static double captureEndTime = -1.0;
    static int captureFps = 30;
    static bool captureIncludeUi = false;
    static char captureTakeName[128] = "SequenceTake";
    if (captureEndTime < 0.0)
        captureEndTime = std::max(totalDuration, 0.0);

    if (vulkanRender->isSequenceCaptureActive()) {
        const uint32_t captured = vulkanRender->getSequenceCaptureFrameCount();
        const uint32_t planned = vulkanRender->getSequenceCaptureTotalFrames();
        const float progress = planned > 0
            ? static_cast<float>(captured) / static_cast<float>(planned)
            : 0.f;
        ImGui::ProgressBar(
            progress, ImVec2(-1.f, 0.f),
            (std::to_string(captured) + " / " + std::to_string(planned)
             + " frames").c_str());
        if (ImGui::Button("Stop After Current Frame"))
            vulkanRender->stopSequenceCapture();
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Keep Partial)"))
            vulkanRender->cancelSequenceCapture();
    } else {
        ImGui::SetNextItemWidth(150.f);
        ImGui::InputText("Take Name", captureTakeName, sizeof(captureTakeName));
        ImGui::SetNextItemWidth(110.f);
        ImGui::DragScalar(
            "Start##capture", ImGuiDataType_Double,
            &captureStartTime, 0.1f, nullptr, nullptr, "%.3f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.f);
        ImGui::DragScalar(
            "End##capture", ImGuiDataType_Double,
            &captureEndTime, 0.1f, nullptr, nullptr, "%.3f");
        ImGui::SameLine();
        if (ImGui::Button("Use Sequence Range")) {
            captureStartTime = 0.0;
            captureEndTime = totalDuration;
        }
        ImGui::SetNextItemWidth(90.f);
        ImGui::DragInt("FPS", &captureFps, 1.f, 1, 240);
        ImGui::SameLine();
        ImGui::Checkbox("Include UI", &captureIncludeUi);

        const bool canRecord =
            sequencerControl
            && sequence.cameraShotTrack
            && !sequence.cameraShotTrack->keyframes.empty()
            && captureEndTime > captureStartTime;
        ImGui::BeginDisabled(!canRecord);
        if (ImGui::Button("Record PNG Sequence")) {
            Application::SequenceCaptureSettings settings;
            settings.startTime = captureStartTime;
            settings.endTime = captureEndTime;
            settings.fps = captureFps;
            settings.includeUi = captureIncludeUi;
            settings.takeName = captureTakeName;
            std::string captureError;
            if (!vulkanRender->beginSequenceCapture(settings, &captureError)) {
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Recording failed: %s", captureError.c_str());
            }
        }
        ImGui::EndDisabled();
        if (!sequencerControl)
            ImGui::TextDisabled("Enable Sequencer Control to record.");
        else if (!sequence.cameraShotTrack
                 || sequence.cameraShotTrack->keyframes.empty())
            ImGui::TextDisabled("Add at least one Camera / Shot key.");
    }
    ImGui::TextWrapped("Status: %s",
                       vulkanRender->getSequenceCaptureStatus().c_str());
    if (!vulkanRender->getSequenceCaptureOutputDirectory().empty()) {
        ImGui::TextWrapped(
            "Output: %s",
            vulkanRender->getSequenceCaptureOutputDirectory().c_str());
    }

    if (animatorStatusMsg_[0]) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", animatorStatusMsg_);
    }

    ImGui::EndChild();
    ImGui::End();
}

// ─── Animator Panel ─────────────────────────────────────────────────────────

void UIManager::drawAnimatorPanel()
{
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animator", &showAnimatorPanel_)) {
        ImGui::End();
        return;
    }

    if (!vulkanRender) {
        ImGui::TextDisabled("(unavailable)");
        ImGui::End();
        return;
    }

    auto& scene = vulkanRender->getSceneManager();
    SceneManager::ModelEntity* ent = nullptr;
    if (selectedEntityId_ != 0)
        ent = scene.getModelEntity(selectedEntityId_);
    if (!ent) {
        const auto& all = scene.getModelEntities();
        if (!all.empty() && all[0].hasSkin_ && all[0].skeleton)
            ent = scene.getModelEntity(all[0].entityId);
    }
    if (!ent) {
        ImGui::TextDisabled("Select a skinned model entity to edit its animator.");
        ImGui::End();
        return;
    }

    auto& clips = ent->animationClips;
    auto& ctrl = ent->animatorController;

    const bool animatorOwnedBySequencer =
        vulkanRender->isEntitySequencerAnimatorControlled(ent->entityId);
    if (animatorOwnedBySequencer) {
        ImGui::TextDisabled(
            "Sequencer Control: this Animator is driven by its bound track.");
    } else {
        ImGui::TextDisabled("Live Animator control");
    }

    if (animatorControllerAssetsEntityId_ != ent->entityId) {
        animatorControllerAssetsEntityId_ = ent->entityId;
        animatorControllerAssetsDirty_ = true;
        animatorSelectedStateIdx_ = -1;
        animatorSelectedTransitionIdx_ = -1;
        animatorEditingTransition_ = false;
        animatorRenamingClipIdx_ = -1;
        animatorClipRenameBuffer_[0] = '\0';
    }

    // 场景加载后修复：检测 Controller 是否被重新加载（路径非空但资产列表为空
    // 或选中状态未重置），同步重置所有 UI 选中/编辑状态和资产列表。
    if (!ent->animatorControllerPath.empty() && animatorControllerAssets_.empty()) {
        animatorControllerAssetsDirty_ = true;
        animatorSelectedStateIdx_ = -1;
        animatorSelectedTransitionIdx_ = -1;
        animatorEditingTransition_ = false;
        animatorRenamingClipIdx_ = -1;
        animatorClipRenameBuffer_[0] = '\0';
    }
    auto refreshControllerAssets = [&]() {
        animatorControllerAssets_ = scanCompatibleAnimatorControllers(
            vulkanRender->getResRoot(), clips);
        animatorControllerAssetsDirty_ = false;
    };
    if (animatorControllerAssetsDirty_) refreshControllerAssets();

    auto applyClipRename = [&](int clipIndex) {
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size())) return;

        const std::string oldName = clips[clipIndex].name;
        const std::string newName = trimCopy(animatorClipRenameBuffer_);
        if (newName.empty()) {
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                     "Rename failed: name cannot be empty");
            return;
        }
        if (newName == oldName) {
            animatorRenamingClipIdx_ = -1;
            return;
        }
        const bool duplicate = std::any_of(clips.begin(), clips.end(), [&](const AnimationClip& clip) {
            return clip.name == newName;
        });
        if (duplicate) {
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                     "Rename failed: clip '%s' already exists", newName.c_str());
            return;
        }
        if (ent->animationAssetPaths.empty()) {
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                     "Rename failed: model has no persisted .anim.ast asset");
            return;
        }

        // 遍历所有 .anim.ast 文件，找到包含该 clip 的文件
        AnimationAssetLoader::setResRoot(vulkanRender->getResRoot());
        std::string renameError;
        bool renameDone = false;
        for (const std::string& astPath : ent->animationAssetPaths) {
            if (AnimationAssetLoader::renameClip(astPath, oldName, newName, &renameError)) {
                renameDone = true;
                break;
            }
        }
        if (!renameDone) {
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                     "Rename failed: %s", renameError.c_str());
            return;
        }

        auto statesForRename = ctrl.states();
        auto transitionsForRename = ctrl.transitions();
        const auto paramsForRename = ctrl.params();
        std::string nextDefaultState = ctrl.currentStateName();
        const bool newStateNameAvailable = std::none_of(
            statesForRename.begin(), statesForRename.end(), [&](const AnimatorState& state) {
                return state.name == newName;
            });
        bool renamedGeneratedState = false;
        for (auto& state : statesForRename) {
            const bool generatedState =
                state.motionType == StateMotionType::SingleClip
                && state.name == oldName && state.clipName == oldName;
            if (state.clipName == oldName) state.clipName = newName;
            for (auto& sample : state.blendSamples) {
                if (sample.clipName == oldName) sample.clipName = newName;
            }
            if (generatedState && newStateNameAvailable) {
                state.name = newName;
                renamedGeneratedState = true;
            }
        }
        if (renamedGeneratedState) {
            for (auto& transition : transitionsForRename) {
                if (transition.fromState == oldName) transition.fromState = newName;
                if (transition.toState == oldName) transition.toState = newName;
            }
            if (nextDefaultState == oldName) nextDefaultState = newName;
        }

        clips[clipIndex].name = newName;
        ctrl.configure(std::move(statesForRename), std::move(transitionsForRename),
                       paramsForRename, nextDefaultState);

        bool controllerSaved = true;
        if (!ent->animatorControllerPath.empty())
            controllerSaved = ctrl.saveToFile(ent->animatorControllerPath);

        animatorControllerAssetsDirty_ = true;
        animatorRenamingClipIdx_ = -1;
        animatorClipRenameBuffer_[0] = '\0';
        if (controllerSaved) {
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                     "Renamed animation: %s -> %s", oldName.c_str(), newName.c_str());
        } else {
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                     "Renamed animation, but controller save failed: %s",
                     ent->animatorControllerPath.c_str());
        }
    };

    // ── 三区布局：左侧侧边栏 | 右侧上半 | 底部 ──────────────────────
    static float leftWidth = 220.f;
    constexpr float animatorVerticalSplitterWidth = 8.f;
    const float animatorAvailableWidth = ImGui::GetContentRegionAvail().x;
    const float animatorMinimumLeftWidth =
        std::min(180.f, animatorAvailableWidth * 0.35f);
    const float animatorMinimumRightWidth =
        std::min(360.f, animatorAvailableWidth * 0.55f);
    const float animatorMaximumLeftWidth = std::max(
        animatorMinimumLeftWidth,
        animatorAvailableWidth
            - animatorMinimumRightWidth
            - animatorVerticalSplitterWidth
            - ImGui::GetStyle().ItemSpacing.x * 2.f);
    leftWidth = std::clamp(
        leftWidth, animatorMinimumLeftWidth, animatorMaximumLeftWidth);
    const float animatorColumnHeight = std::max(
        100.f,
        ImGui::GetContentRegionAvail().y
            - ImGui::GetFrameHeightWithSpacing() * 1.5f);

    // ── 左侧侧边栏 ──────────────────────────────────────────────────────
    ImGui::BeginChild(
        "##animLeft", ImVec2(leftWidth, animatorColumnHeight), true);

    if (ImGui::CollapsingHeader("Animator Controllers", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Compatible with this model");
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh##animctrl")) refreshControllerAssets();

        ImGui::BeginChild("##animControllerAssets", ImVec2(0, 96.f), true);
        if (animatorControllerAssets_.empty()) {
            ImGui::TextDisabled("(no compatible assets)");
        } else {
            for (const std::string& assetPath : animatorControllerAssets_) {
                const std::filesystem::path path(assetPath);
                const bool active = !ent->animatorControllerPath.empty()
                    && std::filesystem::path(ent->animatorControllerPath).lexically_normal()
                        == path.lexically_normal();
                if (ImGui::Selectable(path.filename().string().c_str(), active)) {
                    if (ctrl.loadFromFile(assetPath)) {
                        // 保存相对路径（相对于 res/），便于跨平台场景序列化
                        ent->animatorControllerPath = "animators/" + path.filename().string();
                        ent->previewClipIndex = -1;
                        ent->previewTime = 0.f;
                        animatorSelectedStateIdx_ = -1;
                        animatorSelectedTransitionIdx_ = -1;
                        animatorEditingTransition_ = false;
                        snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                                 "Loaded: %s", path.filename().string().c_str());
                    } else {
                        snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                                 "Load failed: %s", path.filename().string().c_str());
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", assetPath.c_str());
            }
        }
        ImGui::EndChild();

        if (ent->animatorControllerPath.empty()) {
            ImGui::TextDisabled("Current: runtime controller");
        } else {
            ImGui::Text("Current: %s",
                        std::filesystem::path(ent->animatorControllerPath).filename().string().c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", ent->animatorControllerPath.c_str());
        }
        ImGui::Separator();
        auto continueControllerButtonRowIfFits = [](const char* nextLabel) {
            const float nextButtonWidth =
                ImGui::CalcTextSize(nextLabel).x
                + ImGui::GetStyle().FramePadding.x * 2.f;
            const float contentRight =
                ImGui::GetWindowPos().x
                + ImGui::GetWindowContentRegionMax().x;
            const float nextRight =
                ImGui::GetItemRectMax().x
                + ImGui::GetStyle().ItemSpacing.x
                + nextButtonWidth;
            if (nextRight <= contentRight)
                ImGui::SameLine();
        };
        if (ImGui::Button("New AnimController")) {
            const std::filesystem::path directory =
                std::filesystem::path(vulkanRender->getResRoot()) / "animators";
            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            const std::filesystem::path path = makeUniqueAnimatorControllerPath(
                directory, makeAnimatorControllerStem(*ent));

            AnimatorController newController;
            newController.configureFromClips(clips);
            if (!ec && newController.saveToFile(path.string())) {
                ctrl = std::move(newController);
                // 保存相对路径（相对于 res/），便于跨平台场景序列化
                ent->animatorControllerPath = "animators/" + path.filename().string();
                ent->previewClipIndex = -1;
                ent->previewTime = 0.f;
                animatorControllerAssetsDirty_ = true;
                animatorSelectedStateIdx_ = -1;
                animatorSelectedTransitionIdx_ = -1;
                animatorEditingTransition_ = false;
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Created: %s", path.filename().string().c_str());
            } else {
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Create failed: %s", path.string().c_str());
            }
        }
        continueControllerButtonRowIfFits("Save Current");
        if (ImGui::Button("Save Current")) {
            if (ent->animatorControllerPath.empty()) {
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Use New AnimController before saving");
            } else {
                std::string validationError;
                if (!validateAnimatorController(ctrl, clips, validationError)) {
                    snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                             "Save blocked: %s", validationError.c_str());
                } else if (ctrl.saveToFile(ent->animatorControllerPath)) {
                    animatorControllerAssetsDirty_ = true;
                    snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Saved: %s",
                             std::filesystem::path(ent->animatorControllerPath).filename().string().c_str());
                } else {
                    snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Save failed: %s",
                             ent->animatorControllerPath.c_str());
                }
            }
        }
        continueControllerButtonRowIfFits("Clear Controller");
        if (ImGui::Button("Clear Controller")) {
            if (!ent->animatorControllerPath.empty()) {
                // 清除绑定的控制器路径，重置为运行时控制器
                AnimatorController freshCtrl;
                freshCtrl.configureFromClips(clips);
                ent->animatorController = std::move(freshCtrl);
                ent->animatorControllerPath.clear();
                animatorSelectedStateIdx_ = -1;
                animatorSelectedTransitionIdx_ = -1;
                animatorEditingTransition_ = false;
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Controller cleared, reverted to runtime controller");
            } else {
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "No controller to clear");
            }
        }
    }

    ImGui::Spacing();
    char clipsHeader[64];
    snprintf(clipsHeader, sizeof(clipsHeader), "Animation Clips (%zu)", clips.size());
    if (ImGui::CollapsingHeader(clipsHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
        if (clips.empty()) {
            ImGui::TextDisabled("(no clips)");
        } else {
            ImGui::BeginChild("##clipList", ImVec2(0, 0), false);
            for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
                const bool isPreview = (ent->previewClipIndex == i);
                ImGui::PushID(i);
                if (animatorRenamingClipIdx_ == i) {
                    const float applyWidth = ImGui::CalcTextSize("Apply").x
                        + ImGui::GetStyle().FramePadding.x * 2.f;
                    const float cancelWidth = ImGui::CalcTextSize("Cancel").x
                        + ImGui::GetStyle().FramePadding.x * 2.f;
                    const float editWidth = std::max(
                        60.f, ImGui::GetContentRegionAvail().x - applyWidth - cancelWidth
                            - ImGui::GetStyle().ItemSpacing.x * 2.f);
                    ImGui::SetNextItemWidth(editWidth);
                    const bool enterPressed = ImGui::InputText(
                        "##clipRename", animatorClipRenameBuffer_,
                        sizeof(animatorClipRenameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine();
                    const bool applyPressed = ImGui::SmallButton("Apply");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Cancel")) {
                        animatorRenamingClipIdx_ = -1;
                        animatorClipRenameBuffer_[0] = '\0';
                    } else if (enterPressed || applyPressed) {
                        applyClipRename(i);
                    }
                } else {
                    const char* previewLabel = isPreview ? "Stop" : "Preview";
                    const float previewWidth = ImGui::CalcTextSize(previewLabel).x
                        + ImGui::GetStyle().FramePadding.x * 2.f;
                    const float renameWidth = ImGui::CalcTextSize("Rename").x
                        + ImGui::GetStyle().FramePadding.x * 2.f;
                    const float labelWidth = std::max(
                        60.f, ImGui::GetContentRegionAvail().x - previewWidth - renameWidth
                            - ImGui::GetStyle().ItemSpacing.x * 2.f);
                    char label[256];
                    snprintf(label, sizeof(label), "[%d] %s%s", i, clips[i].name.c_str(),
                             isPreview ? " [Previewing]" : "");
                    ImGui::Selectable(label, isPreview, ImGuiSelectableFlags_None,
                                      ImVec2(labelWidth, 0.f));

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        int payloadIdx = i;
                        ImGui::SetDragDropPayload("DND_ANIMCLIP", &payloadIdx, sizeof(int));
                        ImGui::Text("Drag %s to create state", clips[i].name.c_str());
                        ImGui::EndDragDropSource();
                    }

                    ImGui::SameLine();
                    ImGui::BeginDisabled(animatorOwnedBySequencer);
                    const bool previewPressed = ImGui::SmallButton(previewLabel);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)
                        && animatorOwnedBySequencer) {
                        ImGui::SetTooltip(
                            "Disable Sequencer Control to preview clips for this actor.");
                    }
                    if (previewPressed) {
                        if (isPreview) {
                            ent->previewClipIndex = -1;
                            ent->previewTime = 0.f;
                        } else {
                            ent->previewClipIndex = i;
                            ent->previewTime = 0.f;
                            ent->previewSpeed = 1.f;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Rename")) {
                        animatorRenamingClipIdx_ = i;
                        snprintf(animatorClipRenameBuffer_, sizeof(animatorClipRenameBuffer_),
                                 "%s", clips[i].name.c_str());
                    }
                    if (ImGui::IsItemHovered() && ent->animationAssetPaths.empty()) {
                        ImGui::SetTooltip("This model has no persisted .anim.ast asset");
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }

    ImGui::EndChild(); // ##animLeft
    ImGui::SameLine();

    {
        const ImVec2 splitterStart = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(
            "##animatorVerticalSplitter",
            ImVec2(animatorVerticalSplitterWidth, animatorColumnHeight));
        const bool splitterHovered = ImGui::IsItemHovered();
        const bool splitterActive = ImGui::IsItemActive();
        if (splitterHovered || splitterActive)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (splitterActive) {
            leftWidth = std::clamp(
                leftWidth + ImGui::GetIO().MouseDelta.x,
                animatorMinimumLeftWidth,
                animatorMaximumLeftWidth);
        }

        const ImU32 splitterColor = splitterActive
            ? IM_COL32(120, 170, 230, 255)
            : splitterHovered
                ? IM_COL32(100, 130, 170, 255)
                : IM_COL32(70, 70, 70, 255);
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(
                splitterStart.x + animatorVerticalSplitterWidth * 0.5f,
                splitterStart.y),
            ImVec2(
                splitterStart.x + animatorVerticalSplitterWidth * 0.5f,
                splitterStart.y + animatorColumnHeight),
            splitterColor, splitterActive ? 2.f : 1.f);
    }
    ImGui::SameLine();

    // ── 右侧区域 ──────────────────────────────────────────────────────────
    ImGui::BeginGroup();

    const std::vector<AnimatorState> states = ctrl.states();
    const int selState = animatorSelectedStateIdx_;

    // ── 右侧上半：双栏 States | Outgoing Transitions ──────────────────────
    const float halfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    ImGui::BeginChild("##statesList", ImVec2(halfW, 200.f), true);
    ImGui::Text("States");
    ImGui::Separator();
    if (states.empty()) {
        ImGui::TextDisabled("(no states - drag clips here)");
    } else {
        for (int i = 0; i < static_cast<int>(states.size()); ++i) {
            const bool isCurrent = (states[i].name == ctrl.currentStateName());
            const bool isSelected = (i == animatorSelectedStateIdx_);
            ImGui::PushID(i);
            char label[256];
            snprintf(label, sizeof(label), "%s%s##state_%d",
                     isCurrent ? "[Active] " : "[State] ",
                     states[i].name.c_str(), i);
            if (ImGui::Selectable(label, isSelected)) {
                animatorSelectedStateIdx_ = i;
                animatorSelectedTransitionIdx_ = -1;
                animatorEditingTransition_ = false;
            }
            ImGui::PopID();

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ANIMCLIP")) {
                    const int clipIdx = *static_cast<const int*>(payload->Data);
                    if (clipIdx >= 0 && clipIdx < static_cast<int>(clips.size())) {
                        auto ns = ctrl.states();
                        AnimatorState s;
                        s.name = clips[clipIdx].name;
                        s.clipName = clips[clipIdx].name;
                        ns.push_back(std::move(s));
                        ctrl.configure(ns, ctrl.transitions(), ctrl.params(), s.name);
                        snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                                 "Created state: %s", clips[clipIdx].name.c_str());
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ANIMCLIP")) {
                const int clipIdx = *static_cast<const int*>(payload->Data);
                if (clipIdx >= 0 && clipIdx < static_cast<int>(clips.size())) {
                    auto ns = ctrl.states();
                    AnimatorState s;
                    s.name = clips[clipIdx].name;
                    s.clipName = clips[clipIdx].name;
                    ns.push_back(std::move(s));
                    ctrl.configure(ns, ctrl.transitions(), ctrl.params(), s.name);
                    snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                             "Created state: %s", clips[clipIdx].name.c_str());
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##transitionsList", ImVec2(0, 200.f), true);
    ImGui::Text("Transitions (Outgoing)");
    ImGui::Separator();
    if (selState < 0 || selState >= static_cast<int>(states.size())) {
        ImGui::TextDisabled("Select a state to view outgoing transitions");
    } else {
        const std::string& stateName = states[selState].name;
        bool any = false;
        const auto& transRef = ctrl.transitions();
        for (int ti = 0; ti < static_cast<int>(transRef.size()); ++ti) {
            const auto& t = transRef[ti];
            if (!t.fromState.empty() && t.fromState != stateName) continue;
            if (t.toState == stateName) continue;
            any = true;
            const bool sel = (ti == animatorSelectedTransitionIdx_);
            ImGui::PushID(ti);
            char buf[256];
            snprintf(buf, sizeof(buf), "%s -> %s##trans_%d",
                     t.fromState.empty() ? "(Any)" : t.fromState.c_str(),
                     t.toState.c_str(), ti);
            if (ImGui::Selectable(buf, sel)) {
                animatorSelectedTransitionIdx_ = ti;
                animatorEditingTransition_ = true;
            }
            ImGui::PopID();
        }
        if (!any)
            ImGui::TextDisabled("(no transitions from this state)");

        ImGui::Separator();
        if (ImGui::Button("+ Add Transition")) {
            AnimatorTransition newT;
            newT.fromState = (selState < static_cast<int>(states.size()))
                ? states[selState].name : std::string{};
            for (const auto& s : states) {
                if (s.name != newT.fromState) {
                    newT.toState = s.name;
                    break;
                }
            }
            auto nt = ctrl.transitions();
            nt.push_back(std::move(newT));
            ctrl.configure(ctrl.states(), std::move(nt), ctrl.params(), ctrl.currentStateName());
            animatorSelectedTransitionIdx_ = static_cast<int>(ctrl.transitions().size()) - 1;
            animatorEditingTransition_ = true;
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Added transition");
        }
    }
    ImGui::EndChild();

    // ── 底部：Transition 编辑区 + 参数面板 + 状态信息 ────────────────────
    ImGui::BeginChild("##animatorBottom", ImVec2(0, 0), true);
    const float bottomHalfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    // ── 底部左栏：编辑区 ──────────────────────────────────────────────────
    ImGui::BeginChild("##editBottomLeft", ImVec2(bottomHalfW, 0), true);

    if (animatorEditingTransition_ && animatorSelectedTransitionIdx_ >= 0
        && animatorSelectedTransitionIdx_ < static_cast<int>(ctrl.transitions().size()))
    {
        auto& t = const_cast<std::vector<AnimatorTransition>&>(ctrl.transitions())[animatorSelectedTransitionIdx_];
        ImGui::Text("Transition: %s -> %s", t.fromState.c_str(), t.toState.c_str());
        ImGui::Separator();

        if (ImGui::BeginCombo("To##editTransTo", t.toState.c_str())) {
            for (const auto& s : ctrl.states()) {
                if (s.name == t.fromState) continue;
                const bool ssel = (s.name == t.toState);
                if (ImGui::Selectable(s.name.c_str(), ssel))
                    t.toState = s.name;
                if (ssel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::DragFloat("Fade (s)", &t.fadeDuration, 0.01f, 0.f, 10.f, "%.2f");
        ImGui::Checkbox("Has Exit Time", &t.hasExitTime);
        if (t.hasExitTime)
            ImGui::DragFloat("Exit Time", &t.exitTime, 0.01f, 0.f, 1.f, "%.2f");

        const char* curveNames[] = { "Linear", "SmoothStep", "EaseIn", "EaseOut" };
        int curveIdx = static_cast<int>(t.blendCurve);
        if (ImGui::Combo("Blend Curve", &curveIdx, curveNames, IM_ARRAYSIZE(curveNames)))
            t.blendCurve = static_cast<BlendCurve>(curveIdx);

        ImGui::Separator();
        ImGui::Text("Conditions");
        const auto& allParams = ctrl.params();
        int condDel = -1;
        for (int ci = 0; ci < static_cast<int>(t.conditions.size()); ++ci) {
            auto& c = t.conditions[ci];
            ImGui::PushID(ci);

            // 查找当前 param 的类型
            AnimatorParam::Type paramType = AnimatorParam::Type::Float;
            const auto& allParams = ctrl.params();
            int paramIdx = -1;
            for (int pi = 0; pi < static_cast<int>(allParams.size()); ++pi) {
                if (allParams[pi].name == c.paramName) {
                    paramIdx = pi;
                    paramType = allParams[pi].type;
                    break;
                }
            }

            char labelBuf[128];
            if (!allParams.empty()) {
                snprintf(labelBuf, sizeof(labelBuf), "Param##cond_%d", ci);
                ImGui::SetNextItemWidth(100);
                if (ImGui::Combo(labelBuf, &paramIdx,
                                 [](void* data, int idx, const char** out) -> bool {
                                     const auto& params = *static_cast<const std::vector<AnimatorParam>*>(data);
                                     if (idx < 0 || idx >= static_cast<int>(params.size())) return false;
                                     *out = params[idx].name.c_str();
                                     return true;
                                 }, const_cast<std::vector<AnimatorParam>*>(&allParams),
                                 static_cast<int>(allParams.size())))
                {
                    if (paramIdx >= 0 && paramIdx < static_cast<int>(allParams.size())) {
                        c.paramName = allParams[paramIdx].name;
                        paramType = allParams[paramIdx].type;
                        // 切换 param 时重置 operator
                        switch (paramType) {
                        case AnimatorParam::Type::Float:
                            c.op = TransitionCondition::Op::Greater;
                            break;
                        case AnimatorParam::Type::Bool:
                            c.op = TransitionCondition::Op::Equal;
                            break;
                        case AnimatorParam::Type::Trigger:
                            c.op = TransitionCondition::Op::True;
                            break;
                        default:
                            c.op = TransitionCondition::Op::Equal;
                            break;
                        }
                        c.threshold = 0.f;
                    }
                }
            } else {
                char paramBuf[128];
                snprintf(paramBuf, sizeof(paramBuf), "%s", c.paramName.c_str());
                snprintf(labelBuf, sizeof(labelBuf), "Param##cond_%d", ci);
                if (ImGui::InputText(labelBuf, paramBuf, sizeof(paramBuf)))
                    c.paramName = paramBuf;
            }

            ImGui::SameLine();

            // 根据 param type 显示不同的 Operator 列表
            switch (paramType) {
            case AnimatorParam::Type::Float: {
                const char* floatOps[] = { "Greater", "Less", "Equal", "NotEqual" };
                int opIdx = static_cast<int>(c.op);
                if (opIdx > 3) opIdx = 0;
                snprintf(labelBuf, sizeof(labelBuf), "Op##cond_%d", ci);
                ImGui::SetNextItemWidth(80);
                if (ImGui::Combo(labelBuf, &opIdx, floatOps, IM_ARRAYSIZE(floatOps)))
                    c.op = static_cast<TransitionCondition::Op>(opIdx);
                ImGui::SameLine();
                snprintf(labelBuf, sizeof(labelBuf), "Val##cond_%d", ci);
                ImGui::SetNextItemWidth(70);
                ImGui::DragFloat(labelBuf, &c.threshold, 0.01f);
                break;
            }
            case AnimatorParam::Type::Bool: {
                const char* boolOps[] = { "Equal", "NotEqual", "True", "False" };
                int opIdx = static_cast<int>(c.op) - 2;
                if (opIdx < 0) opIdx = 0;
                snprintf(labelBuf, sizeof(labelBuf), "Op##cond_%d", ci);
                if (ImGui::Combo(labelBuf, &opIdx, boolOps, IM_ARRAYSIZE(boolOps)))
                    c.op = static_cast<TransitionCondition::Op>(opIdx + 2);
                break;
            }
            case AnimatorParam::Type::Trigger: {
                const char* triggerOps[] = { "True", "False" };
                int opIdx = static_cast<int>(c.op) - 4;
                if (opIdx < 0) opIdx = 0;
                snprintf(labelBuf, sizeof(labelBuf), "Op##cond_%d", ci);
                if (ImGui::Combo(labelBuf, &opIdx, triggerOps, IM_ARRAYSIZE(triggerOps)))
                    c.op = static_cast<TransitionCondition::Op>(opIdx + 4);
                break;
            }
            default:
                break;
            }

            ImGui::SameLine();
            snprintf(labelBuf, sizeof(labelBuf), "X##delCond_%d", ci);
            if (ImGui::SmallButton(labelBuf)) { condDel = ci; }
            ImGui::PopID();
        }
        if (condDel >= 0)
            t.conditions.erase(t.conditions.begin() + condDel);

        if (ImGui::Button("+ Add Condition")) {
            TransitionCondition newCond;
            if (!allParams.empty()) {
                newCond.paramName = allParams[0].name;
                switch (allParams[0].type) {
                case AnimatorParam::Type::Float:   newCond.op = TransitionCondition::Op::Greater; break;
                case AnimatorParam::Type::Bool:    newCond.op = TransitionCondition::Op::Equal; break;
                case AnimatorParam::Type::Trigger: newCond.op = TransitionCondition::Op::True; break;
                }
            }
            t.conditions.push_back(std::move(newCond));
        }

        ImGui::Separator();
        if (ImGui::Button("Delete Transition")) {
            auto nt = ctrl.transitions();
            if (animatorSelectedTransitionIdx_ < static_cast<int>(nt.size())) {
                nt.erase(nt.begin() + animatorSelectedTransitionIdx_);
                ctrl.configure(ctrl.states(), std::move(nt), ctrl.params(), ctrl.currentStateName());
                animatorSelectedTransitionIdx_ = -1;
                animatorEditingTransition_ = false;
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Transition deleted");
            }
        }

    } else if (selState >= 0 && selState < static_cast<int>(ctrl.states().size())) {
        auto& s = const_cast<std::vector<AnimatorState>&>(ctrl.states())[selState];
        const bool isCurrent = (s.name == ctrl.currentStateName());
        ImGui::Text("State Properties");
        ImGui::Separator();

        char nameBuf[256];
        snprintf(nameBuf, sizeof(nameBuf), "Name: %s", s.name.c_str());
        ImGui::TextUnformatted(nameBuf);

        const char* motionNames[] = { "Single Clip", "Blend Space 1D" };
        int motionIndex = static_cast<int>(s.motionType);
        if (ImGui::Combo("Motion Type##stateMotion", &motionIndex,
                         motionNames, IM_ARRAYSIZE(motionNames))) {
            s.motionType = static_cast<StateMotionType>(motionIndex);
        }

        const char* directionNames[] = { "Forward", "Reverse" };
        int directionIndex = static_cast<int>(s.direction);
        if (ImGui::Combo("Direction##stateDirection", &directionIndex,
                         directionNames, IM_ARRAYSIZE(directionNames))) {
            s.direction = static_cast<PlaybackDirection>(directionIndex);
        }
        ImGui::DragFloat("Play Rate##statePlayRate", &s.playRate,
                         0.01f, 0.f, 10.f, "%.2f");
        ImGui::Checkbox("Loop##stateLoop", &s.loop);

        if (s.motionType == StateMotionType::SingleClip) {
            const char* preview = s.clipName.empty() ? "(select clip)" : s.clipName.c_str();
            if (ImGui::BeginCombo("Clip##stateClip", preview)) {
                for (const auto& clip : clips) {
                    const bool selected = clip.name == s.clipName;
                    if (ImGui::Selectable(clip.name.c_str(), selected))
                        s.clipName = clip.name;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else {
            const char* parameterPreview = s.blendParameter.empty()
                ? "(select Float parameter)"
                : s.blendParameter.c_str();
            if (ImGui::BeginCombo("Blend Parameter##blendParam", parameterPreview)) {
                for (const auto& param : ctrl.params()) {
                    if (param.type != AnimatorParam::Type::Float) continue;
                    const bool selected = param.name == s.blendParameter;
                    if (ImGui::Selectable(param.name.c_str(), selected))
                        s.blendParameter = param.name;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::TextUnformatted("Samples");
            int sampleToDelete = -1;
            for (int sampleIndex = 0;
                 sampleIndex < static_cast<int>(s.blendSamples.size());
                 ++sampleIndex) {
                auto& sample = s.blendSamples[sampleIndex];
                ImGui::PushID(sampleIndex);
                ImGui::SetNextItemWidth(150.f);
                const char* samplePreview = sample.clipName.empty()
                    ? "(select clip)"
                    : sample.clipName.c_str();
                if (ImGui::BeginCombo("##blendClip", samplePreview)) {
                    for (const auto& clip : clips) {
                        const bool selected = clip.name == sample.clipName;
                        if (ImGui::Selectable(clip.name.c_str(), selected))
                            sample.clipName = clip.name;
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.f);
                ImGui::DragFloat("##blendPosition", &sample.position,
                                 0.01f, -1000.f, 1000.f, "%.3f");
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) sampleToDelete = sampleIndex;
                ImGui::PopID();
            }
            if (sampleToDelete >= 0) {
                s.blendSamples.erase(s.blendSamples.begin() + sampleToDelete);
            }
            if (ImGui::Button("+ Add Sample")) {
                BlendSpace1DSample sample;
                if (!clips.empty()) sample.clipName = clips.front().name;
                sample.position = s.blendSamples.empty()
                    ? 0.f
                    : s.blendSamples.back().position + 1.f;
                s.blendSamples.push_back(std::move(sample));
            }
            ImGui::SameLine();
            if (ImGui::Button("Sort Samples")) {
                std::stable_sort(
                    s.blendSamples.begin(), s.blendSamples.end(),
                    [](const BlendSpace1DSample& a, const BlendSpace1DSample& b) {
                        return a.position < b.position;
                    });
            }

            bool duplicatePosition = false;
            auto sortedSamples = s.blendSamples;
            std::stable_sort(
                sortedSamples.begin(), sortedSamples.end(),
                [](const BlendSpace1DSample& a, const BlendSpace1DSample& b) {
                    return a.position < b.position;
                });
            for (size_t i = 1; i < sortedSamples.size(); ++i) {
                if (std::abs(sortedSamples[i].position
                             - sortedSamples[i - 1].position) <= 1e-5f) {
                    duplicatePosition = true;
                    break;
                }
            }

            if (!sortedSamples.empty()) {
                float minPosition = sortedSamples.front().position;
                float maxPosition = sortedSamples.back().position;
                if (std::abs(maxPosition - minPosition) <= 1e-5f) {
                    minPosition -= 0.5f;
                    maxPosition += 0.5f;
                }
                float axisValue = 0.f;
                for (const auto& param : ctrl.params()) {
                    if (param.type == AnimatorParam::Type::Float
                        && param.name == s.blendParameter) {
                        axisValue = param.value.f;
                        break;
                    }
                }

                const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                const ImVec2 canvasSize(
                    std::max(120.f, ImGui::GetContentRegionAvail().x), 34.f);
                ImGui::InvisibleButton("##blendSpacePreview", canvasSize);
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const float left = canvasPos.x + 8.f;
                const float right = canvasPos.x + canvasSize.x - 8.f;
                const float centerY = canvasPos.y + 17.f;
                drawList->AddLine(
                    ImVec2(left, centerY), ImVec2(right, centerY),
                    IM_COL32(150, 150, 150, 255), 2.f);
                auto positionToX = [&](float value) {
                    const float alpha = std::clamp(
                        (value - minPosition) / (maxPosition - minPosition),
                        0.f, 1.f);
                    return left + (right - left) * alpha;
                };
                for (const auto& sample : sortedSamples) {
                    const float x = positionToX(sample.position);
                    drawList->AddLine(
                        ImVec2(x, centerY - 7.f), ImVec2(x, centerY + 7.f),
                        IM_COL32(110, 190, 255, 255), 2.f);
                }
                const float markerX = positionToX(axisValue);
                drawList->AddCircleFilled(
                    ImVec2(markerX, centerY), 4.f,
                    IM_COL32(255, 210, 70, 255));
            }

            if (s.blendParameter.empty())
                ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f),
                                   "BlendSpace requires a Float parameter.");
            if (s.blendSamples.empty())
                ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f),
                                   "BlendSpace requires at least one sample.");
            if (duplicatePosition)
                ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f),
                                   "Sample positions must be unique.");
        }

        ImGui::Separator();
        ImGui::Text("Root Motion");
        const char* rootMotionNames[] = { "None", "Locked", "Follow" };
        int rmIdx = static_cast<int>(s.rootMotion);
        if (ImGui::Combo("Mode##rootMotion", &rmIdx, rootMotionNames, IM_ARRAYSIZE(rootMotionNames)))
            s.rootMotion = static_cast<AnimatorState::RootMotionMode>(rmIdx);

        if (s.rootMotion != AnimatorState::RootMotionMode::None) {
            const auto& skeleton = ent->skeleton;
            if (skeleton && !skeleton->bones.empty()) {
                int boneIdx = 0;
                if (!s.rootBoneName.empty()) {
                    auto it = skeleton->boneNameToIndex.find(s.rootBoneName);
                    if (it != skeleton->boneNameToIndex.end())
                        boneIdx = it->second;
                }
                if (ImGui::Combo("Root Bone##rootBone", &boneIdx,
                                 [](void* data, int idx, const char** out) -> bool {
                                     const auto& bones = *static_cast<const std::vector<Bone>*>(data);
                                     if (idx < 0 || idx >= static_cast<int>(bones.size())) return false;
                                     *out = bones[idx].name.c_str();
                                     return true;
                                 }, const_cast<std::vector<Bone>*>(&skeleton->bones),
                                 static_cast<int>(skeleton->bones.size())))
                {
                    s.rootBoneName = skeleton->bones[boneIdx].name;
                }
            }
        }

        if (!isCurrent) {
            ImGui::SameLine();
            if (ImGui::Button("Set Active State")) {
                ctrl.setActiveState(s.name);
            }
        }

        bool isDefaultState = (s.name == ctrl.defaultStateName());
        if (!isDefaultState) {
            ImGui::SameLine();
            if (ImGui::Button("Set as Default State")) {
                ctrl.configure(ctrl.states(), ctrl.transitions(), ctrl.params(), s.name);
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Default state set to: %s", s.name.c_str());
            }
        }

        if (ImGui::Button("Delete State")) {
            auto ns = ctrl.states();
            if (selState < static_cast<int>(ns.size())) {
                const std::string delName = ns[selState].name;
                ns.erase(ns.begin() + selState);
                auto nt = ctrl.transitions();
                nt.erase(std::remove_if(nt.begin(), nt.end(),
                            [&](const AnimatorTransition& tr) {
                                return tr.fromState == delName || tr.toState == delName;
                            }), nt.end());
                ctrl.configure(std::move(ns), std::move(nt), ctrl.params(), ctrl.currentStateName());
                animatorSelectedStateIdx_ = -1;
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "State deleted: %s", delName.c_str());
            }
        }
    } else {
        ImGui::TextDisabled("Select a state or transition to edit.");
    }

    // Quick Params
    ImGui::Separator();
    ImGui::Text("Parameters");
    {
        auto& params = const_cast<std::vector<AnimatorParam>&>(ctrl.params());
        int paramDel = -1;
        for (int pi = 0; pi < static_cast<int>(params.size()); ++pi) {
            auto& p = params[pi];
            ImGui::PushID(pi);
            char nameBuf[128];
            snprintf(nameBuf, sizeof(nameBuf), "%s", p.name.c_str());
            ImGui::SetNextItemWidth(100);
            if (ImGui::InputText("##paramName", nameBuf, sizeof(nameBuf))) {
                const std::string oldName = p.name;
                const std::string newName = nameBuf;
                if (!newName.empty() && oldName != newName) {
                    if (ctrl.renameParameter(oldName, newName)) {
                        auto& sequence = vulkanRender->getCurrentSequence();
                        for (auto& track : sequence.tracks) {
                            if (track.type != TrackType::AnimatorKeyframe
                                || track.animatorTrack.targetEntityId != ent->entityId) {
                                continue;
                            }
                            for (auto& keyframe : track.animatorTrack.keyframes) {
                                for (auto& event : keyframe.events) {
                                    if (event.paramName == oldName)
                                        event.paramName = newName;
                                }
                            }
                        }
                        vulkanRender->requestSequencerPreview();
                    }
                }
            }

            ImGui::SameLine();
            switch (p.type) {
            case AnimatorParam::Type::Float: {
                float v = p.value.f;
                ImGui::SetNextItemWidth(80);
                if (ImGui::DragFloat("##paramVal", &v, 0.01f))
                    p.setFloat(v);
                break;
            }
            case AnimatorParam::Type::Int: {
                int v = p.value.i;
                ImGui::SetNextItemWidth(60);
                if (ImGui::DragInt("##paramVal", &v, 1))
                    p.setInt(v);
                break;
            }
            case AnimatorParam::Type::Bool: {
                bool v = p.value.b;
                if (ImGui::Checkbox("##paramVal", &v))
                    p.setBool(v);
                break;
            }
            case AnimatorParam::Type::Trigger: {
                if (ImGui::Button("Trigger##paramVal"))
                    p.setTrigger();
                break;
            }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X##delParam")) { paramDel = pi; }
            ImGui::PopID();
        }
        if (paramDel >= 0) {
            const std::string deletedName = ctrl.params()[paramDel].name;
            bool referenced = std::any_of(
                ctrl.states().begin(), ctrl.states().end(),
                [&](const AnimatorState& state) {
                    return state.motionType == StateMotionType::BlendSpace1D
                        && state.blendParameter == deletedName;
                });
            if (!referenced) {
                referenced = std::any_of(
                    ctrl.transitions().begin(), ctrl.transitions().end(),
                    [&](const AnimatorTransition& transition) {
                        return std::any_of(
                            transition.conditions.begin(), transition.conditions.end(),
                            [&](const TransitionCondition& condition) {
                                return condition.paramName == deletedName;
                            });
                    });
            }
            if (!referenced) {
                for (const auto& track : vulkanRender->getCurrentSequence().tracks) {
                    if (track.type != TrackType::AnimatorKeyframe
                        || track.animatorTrack.targetEntityId != ent->entityId) {
                        continue;
                    }
                    for (const auto& keyframe : track.animatorTrack.keyframes) {
                        referenced = std::any_of(
                            keyframe.events.begin(), keyframe.events.end(),
                            [&](const AnimatorParamEvent& event) {
                                return event.paramName == deletedName;
                            });
                        if (referenced) break;
                    }
                    if (referenced) break;
                }
            }

            if (referenced) {
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                         "Parameter '%s' is still referenced and cannot be deleted",
                         deletedName.c_str());
            } else {
                auto np = ctrl.params();
                np.erase(np.begin() + paramDel);
                ctrl.configure(ctrl.states(), ctrl.transitions(),
                               std::move(np), ctrl.currentStateName());
            }
        }
    }

    ImGui::EndChild(); // ##editBottomLeft
    ImGui::SameLine();

    // ── 底部右栏：状态信息 + 控制按钮 ──────────────────────────────────
    ImGui::BeginChild("##editBottomRight", ImVec2(0, 0), true);
    ImGui::Text("State Machine Status");
    ImGui::Separator();
    ImGui::Text("Current State: %s", ctrl.currentStateName().c_str());
    if (ctrl.isTransitioning()) {
        ImGui::Text("Transitioning -> %s (%.1f%%)", ctrl.nextStateName().c_str(),
                    ctrl.blendProgress() * 100.f);
        ImGui::ProgressBar(ctrl.blendProgress(), ImVec2(-1, 0));
    } else {
        ImGui::ProgressBar(0.f, ImVec2(-1, 0), "Stable");
    }
    ImGui::Text("States: %zu", ctrl.states().size());
    ImGui::Text("Transitions: %zu", ctrl.transitions().size());
    ImGui::Text("Params: %zu", ctrl.params().size());
    ImGui::Text("Clips in entity: %zu", clips.size());

    ImGui::Separator();
    if (ImGui::Button("+ Add Float Param")) {
        auto np = ctrl.params();
        AnimatorParam ap;
        ap.name = "NewFloat";
        ap.type = AnimatorParam::Type::Float;
        ap.value.f = 0.f;
        np.push_back(std::move(ap));
        ctrl.configure(ctrl.states(), ctrl.transitions(), std::move(np), ctrl.currentStateName());
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Add Bool Param")) {
        auto np = ctrl.params();
        AnimatorParam ap;
        ap.name = "NewBool";
        ap.type = AnimatorParam::Type::Bool;
        ap.value.b = false;
        np.push_back(std::move(ap));
        ctrl.configure(ctrl.states(), ctrl.transitions(), std::move(np), ctrl.currentStateName());
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Trigger")) {
        auto np = ctrl.params();
        AnimatorParam ap;
        ap.name = "NewTrigger";
        ap.type = AnimatorParam::Type::Trigger;
        ap.value.b = false;
        np.push_back(std::move(ap));
        ctrl.configure(ctrl.states(), ctrl.transitions(), std::move(np), ctrl.currentStateName());
    }

    if (!clips.empty()) {
        static int newStateClipIdx = 0;
        if (newStateClipIdx >= static_cast<int>(clips.size()))
            newStateClipIdx = 0;
        ImGui::Separator();
        // Safety: const_cast is safe here because the lambda only reads
        auto& nonConstClips = const_cast<std::vector<AnimationClip>&>(clips);
        ImGui::Combo("Clip##newState", &newStateClipIdx,
                     [](void* data, int idx, const char** out) -> bool {
                         if (!data) return false;
                         const auto& c = *static_cast<const std::vector<AnimationClip>*>(data);
                         if (idx < 0 || idx >= static_cast<int>(c.size())) return false;
                         *out = c[idx].name.c_str();
                         return true;
                     }, &nonConstClips,
                     static_cast<int>(clips.size()));
        if (ImGui::Button("+ Add State") && newStateClipIdx < static_cast<int>(clips.size())) {
            auto ns = ctrl.states();
            AnimatorState as;
            as.name = clips[newStateClipIdx].name;
            as.clipName = clips[newStateClipIdx].name;
            ns.push_back(std::move(as));
            ctrl.configure(ns, ctrl.transitions(), ctrl.params(), as.name);
            animatorSelectedStateIdx_ = static_cast<int>(ctrl.states().size()) - 1;
            snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_),
                     "Added state: %s", as.name.c_str());
        }
    }

    if (animatorStatusMsg_[0]) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", animatorStatusMsg_);
    }

    ImGui::Separator();
    if (ImGui::Button("Reset Controller")) {
        ctrl.reset();
        ent->previewClipIndex = -1;
        ent->previewTime = 0.f;
        snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Controller reset");
    }

    ImGui::EndChild(); // ##editBottomRight
    ImGui::EndChild(); // ##animatorBottom

    ImGui::EndGroup();

    ImGui::End();
}

void UIManager::drawPropertiesPanel()
{
    ImGui::SetNextWindowSize(ImVec2(280, 350), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Properties", &showPropertiesPanel_)) {
        ImGui::End();
        return;
    }

    if (!vulkanRender) {
        ImGui::TextDisabled("(unavailable)");
        ImGui::End();
        return;
    }

    auto& scene = vulkanRender->getSceneManager();

    // 确定当前选中的目标
    SceneManager::ModelEntity* ent = nullptr;
    if (selectedEntityId_ != 0)
        ent = scene.getModelEntity(selectedEntityId_);

    if (!ent && vulkanRender->pickedBoxEntityId == 0) {
        // 无选中：尝试 fallback 到 entity 0
        if (!scene.getModelEntities().empty())
            ent = scene.getModelEntity(scene.getModelEntities()[0].entityId);
    }

    if (!ent && vulkanRender->pickedBoxEntityId == 0) {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    if (ent) {
        // ── 模型实体属性 ──────────────────────────────────────────────
        ImGui::Text("Model Entity: %s", ent->displayName.c_str());
        ImGui::Separator();

        // Name
        char nameBuf[256];
        snprintf(nameBuf, sizeof(nameBuf), "%s", ent->displayName.c_str());
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            ent->displayName = nameBuf;

        // Visibility
        ImGui::Checkbox("Visible", &ent->visible);

        // Transform
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            float pos[3] = { ent->transform.position.x,
                             ent->transform.position.y,
                             ent->transform.position.z };
            if (ImGui::DragFloat3("Position", pos, 0.1f)) {
                ent->transform.position = { pos[0], pos[1], pos[2] };
                scene.setEntityTransform(ent->entityId, ent->transform);
            }

            glm::vec3 euler = glm::eulerAngles(ent->transform.rotation);
            euler = glm::degrees(euler);
            float rot[3] = { euler.x, euler.y, euler.z };
            if (ImGui::DragFloat3("Rotation", rot, 1.f, -180.f, 180.f)) {
                ent->transform.rotation = glm::quat(glm::radians(glm::vec3(rot[0], rot[1], rot[2])));
                scene.setEntityTransform(ent->entityId, ent->transform);
            }

            float scl[3] = { ent->transform.scale.x,
                             ent->transform.scale.y,
                             ent->transform.scale.z };
            if (ImGui::DragFloat3("Scale", scl, 0.05f, 0.01f, 100.f)) {
                ent->transform.scale = { scl[0], scl[1], scl[2] };
                scene.setEntityTransform(ent->entityId, ent->transform);
            }
        }

        // Material
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& matMgr = vulkanRender->getMaterialManager();
            const auto& allIds = matMgr.getAllMaterialIds();

            // 确保实体持有有效材质 ID
            if (!matMgr.isValid(ent->materialId))
                ent->materialId = matMgr.getDefaultMeshMaterialId();

            if (allIds.empty()) {
                ImGui::TextDisabled("No materials");
            } else {
                int currentMatIndex = -1;
                for (int mi = 0; mi < (int)allIds.size(); ++mi) {
                    if (allIds[mi] == ent->materialId) { currentMatIndex = mi; break; }
                }
                if (currentMatIndex < 0) currentMatIndex = 0;

                std::vector<const char*> names;
                names.reserve(allIds.size());
                for (MaterialId mid : allIds)
                    names.push_back(matMgr.getMaterialName(mid).c_str());

                ImGui::Text("Slot");
                ImGui::SameLine();
                if (ImGui::Combo("##matSlot", &currentMatIndex, names.data(), (int)names.size())) {
                    if (currentMatIndex >= 0 && currentMatIndex < (int)allIds.size()) {
                        ent->materialId = allIds[currentMatIndex];
                        scene.setEntityMaterial(ent->entityId, ent->materialId);
                    }
                }
            }

            // ── 材质参数编辑 ──────────────────────────────────────────
            const MaterialId editId = ent->materialId;
            const MaterialType materialType = matMgr.getMaterialType(editId);
            const bool supportsTextures = materialType == MaterialType::Mesh
                || materialType == MaterialType::Material;

            ImGui::Separator();
            MaterialParams& p = matMgr.getParamsMut(editId);

            char nameBuf[256];
            snprintf(nameBuf, sizeof(nameBuf), "%s", matMgr.getMaterialName(editId).c_str());
            if (ImGui::InputText("Name##matEdit", nameBuf, sizeof(nameBuf)))
                matMgr.setMaterialName(editId, nameBuf);

            ImGui::ColorEdit4("Base Color##matEdit",          &p.baseColor.x);
            ImGui::SliderFloat("Roughness##matEdit",          &p.roughness,          0.f, 1.f);
            ImGui::SliderFloat("Metallic##matEdit",           &p.metallic,           0.f, 1.f);
            ImGui::SliderFloat("Emissive Intensity##matEdit", &p.emissiveIntensity,  0.f, 10.f);
            ImGui::ColorEdit3("Emissive Color##matEdit",      &p.emissiveColor.x);

            if (supportsTextures) {
                // 仅在切换材质时同步，不能每帧覆盖用户正在编辑的输入框。
                if (texturePathMaterialId_ != editId) {
                    snprintf(matAlbedoPath_, sizeof(matAlbedoPath_), "%s",
                             matMgr.getAlbedoPath(editId).c_str());
                    snprintf(matNormalPath_, sizeof(matNormalPath_), "%s",
                             matMgr.getNormalPath(editId).c_str());
                    texturePathMaterialId_ = editId;
                    textureStatusMsg_.clear();
                }

                ImGui::Separator();
                ImGui::Text("Textures");
                ImGui::InputText("Albedo##matEdit", matAlbedoPath_, sizeof(matAlbedoPath_));
                ImGui::SameLine();
                if (ImGui::Button("Load##albedoEdit")) {
                    const bool ok = vulkanRender->setMaterialAlbedo(editId, matAlbedoPath_);
                    textureStatusMsg_ = ok ? "Albedo loaded" : "Albedo load failed; see console";
                    if (ok) {
                        snprintf(matAlbedoPath_, sizeof(matAlbedoPath_), "%s",
                                 matMgr.getAlbedoPath(editId).c_str());
                    }
                }

                ImGui::InputText("Normal##matEdit", matNormalPath_, sizeof(matNormalPath_));
                ImGui::SameLine();
                if (ImGui::Button("Load##normalEdit")) {
                    const bool ok = vulkanRender->setMaterialNormal(editId, matNormalPath_);
                    textureStatusMsg_ = ok ? "Normal loaded" : "Normal load failed; see console";
                    if (ok) {
                        snprintf(matNormalPath_, sizeof(matNormalPath_), "%s",
                                 matMgr.getNormalPath(editId).c_str());
                    }
                }
                if (!textureStatusMsg_.empty())
                    ImGui::TextDisabled("%s", textureStatusMsg_.c_str());
            }

            if (matMgr.isDeletable(editId)) {
                ImGui::Separator();
                if (ImGui::Button("Delete Material")) {
                    vulkanRender->destroyMaterial(editId);
                    ent->materialId = matMgr.getDefaultMeshMaterialId();
                    matAlbedoPath_[0] = '\0';
                    matNormalPath_[0] = '\0';
                    texturePathMaterialId_ = 0;
                    textureStatusMsg_.clear();
                }
            }
        }
    } else {
        // ── Box 属性 ───────────────────────────────────────────────────
        ImGui::Text("Box Entity: %llu",
                    static_cast<unsigned long long>(vulkanRender->pickedBoxEntityId));
        ImGui::Separator();

        glm::vec3 pos = vulkanRender->getBoxPosition(vulkanRender->pickedBoxEntityId);
        float fpos[3] = { pos.x, pos.y, pos.z };
        if (ImGui::DragFloat3("Position", fpos, 0.1f))
            vulkanRender->setBoxPosition(vulkanRender->pickedBoxEntityId,
                                         glm::vec3(fpos[0], fpos[1], fpos[2]));
    }

    ImGui::End();
}
