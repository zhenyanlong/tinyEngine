#include "IMGUIManager.hpp"
#include "Application.hpp"
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
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
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
	initImGuiVulkanBackend();
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

	if (vulkanRender != nullptr) {
		ImGuiIO& io = ImGui::GetIO();
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
		ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
		glm::mat4 view = vulkanRender->getSceneViewMatrix();
		glm::mat4 proj = vulkanRender->getSceneProjMatrixForImGuizmo();

		ImGuizmo::OPERATION op = static_cast<ImGuizmo::OPERATION>(gizmoOperation_);

		// 优先对选中的非 Box 实体操作 ImGuizmo
		if (selectedEntityId_ != 0) {
			auto* ent = vulkanRender->getSceneManager().getModelEntity(selectedEntityId_);
			if (ent) {
				glm::mat4 model = ent->transform.GetModelMatrix();
				ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, ImGuizmo::WORLD,
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
			glm::mat4 model = vulkanRender->mainModelTransform.GetModelMatrix();
			ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, ImGuizmo::WORLD,
				glm::value_ptr(model), nullptr, nullptr);
			vulkanRender->mainModelTransform.position = glm::vec3(model[3]);
			vulkanRender->mainModelTransform.scale = glm::vec3(
				glm::length(glm::vec3(model[0])),
				glm::length(glm::vec3(model[1])),
				glm::length(glm::vec3(model[2])));
			glm::mat3 rot;
			rot[0] = glm::vec3(model[0]) / vulkanRender->mainModelTransform.scale.x;
			rot[1] = glm::vec3(model[1]) / vulkanRender->mainModelTransform.scale.y;
			rot[2] = glm::vec3(model[2]) / vulkanRender->mainModelTransform.scale.z;
			vulkanRender->mainModelTransform.rotation = glm::quat_cast(rot);
		}
		else if (vulkanRender->pickedBoxEntityId != 0) {
			glm::vec3 boxPos = vulkanRender->getBoxPosition(vulkanRender->pickedBoxEntityId);
			glm::mat4 model = glm::translate(glm::mat4(1.0f), boxPos);
			ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, ImGuizmo::WORLD,
				glm::value_ptr(model), nullptr, nullptr);
			vulkanRender->setBoxPosition(vulkanRender->pickedBoxEntityId, glm::vec3(model[3]));
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
        if (reg) reg->refresh();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import...")) {
#ifdef _WIN32
        if (vulkanRender) {
            const std::string selected = openFileDialog(vulkanRender->getMainWindow());
            if (!selected.empty()) {
                if (vulkanRender->importModel(selected, currentFolder_)) {
                    if (reg) reg->refresh();
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
        char label[256];
        const char* name = ent.displayName.empty() ? "(unnamed)" : ent.displayName.c_str();
        snprintf(label, sizeof(label), "%s##ent%llu", name,
                 static_cast<unsigned long long>(ent.entityId));

        if (ImGui::Selectable(label, selectedEntityId_ == ent.entityId)) {
            selectedEntityId_ = ent.entityId;
            // 同步到 Application 的选中状态
            vulkanRender->mainModelSelected = (i == 0);
            vulkanRender->pickedBoxEntityId = 0;
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

// ── Animator Panel ──────────────────────────────────────────────────────────

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

    const auto& clips = ent->animationClips;
    auto& ctrl = ent->animatorController;
    const std::vector<AnimatorState> states = ctrl.states();
    const int selState = animatorSelectedStateIdx_;

    // ── 三区布局：左侧侧边栏 | 右侧上半 | 底部 ──────────────────────
    static float leftWidth = 220.f;

    // ── 左侧侧边栏 ──────────────────────────────────────────────────────
    ImGui::BeginChild("##animLeft", ImVec2(leftWidth, -ImGui::GetFrameHeightWithSpacing() * 1.5f), true);

    if (ImGui::CollapsingHeader("Animator Controllers", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ctrl.hasStates()) {
            ImGui::Selectable("current controller##animctrl", false);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("DND_ANIMCTRL", ctrl.currentStateName().c_str(),
                                          ctrl.currentStateName().size() + 1);
                ImGui::Text("Current AnimatorController");
                ImGui::EndDragDropSource();
            }
        } else {
            ImGui::TextDisabled("No controller loaded");
        }
        ImGui::Separator();
        if (ImGui::Button("Load .animctrl.json##animload")) {
            const std::string path = vulkanRender->getResRoot() + "/animators/example.animctrl.json";
            if (ctrl.loadFromFile(path))
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Loaded: example.animctrl.json");
            else
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Load failed: %s", path.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Save##animctrlSave")) {
            const std::string path = vulkanRender->getResRoot() + "/animators/saved.animctrl.json";
            if (ctrl.saveToFile(path))
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Saved: saved.animctrl.json");
            else
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Save failed: %s", path.c_str());
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Animation Clips", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (clips.empty()) {
            ImGui::TextDisabled("(no clips)");
        } else {
            ImGui::BeginChild("##clipList", ImVec2(0, 0), false);
            for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
                const bool isPreview = (ent->previewClipIndex == i);
                ImGui::PushID(i);
                char label[256];
                snprintf(label, sizeof(label), "[%d] %s%s", i, clips[i].name.c_str(),
                         isPreview ? " [▶]" : "");
                ImGui::Selectable(label, isPreview);

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    int payloadIdx = i;
                    ImGui::SetDragDropPayload("DND_ANIMCLIP", &payloadIdx, sizeof(int));
                    ImGui::Text("Drag %s to create state", clips[i].name.c_str());
                    ImGui::EndDragDropSource();
                }

                if (!isPreview) {
                    ImGui::SameLine();
                    char btnLbl[32];
                    snprintf(btnLbl, sizeof(btnLbl), "▶##preview%d", i);
                    if (ImGui::SmallButton(btnLbl)) {
                        ent->previewClipIndex = i;
                        ent->previewTime = 0.f;
                        ent->previewSpeed = 1.f;
                    }
                } else {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("■##stopPreview")) {
                        ent->previewClipIndex = -1;
                        ent->previewTime = 0.f;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }

    ImGui::EndChild(); // ##animLeft
    ImGui::SameLine();

    // ── 右侧区域 ──────────────────────────────────────────────────────────
    ImGui::BeginGroup();

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
                     isCurrent ? "\xe2\x97\x8f " : "\xe2\x97\x8b ",
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
                        ctrl.configure(ns, ctrl.transitions(), ctrl.params(), ctrl.currentStateName());
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
                    ctrl.configure(ns, ctrl.transitions(), ctrl.params(), ctrl.currentStateName());
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
            snprintf(buf, sizeof(buf), "%s \xe2\x86\x92 %s##trans_%d",
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
        const auto& t = ctrl.transitions()[animatorSelectedTransitionIdx_];
        ImGui::Text("Transition: %s -> %s", t.fromState.c_str(), t.toState.c_str());
        ImGui::Separator();

        AnimatorTransition mutableT = t;

        if (ImGui::BeginCombo("To##editTransTo", mutableT.toState.c_str())) {
            for (const auto& s : ctrl.states()) {
                if (s.name == mutableT.fromState) continue;
                const bool ssel = (s.name == mutableT.toState);
                if (ImGui::Selectable(s.name.c_str(), ssel))
                    mutableT.toState = s.name;
                if (ssel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::DragFloat("Fade (s)", &mutableT.fadeDuration, 0.01f, 0.f, 10.f, "%.2f");
        ImGui::Checkbox("Has Exit Time", &mutableT.hasExitTime);
        if (mutableT.hasExitTime)
            ImGui::DragFloat("Exit Time", &mutableT.exitTime, 0.01f, 0.f, 1.f, "%.2f");

        const char* curveNames[] = { "Linear", "SmoothStep", "EaseIn", "EaseOut" };
        int curveIdx = static_cast<int>(mutableT.blendCurve);
        if (ImGui::Combo("Blend Curve", &curveIdx, curveNames, IM_ARRAYSIZE(curveNames)))
            mutableT.blendCurve = static_cast<BlendCurve>(curveIdx);

        ImGui::Separator();
        ImGui::Text("Conditions");
        int condDel = -1;
        for (int ci = 0; ci < static_cast<int>(mutableT.conditions.size()); ++ci) {
            auto& c = mutableT.conditions[ci];
            ImGui::PushID(ci);
            // Use a temporary char buffer for InputText (can't bind std::string directly)
            char paramBuf[128];
            snprintf(paramBuf, sizeof(paramBuf), "%s", c.paramName.c_str());
            char labelBuf[128];
            snprintf(labelBuf, sizeof(labelBuf), "Param##cond_%d", ci);
            if (ImGui::InputText(labelBuf, paramBuf, sizeof(paramBuf)))
                c.paramName = paramBuf;

            const char* opNames[] = { "Greater", "Less", "Equal", "NotEqual", "True", "False" };
            int opIdx = static_cast<int>(c.op);
            snprintf(labelBuf, sizeof(labelBuf), "Op##cond_%d", ci);
            if (ImGui::Combo(labelBuf, &opIdx, opNames, IM_ARRAYSIZE(opNames)))
                c.op = static_cast<TransitionCondition::Op>(opIdx);

            snprintf(labelBuf, sizeof(labelBuf), "Threshold##cond_%d", ci);
            ImGui::DragFloat(labelBuf, &c.threshold, 0.01f);

            snprintf(labelBuf, sizeof(labelBuf), "X##delCond_%d", ci);
            if (ImGui::SmallButton(labelBuf)) { condDel = ci; }
            ImGui::PopID();
        }
        if (condDel >= 0)
            mutableT.conditions.erase(mutableT.conditions.begin() + condDel);

        if (ImGui::Button("+ Add Condition"))
            mutableT.conditions.push_back({});

        ImGui::Separator();
        if (ImGui::Button("Apply Transition")) {
            auto nt = ctrl.transitions();
            if (animatorSelectedTransitionIdx_ < static_cast<int>(nt.size())) {
                nt[animatorSelectedTransitionIdx_] = std::move(mutableT);
                ctrl.configure(ctrl.states(), std::move(nt), ctrl.params(), ctrl.currentStateName());
                snprintf(animatorStatusMsg_, sizeof(animatorStatusMsg_), "Transition updated");
            }
        }
        ImGui::SameLine();
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
        const auto& s = ctrl.states()[selState];
        ImGui::Text("State Properties");
        ImGui::Separator();

        AnimatorState mutableS = s;
        char nameBuf[256];
        snprintf(nameBuf, sizeof(nameBuf), "Name: %s", s.name.c_str());
        ImGui::TextUnformatted(nameBuf);
        ImGui::DragFloat("Speed##stateSpeed", &mutableS.speed, 0.01f, 0.f, 10.f, "%.2f");
        ImGui::Checkbox("Loop##stateLoop", &mutableS.loop);

        if (ImGui::Button("Apply State")) {
            auto ns = ctrl.states();
            if (selState < static_cast<int>(ns.size())) {
                ns[selState] = std::move(mutableS);
                ctrl.configure(std::move(ns), ctrl.transitions(), ctrl.params(), ctrl.currentStateName());
            }
        }
        ImGui::SameLine();
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
    ImGui::Text("Quick Params (click to trigger / change)");
    for (const auto& p : ctrl.params()) {
        switch (p.type) {
        case AnimatorParam::Type::Float: {
            float v = p.value.f;
            if (ImGui::DragFloat(p.name.c_str(), &v, 0.01f))
                ctrl.setFloat(p.name, v);
            break;
        }
        case AnimatorParam::Type::Int: {
            int v = p.value.i;
            if (ImGui::DragInt(p.name.c_str(), &v, 1))
                ctrl.setInt(p.name, v);
            break;
        }
        case AnimatorParam::Type::Bool: {
            bool v = p.value.b;
            if (ImGui::Checkbox(p.name.c_str(), &v))
                ctrl.setBool(p.name, v);
            break;
        }
        case AnimatorParam::Type::Trigger: {
            if (ImGui::Button(p.name.c_str()))
                ctrl.setTrigger(p.name);
            break;
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
            ctrl.configure(ns, ctrl.transitions(), ctrl.params(), ctrl.currentStateName());
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
