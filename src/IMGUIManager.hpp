#pragma once

#include <imgui.h>

#include <backends/imgui_impl_vulkan.h>

#include <backends/imgui_impl_glfw.h>

#include <vector>
#include <unordered_map>

#include <GLFW/glfw3.h>

#include <string>

#include <cstdint>

class Application;

/**
 * @class UIManager
 * @brief Dear ImGui + ImGui_ImplVulkan/GLFW 封装：负责每帧 UI、与 VulkanRender 的资源/场景联动、ImGuizmo 平移手柄。
 *
 * @details 教学流水线：initIMGUI →（每帧）prepareFrame（NewFrame + 面板 + Manipulate + ImGui::Render）→
 * 录制命令缓冲时在 swapchain 上绘制 ImGui 绘制数据。
 */
class UIManager {

public:

	/** @brief 默认构造：成员多为零初始化或空指针 */
	UIManager() = default;

	/** @brief 虚析构：派生类可重写；当前由 cleanUp 显式释放 ImGui/Vulkan 后端 */
	virtual ~UIManager() = default;

	/** @brief ImGui 清屏色，可由面板 ColorEdit3 修改并供渲染通路读回 */
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	/**
	 * @brief 创建 ImGui 上下文、绑定 GLFW 与 Vulkan 后端；需在 VulkanRender 已创建 window/device/renderPass 后调用。
	 */
	void initIMGUI();

	/**
	 * @brief 保存 VkInstance 与可选分配器；在 initImGuiVulkanBackend 前必须设置 Instance。
	 */
	void setVulkanInstance(const VkInstance& instance, VkAllocationCallbacks* allocator);

	/**
	 * @brief 销毁 ImGui Vulkan 后端并释放 GLFW 相关；进程退出或重建前应调用。
	 */
	void cleanUp();

	/**
	 * @brief 每帧在 drawFrame 录制前调用：NewFrame、业务窗口、ImGuizmo::Manipulate、ImGui::Render。
	 */
	void prepareFrame();

	/**
	 * @brief 交换链重建后：Shutdown ImGui Vulkan 后端并用新的 RenderPass/ImageCount 重新 Init。
	 */
	void reloadImGuiVulkanAfterSwapchainRecreate(Application* app);

	/**
	 * @brief 设置逻辑设备与物理设备句柄，供 ImGui_ImplVulkan_Init 使用。
	 */
	void setPhysicalDevice(const VkDevice& device, const VkPhysicalDevice& physicalDevice);

	/**
	 * @brief 若返回 true，主循环应触发 recreateSwapChain（例如着色器路径变更）。
	 */
	bool refreshVulkanShader();

	/**
	 * @brief 设置是否需要在下一帧刷新 Vulkan（与 refreshVulkanShader 读侧配对）。
	 */
	void setRefreshVulkanStatus(bool status);

	/**
	 * @brief 用 exe 旁 res/ 目录填充默认 shader/model/texture 路径字符串缓冲区。
	 */
	void setModelDefaultPath();

	/** @brief 关联场景渲染器，供面板读写选中物体、矩阵与资源路径 */
	void setVulkanRender(Application* app) { vulkanRender = app; }

	/** @brief 返回当前清屏颜色（RGBA） */
	[[nodiscard]] ImVec4 getClearColor() const { return clear_color; }

	/** @brief 当前主模型顶点着色器 SPIR-V 路径（可从面板同步） */
	std::string vertexShaderPath;

	/** @brief 当前主模型片段着色器 SPIR-V 路径 */
	std::string fragShaderPath;

	/** @brief 盒子所用片段着色器路径（通常由 frag 路径派生为 box.spv） */
	std::string boxFragShaderPath;

	/** @brief 盒子所用顶点着色器 SPIR-V 路径（由 vert.spv 派生为 box_vert.spv） */
	std::string boxVertShaderPath;

	/** @brief 当前加载的 OBJ 模型路径 */
	std::string modelPath;

	/** @brief 当前主纹理路径 */
	std::string texturePath;

	// ── Scene Outliner & Properties ─────────────────────────────────────────
	bool     showOutliner_        = true;
	bool     showPropertiesPanel_ = true;
	uint64_t selectedEntityId_    = 0;

	// ── Animator Panel ──────────────────────────────────────────────────────
    bool     showAnimatorPanel_ = false;
    int      animatorSelectedStateIdx_ = -1;   ///< 右侧上半选中的 state 索引
    int      animatorSelectedTransitionIdx_ = -1;  ///< 底部选中的 transition 索引
    bool     animatorEditingTransition_ = false;  ///< 底部是否正在编辑某条 transition
    char     animatorStatusMsg_[256]{};
    std::vector<std::string> animatorControllerAssets_; ///< 与当前模型动画片段兼容的控制器资产绝对路径。
    uint64_t animatorControllerAssetsEntityId_ = 0;
    uint64_t animatorObservedAnimationDataRevision_ = 0;
    bool     animatorControllerAssetsDirty_ = true;
    int      animatorRenamingClipIdx_ = -1;
    char     animatorClipRenameBuffer_[128]{};
    int      animatorRenamingStateIdx_ = -1;
    char     animatorStateRenameBuffer_[128]{};

    // ── PiP (Picture-in-Picture) ──────────────────────────────────────────
    bool showPipWindow_ = false;
    int  pipWindowWidth_  = 320;
    int  pipWindowHeight_ = 240;

    // ── Sequencer Panel ─────────────────────────────────────────────────────
    bool showSequencerPanel_ = false;

    // Track mute/solo state (indexed by track index in current sequence)


    // Sequencer keyframe editing state
    double sequencerEditTime_ = 0.0;
    bool   sequencerEditTimeSet_ = false;
    int    selectedKeyframeIdx_ = -1;
    // 双列布局垂直滚动同步：左列标签跟随右列时间线的滚动位置（1 帧延迟）
    float  seqTimelineScrollY_ = 0.0f;

    struct SequenceAssetEntry {
        std::string absolutePath;
        std::string relativePath;
        std::string displayName;
        std::string error;
        double duration = 0.0;
        size_t trackCount = 0;
        bool valid = false;
    };
    std::vector<SequenceAssetEntry> sequenceAssets_;
    int sequenceAssetSelected_ = -1;
    char sequenceAssetSearch_[128]{};
    std::vector<size_t> sequenceRebindTrackIndices_;
    std::vector<uint64_t> sequenceRebindEntityIds_;
    bool sequenceRebindPopupPending_ = false;
    void scanSequenceAssets();

	int gizmoOperation_ = 7;  // ImGuizmo::TRANSLATE (bitmask: 7=TRANSLATE, 120=ROTATE, 896=SCALE)
	bool gizmoLocal_ = false; // false=世界坐标(ImGuizmo::WORLD), true=本地坐标(ImGuizmo::LOCAL)，按 4 键切换

	/** @brief 共享缩略图占位符纹理（ImTextureID） */
	ImTextureID thumbnailIcon_ = (ImTextureID)0;

	/** @brief 按 assetId 缓存的缩略图纹理 */
	std::unordered_map<uint64_t, ImTextureID> thumbCache_;
	std::unordered_map<uint64_t, VkSampler>   thumbSamplers_;

private:

	/** @brief 根据 fragShaderPath 生成 boxFragShaderPath（frag.spv → box.spv） */
	void syncBoxFragShaderPathFromFrag();

	/** @brief 根据 vertexShaderPath 生成 boxVertShaderPath（vert.spv → box_vert.spv） */
	void syncBoxVertShaderPathFromVert();

	/** @brief 使用已保存的 Instance/Device/Queue 等调用 ImGui_ImplVulkan_Init */
	void initImGuiVulkanBackend();

	VkAllocationCallbacks* Allocator = nullptr;

	VkInstance Instance = VK_NULL_HANDLE;

	VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;

	VkDevice Device = VK_NULL_HANDLE;

	uint32_t QueueFamily = 0;

	VkQueue Queue = VK_NULL_HANDLE;

	VkPipelineCache PipelineCache = VK_NULL_HANDLE;

	/** @brief ImGui IO 快照（部分路径历史代码使用） */
	ImGuiIO g_io{};

	GLFWwindow* window = nullptr;

	/** @brief 为 true 时 refreshVulkanShader() 返回 true */
	bool refreshVulkanRender = false;

	/** @brief 顶点着色器目录前缀 + 文件名缓冲区 */
	char VertexShaderPath[1024]{};

	char FragShdaerPath[1024]{};

	char ModelPath[1024]{};

	char TexturePath[1024]{};

	char currentVertexShaderPath[1024]{};

	char currentFragShdaerPath[1024]{};

	char currentModelPath[1024]{};

	char currentTexturePath[1024]{};

	int currentIndex = 0;

	float boxPosition[3] = { 0.0f, 0.0f, 0.0f };

	uint64_t lastAddedBoxId = 0;

	uint64_t deleteBoxId = 0;

	Application* vulkanRender = nullptr;

	// Material panel state
	uint32_t editingMaterialId_ = 0;
	uint32_t texturePathMaterialId_ = 0;
	char     matAlbedoPath_[1024]{};
	char     matNormalPath_[1024]{};
	std::string textureStatusMsg_;

	// Material asset (.ast) picker state
	std::vector<std::string> astAssetFiles_;   // basenames under res/materials/, sorted
	int                      astAssetIndex_ = 0;
	bool                     astAssetScanned_ = false;
	std::string              astStatusMsg_;

	/** @brief 扫描 res/materials/ 下的 .ast 文件填充 astAssetFiles_ */
	void scanMaterialAssets();

	// Content Browser state
	bool                     showContentBrowser_ = false;
	char                     contentBrowserSearch_[256]{};
	float                    placementDistance_ = 5.0f;
	std::string              currentFolder_;             ///< 当前浏览的子文件夹（空 = 根目录）
	char                     newFolderName_[128]{};       ///< 新文件夹名输入缓冲
	int                      typeFilterIdx_ = 0;          ///< 类型筛选: 0=All, 1=Mesh, 2=Box, 3=Material, 4=Anim

	// ── Import Animation state ───────────────────────────────────────────
	std::string              importAnimFbxPath_;           ///< 用户通过文件对话框选择的 FBX 路径
	std::string              importAnimStatus_;            ///< 最近一次动画导入与场景刷新结果
	bool                     showMeshPicker_ = false;      ///< 选择目标 mesh 的弹窗可见
	int                      meshPickerSelected_ = 0;      ///< mesh 列表当前选中的索引

	/** @brief 绘制 Content Browser 面板 */
	void drawContentBrowser();

	/** @brief 绘制动画总控面板（Animator） */
	void drawAnimatorPanel();

	/** @brief 绘制场景实体列表面板 */
	void drawSceneOutliner();

	/** @brief 绘制选中实体属性面板 */
    void drawPropertiesPanel();

    /** @brief 绘制 PiP 摄像机预览小窗 */
    void drawPipWindow();

    /** @brief 绘制 Sequencer 编辑面板（时间线 + 轨道列表 + 片段） */
    void drawSequencerPanel();

	/** @brief 加载 res/icons/model.png 作为共享缩略图图标 */
	void loadThumbnailIcon();

	// ── Thumbnail GPU resources ──────────────────────────────────────────
	VkImage        thumbIconImage_ {};
	VkDeviceMemory thumbIconMemory_{};
	VkImageView    thumbIconView_ {};
	VkSampler      thumbIconSampler_{};

};
