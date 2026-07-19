#pragma once
#include "VulkanContext.hpp"
#include "SwapChain.hpp"
#include "RenderPassManager.hpp"
#include "FramebufferManager.hpp"
#include "CommandManager.hpp"
#include "BufferManager.hpp"
#include "PipelineManager.hpp"
#include "DescriptorManager.hpp"
#include "MaterialManager.hpp"
#include "SceneManager.hpp"
#include "ModelRegistry.hpp"
#include "PickSystem.hpp"
#include "IMGUIManager.hpp"
#include "ThumbnailRenderer.hpp"
#include "camera.hpp"
#include "VulkanTypes.hpp"
#include "Transform.hpp"
#include "Animation/Sequence.hpp"
#include "Animation/SequencePlayer.hpp"
#include "Animation/SequenceAssetLoader.hpp"
#include "Animation/SequencerCamera.hpp"
#include "Mcp/CommandBridge.hpp"
#include "Mcp/FrameCapture.hpp"
#include "Mcp/IpcServer.hpp"
#include <glm/glm.hpp>
#include <unordered_map>

class Application {
public:
    Application();
    using RenderEntityId = uint64_t;

    void run(int argc = 0, char* argv[] = nullptr);

    // ── Drag-place state ──────────────────────────────────────────────────────
    struct DragPlaceState {
        bool     active   = false;
        uint64_t assetId  = 0;
        uint64_t entityId = 0;
        float    distance = 5.0f;
    };
    DragPlaceState dragPlace;

    // Public state read/written by UIManager
    ObjectTransform mainModelTransform;             ///< Main model TRS (position, rotation, scale)
    bool           mainModelSelected  = false;
    RenderEntityId pickedBoxEntityId  = 0;
    MaterialId     selectedMaterialId = kInvalidMaterialId;

    // UIManager-facing API
    GLFWwindow*  getMainWindow()           const { return window_; }
    VkRenderPass getMainRenderPass()       const { return rpMgr_.getMainRenderPass(); }
    VkQueue      getGraphicsQueue()        const { return ctx_.getGraphicsQueue(); }
    uint32_t     getGraphicsQueueFamily()  const { return ctx_.getGraphicsQueueFamily(); }
    uint32_t     getSwapChainImageCount()  const { return swapChain_.getImageCount(); }

    glm::mat4 getSceneViewMatrix();
    glm::mat4 getSceneProjMatrixForImGuizmo();

    // ── Drag-place API ────────────────────────────────────────────────────────
    /** @brief 屏幕坐标 → 世界坐标：通过相机 invViewProj 反投影 + 指定距离 */
    glm::vec3 screenToWorld(float mx, float my, float distance) const;
    void      beginDragPlace(uint64_t assetId);
    void      updateDragPlace(float mx, float my);
    /** @brief 结束拖拽（预览实体转为正式实体） */
    void      endDragPlace();

    /** @brief 删除指定模型实体（需 GPU 空闲时调用） */
    void      deleteModelEntity(uint64_t entityId);

    /** @brief 保存场景到 .scene.json */
    bool saveScene(const std::string& path);

    /** @brief 从 .scene.json 加载场景 */
    bool loadScene(const std::string& path);

    /** @brief 导入外部模型文件到 res/ 并创建入口 .ast，返回 true 表示成功
     *  @param outAnimFbxPath  非空时表示该 FBX 是 without-skin 动画文件，
     *                         调用方应使用此路径弹出 mesh 选择弹窗后调用 importAnimationFbx */
    bool importModel(const std::string& sourcePath,
                     const std::string& subFolder = "",
                     std::string* outAnimFbxPath = nullptr);

    /** @brief 导入 Mixamo without-skin 动画 FBX，链接到指定 mesh.ast 的骨架。 */
    bool importAnimationFbx(const std::string& fbxPath,
                            const std::string& targetMeshAstRelPath);

    /** @brief 返回资源根目录 */
    std::string getResRoot() const { return modelRegistry_.getResRoot(); }

    // ── Sequencer API ─────────────────────────────────────────────────────────
    SequencePlayer& getSequencePlayer() { return seqPlayer_; }
    Sequence&       getCurrentSequence() { return currentSequence_; }
    bool            isSequencerPlaying() const { return seqPlayer_.isPlaying(); }
    void            requestSequencerPreview() { sequencerPreviewPending_ = true; }

    // ── Animator Preview Mode API ─────────────────────────────────────────────
    bool            isAnimatorPreviewMode() const { return animatorPreviewMode_; }
    void            setAnimatorPreviewMode(bool on) { animatorPreviewMode_ = on; }

    // ── Camera Path recording API ────────────────────────────────────────────
    void beginCameraPathRecording(const std::string& pathName);
    void endCameraPathRecording();
    bool isRecordingCameraPath() const { return recordingCameraPath_; }
    const CameraPath& getRecordingPath() const { return recordingPath_; }
    double getRecordingTime() const { return recordingTime_; }
    void setRecordInterval(double sec) { recordInterval_ = std::max(0.05, sec); }

    // ── PiP (Picture-in-Picture) API ─────────────────────────────────────────
    ImTextureID getPipTextureId() const { return pipTextureId_; }
    bool        isPipActive()      const { return pipActive_; }
    void        setPipActive(bool active) { pipActive_ = active; }
    SequencerCamera& getPipCamera() { return pipCamera_; }
    Camera&     getCamera()              { return camera_; }
    const Camera& getCamera() const      { return camera_; }

    /** @brief 创建 Camera Actor 实体 */
    uint64_t createCameraActor(const glm::vec3& position, const glm::quat& orientation);

    /** @brief 当前选中的 Camera entityId（0 表示无） */
    uint64_t selectedCameraEntityId_ = 0;

    /** @brief 将 RGBA 像素数据上传为 ImTextureID（调用方负责加载 PNG） */
    ImTextureID createUITexture(const void* rgbaPixels, int w, int h, VkSampler& outSampler);

    /** @brief 加载 PNG 文件为 ImTextureID */
    ImTextureID loadPNGTexture(const std::string& path, VkSampler& outSampler);

    RenderEntityId addBox(const glm::vec3& pos);
    bool           removeBox(RenderEntityId id);
    glm::vec3      getBoxPosition(RenderEntityId id) const;
    void           setBoxPosition(RenderEntityId id, const glm::vec3& pos);

    // Material accessors for UIManager
    MaterialManager& getMaterialManager()            { return matMgr_; }
    SceneManager&    getSceneManager()               { return sceneMgr_; }
    ModelRegistry&   getModelRegistry()              { return modelRegistry_; }

    void setModelMaterial(MaterialId id);
    void setBoxMaterial(RenderEntityId eid, MaterialId id);

    // Load a material asset (.ast, JSON) at runtime and apply it to the main
    // model. astRelPath is relative to res/, e.g. "materials/viking_room.ast".
    // Returns true on success; on failure, the current model material is kept.
    bool loadAndApplyMaterialAsset(const std::string& astRelPath);
    MaterialId getModelMaterial()               const { return sceneMgr_.getModelMaterialId(); }
    MaterialId getBoxMaterial(RenderEntityId e) const { return sceneMgr_.getBoxMaterialId(e); }

    // Thin wrappers so UIManager can drive MaterialManager without Vulkan context access
    MaterialId createMeshMaterial(const std::string& name,
                                  const std::string& albedoPath,
                                  const std::string& normalPath,
                                  const MaterialParams& params);
    MaterialId createBoxMaterial(const std::string& name, const MaterialParams& params);
    void       destroyMaterial(MaterialId id);
    bool       setMaterialAlbedo(MaterialId id, const std::string& path);
    bool       setMaterialNormal(MaterialId id, const std::string& path);

    // Thumbnail renderer access
    ThumbnailRenderer& getThumbnailRenderer() { return thumbnailRenderer_; }
    void refreshAllThumbnails() { thumbnailRenderer_.generateAll(modelRegistry_.getResRoot()); }

private:
    GLFWwindow* window_  = nullptr;
    bool        framebufferResized_ = false;
    uint32_t    currentFrame_ = 0;

    VulkanContext      ctx_;
    SwapChain          swapChain_;
    RenderPassManager  rpMgr_;
    FramebufferManager fbMgr_;
    CommandManager     cmdMgr_;
    BufferManager      bufMgr_;
    PipelineManager    pipeMgr_;
    DescriptorManager  descMgr_;
    MaterialManager    matMgr_;
    SceneManager       sceneMgr_;
    ModelRegistry      modelRegistry_;
    ThumbnailRenderer  thumbnailRenderer_;
    PickSystem         pickSys_;
    UIManager*         ui_  = nullptr;
    Camera             camera_;

    // ── Sequencer state ───────────────────────────────────────────────────────
    SequencePlayer     seqPlayer_;
    Sequence           currentSequence_;
    bool               sequencerPreviewPending_ = false;
    bool               sequenceCameraActive_ = false;

    // ── Animator Preview Mode ─────────────────────────────────────────────────
    // ON (default)：Animator 面板自由驱动状态机
    // OFF：动画由 Sequencer 时间轴控制，状态机不自动推进
    bool               animatorPreviewMode_ = true;

    // ── Camera Path cache ──────────────────────────────────────────────────
    std::unordered_map<std::string, CameraPath> cameraPathCache_;

    // ── Camera Path recording ────────────────────────────────────────────────
    bool               recordingCameraPath_ = false;
    CameraPath         recordingPath_;
    double             recordingTime_  = 0.0;
    double             recordInterval_ = 0.1;
    double             recordTimer_    = 0.0;

    // ── PiP state ─────────────────────────────────────────────────────────────
    SequencerCamera    pipCamera_;
    bool               pipActive_     = false;
    ImTextureID        pipTextureId_  = (ImTextureID)0;
    bool               pipTextureCreated_ = false;  // 标记纹理是否已创建

    // ── MCP / IPC state ────────────────────────────────────────────────────────
    std::unique_ptr<IpcServer> ipc_;
    FrameCapture               frameCapture_;
    int                        mcpPort_ = 9527;
    int                        exitAfterFrames_ = -1;
    int                        frameCount_ = 0;
    bool                       mcpShutdownRequested_ = false;

    bool  firstMouse_       = true;
    bool  rightMouseDown_   = false;
    float lastX_            = WIDTH / 2.f;
    float lastY_            = HEIGHT / 2.f;

    void initGLFW();
    void initVulkan();
    void gameLoop();
    void drawFrame(float dt);
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex);
    void recreateSwapChain();
    void processInput(GLFWwindow* w);
    void tryPickMainModel(float cx, float cy);
    void tryBeginCameraFocusOnPick();
    void cleanUp();
    void registerMcpHandlers();
    bool ensureAnimationAssetForMeshAst(const std::string& meshAstRelPath,
                                        const std::string& modelPathOrRel,
                                        bool refreshRegistryAfterWrite,
                                        uint64_t entityId = 0);

    /** @brief 将含骨骼实体的所有槽位材质转换为蒙皮版本（保留原有纹理和参数） */
    void convertModelMaterialsToSkinned();

    // Returns the first material ID that is actually rendered on the main model.
    // For glTF models with per-submesh materials, this is slot 0's material;
    // for single-material (.obj) models it is modelMaterialId_.
    MaterialId firstRenderedModelMaterialId() const;

    static void framebufferResizeCallback(GLFWwindow* w, int, int);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void mouseCallback(GLFWwindow* w, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* w, double xoffset, double yoffset);
};
