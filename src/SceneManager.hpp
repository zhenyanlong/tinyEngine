#pragma once
#include "VulkanContext.hpp"
#include "BufferManager.hpp"
#include "VulkanTypes.hpp"
#include "vectex.hpp"
#include "Transform.hpp"
#include "Animation/Skeleton.hpp"
#include "Animation/AnimationClip.hpp"
#include <memory>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

class SceneManager {
public:
    static constexpr uint32_t kPickIdNone      = 0;
    static constexpr uint32_t kPickIdMainModel = 1;
    static constexpr uint32_t kPickIdBoxBase   = 2;

    /** @brief 一个子网格范围，对应 glTF 的一个 primitive 或 OBJ 的整个网格 */
    struct SubMesh {
        uint32_t indexOffset = 0;
        uint32_t indexCount  = 0;
        int      materialSlot = -1; ///< modelSubMeshMaterials_ 中的槽位，-1 表示回退到 materialId
    };

    /** @brief 场景中的模型实体：拥有独立的 Transform、GPU 缓冲、材质和选中状态 */
    struct ModelEntity {
        uint64_t       entityId = 0;       ///< 全局唯一 ID，也用作 PickId
        uint64_t       modelAssetId = 0;   ///< ModelAsset::id，关联到 ModelRegistry
        ObjectTransform transform;
        uint32_t       materialId = 0;     ///< 0 表示使用回退材质
        bool           visible    = true;
        bool           selected   = false;
        std::string    displayName;        ///< Outliner 中显示的名称
        std::string    astRelPath;         ///< 创建此实体的 .ast 资产路径（可能为空，OBJ 直接加载时为空）

        // 顶点/索引数据（由 SceneManager 管理生命周期）
        std::vector<Vertex>    vertices;
        std::vector<uint32_t>  indices;

        // GPU 资源句柄
        VkBuffer       vertexBuffer{};  VkDeviceMemory vertexMemory{};
        VkBuffer       indexBuffer{};   VkDeviceMemory indexMemory{};
        uint32_t       indexCount  = 0;

        // 子网格列表 + 逐槽位材质
        std::vector<SubMesh>     subMeshes;
        std::vector<uint32_t>    subMeshMaterials; ///< 每个槽位的 MaterialId，0 = 未绑定
        std::vector<std::string> autoAstPaths;     ///< glTF 自动导出的 .ast 路径
    };

    // ── 模型生命周期 ──────────────────────────────────────────────────────

    /** @brief 兼容旧接口：加载单模型到第一个实体槽位 */
    void loadModel(const std::string& path, const glm::vec3& position, const BufferManager& bufMgr);

    /** @brief 创建新的模型实体（从文件路径加载），返回 entityId */
    uint64_t createModelEntity(const std::string& path, const glm::vec3& position, const BufferManager& bufMgr);

    /** @brief 销毁指定模型实体的 GPU 资源并从列表中移除 */
    bool removeModelEntity(uint64_t entityId, const VulkanContext& ctx);

    /** @brief 销毁当前主模型的 GPU 缓冲（兼容旧接口） */
    void destroyModelBuffers(const VulkanContext& ctx);

    /** @brief 销毁所有资源 */
    void destroy(const VulkanContext& ctx);

    // ── 多实体访问器 ──────────────────────────────────────────────────────

    const std::vector<ModelEntity>& getModelEntities() const { return modelEntities_; }
    ModelEntity* getModelEntity(uint64_t id);
    void setEntityTransform(uint64_t id, const ObjectTransform& t);
    void setEntityMaterial(uint64_t id, uint32_t matId);
    void setEntityVisibility(uint64_t id, bool v);

    // ── 兼容旧接口：委托到第一个 ModelEntity ──────────────────────────────

    VkBuffer getVertexBuffer()     const;
    VkBuffer getIndexBuffer()      const;
    uint32_t getModelIndexCount()  const;
    glm::vec3 getModelPosition()   const;
    void      setModelPosition(const glm::vec3& p);
    glm::vec3 getModelBoundsMin()  const { return modelLocalBoundsMin_; }
    glm::vec3 getModelBoundsMax()  const { return modelLocalBoundsMax_; }

    const std::vector<SubMesh>&     getModelSubMeshes()        const;
    const std::vector<std::string>& getModelAutoAstPaths()     const;

    uint32_t getModelMaterialId()                              const;
    void     setModelMaterialId(uint32_t id);

    uint32_t getModelSubMeshMaterialId(int slot) const;
    void     setModelSubMeshMaterialId(int slot, uint32_t id);

    // ── 骨骼动画访问器 ────────────────────────────────────────────────────

    std::shared_ptr<Skeleton>           getSkeleton()         const { return skeleton_; }
    const std::vector<AnimationClip>&    getAnimationClips()   const { return animationClips_; }

    // ── Box 系统（不变）───────────────────────────────────────────────────

    void createCubeTemplate(const BufferManager& bufMgr);
    RenderEntityId addBox(const glm::vec3& position, const VulkanContext& ctx, const BufferManager& bufMgr);
    bool           removeBox(RenderEntityId id, const VulkanContext& ctx, const BufferManager& bufMgr);
    glm::vec3      getBoxPosition(RenderEntityId id) const;
    void           setBoxPosition(RenderEntityId id, const glm::vec3& pos,
                                  const VulkanContext& ctx, const BufferManager& bufMgr);
    VkBuffer getCubeVertexBuffer() const { return cubeVertexBuffer_; }
    VkBuffer getCubeIndexBuffer()  const { return cubeIndexBuffer_; }
    uint32_t getCubeIndexCount()   const { return cubeIndexCount_; }
    VkBuffer getInstanceBuffer()   const { return instanceBuffer_; }
    uint32_t getInstanceCount()    const { return instanceCount_; }
    const std::vector<RenderEntityId>& getBoxRangeEntityIds() const { return boxRangeEntityIds_; }
    const std::unordered_map<RenderEntityId, glm::vec3>& getBoxes() const { return boxes_; }

    void     setBoxMaterialId(RenderEntityId eid, uint32_t id) { boxMaterialIds_[eid] = id; }
    uint32_t getBoxMaterialId(RenderEntityId eid) const;
    bool     hasBoxMaterialId(RenderEntityId eid) const { return boxMaterialIds_.count(eid) > 0; }

private:
    // ── 模型实体列表 ──────────────────────────────────────────────────────
    std::vector<ModelEntity> modelEntities_;

    // ── AABB 缓存（所有实体的总包围盒）────────────────────────────────────
    glm::vec3 modelLocalBoundsMin_{};
    glm::vec3 modelLocalBoundsMax_{};

    // ── 骨骼动画数据 ──────────────────────────────────────────────────────
    std::shared_ptr<Skeleton>        skeleton_;
    std::vector<AnimationClip>       animationClips_;

    // ── Box 系统 ──────────────────────────────────────────────────────────
    std::vector<Vertex>    cubeTemplateVertices_;
    std::vector<uint32_t>  cubeTemplateIndices_;
    std::unordered_map<RenderEntityId, glm::vec3> boxes_;
    std::vector<RenderEntityId>                   boxRangeEntityIds_;
    RenderEntityId nextId_ = 1;
    std::unordered_map<RenderEntityId, uint32_t>  boxMaterialIds_;

    VkBuffer cubeVertexBuffer_{};  VkDeviceMemory cubeVertexMemory_{};
    VkBuffer cubeIndexBuffer_{};   VkDeviceMemory cubeIndexMemory_{};
    VkBuffer instanceBuffer_{};    VkDeviceMemory instanceMemory_{};
    uint32_t cubeIndexCount_ = 0;
    uint32_t instanceCount_  = 0;

    void rebuildInstanceBuffer(const VulkanContext& ctx, const BufferManager& bufMgr);
    static void destroyBuf(const VulkanContext& ctx, VkBuffer& buf, VkDeviceMemory& mem);

    void loadModelFromObj (const std::string& path, const glm::vec3& position, const BufferManager& bufMgr);
    void loadModelFromGltf(const std::string& path, const glm::vec3& position, const BufferManager& bufMgr);
};
