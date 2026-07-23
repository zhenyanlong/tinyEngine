#pragma once

#include "VulkanContext.hpp"
#include "BufferManager.hpp"
#include "VulkanTypes.hpp"
#include "vectex.hpp"
#include "Transform.hpp"
#include "Animation/Skeleton.hpp"
#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorController.hpp"
#include "Animation/SequencerCamera.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class SceneManager {
public:
    static constexpr uint32_t kPickIdNone = 0;
    static constexpr uint32_t kPickIdMainModel = 1;
    static constexpr uint32_t kPickIdBoxBase = 2;

    struct SubMesh {
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        int skinIndex = -1;
        int materialSlot = -1;
    };

    struct ModelEntity {
        struct RootMotionClipHistory {
            float sampleTime = 0.f;
            glm::vec3 translation{0.f};
            glm::quat rotation{1.f, 0.f, 0.f, 0.f};
        };

        enum class Type { Mesh, Camera };
        Type type = Type::Mesh;
        
        uint64_t entityId = 0;
        uint64_t modelAssetId = 0;
        ObjectTransform transform;
        uint32_t materialId = 0;
        bool visible = true;
        bool selected = false;
        std::string displayName;
        std::string astRelPath;
        glm::vec3 localBoundsMin{};
        glm::vec3 localBoundsMax{};
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        VkBuffer vertexBuffer{};
        VkDeviceMemory vertexMemory{};
        VkBuffer indexBuffer{};
        VkDeviceMemory indexMemory{};
        bool ownsMeshBuffers = true;
        std::string meshResourceKey;
        uint32_t indexCount = 0;
        std::vector<SubMesh> subMeshes;
        std::vector<uint32_t> subMeshMaterials;
        // 运行时蒙皮绑定：与 subMeshes 一一对应，仅保存 descriptor/bone UBO 句柄，
        // 不参与资产或场景序列化。相同材质但不同 skin 必须使用不同 binding。
        std::vector<uint32_t> subMeshSkinBindings;
        uint32_t skinBindingId = 0; ///< 无 submesh 实体的运行时蒙皮绑定。
        std::vector<std::string> autoAstPaths;
        bool hasSkin_ = false;
        // 每实体独立的动画状态（下沉自 SceneManager 全局单例）
        std::shared_ptr<Skeleton> skeleton;
        std::vector<AnimationClip> animationClips;
        std::string animationAssetPath; ///< 提供当前 clips 的 .anim.ast 路径，用于元数据编辑与持久化。
        std::vector<std::string> animationAssetPaths; ///< mesh.ast 中引用的全部 .anim.ast 路径
        AnimatorController animatorController;
        std::string animatorControllerPath; ///< 当前绑定的 .animctrl.json 资产路径；为空表示运行时临时控制器。
        // ── 预览模式 ─────────────────────────────────────────
        int previewClipIndex = -1;   // >=0 时覆盖状态机，直接播放 animationClips[previewClipIndex]
        float previewTime = 0.f;     // 预览 clip 的当前时间
        float previewSpeed = 1.f;    // 预览播放速度

        // ── Sequencer AnimatorKeyframe 求值缓存 ──────────────
        // 当 Sequencer 的 AnimatorKeyframe 轨道求值时，将计算出的 BlendCommand
        // 缓存于此，供 drawFrame 直接使用，避免与状态机自身的 update(dt) 重复推进。
        bool hasPendingSequencerBlend = false;
        bool sequencerAnimatorActive = false;
        AnimatorController::BlendCommand pendingSequencerBlend;

        // ── Root Motion 运行时状态 ─────────────────────────────
        glm::vec3 prevRootTranslation{0.f};
        glm::quat prevRootRotation{1.f, 0.f, 0.f, 0.f};
        bool      rootMotionInitialized = false;
        std::unordered_map<std::string, RootMotionClipHistory> rootMotionClipHistory;

        // ── Camera 专属字段（仅 type == Camera 时有效） ─────────────────────
        SequencerCamera cameraData;
        bool cameraPreviewEnabled = true;
        float cameraPreviewScale = 0.25f;
        // 模型几何体相对于 Actor 朝向的额外旋转偏移（仅影响渲染，不影响 cameraData 预览方向）。
        // Camera 模型 mesh 的默认朝向与预览 -Z 前向相差 Y -90°，需用此偏移补偿。
        glm::quat modelRotationOffset = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        bool isCamera() const { return type == Type::Camera; }
        void syncCameraFromTransform() {
            if (!isCamera()) return;
            cameraData.position = transform.position;
            cameraData.orientation = transform.rotation;
        }
    };

    void loadModel(const std::string& path, const glm::vec3& position, const BufferManager& bufMgr);
    uint64_t createModelEntity(const std::string& path,
                               const glm::vec3& position,
                               const BufferManager& bufMgr,
                               bool useResourceCache = true,
                               uint64_t preferredEntityId = 0);
    bool removeModelEntity(uint64_t entityId, const VulkanContext& ctx);
    void destroyModelBuffers(const VulkanContext& ctx);
    void destroy(const VulkanContext& ctx);

    /** @brief 创建 Camera 实体，使用共享的 Camera 模型资源 */
    uint64_t createCameraEntity(const glm::vec3& position,
                                 const glm::quat& orientation,
                                 const VulkanContext& ctx,
                                 const BufferManager& bufMgr,
                                 uint64_t preferredEntityId = 0);
    
    /** @brief 判断 entityId 是否为 Camera 类型 */
    bool isCameraEntity(uint64_t entityId) const;

    const std::vector<ModelEntity>& getModelEntities() const { return modelEntities_; }
    std::vector<ModelEntity>& getModelEntities() { return modelEntities_; }
    ModelEntity* getModelEntity(uint64_t id);
    void setEntityTransform(uint64_t id, const ObjectTransform& t);
    void setEntityMaterial(uint64_t id, uint32_t matId);
    void setEntityVisibility(uint64_t id, bool v);

    VkBuffer getVertexBuffer() const;
    VkBuffer getIndexBuffer() const;
    uint32_t getModelIndexCount() const;
    glm::vec3 getModelPosition() const;
    void setModelPosition(const glm::vec3& p);
    glm::vec3 getModelBoundsMin() const { return modelLocalBoundsMin_; }
    glm::vec3 getModelBoundsMax() const { return modelLocalBoundsMax_; }

    const std::vector<SubMesh>& getModelSubMeshes() const;
    const std::vector<std::string>& getModelAutoAstPaths() const;

    uint32_t getModelMaterialId() const;
    void setModelMaterialId(uint32_t id);
    uint32_t getModelSubMeshMaterialId(int slot) const;
    void setModelSubMeshMaterialId(int slot, uint32_t id);

    std::shared_ptr<Skeleton> getEntitySkeleton(uint64_t entityId) const;
    const std::vector<AnimationClip>& getEntityAnimationClips(uint64_t entityId) const;
    void setEntityAnimationData(uint64_t entityId,
                                std::shared_ptr<Skeleton> skeleton,
                                std::vector<AnimationClip> clips);

    void createCubeTemplate(const BufferManager& bufMgr);
    RenderEntityId addBox(const glm::vec3& position, const VulkanContext& ctx, const BufferManager& bufMgr);
    bool removeBox(RenderEntityId id, const VulkanContext& ctx, const BufferManager& bufMgr);
    glm::vec3 getBoxPosition(RenderEntityId id) const;
    void setBoxPosition(RenderEntityId id, const glm::vec3& pos, const VulkanContext& ctx, const BufferManager& bufMgr);
    VkBuffer getCubeVertexBuffer() const { return cubeVertexBuffer_; }
    VkBuffer getCubeIndexBuffer() const { return cubeIndexBuffer_; }
    uint32_t getCubeIndexCount() const { return cubeIndexCount_; }
    VkBuffer getInstanceBuffer() const { return instanceBuffer_; }
    uint32_t getInstanceCount() const { return instanceCount_; }
    const std::vector<RenderEntityId>& getBoxRangeEntityIds() const { return boxRangeEntityIds_; }
    const std::unordered_map<RenderEntityId, glm::vec3>& getBoxes() const { return boxes_; }

    void setBoxMaterialId(RenderEntityId eid, uint32_t id) { boxMaterialIds_[eid] = id; }
    uint32_t getBoxMaterialId(RenderEntityId eid) const;
    bool hasBoxMaterialId(RenderEntityId eid) const { return boxMaterialIds_.count(eid) > 0; }

    static std::string dumpGltfMaterialAst(const void* cgltfMaterial,
                                           const std::string& baseName,
                                           int primIndex,
                                           const std::string& gltfDir,
                                           const std::string& resRoot,
                                           const std::string& materialSubFolder = "");

private:
    std::vector<ModelEntity> modelEntities_;
    glm::vec3 modelLocalBoundsMin_{};
    glm::vec3 modelLocalBoundsMax_{};

    struct CachedModelResource {
        VkBuffer vertexBuffer{};
        VkDeviceMemory vertexMemory{};
        VkBuffer indexBuffer{};
        VkDeviceMemory indexMemory{};
        uint32_t indexCount = 0;
        std::vector<SubMesh> subMeshes;
        std::vector<std::string> autoAstPaths;
        bool hasSkin = false;
        glm::vec3 boundsMin{};
        glm::vec3 boundsMax{};
        // 缓存动画状态，重复加载同模型时共享
        std::shared_ptr<Skeleton> skeleton;
        std::vector<AnimationClip> animationClips;
    };
    std::unordered_map<std::string, CachedModelResource> modelResourceCache_;

    std::vector<Vertex> cubeTemplateVertices_;
    std::vector<uint32_t> cubeTemplateIndices_;
    std::unordered_map<RenderEntityId, glm::vec3> boxes_;
    std::vector<RenderEntityId> boxRangeEntityIds_;
    RenderEntityId nextId_ = 1;
    std::unordered_map<RenderEntityId, uint32_t> boxMaterialIds_;

    VkBuffer cubeVertexBuffer_{};
    VkDeviceMemory cubeVertexMemory_{};
    VkBuffer cubeIndexBuffer_{};
    VkDeviceMemory cubeIndexMemory_{};
    VkBuffer instanceBuffer_{};
    VkDeviceMemory instanceMemory_{};
    uint32_t cubeIndexCount_ = 0;
    uint32_t instanceCount_ = 0;

    void rebuildInstanceBuffer(const VulkanContext& ctx, const BufferManager& bufMgr);
    static void destroyBuf(const VulkanContext& ctx, VkBuffer& buf, VkDeviceMemory& mem);
    static std::string normalizeModelPath(const std::string& path);
    bool applyCachedModelResource(ModelEntity& ent,
                                  const std::string& key,
                                  const glm::vec3& position);
    void cacheModelResourceFromEntity(const std::string& key, ModelEntity& ent);
    void releaseEntityMeshBuffers(const VulkanContext& ctx, ModelEntity& ent);
    void destroyCachedModelResources(const VulkanContext& ctx);
    void loadModelFromObj(const std::string& path, const glm::vec3& position, const BufferManager& bufMgr);
    void loadModelFromGltf(const std::string& path, const glm::vec3& position, const BufferManager& bufMgr);
    void loadModelFromFbx(const std::string& path, const glm::vec3& position, const BufferManager& bufMgr);

    // ── Camera Actor 模型缓存 ─────────────────────────────────────────────
    VkBuffer cameraModelVertexBuffer_{};
    VkDeviceMemory cameraModelVertexMemory_{};
    VkBuffer cameraModelIndexBuffer_{};
    VkDeviceMemory cameraModelIndexMemory_{};
    uint32_t cameraModelIndexCount_ = 0;
    std::vector<SubMesh> cameraModelSubMeshes_;
    bool cameraModelLoaded_ = false;
    uint64_t nextEntityId_ = 1000;

    uint64_t allocateEntityId(uint64_t preferredEntityId = 0);
    
    void loadCameraModelOnce(const VulkanContext& ctx, const BufferManager& bufMgr);
};
