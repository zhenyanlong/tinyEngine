#include "PickSystem.hpp"
#include "TinyEngineDebug.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <cstring>
#include <stdexcept>

static constexpr uint32_t kPickPushConstantSize = 68u;

void PickSystem::create(const VulkanContext& ctx, const CommandManager& cmdMgr)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = cmdMgr.getCommandPool();
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(ctx.getDevice(), &ai, &pickCmdBuf_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate pick command buffer!");

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = sizeof(uint32_t);
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx.getDevice(), &bi, nullptr, &readbackBuf_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pick readback buffer!");

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx.getDevice(), readbackBuf_, &req);
    VkMemoryAllocateInfo mi{};
    mi.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mi.allocationSize  = req.size;
    mi.memoryTypeIndex = ctx.findMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(ctx.getDevice(), &mi, nullptr, &readbackMem_) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate pick readback memory!");
    vkBindBufferMemory(ctx.getDevice(), readbackBuf_, readbackMem_, 0);
    vkMapMemory(ctx.getDevice(), readbackMem_, 0, sizeof(uint32_t), 0, &readbackMapped_);
}

void PickSystem::destroy(const VulkanContext& ctx, const CommandManager& cmdMgr)
{
    auto dev = ctx.getDevice();
    if (readbackMapped_) { vkUnmapMemory(dev, readbackMem_); readbackMapped_ = nullptr; }
    if (readbackBuf_ != VK_NULL_HANDLE) { vkDestroyBuffer(dev, readbackBuf_, nullptr); readbackBuf_ = VK_NULL_HANDLE; }
    if (readbackMem_ != VK_NULL_HANDLE) { vkFreeMemory(dev, readbackMem_, nullptr);    readbackMem_ = VK_NULL_HANDLE; }
    if (pickCmdBuf_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(dev, cmdMgr.getCommandPool(), 1, &pickCmdBuf_);
        pickCmdBuf_ = VK_NULL_HANDLE;
    }
}

uint32_t PickSystem::runPick(const VulkanContext& ctx,
                              const RenderPassManager& rpMgr,
                              const FramebufferManager& fbMgr,
                              const PipelineManager& pipelineMgr,
                              const MaterialManager& materialMgr,
                              VkDescriptorSet boxDescSet0,
                              uint32_t materialImageIndex,
                              const SceneManager& scene,
                              VkExtent2D extent,
                              uint32_t pixelX, uint32_t pixelY)
{
    if (pickCmdBuf_ == VK_NULL_HANDLE || readbackMapped_ == nullptr) return SceneManager::kPickIdNone;

    const auto& entities = scene.getModelEntities();
    if (entities.empty() || entities[0].vertexBuffer == VK_NULL_HANDLE) return SceneManager::kPickIdNone;

    vkDeviceWaitIdle(ctx.getDevice());
    vkResetCommandBuffer(pickCmdBuf_, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(pickCmdBuf_, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("pick: begin command buffer");

    TINYENGINE(pickCmdBuf_, "Pick Pass");

    VkClearValue clears[2]{};
    clears[0].color.uint32[0] = 0;
    clears[1].depthStencil    = { 1.0f, 0 };

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass        = rpMgr.getPickRenderPass();
    rpBegin.framebuffer       = fbMgr.getPickFramebuffer();
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount   = 2;
    rpBegin.pClearValues      = clears;
    vkCmdBeginRenderPass(pickCmdBuf_, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // pick 管线已启用动态 viewport/scissor，需显式设置与 framebuffer 一致的范围。
    {
        VkViewport vp{ 0.f, 0.f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.f, 1.f };
        VkRect2D sc{ {0, 0}, extent };
        vkCmdSetViewport(pickCmdBuf_, 0, 1, &vp);
        vkCmdSetScissor(pickCmdBuf_, 0, 1, &sc);
    }

    // 绘制每个模型实体（各自独立 vertex/index buffer，PickId = entityId）
    for (const auto& ent : entities) {
        if (!ent.visible || ent.indexCount == 0 || !ent.vertexBuffer || !ent.indexBuffer)
            continue;

        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(pickCmdBuf_, 0, 1, &ent.vertexBuffer, &off);
        vkCmdBindIndexBuffer(pickCmdBuf_, ent.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // 与主/PiP 渲染路径一致：Camera 实体叠加 modelRotationOffset（如 Y -90°），
        // 使 pick 命中测试与实际显示的模型几何体对齐。
        glm::mat4 m = ent.transform.GetModelMatrix() * glm::mat4_cast(ent.modelRotationOffset);
        std::array<uint8_t, kPickPushConstantSize> bytes{};
        std::memcpy(bytes.data(), &m, sizeof(glm::mat4));
        const uint32_t oid = static_cast<uint32_t>(ent.entityId);
        std::memcpy(bytes.data() + sizeof(glm::mat4), &oid, sizeof(uint32_t));

        auto drawRange = [&](MaterialId materialId, SkinBindingId skinBinding,
                             uint32_t indexOffset, uint32_t indexCount) {
            const bool skinned = ent.hasSkin_ && materialMgr.isValidSkinBinding(skinBinding);
            const VkPipeline pipeline = skinned
                ? pipelineMgr.getSkinnedPickPipeline()
                : pipelineMgr.getPickPipeline();
            const VkPipelineLayout layout = skinned
                ? pipelineMgr.getSkinnedPickPipelineLayout()
                : pipelineMgr.getPickPipelineLayout();

            vkCmdBindPipeline(pickCmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            VkDescriptorSet descriptorSet = skinned
                ? materialMgr.getSkinDescriptorSet(skinBinding, materialImageIndex)
                : boxDescSet0;
            vkCmdBindDescriptorSets(pickCmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    layout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdPushConstants(pickCmdBuf_, layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, kPickPushConstantSize, bytes.data());
            vkCmdDrawIndexed(pickCmdBuf_, indexCount, 1, indexOffset, 0, 0);
        };

        if (ent.subMeshes.empty()) {
            drawRange(ent.materialId, ent.skinBindingId, 0, ent.indexCount);
        } else {
            for (size_t subMeshIndex = 0; subMeshIndex < ent.subMeshes.size(); ++subMeshIndex) {
                const auto& subMesh = ent.subMeshes[subMeshIndex];
                MaterialId materialId = ent.materialId;
                if (subMesh.materialSlot >= 0
                    && subMesh.materialSlot < static_cast<int>(ent.subMeshMaterials.size())) {
                    const MaterialId slotMaterial = ent.subMeshMaterials[subMesh.materialSlot];
                    if (materialMgr.isValid(slotMaterial)) materialId = slotMaterial;
                }
                const SkinBindingId skinBinding = subMeshIndex < ent.subMeshSkinBindings.size()
                    ? static_cast<SkinBindingId>(ent.subMeshSkinBindings[subMeshIndex])
                    : kInvalidSkinBindingId;
                drawRange(materialId, skinBinding, subMesh.indexOffset, subMesh.indexCount);
            }
        }
    }

    // Draw boxes
    const auto& entityIds = scene.getBoxRangeEntityIds();
    if (scene.getCubeVertexBuffer() != VK_NULL_HANDLE && scene.getCubeIndexCount() > 0 &&
        !entityIds.empty())
    {
        vkCmdBindPipeline(pickCmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineMgr.getPickPipeline());
        vkCmdBindDescriptorSets(pickCmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineMgr.getPickPipelineLayout(), 0, 1,
                                &boxDescSet0, 0, nullptr);
        VkDeviceSize off = 0;
        VkBuffer cvb = scene.getCubeVertexBuffer();
        vkCmdBindVertexBuffers(pickCmdBuf_, 0, 1, &cvb, &off);
        vkCmdBindIndexBuffer(pickCmdBuf_, scene.getCubeIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        const auto& boxes = scene.getBoxes();
        for (size_t i = 0; i < entityIds.size(); ++i) {
            glm::mat4 bm = glm::translate(glm::mat4(1.0f), boxes.at(entityIds[i]));
            std::array<uint8_t, kPickPushConstantSize> bytes{};
            std::memcpy(bytes.data(), &bm, sizeof(glm::mat4));
            const uint32_t oid = SceneManager::kPickIdBoxBase + static_cast<uint32_t>(i);
            std::memcpy(bytes.data() + sizeof(glm::mat4), &oid, sizeof(uint32_t));
            vkCmdPushConstants(pickCmdBuf_, pipelineMgr.getPickPipelineLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, kPickPushConstantSize, bytes.data());
            vkCmdDrawIndexed(pickCmdBuf_, scene.getCubeIndexCount(), 1, 0, 0, 0);
        }
    }

    vkCmdEndRenderPass(pickCmdBuf_);

    // Transition color image → transfer src, copy pixel, transition back
    VkImageMemoryBarrier toSrc{};
    toSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image               = fbMgr.getPickColorImage();
    toSrc.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toSrc.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(pickCmdBuf_,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toSrc);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset      = { static_cast<int32_t>(pixelX), static_cast<int32_t>(pixelY), 0 };
    region.imageExtent      = { 1, 1, 1 };
    vkCmdCopyImageToBuffer(pickCmdBuf_, fbMgr.getPickColorImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuf_, 1, &region);

    VkImageMemoryBarrier toDst{};
    toDst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toDst.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image               = fbMgr.getPickColorImage();
    toDst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toDst.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    toDst.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(pickCmdBuf_,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toDst);

    if (vkEndCommandBuffer(pickCmdBuf_) != VK_SUCCESS)
        throw std::runtime_error("pick: end command buffer");

    VkSubmitInfo sub{};
    sub.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers    = &pickCmdBuf_;
    if (vkQueueSubmit(ctx.getGraphicsQueue(), 1, &sub, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("pick: queue submit");
    vkQueueWaitIdle(ctx.getGraphicsQueue());

    uint32_t id = SceneManager::kPickIdNone;
    std::memcpy(&id, readbackMapped_, sizeof(uint32_t));
    return id;
}
