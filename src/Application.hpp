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
#include <glm/glm.hpp>

class Application {
public:
    Application();
    using RenderEntityId = uint64_t;

    void run();

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

    /** @brief 导入外部模型文件到 res/ 并创建入口 .ast，返回 true 表示成功 */
    bool importModel(const std::string& sourcePath,
                     const std::string& subFolder = "");

    /** @brief 返回资源根目录 */
    std::string getResRoot() const { return modelRegistry_.getResRoot(); }

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
    void       setMaterialAlbedo(MaterialId id, const std::string& path);
    void       setMaterialNormal(MaterialId id, const std::string& path);

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

    bool  firstMouse_       = true;
    bool  rightMouseDown_   = false;
    float lastX_            = WIDTH / 2.f;
    float lastY_            = HEIGHT / 2.f;

    void initGLFW();
    void initVulkan();
    void gameLoop();
    void drawFrame();
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex);
    void recreateSwapChain();
    void processInput(GLFWwindow* w);
    void tryPickMainModel(float cx, float cy);
    void tryBeginCameraFocusOnPick();
    void cleanUp();
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
