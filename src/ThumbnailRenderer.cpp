#include "ThumbnailRenderer.hpp"
#include "VulkanContext.hpp"
#include "CommandManager.hpp"
#include "PipelineManager.hpp"
#include "MaterialManager.hpp"
#include "SceneManager.hpp"
#include "BufferManager.hpp"
#include "FramebufferManager.hpp"
#include "nlohmann/json.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

// ─── Helpers ──────────────────────────────────────────────────────────────────

static VkFormat findDepthFormat(const VulkanContext& ctx)
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT
    };
    for (VkFormat f : candidates) {
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(ctx.getPhysicalDevice(), f, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return f;
    }
    throw std::runtime_error("No depth format found");
}

static uint32_t findMemoryType(const VulkanContext& ctx, uint32_t bits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(ctx.getPhysicalDevice(), &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("No suitable memory type");
}

static void transitionImage(const VulkanContext& ctx, VkCommandBuffer cb,
                            VkImage img, VkImageLayout oldL, VkImageLayout newL)
{
    VkImageMemoryBarrier bar{};
    bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.oldLayout           = oldL;
    bar.newLayout           = newL;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image               = img;
    bar.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkPipelineStageFlags srcStage = 0, dstStage = 0;
    if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldL == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newL == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        bar.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        bar.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        bar.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &bar);
}

// ─── Create / Destroy ─────────────────────────────────────────────────────────

void ThumbnailRenderer::create(const VulkanContext& ctx,
                               const CommandManager& cmdMgr,
                               const PipelineManager& pipeMgr,
                               const FramebufferManager& fbMgr,
                               MaterialManager& matMgr,
                               SceneManager& sceneMgr,
                               const BufferManager& bufMgr,
                               const std::string& resRoot,
                               const std::string& vertSpv,
                               const std::string& fragSpv)
{
    ctx_      = const_cast<VulkanContext*>(&ctx);
    cmdMgr_   = &cmdMgr;
    pipeMgr_  = &pipeMgr;
    fbMgr_   = &fbMgr;
    matMgr_   = &matMgr;
    sceneMgr_ = &sceneMgr;
    bufMgr_   = &bufMgr;
    resRoot_  = resRoot;

    // 优先 BGRA8，回退 RGBA8
    const VkFormat colorFmt = VK_FORMAT_B8G8R8A8_UNORM;
    const VkFormat depthFmt = findDepthFormat(ctx);

    createRenderPass(colorFmt);
    createFramebuffer(colorFmt, depthFmt);
    createReadbackBuffer();
    createThumbCommandBuffer();
    createPipeline(ctx, vertSpv, fragSpv);
}

void ThumbnailRenderer::destroy(const VulkanContext& ctx, MaterialManager&, SceneManager&)
{
    auto dev = ctx.getDevice();

    if (pipeline_ != VK_NULL_HANDLE) { vkDestroyPipeline(dev, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    pipelineLayout_ = VK_NULL_HANDLE; // owned by PipelineManager

    if (cmdBuf_)  { vkFreeCommandBuffers(dev, cmdPool_, 1, &cmdBuf_); cmdBuf_ = VK_NULL_HANDLE; }
    if (cmdPool_) { vkDestroyCommandPool(dev, cmdPool_, nullptr);       cmdPool_ = VK_NULL_HANDLE; }

    if (readbackBuf_ ) { vkDestroyBuffer(dev, readbackBuf_,  nullptr); readbackBuf_ = VK_NULL_HANDLE; }
    if (readbackMem_ ) { vkFreeMemory(dev, readbackMem_, nullptr);     readbackMem_ = VK_NULL_HANDLE; }

    if (framebuffer_) { vkDestroyFramebuffer(dev, framebuffer_, nullptr); framebuffer_ = VK_NULL_HANDLE; }
    if (colorView_)   { vkDestroyImageView(dev, colorView_, nullptr);     colorView_ = VK_NULL_HANDLE; }
    if (colorImage_)  { vkDestroyImage(dev, colorImage_, nullptr);        colorImage_ = VK_NULL_HANDLE; }
    if (colorMemory_) { vkFreeMemory(dev, colorMemory_, nullptr);         colorMemory_ = VK_NULL_HANDLE; }
    if (depthView_)   { vkDestroyImageView(dev, depthView_, nullptr);     depthView_ = VK_NULL_HANDLE; }
    if (depthImage_)  { vkDestroyImage(dev, depthImage_, nullptr);        depthImage_ = VK_NULL_HANDLE; }
    if (depthMemory_) { vkFreeMemory(dev, depthMemory_, nullptr);         depthMemory_ = VK_NULL_HANDLE; }
    if (renderPass_)  { vkDestroyRenderPass(dev, renderPass_, nullptr);   renderPass_ = VK_NULL_HANDLE; }
}

void ThumbnailRenderer::createRenderPass(VkFormat colorFormat)
{
    VkAttachmentDescription color{};
    color.format         = colorFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth{};
    depth.format         = findDepthFormat(*ctx_);
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> atts{ color, depth };
    VkRenderPassCreateInfo rpi{};
    rpi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = static_cast<uint32_t>(atts.size());
    rpi.pAttachments    = atts.data();
    rpi.subpassCount    = 1;
    rpi.pSubpasses      = &subpass;
    rpi.dependencyCount = 1;
    rpi.pDependencies   = &dep;

    if (vkCreateRenderPass(ctx_->getDevice(), &rpi, nullptr, &renderPass_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create thumbnail render pass!");
}

void ThumbnailRenderer::createFramebuffer(VkFormat colorFormat, VkFormat depthFormat)
{
    auto dev = ctx_->getDevice();
    const uint32_t w = kThumbSize, h = kThumbSize;

    // Color image
    {
        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = colorFormat;
        ci.extent        = { w, h, 1 };
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(dev, &ci, nullptr, &colorImage_);

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(dev, colorImage_, &req);
        VkMemoryAllocateInfo mi{};
        mi.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mi.allocationSize  = req.size;
        mi.memoryTypeIndex = findMemoryType(*ctx_, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(dev, &mi, nullptr, &colorMemory_);
        vkBindImageMemory(dev, colorImage_, colorMemory_, 0);

        VkImageViewCreateInfo vi{};
        vi.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image      = colorImage_;
        vi.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        vi.format     = colorFormat;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(dev, &vi, nullptr, &colorView_);
    }

    // Depth image
    {
        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = depthFormat;
        ci.extent        = { w, h, 1 };
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(dev, &ci, nullptr, &depthImage_);

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(dev, depthImage_, &req);
        VkMemoryAllocateInfo mi{};
        mi.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mi.allocationSize  = req.size;
        mi.memoryTypeIndex = findMemoryType(*ctx_, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(dev, &mi, nullptr, &depthMemory_);
        vkBindImageMemory(dev, depthImage_, depthMemory_, 0);

        VkImageViewCreateInfo vi{};
        vi.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image      = depthImage_;
        vi.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        vi.format     = depthFormat;
        vi.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        vkCreateImageView(dev, &vi, nullptr, &depthView_);
    }

    // Framebuffer
    VkImageView attachments[2] = { colorView_, depthView_ };
    VkFramebufferCreateInfo fi{};
    fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass      = renderPass_;
    fi.attachmentCount = 2;
    fi.pAttachments    = attachments;
    fi.width           = w;
    fi.height          = h;
    fi.layers          = 1;
    vkCreateFramebuffer(dev, &fi, nullptr, &framebuffer_);
}

void ThumbnailRenderer::createReadbackBuffer()
{
    auto dev = ctx_->getDevice();
    const VkDeviceSize size = kThumbSize * kThumbSize * 4; // RGBA8

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(dev, &bi, nullptr, &readbackBuf_);

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(dev, readbackBuf_, &req);
    VkMemoryAllocateInfo mi{};
    mi.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mi.allocationSize  = req.size;
    mi.memoryTypeIndex = findMemoryType(*ctx_, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(dev, &mi, nullptr, &readbackMem_);
    vkBindBufferMemory(dev, readbackBuf_, readbackMem_, 0);
}

void ThumbnailRenderer::createThumbCommandBuffer()
{
    VkCommandPoolCreateInfo pi{};
    pi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = ctx_->getGraphicsQueueFamily();
    vkCreateCommandPool(ctx_->getDevice(), &pi, nullptr, &cmdPool_);

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = cmdPool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(ctx_->getDevice(), &ai, &cmdBuf_);
}

void ThumbnailRenderer::createPipeline(const VulkanContext& ctx,
                                        const std::string& vertSpv,
                                        const std::string& fragSpv)
{
    VkExtent2D extent{ kThumbSize, kThumbSize };
    pipeline_ = const_cast<PipelineManager*>(pipeMgr_)->buildMainPipeline(ctx, renderPass_, vertSpv, fragSpv, extent);
    if (pipeline_ == VK_NULL_HANDLE) {
        std::cerr << "[Thumbnail] Failed to create thumbnail pipeline.\n";
        return;
    }
    pipelineLayout_ = pipeMgr_->getMainPipelineLayout();
    if (pipelineLayout_ == VK_NULL_HANDLE) {
        std::cerr << "[Thumbnail] Main pipeline layout is null, thumbnail may not render.\n";
    }
}

// ─── Render & Save ────────────────────────────────────────────────────────────

bool ThumbnailRenderer::renderAndSave(const std::string& modelPath, const std::string& pngOutputPath)
{
    if (modelPath.empty()) return false;

    std::cout << "[Thumbnail] Generating: " << modelPath << " -> " << pngOutputPath << "\n";

    // 动画状态已下沉到每实体（ModelEntity::skeleton/animationClips），
    // 临时实体的加载不会污染主场景的其他实体，无需 save/restore。

    // 1. Load model temporarily
    vkDeviceWaitIdle(ctx_->getDevice());
    uint64_t eid = 0;
    SceneManager::ModelEntity* ent = nullptr;
    try {
        eid = sceneMgr_->createModelEntity(modelPath, glm::vec3(0.f), *bufMgr_, false);
        ent = sceneMgr_->getModelEntity(eid);
    } catch (const std::exception& ex) {
        std::cerr << "[Thumbnail] createModelEntity failed: " << ex.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "[Thumbnail] createModelEntity failed (unknown exception)\n";
        return false;
    }
    if (!ent || ent->indexCount == 0) {
        std::cerr << "[Thumbnail] Failed to load model: " << modelPath
                  << " (ent=" << (ent ? "non-null" : "null")
                  << ", indexCount=" << (ent ? ent->indexCount : 0) << ")\n";
        if (eid) sceneMgr_->removeModelEntity(eid, *ctx_);
        return false;
    }
    std::cout << "[Thumbnail] Loaded " << modelPath
              << " verts=" << ent->vertexBuffer
              << " idx=" << ent->indexBuffer
              << " indexCount=" << ent->indexCount
              << " subMeshes=" << ent->subMeshes.size() << "\n";

    // 2. Load materials for each subMesh (auto-generated .ast files from glTF)
    if (!ent->autoAstPaths.empty()) {
        if (ent->subMeshMaterials.size() < ent->autoAstPaths.size())
            ent->subMeshMaterials.resize(ent->autoAstPaths.size(), 0u);
        for (size_t slot = 0; slot < ent->autoAstPaths.size(); ++slot) {
            const MaterialId mid = matMgr_->loadMaterialFromAsset(
                ent->autoAstPaths[slot], *ctx_, *cmdMgr_, *bufMgr_, *fbMgr_, *pipeMgr_);
            if (mid != kInvalidMaterialId && slot < ent->subMeshMaterials.size()) {
                ent->subMeshMaterials[slot] = mid;
            }
        }
        if (!ent->subMeshMaterials.empty() && ent->subMeshMaterials[0] != 0u)
            ent->materialId = ent->subMeshMaterials[0];
    }

    // 3. Compute camera position (fit model in frame)
    // 等距视角：从中心偏移，沿 X/Y/Z 各斜 45° 方向观察
    glm::vec3 modelMin = sceneMgr_->getModelBoundsMin();
    glm::vec3 modelMax = sceneMgr_->getModelBoundsMax();
    glm::vec3 center = 0.5f * (modelMin + modelMax);
    float extent = glm::length(modelMax - modelMin);
    if (extent < 1e-5f) extent = 2.f;

    // 从 (1,1,1) 方向斜 45° 观察，保证模型最大维度刚好填满画面
    const float camDist = extent * 2.2f;
    const glm::vec3 camDir = glm::normalize(glm::vec3(1.f, 1.f, 1.f));
    const glm::vec3 camPos = center + camDir * camDist;

    glm::mat4 view = glm::lookAt(camPos, center, glm::vec3(0.f, 1.f, 0.f));
    float aspect = 1.0f; // square
    glm::mat4 proj = glm::perspective(glm::radians(45.f), aspect, 0.1f, 1000.f);
    proj[1][1] *= -1.f; // Vulkan Y-flip

    // 3. Update material UBO with this view/proj (slot 0, the default mesh material)
    matMgr_->updateAllUBOs(0, view, proj);

    // 4. Record command buffer
    vkDeviceWaitIdle(ctx_->getDevice());
    vkResetCommandBuffer(cmdBuf_, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf_, &bi);

    // Render pass 自动处理 layout 转换：UNDEFINED→COLOR_ATTACHMENT_OPTIMAL
    // 深度附件同理：UNDEFINED→DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    VkClearValue clears[2]{};
    clears[0].color = { 0.3f, 0.3f, 0.3f, 1.f };
    clears[1].depthStencil = { 1.f, 0 };

    VkRenderPassBeginInfo rpi{};
    rpi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass        = renderPass_;
    rpi.framebuffer       = framebuffer_;
    rpi.renderArea.extent = { kThumbSize, kThumbSize };
    rpi.clearValueCount   = 2;
    rpi.pClearValues      = clears;
    vkCmdBeginRenderPass(cmdBuf_, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport / scissors to match 128x128
    VkViewport vp{ 0.f, 0.f, (float)kThumbSize, (float)kThumbSize, 0.f, 1.f };
    VkRect2D sc{ { 0, 0 }, { kThumbSize, kThumbSize } };
    vkCmdSetViewport(cmdBuf_, 0, 1, &vp);
    vkCmdSetScissor(cmdBuf_, 0, 1, &sc);

    // Bind vertex / index buffer
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmdBuf_, 0, 1, &ent->vertexBuffer, &off);
    vkCmdBindIndexBuffer(cmdBuf_, ent->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Render with per-subMesh materials
    // For skinned models: force non-skinned pipeline (simple T-pose)
    const VkPipeline pipe = pipeline_;
    vkCmdBindPipeline(cmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

    // Push constants: model matrix at origin (we offset the camera instead)
    struct { glm::mat4 model; glm::mat4 normal; } pc;
    pc.model  = ent->transform.GetModelMatrix();
    pc.normal = ent->transform.GetNormalMatrix();

    if (ent->subMeshes.empty()) {
        MaterialId matId = ent->materialId;
        if (!matMgr_->isValid(matId)) matId = matMgr_->getDefaultMeshMaterialId();
        VkDescriptorSet ds = matMgr_->getDescriptorSet(matId, 0);
        if (ds != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout_, 0, 1, &ds, 0, nullptr);
        }
        vkCmdPushConstants(cmdBuf_, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cmdBuf_, ent->indexCount, 1, 0, 0, 0);
    } else {
        for (const auto& sm : ent->subMeshes) {
            MaterialId matId = ent->materialId;
            if (sm.materialSlot >= 0
                && sm.materialSlot < static_cast<int>(ent->subMeshMaterials.size())
                && ent->subMeshMaterials[sm.materialSlot] != 0u)
            {
                const MaterialId slotMat = static_cast<MaterialId>(ent->subMeshMaterials[sm.materialSlot]);
                if (matMgr_->isValid(slotMat))
                    matId = slotMat;
            }
            if (!matMgr_->isValid(matId)) matId = matMgr_->getDefaultMeshMaterialId();

            VkDescriptorSet ds = matMgr_->getDescriptorSet(matId, 0);
            if (ds != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmdBuf_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout_, 0, 1, &ds, 0, nullptr);
            }
            vkCmdPushConstants(cmdBuf_, pipelineLayout_,
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cmdBuf_, sm.indexCount, 1, sm.indexOffset, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmdBuf_);

    // 显式转换 COLOR_ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL 再拷贝
    {
        VkImageMemoryBarrier bar{};
        bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        bar.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image               = colorImage_;
        bar.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        bar.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        bar.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmdBuf_,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &bar);
    }

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset      = 0;
    copyRegion.bufferRowLength   = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.imageOffset       = { 0, 0, 0 };
    copyRegion.imageExtent       = { kThumbSize, kThumbSize, 1 };
    vkCmdCopyImageToBuffer(cmdBuf_, colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuf_, 1, &copyRegion);

    // 拷贝完成后转回 UNDEFINED：下一轮 render pass 的 implicit transition
    // UNDEFINED→COLOR_ATTACHMENT_OPTIMAL 会正确处理（LOAD_OP_CLEAR 不依赖旧内容）
    {
        VkImageMemoryBarrier bar{};
        bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        bar.newLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image               = colorImage_;
        bar.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        bar.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        bar.dstAccessMask       = 0;
        vkCmdPipelineBarrier(cmdBuf_,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &bar);
    }

    vkEndCommandBuffer(cmdBuf_);

    // 5. Submit and wait
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmdBuf_;
    vkQueueSubmit(ctx_->getGraphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkDeviceWaitIdle(ctx_->getDevice());

    // 6. Readback & BGRA→RGBA swap & save PNG
    void* mapped = nullptr;
    vkMapMemory(ctx_->getDevice(), readbackMem_, 0, VK_WHOLE_SIZE, 0, &mapped);

    // 颜色附件格式 B8G8R8A8_UNORM，读回数据为 BGRA；stbi_write_png 需 RGBA
    const size_t pixelCount = kThumbSize * kThumbSize;
    std::vector<uint8_t> rgba(pixelCount * 4);
    const uint8_t* src = static_cast<const uint8_t*>(mapped);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = src[i * 4 + 2]; // B → R
        rgba[i * 4 + 1] = src[i * 4 + 1]; // G → G
        rgba[i * 4 + 2] = src[i * 4 + 0]; // R → B
        rgba[i * 4 + 3] = src[i * 4 + 3]; // A → A
    }
    vkUnmapMemory(ctx_->getDevice(), readbackMem_);

    stbi_flip_vertically_on_write(1);
    const int result = stbi_write_png(pngOutputPath.c_str(), kThumbSize, kThumbSize, 4,
                                      rgba.data(), kThumbSize * 4);

    // 诊断：检查是否全是清除色（模型未被正确渲染）
    {
        uint8_t firstR = rgba[0], firstG = rgba[1], firstB = rgba[2];
        bool solid = true;
        for (size_t i = 0; i < pixelCount; ++i) {
            if (rgba[i*4] != firstR || rgba[i*4+1] != firstG || rgba[i*4+2] != firstB) {
                solid = false;
                break;
            }
        }
        if (solid) {
            std::cout << "[Thumbnail] WARNING: " << pngOutputPath
                      << " is solid color (" << (int)firstR << "," << (int)firstG << "," << (int)firstB << ")"
                      << " — model may not have rendered.\n";
        } else {
            std::cout << "[Thumbnail] OK: " << pngOutputPath << " has varied pixels.\n";
        }
    }

    // 7. Cleanup temp entity
    sceneMgr_->removeModelEntity(eid, *ctx_);

    if (result == 0) {
        std::cerr << "[Thumbnail] PNG write failed: " << pngOutputPath << "\n";
        return false;
    }
    return true;
}

bool ThumbnailRenderer::renderForAst(const std::string& astRelPath)
{
    const std::filesystem::path astFull = std::filesystem::path(resRoot_) / astRelPath;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(astFull, ec)) {
        std::cerr << "[Thumbnail] .ast file not found: " << astFull << "\n";
        return false;
    }

    // 解析 .ast JSON，提取 model 路径
    std::ifstream f(astFull);
    if (!f.is_open()) return false;
    std::string modelRelPath;
    try {
        nlohmann::json j;
        f >> j;
        // 只处理 Mesh 类型资产：Anim/Material 资产没有可渲染的模型
        std::string astType = j.value("type", std::string("Mesh"));
        if (astType != "Mesh") return false;
        if (j.contains("model")) {
            const auto& m = j["model"];
            if (m.is_string())
                modelRelPath = m.get<std::string>();
            else if (m.is_object() && m.contains("path"))
                modelRelPath = m["path"].get<std::string>();
        }
    } catch (...) {
        return false;
    }

    if (modelRelPath.empty()) {
        std::cerr << "[Thumbnail] No model path in .ast: " << astFull << "\n";
        return false;
    }

    const std::string modelFullPath = (std::filesystem::path(resRoot_) / modelRelPath).string();
    const std::string stem = astFull.stem().string();
    const std::string pngPath = (std::filesystem::path(resRoot_) / "thumbnails" / (stem + ".png")).string();

    return renderAndSave(modelFullPath, pngPath);
}

int ThumbnailRenderer::generateAll(const std::string& resRoot)
{
    const std::filesystem::path thumbDir  = std::filesystem::path(resRoot) / "thumbnails";
    std::error_code ec;

    std::filesystem::create_directories(thumbDir, ec);

    int generated = 0;

    // 阶段1：扫描原始模型目录（旧路径兼容）
    const std::filesystem::path modelRoots[] = {
        std::filesystem::path(resRoot) / "bin" / "mesh",
        std::filesystem::path(resRoot) / "models",
    };
    for (const auto& root : modelRoots) {
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".obj" && ext != ".glb" && ext != ".gltf" && ext != ".fbx") continue;

            const std::string stem = entry.path().stem().string();
            const std::string pngPath = (thumbDir / (stem + ".png")).string();
            if (std::filesystem::exists(pngPath)) continue;

            if (renderAndSave(entry.path().string(), pngPath))
                ++generated;
        }
    }

    // 阶段2：扫描 res/content/ 下的 .ast 文件，通过 .ast 中的 model 路径生成缩略图
    const std::filesystem::path contentRoot = std::filesystem::path(resRoot) / "content";
    if (std::filesystem::is_directory(contentRoot, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(contentRoot, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".ast") continue;

            const std::string stem = entry.path().stem().string();
            const std::string pngPath = (thumbDir / (stem + ".png")).string();
            if (std::filesystem::exists(pngPath) && std::filesystem::file_size(pngPath, ec) > 100) continue;

            const std::string relAst = std::filesystem::relative(entry.path(), std::filesystem::path(resRoot), ec).generic_string();
            if (ec) continue;

            if (renderForAst(relAst))
                ++generated;
        }
    }

    std::cout << "[Thumbnail] Generated " << generated << " thumbnails.\n";
    return generated;
}
