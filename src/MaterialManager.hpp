#pragma once
#include "VulkanContext.hpp"
#include "CommandManager.hpp"
#include "BufferManager.hpp"
#include "FramebufferManager.hpp"
#include "PipelineManager.hpp"
#include "VulkanTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

using MaterialId = uint32_t;
constexpr MaterialId kInvalidMaterialId = 0;

enum class MaterialType { Mesh, Box, Material };

struct MaterialParams {
    glm::vec4 baseColor         = { 1.f, 1.f, 1.f, 1.f };
    float     roughness         = 0.5f;
    float     metallic          = 0.f;
    float     emissiveIntensity = 0.f;
    float     _pad              = 0.f;
    glm::vec4 emissiveColor     = { 0.f, 0.f, 0.f, 1.f };
};

class MaterialManager {
public:
    // ── 生命周期 ──────────────────────────────────────────────────────
    void init(const VulkanContext& ctx, const CommandManager& cmdMgr,
              const BufferManager& bufMgr, const FramebufferManager& fbMgr,
              const PipelineManager& pipeMgr, uint32_t imageCount,
              const std::string& defaultTexturePath);

    void onSwapchainRecreate(const VulkanContext& ctx, const CommandManager& cmdMgr,
                             const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                             const PipelineManager& pipeMgr, uint32_t imageCount);

    void destroy(const VulkanContext& ctx);

    // ── 材质创建 ──────────────────────────────────────────────────────
    MaterialId createMeshMaterial(const std::string& name,
                                  const std::string& albedoPath,
                                  const std::string& normalPath,
                                  const std::string& metallicRoughnessPath,
                                  const std::string& aoPath,
                                  const std::string& emissivePath,
                                  const MaterialParams& params,
                                  const VulkanContext& ctx, const CommandManager& cmdMgr,
                                  const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                                  const PipelineManager& pipeMgr);

    // Backward-compatible overload (albedo + normal only).
    MaterialId createMeshMaterial(const std::string& name,
                                  const std::string& albedoPath,
                                  const std::string& normalPath,
                                  const MaterialParams& params,
                                  const VulkanContext& ctx, const CommandManager& cmdMgr,
                                  const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                                  const PipelineManager& pipeMgr) {
        return createMeshMaterial(name, albedoPath, normalPath, "", "", "",
                                  params, ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
    }

    MaterialId createBoxMaterial(const std::string& name, const MaterialParams& params,
                                 const VulkanContext& ctx, const BufferManager& bufMgr,
                                 const PipelineManager& pipeMgr);

    // Load a material from a .ast (JSON) asset file.
    //   astRelPath: relative to res/, e.g. "materials/mainmodel.ast".
    // On success returns a valid MaterialId; on failure returns kInvalidMaterialId
    // and prints the reason to stderr. The caller should fall back to a default
    // material in that case.
    MaterialId loadMaterialFromAsset(const std::string& astRelPath,
                                     const VulkanContext& ctx, const CommandManager& cmdMgr,
                                     const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                                     const PipelineManager& pipeMgr);

    // Resolve the VkPipeline this material should be drawn with.
    // Falls back to the default Mesh / Box pipeline when the material has
    // no custom shader assigned. PipelineManager is non-const because it
    // may have to lazily create a new pipeline for a previously unseen
    // (vert,frag) pair.
    VkPipeline getPipeline(MaterialId id,
                           const VulkanContext& ctx,
                           PipelineManager& pipeMgr) const;

    MaterialId cloneMaterial(MaterialId src,
                             const VulkanContext& ctx, const CommandManager& cmdMgr,
                             const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                             const PipelineManager& pipeMgr);
    MaterialId createSkinnedMaterialFrom(MaterialId src,
                                         const VulkanContext& ctx,
                                         const CommandManager& cmdMgr,
                                         const BufferManager& bufMgr,
                                         const FramebufferManager& fbMgr,
                                         const PipelineManager& pipeMgr);
    bool hasSkinning(MaterialId id) const;
    void updateBoneMatrices(MaterialId id,
                            uint32_t imageIndex,
                            const std::vector<glm::mat4>& bones);

    void destroyMaterial(MaterialId id, const VulkanContext& ctx);

    // ── 参数更新 ──────────────────────────────────────────────────────
    void setParams(MaterialId id, const MaterialParams& params);
    const MaterialParams& getParams(MaterialId id) const;
    MaterialParams& getParamsMut(MaterialId id);

    // ── 纹理热替换（Mesh 材质） ────────────────────────────────────────
    bool setAlbedoPath(MaterialId id, const std::string& path,
                       const VulkanContext& ctx, const CommandManager& cmdMgr,
                       const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                       const PipelineManager& pipeMgr, std::string* error = nullptr);

    bool setNormalPath(MaterialId id, const std::string& path,
                       const VulkanContext& ctx, const CommandManager& cmdMgr,
                       const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                       const PipelineManager& pipeMgr, std::string* error = nullptr);

    // ── 每帧 UBO 更新 ─────────────────────────────────────────────────
    // 将 view/proj 和材质颜色写入所有材质的 imageIndex 号 UBO
    void updateAllUBOs(uint32_t imageIndex, const glm::mat4& view, const glm::mat4& proj);

    /** @brief 仅更新所有材质的 PiP UBO（独立于主场景 UBO） */
    void updateAllPipUBOs(uint32_t imageIndex, const glm::mat4& view, const glm::mat4& proj);

    // ── Descriptor Set 访问 ──────────────────────────────────────────
    VkDescriptorSet getDescriptorSet(MaterialId id, uint32_t imageIndex) const;

    /** @brief 返回材质的 PiP 专用 descriptor set（binding 0 指向 PiP UBO） */
    VkDescriptorSet getPipDescriptorSet(MaterialId id, uint32_t imageIndex) const;

    // ── 查询 ─────────────────────────────────────────────────────────
    MaterialType        getMaterialType(MaterialId id) const;
    const std::string&  getMaterialName(MaterialId id) const;
    void                setMaterialName(MaterialId id, const std::string& name);
    const std::string&  getAlbedoPath(MaterialId id) const;
    const std::string&  getNormalPath(MaterialId id) const;
    bool                isValid(MaterialId id) const;
    bool                isDeletable(MaterialId id) const;
    MaterialId          getDefaultMeshMaterialId() const { return defaultMeshId_; }
    MaterialId          getDefaultBoxMaterialId()  const { return defaultBoxId_;  }
    const std::vector<MaterialId>& getAllMaterialIds() const { return allIds_; }

    // ── Swapchain recreate 时重载默认 Mesh 材质纹理 ──────────────────
    void reloadDefaultMeshTextures(const std::string& texturePath,
                                   const VulkanContext& ctx, const CommandManager& cmdMgr,
                                   const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                                   const PipelineManager& pipeMgr);

private:
    struct TextureGPU {
        VkImage        image   {};
        VkDeviceMemory memory  {};
        VkImageView    view    {};
        VkSampler      sampler {};
        bool           isDefault = true;
        std::string    path;
    };

    struct MaterialEntry {
        std::string    name;
        MaterialType   type      = MaterialType::Mesh;
        MaterialParams params;
        bool           deletable = true;

        std::vector<VkBuffer>        ubos;
        std::vector<VkDeviceMemory>  uboMemory;
        std::vector<void*>           uboMapped;
        std::vector<VkBuffer>        boneUbos;
        std::vector<VkDeviceMemory>  boneUboMemory;
        std::vector<void*>           boneUboMapped;
        std::vector<VkDescriptorSet> descSets;
        bool                         skinned = false;

        // PiP 预览专用 UBO + descriptor set：view/proj 独立于主相机，
        // 避免与主场景共享同一块 mapped UBO 导致的 GPU 读取时序冲突。
        std::vector<VkBuffer>        pipUbos;
        std::vector<VkDeviceMemory>  pipUboMemory;
        std::vector<void*>           pipUboMapped;
        std::vector<VkDescriptorSet> pipDescSets;

        TextureGPU albedo, normal; // Mesh material textures (legacy)
        TextureGPU metallicRoughness, ao, emissive; // PBR additions

        // Optional shader override (paths already resolved against res/).
        // Empty -> use the default pipeline for this MaterialType.
        std::string vertSpvPath;
        std::string fragSpvPath;
    };

    std::unordered_map<MaterialId, MaterialEntry> materials_;
    std::vector<MaterialId> allIds_;
    MaterialId nextId_        = 1;
    MaterialId defaultMeshId_ = kInvalidMaterialId;
    MaterialId defaultBoxId_  = kInvalidMaterialId;
    uint32_t   imageCount_    = 0;

    // 资产缓存：已加载的 .ast → MaterialId，避免重复创建
    std::unordered_map<std::string, MaterialId> assetCache_;
    std::unordered_map<MaterialId, MaterialId> skinnedCache_;

    // Shared 1x1 fallback textures
    TextureGPU defaultAlbedo_, defaultNormal_;
    TextureGPU defaultMR_, defaultAO_, defaultEmissive_;
    VkDescriptorPool pool_{};

    // Internal helpers
    void createDefaultTextures(const VulkanContext& ctx, const CommandManager& cmdMgr,
                               const FramebufferManager& fbMgr);
    void destroyDefaultTextures(const VulkanContext& ctx);
    void createPool(const VulkanContext& ctx);

    MaterialId allocateId();
    void createUBOs(MaterialEntry& e, const VulkanContext& ctx, const BufferManager& bufMgr);
    void destroyUBOs(MaterialEntry& e, const VulkanContext& ctx);
    void allocateDescSets(MaterialEntry& e, const VulkanContext& ctx,
                          const PipelineManager& pipeMgr);
    void writeDescSets(MaterialEntry& e, const VulkanContext& ctx,
                       const PipelineManager& pipeMgr);
    /** @brief 为材质创建 PiP 专用 UBO + descriptor set（纹理/bone UBO 复用主资源） */
    void createPipResources(MaterialEntry& e, const VulkanContext& ctx,
                            const BufferManager& bufMgr, const PipelineManager& pipeMgr);
    void writePipDescSets(MaterialEntry& e, const VulkanContext& ctx);
    void destroyPipUBOs(MaterialEntry& e, const VulkanContext& ctx);
    void destroyEntry(MaterialEntry& e, const VulkanContext& ctx);

    static void loadTexture(TextureGPU& tex, const std::string& path,
                            const VulkanContext& ctx, const CommandManager& cmdMgr,
                            const BufferManager& bufMgr, const FramebufferManager& fbMgr);
    static void destroyTexture(TextureGPU& tex, const VulkanContext& ctx);
    static void createSampler(const VulkanContext& ctx, VkSampler& sampler);
    static void transitionLayout(const VulkanContext& ctx, const CommandManager& cmdMgr,
                                 VkImage image, VkImageLayout from, VkImageLayout to);
    static void copyBufToImage(const VulkanContext& ctx, const CommandManager& cmdMgr,
                               VkBuffer buf, VkImage img, uint32_t w, uint32_t h);
    static void uploadTexture1x1(TextureGPU& tex, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                 const VulkanContext& ctx, const CommandManager& cmdMgr,
                                 const FramebufferManager& fbMgr);
};
