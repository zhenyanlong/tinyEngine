#include "MaterialManager.hpp"
#include "MaterialAssetLoader.hpp"
#include "VulkanTypes.hpp"
// STB_IMAGE_IMPLEMENTATION defined globally; undefine here to avoid
// duplicate symbol errors with TextureManager.obj
#undef STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <array>
#include <algorithm>

// ── Static helpers ─────────────────────────────────────────────────────────────

void MaterialManager::transitionLayout(const VulkanContext& ctx, const CommandManager& cmdMgr,
                                       VkImage image, VkImageLayout from, VkImageLayout to)
{
    VkCommandBuffer cb = cmdMgr.beginSingleTimeCommands(ctx);

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = from;
    barrier.newLayout           = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkPipelineStageFlags srcStage{}, dstStage{};
    if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    cmdMgr.endSingleTimeCommands(ctx, cb);
}

void MaterialManager::copyBufToImage(const VulkanContext& ctx, const CommandManager& cmdMgr,
                                     VkBuffer buf, VkImage img, uint32_t w, uint32_t h)
{
    VkCommandBuffer cb = cmdMgr.beginSingleTimeCommands(ctx);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { w, h, 1 };
    vkCmdCopyBufferToImage(cb, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    cmdMgr.endSingleTimeCommands(ctx, cb);
}

void MaterialManager::createSampler(const VulkanContext& ctx, VkSampler& sampler)
{
    VkSamplerCreateInfo si{};
    si.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter        = VK_FILTER_LINEAR;
    si.minFilter        = VK_FILTER_LINEAR;
    si.addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = VK_TRUE;
    si.maxAnisotropy    = ctx.getMaxAnisotropy();
    si.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    si.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(ctx.getDevice(), &si, nullptr, &sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create material sampler!");
}

void MaterialManager::loadTexture(TextureGPU& tex, const std::string& path,
                                  const VulkanContext& ctx, const CommandManager& cmdMgr,
                                  const BufferManager& bufMgr, const FramebufferManager& fbMgr)
{
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!pixels) throw std::runtime_error("Failed to load texture: " + path);

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w * h * 4);

    VkBuffer staging{}; VkDeviceMemory stagingMem{};
    bufMgr.createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        staging, stagingMem);

    void* data = nullptr;
    vkMapMemory(ctx.getDevice(), stagingMem, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(ctx.getDevice(), stagingMem);
    stbi_image_free(pixels);

    fbMgr.createImage(ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
        VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tex.image, tex.memory);

    transitionLayout(ctx, cmdMgr, tex.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufToImage(ctx, cmdMgr, staging, tex.image,
        static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    transitionLayout(ctx, cmdMgr, tex.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(ctx.getDevice(), staging, nullptr);
    vkFreeMemory(ctx.getDevice(), stagingMem, nullptr);

    tex.view = fbMgr.createImageView(ctx, tex.image, VK_FORMAT_R8G8B8A8_SRGB,
                                     VK_IMAGE_ASPECT_COLOR_BIT);
    createSampler(ctx, tex.sampler);
    tex.path      = path;
    tex.isDefault = false;
}

void MaterialManager::destroyTexture(TextureGPU& tex, const VulkanContext& ctx)
{
    auto dev = ctx.getDevice();
    if (tex.sampler != VK_NULL_HANDLE) { vkDestroySampler(dev, tex.sampler, nullptr);   tex.sampler = VK_NULL_HANDLE; }
    if (tex.view    != VK_NULL_HANDLE) { vkDestroyImageView(dev, tex.view, nullptr);    tex.view    = VK_NULL_HANDLE; }
    if (tex.image   != VK_NULL_HANDLE) { vkDestroyImage(dev, tex.image, nullptr);       tex.image   = VK_NULL_HANDLE; }
    if (tex.memory  != VK_NULL_HANDLE) { vkFreeMemory(dev, tex.memory, nullptr);        tex.memory  = VK_NULL_HANDLE; }
    tex.path.clear();
    tex.isDefault = true;
}

void MaterialManager::uploadTexture1x1(TextureGPU& tex,
                                       uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                       const VulkanContext& ctx, const CommandManager& cmdMgr,
                                       const FramebufferManager& fbMgr)
{
    const uint8_t pixels[4] = { r, g, b, a };

    VkBuffer staging{}; VkDeviceMemory stagingMem{};
    {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = 4;
        bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(ctx.getDevice(), &bi, nullptr, &staging);

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(ctx.getDevice(), staging, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = ctx.findMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(ctx.getDevice(), &ai, nullptr, &stagingMem);
        vkBindBufferMemory(ctx.getDevice(), staging, stagingMem, 0);
    }

    void* data = nullptr;
    vkMapMemory(ctx.getDevice(), stagingMem, 0, 4, 0, &data);
    memcpy(data, pixels, 4);
    vkUnmapMemory(ctx.getDevice(), stagingMem);

    fbMgr.createImage(ctx, 1, 1, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tex.image, tex.memory);

    transitionLayout(ctx, cmdMgr, tex.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufToImage(ctx, cmdMgr, staging, tex.image, 1, 1);
    transitionLayout(ctx, cmdMgr, tex.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(ctx.getDevice(), staging, nullptr);
    vkFreeMemory(ctx.getDevice(), stagingMem, nullptr);

    tex.view = fbMgr.createImageView(ctx, tex.image, VK_FORMAT_R8G8B8A8_SRGB,
                                     VK_IMAGE_ASPECT_COLOR_BIT);
    createSampler(ctx, tex.sampler);
    tex.isDefault = true;
}

// ── Default textures ───────────────────────────────────────────────────────────

void MaterialManager::createDefaultTextures(const VulkanContext& ctx, const CommandManager& cmdMgr,
                                            const FramebufferManager& fbMgr)
{
    uploadTexture1x1(defaultAlbedo_,   255, 255, 255, 255, ctx, cmdMgr, fbMgr);
    uploadTexture1x1(defaultNormal_,   128, 128, 255, 255, ctx, cmdMgr, fbMgr);
    // 缺少 MR 贴图时，g/b 均使用乘法单位值 1，确保 UBO 中的
    // roughness/metallic 参数直接生效，而不是被 fallback 纹理覆盖。
    uploadTexture1x1(defaultMR_,         0, 255, 255, 255, ctx, cmdMgr, fbMgr);
    uploadTexture1x1(defaultAO_,       255, 255, 255, 255, ctx, cmdMgr, fbMgr);
    // 缺少 emissive 贴图时采样白色，让 emissiveColor * emissiveIntensity
    // 成为最终自发光颜色；黑色 fallback 会把参数结果完全乘没。
    uploadTexture1x1(defaultEmissive_, 255, 255, 255, 255, ctx, cmdMgr, fbMgr);
}

void MaterialManager::destroyDefaultTextures(const VulkanContext& ctx)
{
    destroyTexture(defaultAlbedo_,   ctx);
    destroyTexture(defaultNormal_,   ctx);
    destroyTexture(defaultMR_,       ctx);
    destroyTexture(defaultAO_,       ctx);
    destroyTexture(defaultEmissive_, ctx);
}

// ── Descriptor pool ────────────────────────────────────────────────────────────

void MaterialManager::createPool(const VulkanContext& ctx)
{
    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 2000;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = 1000;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.poolSizeCount = static_cast<uint32_t>(sizes.size());
    ci.pPoolSizes    = sizes.data();
    ci.maxSets       = 1000;
    if (vkCreateDescriptorPool(ctx.getDevice(), &ci, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create MaterialManager descriptor pool!");
}

// ── Lifecycle ──────────────────────────────────────────────────────────────────

void MaterialManager::init(const VulkanContext& ctx, const CommandManager& cmdMgr,
                           const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                           const PipelineManager& pipeMgr, uint32_t imageCount,
                           const std::string& defaultTexturePath)
{
    imageCount_ = imageCount;
    createDefaultTextures(ctx, cmdMgr, fbMgr);
    createPool(ctx);

    defaultMeshId_ = createMeshMaterial("Default Mesh",
                                        defaultTexturePath, "",
                                        MaterialParams{},
                                        ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
    materials_.at(defaultMeshId_).deletable = false;

    defaultBoxId_ = createBoxMaterial("Default Box", MaterialParams{}, ctx, bufMgr, pipeMgr);
    materials_.at(defaultBoxId_).deletable = false;
}

void MaterialManager::onSwapchainRecreate(const VulkanContext& ctx, const CommandManager& cmdMgr,
                                          const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                                          const PipelineManager& pipeMgr, uint32_t imageCount)
{
    imageCount_ = imageCount;
    vkDeviceWaitIdle(ctx.getDevice());

    for (auto& [id, e] : materials_) {
        destroyUBOs(e, ctx);
        destroyPipUBOs(e, ctx);
        e.descSets.clear();
        e.pipDescSets.clear();
    }

    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx.getDevice(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }

    createPool(ctx);

    for (auto& [id, e] : materials_) {
        createUBOs(e, ctx, bufMgr);
        allocateDescSets(e, ctx, pipeMgr);
        writeDescSets(e, ctx, pipeMgr);
        createPipResources(e, ctx, bufMgr, pipeMgr);
    }
}

void MaterialManager::destroy(const VulkanContext& ctx)
{
    for (auto& [id, e] : materials_)
        destroyEntry(e, ctx);
    materials_.clear();
    allIds_.clear();
    assetCache_.clear();
    skinnedCache_.clear();

    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(ctx.getDevice(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }

    destroyDefaultTextures(ctx);
}

// ── ID allocation ──────────────────────────────────────────────────────────────

MaterialId MaterialManager::allocateId()
{
    MaterialId id = nextId_++;
    allIds_.push_back(id);
    return id;
}

// ── UBO management ─────────────────────────────────────────────────────────────

void MaterialManager::createUBOs(MaterialEntry& e, const VulkanContext& ctx,
                                  const BufferManager& bufMgr)
{
    e.ubos.resize(imageCount_);
    e.uboMemory.resize(imageCount_);
    e.uboMapped.resize(imageCount_, nullptr);
    if (e.skinned) {
        e.boneUbos.resize(imageCount_);
        e.boneUboMemory.resize(imageCount_);
        e.boneUboMapped.resize(imageCount_, nullptr);
    }

    for (uint32_t i = 0; i < imageCount_; ++i) {
        bufMgr.createBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            e.ubos[i], e.uboMemory[i]);
        vkMapMemory(ctx.getDevice(), e.uboMemory[i], 0, sizeof(UniformBufferObject), 0,
                    &e.uboMapped[i]);
        if (e.skinned) {
            bufMgr.createBuffer(sizeof(BoneMatricesUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                e.boneUbos[i], e.boneUboMemory[i]);
            vkMapMemory(ctx.getDevice(), e.boneUboMemory[i], 0, sizeof(BoneMatricesUBO), 0,
                        &e.boneUboMapped[i]);
            BoneMatricesUBO identity{};
            for (int b = 0; b < kMaxBones; ++b)
                identity.bones[b] = glm::mat4(1.f);
            memcpy(e.boneUboMapped[i], &identity, sizeof(identity));
        }
    }
}

void MaterialManager::destroyUBOs(MaterialEntry& e, const VulkanContext& ctx)
{
    auto dev = ctx.getDevice();
    for (uint32_t i = 0; i < static_cast<uint32_t>(e.ubos.size()); ++i) {
        if (e.uboMapped[i])           { vkUnmapMemory(dev, e.uboMemory[i]);          e.uboMapped[i] = nullptr;          }
        if (e.ubos[i]      != VK_NULL_HANDLE) { vkDestroyBuffer(dev, e.ubos[i], nullptr);      e.ubos[i]      = VK_NULL_HANDLE; }
        if (e.uboMemory[i] != VK_NULL_HANDLE) { vkFreeMemory(dev, e.uboMemory[i], nullptr);    e.uboMemory[i] = VK_NULL_HANDLE; }
    }
    e.ubos.clear(); e.uboMemory.clear(); e.uboMapped.clear();
    for (uint32_t i = 0; i < static_cast<uint32_t>(e.boneUbos.size()); ++i) {
        if (e.boneUboMapped[i])               { vkUnmapMemory(dev, e.boneUboMemory[i]);              e.boneUboMapped[i] = nullptr; }
        if (e.boneUbos[i]      != VK_NULL_HANDLE) { vkDestroyBuffer(dev, e.boneUbos[i], nullptr);        e.boneUbos[i]      = VK_NULL_HANDLE; }
        if (e.boneUboMemory[i] != VK_NULL_HANDLE) { vkFreeMemory(dev, e.boneUboMemory[i], nullptr);      e.boneUboMemory[i] = VK_NULL_HANDLE; }
    }
    e.boneUbos.clear(); e.boneUboMemory.clear(); e.boneUboMapped.clear();
}

// ── Descriptor set management ──────────────────────────────────────────────────

void MaterialManager::allocateDescSets(MaterialEntry& e, const VulkanContext& ctx,
                                       const PipelineManager& pipeMgr)
{
    VkDescriptorSetLayout layout = e.skinned
        ? pipeMgr.getSkinnedDescSetLayout()
        : ((e.type == MaterialType::Mesh)
            ? pipeMgr.getMainDescSetLayout()
            : pipeMgr.getBoxDescSetLayout());

    std::vector<VkDescriptorSetLayout> layouts(imageCount_, layout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool_;
    ai.descriptorSetCount = imageCount_;
    ai.pSetLayouts        = layouts.data();

    e.descSets.resize(imageCount_);
    if (vkAllocateDescriptorSets(ctx.getDevice(), &ai, e.descSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate material descriptor sets!");
}

void MaterialManager::writeDescSets(MaterialEntry& e, const VulkanContext& ctx,
                                    const PipelineManager& /*pipeMgr*/)
{
    for (uint32_t i = 0; i < imageCount_; ++i) {
        VkDescriptorBufferInfo bi{};
        bi.buffer = e.ubos[i]; bi.offset = 0; bi.range = sizeof(UniformBufferObject);

        if (e.type == MaterialType::Mesh) {
            // Resolve every sampler binding to either the user-supplied texture
            // or the corresponding 1x1 default. The frag shader expects all 5
            // samplers to be bound regardless of which the asset actually uses.
            auto pick = [](const TextureGPU& t, const TextureGPU& fb) -> VkDescriptorImageInfo {
                VkDescriptorImageInfo di{};
                di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                di.imageView   = (t.view    != VK_NULL_HANDLE) ? t.view    : fb.view;
                di.sampler     = (t.sampler != VK_NULL_HANDLE) ? t.sampler : fb.sampler;
                return di;
            };
            const VkDescriptorImageInfo albedoI = pick(e.albedo,            defaultAlbedo_);
            const VkDescriptorImageInfo normalI = pick(e.normal,            defaultNormal_);
            const VkDescriptorImageInfo mrI     = pick(e.metallicRoughness, defaultMR_);
            const VkDescriptorImageInfo aoI     = pick(e.ao,                defaultAO_);
            const VkDescriptorImageInfo emI     = pick(e.emissive,          defaultEmissive_);

            std::array<VkWriteDescriptorSet, 7> writes{};
            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = e.descSets[i];
            writes[0].dstBinding      = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].pBufferInfo     = &bi;

            auto fillTex = [&](size_t idx, uint32_t binding, const VkDescriptorImageInfo* info) {
                writes[idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[idx].dstSet          = e.descSets[i];
                writes[idx].dstBinding      = binding;
                writes[idx].descriptorCount = 1;
                writes[idx].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[idx].pImageInfo      = info;
            };
            fillTex(1, 1, &albedoI);
            fillTex(2, 2, &normalI);
            fillTex(3, 3, &mrI);
            fillTex(4, 4, &aoI);
            fillTex(5, 5, &emI);

            uint32_t writeCount = 6;
            VkDescriptorBufferInfo boneBI{};
            if (e.skinned && i < e.boneUbos.size()) {
                boneBI.buffer = e.boneUbos[i];
                boneBI.offset = 0;
                boneBI.range = sizeof(BoneMatricesUBO);
                writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[6].dstSet          = e.descSets[i];
                writes[6].dstBinding      = 6;
                writes[6].descriptorCount = 1;
                writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[6].pBufferInfo     = &boneBI;
                writeCount = 7;
            }

            vkUpdateDescriptorSets(ctx.getDevice(), writeCount, writes.data(), 0, nullptr);
        } else {
            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = e.descSets[i];
            w.dstBinding      = 0;
            w.descriptorCount = 1;
            w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo     = &bi;
            vkUpdateDescriptorSets(ctx.getDevice(), 1, &w, 0, nullptr);
        }
    }
}

void MaterialManager::createPipResources(MaterialEntry& e, const VulkanContext& ctx,
                                          const BufferManager& bufMgr, const PipelineManager& pipeMgr)
{
    e.pipUbos.resize(imageCount_);
    e.pipUboMemory.resize(imageCount_);
    e.pipUboMapped.resize(imageCount_, nullptr);

    for (uint32_t i = 0; i < imageCount_; ++i) {
        bufMgr.createBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            e.pipUbos[i], e.pipUboMemory[i]);
        vkMapMemory(ctx.getDevice(), e.pipUboMemory[i], 0, sizeof(UniformBufferObject), 0,
                    &e.pipUboMapped[i]);
    }

    VkDescriptorSetLayout layout = e.skinned
        ? pipeMgr.getSkinnedDescSetLayout()
        : ((e.type == MaterialType::Mesh)
            ? pipeMgr.getMainDescSetLayout()
            : pipeMgr.getBoxDescSetLayout());

    std::vector<VkDescriptorSetLayout> layouts(imageCount_, layout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool_;
    ai.descriptorSetCount = imageCount_;
    ai.pSetLayouts        = layouts.data();

    e.pipDescSets.resize(imageCount_);
    if (vkAllocateDescriptorSets(ctx.getDevice(), &ai, e.pipDescSets.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate PiP material descriptor sets!");

    writePipDescSets(e, ctx);
}

void MaterialManager::writePipDescSets(MaterialEntry& e, const VulkanContext& ctx)
{
    for (uint32_t i = 0; i < imageCount_; ++i) {
        VkDescriptorBufferInfo bi{};
        bi.buffer = e.pipUbos[i]; bi.offset = 0; bi.range = sizeof(UniformBufferObject);

        if (e.type == MaterialType::Mesh) {
            auto pick = [](const TextureGPU& t, const TextureGPU& fb) -> VkDescriptorImageInfo {
                VkDescriptorImageInfo di{};
                di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                di.imageView   = (t.view    != VK_NULL_HANDLE) ? t.view    : fb.view;
                di.sampler     = (t.sampler != VK_NULL_HANDLE) ? t.sampler : fb.sampler;
                return di;
            };
            const VkDescriptorImageInfo albedoI = pick(e.albedo,            defaultAlbedo_);
            const VkDescriptorImageInfo normalI = pick(e.normal,            defaultNormal_);
            const VkDescriptorImageInfo mrI     = pick(e.metallicRoughness, defaultMR_);
            const VkDescriptorImageInfo aoI     = pick(e.ao,                defaultAO_);
            const VkDescriptorImageInfo emI     = pick(e.emissive,          defaultEmissive_);

            std::array<VkWriteDescriptorSet, 7> writes{};
            writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = e.pipDescSets[i];
            writes[0].dstBinding      = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].pBufferInfo     = &bi;

            auto fillTex = [&](size_t idx, uint32_t binding, const VkDescriptorImageInfo* info) {
                writes[idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[idx].dstSet          = e.pipDescSets[i];
                writes[idx].dstBinding      = binding;
                writes[idx].descriptorCount = 1;
                writes[idx].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[idx].pImageInfo      = info;
            };
            fillTex(1, 1, &albedoI);
            fillTex(2, 2, &normalI);
            fillTex(3, 3, &mrI);
            fillTex(4, 4, &aoI);
            fillTex(5, 5, &emI);

            uint32_t writeCount = 6;
            VkDescriptorBufferInfo boneBI{};
            if (e.skinned && i < e.boneUbos.size()) {
                boneBI.buffer = e.boneUbos[i];
                boneBI.offset = 0;
                boneBI.range = sizeof(BoneMatricesUBO);
                writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[6].dstSet          = e.pipDescSets[i];
                writes[6].dstBinding      = 6;
                writes[6].descriptorCount = 1;
                writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[6].pBufferInfo     = &boneBI;
                writeCount = 7;
            }

            vkUpdateDescriptorSets(ctx.getDevice(), writeCount, writes.data(), 0, nullptr);
        } else {
            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = e.pipDescSets[i];
            w.dstBinding      = 0;
            w.descriptorCount = 1;
            w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo     = &bi;
            vkUpdateDescriptorSets(ctx.getDevice(), 1, &w, 0, nullptr);
        }
    }
}

void MaterialManager::destroyPipUBOs(MaterialEntry& e, const VulkanContext& ctx)
{
    auto dev = ctx.getDevice();
    for (uint32_t i = 0; i < static_cast<uint32_t>(e.pipUbos.size()); ++i) {
        if (e.pipUboMapped[i])               { vkUnmapMemory(dev, e.pipUboMemory[i]);        e.pipUboMapped[i] = nullptr; }
        if (e.pipUbos[i]      != VK_NULL_HANDLE) { vkDestroyBuffer(dev, e.pipUbos[i], nullptr);    e.pipUbos[i]      = VK_NULL_HANDLE; }
        if (e.pipUboMemory[i] != VK_NULL_HANDLE) { vkFreeMemory(dev, e.pipUboMemory[i], nullptr);  e.pipUboMemory[i] = VK_NULL_HANDLE; }
    }
    e.pipUbos.clear(); e.pipUboMemory.clear(); e.pipUboMapped.clear();
}

void MaterialManager::destroyEntry(MaterialEntry& e, const VulkanContext& ctx)
{
    destroyUBOs(e, ctx);
    e.descSets.clear();
    if (!e.albedo.isDefault)            destroyTexture(e.albedo,            ctx);
    if (!e.normal.isDefault)            destroyTexture(e.normal,            ctx);
    if (!e.metallicRoughness.isDefault) destroyTexture(e.metallicRoughness, ctx);
    if (!e.ao.isDefault)                destroyTexture(e.ao,                ctx);
    if (!e.emissive.isDefault)          destroyTexture(e.emissive,          ctx);
}

// ── Material creation ──────────────────────────────────────────────────────────

MaterialId MaterialManager::createMeshMaterial(const std::string& name,
                                               const std::string& albedoPath,
                                               const std::string& normalPath,
                                               const std::string& metallicRoughnessPath,
                                               const std::string& aoPath,
                                               const std::string& emissivePath,
                                               const MaterialParams& params,
                                               const VulkanContext& ctx,
                                               const CommandManager& cmdMgr,
                                               const BufferManager& bufMgr,
                                               const FramebufferManager& fbMgr,
                                               const PipelineManager& pipeMgr)
{
    MaterialId id      = allocateId();
    MaterialEntry& e   = materials_[id];
    e.name             = name;
    e.type             = MaterialType::Mesh;
    e.params           = params;
    e.deletable        = true;

    auto bind = [&](TextureGPU& slot, const std::string& path, const TextureGPU& fallback) {
        if (!path.empty()) {
            try {
                loadTexture(slot, path, ctx, cmdMgr, bufMgr, fbMgr);
                return;
            } catch (const std::exception& ex) {
                std::cerr << "[MaterialManager] texture load failed (" << path
                          << "): " << ex.what() << " — falling back to default\n";
            }
        }
        slot.view      = fallback.view;
        slot.sampler   = fallback.sampler;
        slot.isDefault = true;
        slot.path.clear();
    };
    bind(e.albedo,            albedoPath,            defaultAlbedo_);
    bind(e.normal,            normalPath,            defaultNormal_);
    bind(e.metallicRoughness, metallicRoughnessPath, defaultMR_);
    bind(e.ao,                aoPath,                defaultAO_);
    bind(e.emissive,          emissivePath,          defaultEmissive_);

    createUBOs(e, ctx, bufMgr);
    allocateDescSets(e, ctx, pipeMgr);
    writeDescSets(e, ctx, pipeMgr);
    createPipResources(e, ctx, bufMgr, pipeMgr);
    return id;
}

MaterialId MaterialManager::createBoxMaterial(const std::string& name,
                                              const MaterialParams& params,
                                              const VulkanContext& ctx,
                                              const BufferManager& bufMgr,
                                              const PipelineManager& pipeMgr)
{
    MaterialId id    = allocateId();
    MaterialEntry& e = materials_[id];
    e.name           = name;
    e.type           = MaterialType::Box;
    e.params         = params;
    e.deletable      = true;

    createUBOs(e, ctx, bufMgr);
    allocateDescSets(e, ctx, pipeMgr);
    writeDescSets(e, ctx, pipeMgr);
    createPipResources(e, ctx, bufMgr, pipeMgr);
    return id;
}

MaterialId MaterialManager::cloneMaterial(MaterialId src,
                                          const VulkanContext& ctx,
                                          const CommandManager& cmdMgr,
                                          const BufferManager& bufMgr,
                                          const FramebufferManager& fbMgr,
                                          const PipelineManager& pipeMgr)
{
    if (materials_.find(src) == materials_.end())
        throw std::runtime_error("cloneMaterial: invalid source ID");

    const MaterialEntry& s = materials_.at(src);
    if (s.type == MaterialType::Mesh || s.type == MaterialType::Material)
        return createMeshMaterial(s.name + " (copy)",
                                  s.albedo.path,            s.normal.path,
                                  s.metallicRoughness.path, s.ao.path, s.emissive.path,
                                  s.params, ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
    else
        return createBoxMaterial(s.name + " (copy)", s.params, ctx, bufMgr, pipeMgr);
}

MaterialId MaterialManager::createSkinnedMaterialFrom(MaterialId src,
                                                      const VulkanContext& ctx,
                                                      const CommandManager& cmdMgr,
                                                      const BufferManager& bufMgr,
                                                      const FramebufferManager& fbMgr,
                                                      const PipelineManager& pipeMgr)
{
    auto cached = skinnedCache_.find(src);
    if (cached != skinnedCache_.end() && isValid(cached->second))
        return cached->second;

    auto srcIt = materials_.find(src);
    if (srcIt == materials_.end())
        return kInvalidMaterialId;

    const MaterialEntry& s = srcIt->second;
    if (s.type != MaterialType::Mesh && s.type != MaterialType::Material)
        return kInvalidMaterialId;
    if (s.skinned)
        return src;

    MaterialId id = allocateId();
    MaterialEntry& e = materials_[id];
    e.name = s.name + " (skinned)";
    e.type = MaterialType::Mesh;
    e.params = s.params;
    e.deletable = true;
    e.skinned = true;
    e.vertSpvPath = s.vertSpvPath;
    e.fragSpvPath = s.fragSpvPath;

    auto bind = [&](TextureGPU& slot, const std::string& path, const TextureGPU& fallback) {
        if (!path.empty()) {
            try {
                loadTexture(slot, path, ctx, cmdMgr, bufMgr, fbMgr);
                return;
            } catch (const std::exception& ex) {
                std::cerr << "[MaterialManager] skinned texture load failed (" << path
                          << "): " << ex.what() << " - falling back to default\n";
            }
        }
        slot.view = fallback.view;
        slot.sampler = fallback.sampler;
        slot.isDefault = true;
        slot.path.clear();
    };

    bind(e.albedo,            s.albedo.path,            defaultAlbedo_);
    bind(e.normal,            s.normal.path,            defaultNormal_);
    bind(e.metallicRoughness, s.metallicRoughness.path, defaultMR_);
    bind(e.ao,                s.ao.path,                defaultAO_);
    bind(e.emissive,          s.emissive.path,          defaultEmissive_);

    createUBOs(e, ctx, bufMgr);
    allocateDescSets(e, ctx, pipeMgr);
    writeDescSets(e, ctx, pipeMgr);
    // 蒙皮材质同样需要 PiP 专用 descriptor set（binding 0 指向 PiP UBO、binding 6 指向 bone UBO），
    // 否则 getPipDescriptorSet 会回退到主 descriptor set，导致 PiP 中蒙皮模型用主视角渲染。
    createPipResources(e, ctx, bufMgr, pipeMgr);

    skinnedCache_[src] = id;
    return id;
}

void MaterialManager::destroyMaterial(MaterialId id, const VulkanContext& ctx)
{
    auto it = materials_.find(id);
    if (it == materials_.end() || !it->second.deletable) return;
    destroyEntry(it->second, ctx);
    materials_.erase(it);
    allIds_.erase(std::remove(allIds_.begin(), allIds_.end(), id), allIds_.end());

    // 从资产缓存中移除失效条目
    for (auto ci = assetCache_.begin(); ci != assetCache_.end(); ) {
        if (ci->second == id) ci = assetCache_.erase(ci);
        else ++ci;
    }
    for (auto ci = skinnedCache_.begin(); ci != skinnedCache_.end(); ) {
        if (ci->first == id || ci->second == id) ci = skinnedCache_.erase(ci);
        else ++ci;
    }
}

// ── Parameter accessors ────────────────────────────────────────────────────────

void MaterialManager::setParams(MaterialId id, const MaterialParams& params)
{
    materials_.at(id).params = params;
}

const MaterialParams& MaterialManager::getParams(MaterialId id) const
{
    return materials_.at(id).params;
}

MaterialParams& MaterialManager::getParamsMut(MaterialId id)
{
    return materials_.at(id).params;
}

// ── Texture hot-replace ────────────────────────────────────────────────────────

bool MaterialManager::setAlbedoPath(MaterialId id, const std::string& path,
                                    const VulkanContext& ctx, const CommandManager& cmdMgr,
                                    const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                                    const PipelineManager& pipeMgr, std::string* error)
{
    MaterialEntry& e = materials_.at(id);
    vkDeviceWaitIdle(ctx.getDevice());

    if (!path.empty()) {
        TextureGPU replacement;
        try {
            loadTexture(replacement, path, ctx, cmdMgr, bufMgr, fbMgr);
        } catch (const std::exception& ex) {
            destroyTexture(replacement, ctx);
            if (error) *error = ex.what();
            std::cerr << "[MaterialManager] " << ex.what() << "\n";
            return false;
        }
        if (!e.albedo.isDefault) destroyTexture(e.albedo, ctx);
        e.albedo = std::move(replacement);
    } else {
        if (!e.albedo.isDefault) destroyTexture(e.albedo, ctx);
        e.albedo.view      = defaultAlbedo_.view;
        e.albedo.sampler   = defaultAlbedo_.sampler;
        e.albedo.isDefault = true;
        e.albedo.path.clear();
    }
    writeDescSets(e, ctx, pipeMgr);
    return true;
}

bool MaterialManager::setNormalPath(MaterialId id, const std::string& path,
                                    const VulkanContext& ctx, const CommandManager& cmdMgr,
                                    const BufferManager& bufMgr, const FramebufferManager& fbMgr,
                                    const PipelineManager& pipeMgr, std::string* error)
{
    MaterialEntry& e = materials_.at(id);
    vkDeviceWaitIdle(ctx.getDevice());

    if (!path.empty()) {
        TextureGPU replacement;
        try {
            loadTexture(replacement, path, ctx, cmdMgr, bufMgr, fbMgr);
        } catch (const std::exception& ex) {
            destroyTexture(replacement, ctx);
            if (error) *error = ex.what();
            std::cerr << "[MaterialManager] " << ex.what() << "\n";
            return false;
        }
        if (!e.normal.isDefault) destroyTexture(e.normal, ctx);
        e.normal = std::move(replacement);
    } else {
        if (!e.normal.isDefault) destroyTexture(e.normal, ctx);
        e.normal.view      = defaultNormal_.view;
        e.normal.sampler   = defaultNormal_.sampler;
        e.normal.isDefault = true;
        e.normal.path.clear();
    }
    writeDescSets(e, ctx, pipeMgr);
    return true;
}

// ── Per-frame UBO update ───────────────────────────────────────────────────────

void MaterialManager::updateAllUBOs(uint32_t imageIndex,
                                    const glm::mat4& view, const glm::mat4& proj)
{
    // Pre-compute shared matrices once per frame.
    const glm::mat4 viewProj = proj * view;
    const glm::mat4 invView  = glm::inverse(view);
    const glm::mat4 invProj  = glm::inverse(proj);
    // Camera world position is the translation column of the Camera-to-World matrix (= inverse view).
    const glm::vec3 camPos   = glm::vec3(invView[3]);

    // Hard-coded directional sun for now; later this becomes a Scene/Light API.
    const glm::vec3 sunDir   = glm::normalize(glm::vec3(0.4f, 0.8f, 0.5f));
    const glm::vec3 sunColor = glm::vec3(3.0f, 2.95f, 2.85f); // mild warm white
    const float     ambient  = 0.18f;

    for (auto& [id, e] : materials_) {
        UniformBufferObject ubo{};
        ubo.view            = view;
        ubo.proj            = proj;
        ubo.materialTint    = e.params.baseColor;
        ubo.boxMaterialTint = e.params.baseColor;
        ubo.emissive        = glm::vec4(glm::vec3(e.params.emissiveColor) * e.params.emissiveIntensity,
                                        e.params.emissiveIntensity);
        ubo.cameraPos       = glm::vec4(camPos, 1.0f);
        ubo.lightDir        = glm::vec4(sunDir, 0.0f);
        ubo.lightColor      = glm::vec4(sunColor, ambient);
        ubo.pbrFactors      = glm::vec4(e.params.metallic, e.params.roughness, 1.0f, 1.0f);
        ubo.viewProj        = viewProj;
        ubo.invView         = invView;
        ubo.invProj         = invProj;
        memcpy(e.uboMapped[imageIndex], &ubo, sizeof(ubo));
    }
}

void MaterialManager::updateAllPipUBOs(uint32_t imageIndex,
                                        const glm::mat4& view, const glm::mat4& proj)
{
    const glm::mat4 viewProj = proj * view;
    const glm::mat4 invView  = glm::inverse(view);
    const glm::mat4 invProj  = glm::inverse(proj);
    const glm::vec3 camPos   = glm::vec3(invView[3]);

    const glm::vec3 sunDir   = glm::normalize(glm::vec3(0.4f, 0.8f, 0.5f));
    const glm::vec3 sunColor = glm::vec3(3.0f, 2.95f, 2.85f);
    const float     ambient  = 0.18f;

    for (auto& [id, e] : materials_) {
        if (imageIndex >= e.pipUboMapped.size() || !e.pipUboMapped[imageIndex])
            continue;
        UniformBufferObject ubo{};
        ubo.view            = view;
        ubo.proj            = proj;
        ubo.materialTint    = e.params.baseColor;
        ubo.boxMaterialTint = e.params.baseColor;
        ubo.emissive        = glm::vec4(glm::vec3(e.params.emissiveColor) * e.params.emissiveIntensity,
                                        e.params.emissiveIntensity);
        ubo.cameraPos       = glm::vec4(camPos, 1.0f);
        ubo.lightDir        = glm::vec4(sunDir, 0.0f);
        ubo.lightColor      = glm::vec4(sunColor, ambient);
        ubo.pbrFactors      = glm::vec4(e.params.metallic, e.params.roughness, 1.0f, 1.0f);
        ubo.viewProj        = viewProj;
        ubo.invView         = invView;
        ubo.invProj         = invProj;
        memcpy(e.pipUboMapped[imageIndex], &ubo, sizeof(ubo));
    }
}

// ── Descriptor set access ──────────────────────────────────────────────────────

VkDescriptorSet MaterialManager::getDescriptorSet(MaterialId id, uint32_t imageIndex) const
{
    return materials_.at(id).descSets[imageIndex];
}

VkDescriptorSet MaterialManager::getPipDescriptorSet(MaterialId id, uint32_t imageIndex) const
{
    const auto& e = materials_.at(id);
    if (imageIndex < e.pipDescSets.size())
        return e.pipDescSets[imageIndex];
    return e.descSets[imageIndex];
}

// ── Queries ────────────────────────────────────────────────────────────────────

MaterialType MaterialManager::getMaterialType(MaterialId id) const
{
    return materials_.at(id).type;
}

const std::string& MaterialManager::getMaterialName(MaterialId id) const
{
    return materials_.at(id).name;
}

void MaterialManager::setMaterialName(MaterialId id, const std::string& name)
{
    materials_.at(id).name = name;
}

const std::string& MaterialManager::getAlbedoPath(MaterialId id) const
{
    return materials_.at(id).albedo.path;
}

const std::string& MaterialManager::getNormalPath(MaterialId id) const
{
    return materials_.at(id).normal.path;
}

bool MaterialManager::isValid(MaterialId id) const
{
    return id != kInvalidMaterialId && materials_.find(id) != materials_.end();
}

bool MaterialManager::isDeletable(MaterialId id) const
{
    auto it = materials_.find(id);
    return it != materials_.end() && it->second.deletable;
}

bool MaterialManager::hasSkinning(MaterialId id) const
{
    auto it = materials_.find(id);
    return it != materials_.end() && it->second.skinned;
}

void MaterialManager::updateBoneMatrices(MaterialId id,
                                         uint32_t imageIndex,
                                         const std::vector<glm::mat4>& bones)
{
    auto it = materials_.find(id);
    if (it == materials_.end()) return;
    MaterialEntry& e = it->second;
    if (!e.skinned || imageIndex >= e.boneUboMapped.size() || !e.boneUboMapped[imageIndex])
        return;

    BoneMatricesUBO ubo{};
    const size_t count = std::min(bones.size(), static_cast<size_t>(kMaxBones));
    for (size_t i = 0; i < count; ++i)
        ubo.bones[i] = bones[i];
    for (size_t i = count; i < static_cast<size_t>(kMaxBones); ++i)
        ubo.bones[i] = glm::mat4(1.f);
    memcpy(e.boneUboMapped[imageIndex], &ubo, sizeof(ubo));
}

// ── Reload default mesh textures ───────────────────────────────────────────────

void MaterialManager::reloadDefaultMeshTextures(const std::string& texturePath,
                                                const VulkanContext& ctx,
                                                const CommandManager& cmdMgr,
                                                const BufferManager& bufMgr,
                                                const FramebufferManager& fbMgr,
                                                const PipelineManager& pipeMgr)
{
    if (texturePath.empty()) return;
    setAlbedoPath(defaultMeshId_, texturePath, ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
}

// ── Asset-based material loading & pipeline resolution ───────────────────────────

MaterialId MaterialManager::loadMaterialFromAsset(const std::string& astRelPath,
                                                  const VulkanContext& ctx,
                                                  const CommandManager& cmdMgr,
                                                  const BufferManager& bufMgr,
                                                  const FramebufferManager& fbMgr,
                                                  const PipelineManager& pipeMgr)
{
    // 缓存命中：同一 .ast 文件只创建一次材质
    auto cacheIt = assetCache_.find(astRelPath);
    if (cacheIt != assetCache_.end() && isValid(cacheIt->second))
        return cacheIt->second;

    MaterialAssetDesc desc;
    std::string err;
    if (!MaterialAssetLoader::load(astRelPath, desc, &err)) {
        std::cerr << "[MaterialAsset] load failed (" << astRelPath << "): " << err << "\n";
        return kInvalidMaterialId;
    }

    MaterialId id = kInvalidMaterialId;
    try {
        if (desc.type == MaterialType::Mesh || desc.type == MaterialType::Material) {
            id = createMeshMaterial(desc.name.empty() ? std::string("AssetMaterial") : desc.name,
                                    desc.albedoPath, desc.normalPath,
                                    desc.metallicRoughnessPath, desc.aoPath, desc.emissivePath,
                                    desc.params,
                                    ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
        } else {
            id = createBoxMaterial(desc.name.empty() ? std::string("AssetMaterial") : desc.name,
                                   desc.params, ctx, bufMgr, pipeMgr);
        }
    } catch (const std::exception& ex) {
        std::cerr << "[MaterialAsset] creation failed (" << astRelPath << "): "
                  << ex.what() << "\n";
        return kInvalidMaterialId;
    }

    if (id == kInvalidMaterialId) return id;
    MaterialEntry& e = materials_.at(id);
    e.vertSpvPath = desc.vertSpv;
    e.fragSpvPath = desc.fragSpv;

    assetCache_[astRelPath] = id;
    return id;
}

VkPipeline MaterialManager::getPipeline(MaterialId id,
                                        const VulkanContext& ctx,
                                        PipelineManager& pipeMgr) const
{
    auto it = materials_.find(id);
    if (it == materials_.end()) {
        // Unknown material -> safest is the default mesh pipeline.
        return pipeMgr.getMainPipeline();
    }
    const MaterialEntry& e = it->second;
    if (e.skinned)
        return pipeMgr.getSkinnedPipeline();
    const PipelineVariant variant = (e.type == MaterialType::Box)
                                  ? PipelineVariant::Box
                                  : PipelineVariant::Mesh;
    return pipeMgr.acquirePipeline(ctx, variant, e.vertSpvPath, e.fragSpvPath);
}
