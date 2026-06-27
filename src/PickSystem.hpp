#pragma once
#include "VulkanContext.hpp"
#include "CommandManager.hpp"
#include "RenderPassManager.hpp"
#include "FramebufferManager.hpp"
#include "PipelineManager.hpp"
#include "MaterialManager.hpp"
#include "SceneManager.hpp"
#include <glm/glm.hpp>
#include <cstdint>

class PickSystem {
public:
    void create(const VulkanContext& ctx, const CommandManager& cmdMgr);
    void destroy(const VulkanContext& ctx, const CommandManager& cmdMgr);

    /**
     * @brief 执行 GPU 拾取，返回命中实体的 PickId。
     *
     * PickId 约定：
     *  - kPickIdNone (0)    无命中
     *  - entityId 的 uint32_t 截断值  命中某个 ModelEntity
     *  - kPickIdBoxBase + i        命中第 i 个 Box 实例
     */
    uint32_t runPick(const VulkanContext& ctx,
                     const RenderPassManager& rpMgr,
                     const FramebufferManager& fbMgr,
                     const PipelineManager& pipelineMgr,
                     const MaterialManager& materialMgr,
                     VkDescriptorSet boxDescSet0,
                     uint32_t materialImageIndex,
                     const SceneManager& scene,
                     VkExtent2D extent,
                     uint32_t pixelX, uint32_t pixelY);

private:
    VkCommandBuffer  pickCmdBuf_{};
    VkBuffer         readbackBuf_{};
    VkDeviceMemory   readbackMem_{};
    void*            readbackMapped_ = nullptr;
};
