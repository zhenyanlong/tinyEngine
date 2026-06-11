#include "IMGUIManager.hpp"
#include "Application.hpp"
#include "ImGuizmo.h"
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
#include <iostream>
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
    ofn.lpstrFilter = L"3D Models (*.obj;*.gltf;*.glb)\0*.obj;*.gltf;*.glb\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = sizeof(fileBuf) / sizeof(wchar_t);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"gltf";

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

/** @brief 返回 exe 所在目录，用于拼接 res/shaders 等相对路径 */
static std::string applicationResourceRoot()
{
#ifdef _WIN32
	char path[MAX_PATH]{};
	if (GetModuleFileNameA(nullptr, path, MAX_PATH) != 0) {
		std::filesystem::path p(path);
		return p.parent_path().string();
	}
#endif
	return std::filesystem::current_path().string();
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
            ImGui::TextDisabled("(no .ast files in res/materials)");
            if (ImGui::Button("Rescan##ast")) scanMaterialAssets();
        } else {
            std::vector<const char*> items;
            items.reserve(astAssetFiles_.size());
            for (const auto& s : astAssetFiles_) items.push_back(s.c_str());
            if (astAssetIndex_ < 0 || astAssetIndex_ >= static_cast<int>(items.size()))
                astAssetIndex_ = 0;
            ImGui::Combo("##astpick", &astAssetIndex_, items.data(), static_cast<int>(items.size()));

            if (ImGui::Button("Apply##ast")) {
                const std::string rel = std::string("materials/") + astAssetFiles_[astAssetIndex_];
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
                snprintf(label, sizeof(label), "[%u] %s (%s)",
                         id, matMgr.getMaterialName(id).c_str(),
                         matMgr.getMaterialType(id) == MaterialType::Mesh ? "Mesh" : "Box");
                if (ImGui::Selectable(label, sel)) {
                    editingMaterialId_ = id;
                    // Sync texture path buffers to this material
                    const std::string& ap = matMgr.getAlbedoPath(id);
                    const std::string& np = matMgr.getNormalPath(id);
                    snprintf(matAlbedoPath_, sizeof(matAlbedoPath_), "%s", ap.c_str());
                    snprintf(matNormalPath_, sizeof(matNormalPath_), "%s", np.c_str());
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
    const std::filesystem::path matDir =
        std::filesystem::path(applicationResourceRoot()) / "res" / "materials";
    std::error_code ec;
    if (std::filesystem::is_directory(matDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(matDir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".ast") {
                astAssetFiles_.push_back(entry.path().filename().string());
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

    ImGui::SetNextWindowSize(ImVec2(360, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Content Browser", &showContentBrowser_)) {
        ImGui::End();
        return;
    }

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
                if (vulkanRender->importModel(selected)) {
                    if (reg) reg->refresh();
                }
            }
        }
#else
        std::cerr << "[Import] not supported on this platform\n";
#endif
    }

    // 过滤模型列表（通过 ModelRegistry::search）
    std::vector<const ModelAsset*> filtered;
    if (reg)
        filtered = reg->search(contentBrowserSearch_);

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
            // 从 astRelPath 的 stem 推导缩略图文件名
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
            // 纯色占位方块
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImU32 col = IM_COL32(60, 90, 140, 255);
            ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + thumbSize, p0.y + thumbSize), col, 4.f);
            ImGui::InvisibleButton(("##thumb" + std::to_string(n)).c_str(), ImVec2(thumbSize, thumbSize));
        }

        // 拖拽源
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            uint64_t id = asset->id;
            ImGui::SetDragDropPayload("MODEL_ASSET", &id, sizeof(uint64_t));
            ImGui::Text("%s", asset->name.c_str());
            ImGui::EndDragDropSource();
        }

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

    // Place Distance 滑块
    if (vulkanRender) {
        ImGui::SliderFloat("Place Distance", &placementDistance_, 1.0f, 50.0f);
        vulkanRender->dragPlace.distance = placementDistance_;
    }

    if (reg)
        ImGui::Text("Models: %zu", reg->size());
    else
        ImGui::TextDisabled("(unavailable)");

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
            const bool isMesh = (matMgr.getMaterialType(editId) == MaterialType::Mesh);

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

            if (isMesh) {
                // 每次打开都同步纹理路径缓冲区（snprintf 成本极低）
                snprintf(matAlbedoPath_, sizeof(matAlbedoPath_), "%s",
                         matMgr.getAlbedoPath(editId).c_str());
                snprintf(matNormalPath_, sizeof(matNormalPath_), "%s",
                         matMgr.getNormalPath(editId).c_str());

                ImGui::Separator();
                ImGui::Text("Textures");
                ImGui::InputText("Albedo##matEdit", matAlbedoPath_, sizeof(matAlbedoPath_));
                ImGui::SameLine();
                if (ImGui::Button("Load##albedoEdit"))
                    vulkanRender->setMaterialAlbedo(editId, matAlbedoPath_);

                ImGui::InputText("Normal##matEdit", matNormalPath_, sizeof(matNormalPath_));
                ImGui::SameLine();
                if (ImGui::Button("Load##normalEdit"))
                    vulkanRender->setMaterialNormal(editId, matNormalPath_);
            }

            if (matMgr.isDeletable(editId)) {
                ImGui::Separator();
                if (ImGui::Button("Delete Material")) {
                    vulkanRender->destroyMaterial(editId);
                    ent->materialId = matMgr.getDefaultMeshMaterialId();
                    matAlbedoPath_[0] = '\0';
                    matNormalPath_[0] = '\0';
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
