#include "Video/SequenceCaptureTarget.hpp"

#include "FramebufferManager.hpp"
#include "RenderPassManager.hpp"

#include <array>
#include <cstring>
#include <exception>
#include <limits>

namespace {

void setError(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

bool isSupportedColorFormat(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_SRGB
        || format == VK_FORMAT_B8G8R8A8_UNORM
        || format == VK_FORMAT_R8G8B8A8_SRGB
        || format == VK_FORMAT_R8G8B8A8_UNORM;
}

bool isBgraFormat(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_SRGB
        || format == VK_FORMAT_B8G8R8A8_UNORM;
}

} // namespace

bool SequenceCaptureTarget::create(
    const VulkanContext& ctx,
    const FramebufferManager& framebufferManager,
    const RenderPassManager& renderPassManager,
    uint32_t width, uint32_t height,
    std::string* error)
{
    if (error)
        error->clear();
    destroy(ctx);

    if (width == 0 || height == 0) {
        setError(error, "Sequence capture target dimensions must be greater than zero");
        return false;
    }
    const uint64_t pixelCount =
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    if (pixelCount > std::numeric_limits<VkDeviceSize>::max() / 4u
        || pixelCount > std::numeric_limits<size_t>::max() / 4u) {
        setError(error, "Sequence capture target dimensions are too large");
        return false;
    }

    width_ = width;
    height_ = height;
    colorFormat_ = renderPassManager.getPipColorFormat();
    readbackBytes_ = static_cast<VkDeviceSize>(pixelCount * 4u);
    if (!isSupportedColorFormat(colorFormat_)) {
        setError(error, "Sequence capture target color format is unsupported");
        destroy(ctx);
        return false;
    }

    try {
        framebufferManager.createImage(
            ctx, width_, height_, colorFormat_, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                | VK_IMAGE_USAGE_SAMPLED_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            colorImage_, colorMemory_);
        colorImageView_ = framebufferManager.createImageView(
            ctx, colorImage_, colorFormat_, VK_IMAGE_ASPECT_COLOR_BIT);

        const VkFormat depthFormat = renderPassManager.findDepthFormat(ctx);
        framebufferManager.createImage(
            ctx, width_, height_, depthFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            depthImage_, depthMemory_);
        depthImageView_ = framebufferManager.createImageView(
            ctx, depthImage_, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        const std::array<VkImageView, 2> attachments{
            colorImageView_, depthImageView_
        };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPassManager.getPipRenderPass();
        framebufferInfo.attachmentCount =
            static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width_;
        framebufferInfo.height = height_;
        framebufferInfo.layers = 1;
        if (vkCreateFramebuffer(
                ctx.getDevice(), &framebufferInfo, nullptr,
                &framebuffer_) != VK_SUCCESS) {
            setError(error, "Failed to create the Sequence capture framebuffer");
            destroy(ctx);
            return false;
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = readbackBytes_;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(
                ctx.getDevice(), &bufferInfo, nullptr,
                &readbackBuffer_) != VK_SUCCESS) {
            setError(error, "Failed to create the Sequence capture readback buffer");
            destroy(ctx);
            return false;
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(
            ctx.getDevice(), readbackBuffer_, &requirements);
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = ctx.findMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(
                ctx.getDevice(), &allocation, nullptr,
                &readbackMemory_) != VK_SUCCESS) {
            setError(error, "Failed to allocate Sequence capture readback memory");
            destroy(ctx);
            return false;
        }
        if (vkBindBufferMemory(
                ctx.getDevice(), readbackBuffer_,
                readbackMemory_, 0) != VK_SUCCESS) {
            setError(error, "Failed to bind Sequence capture readback memory");
            destroy(ctx);
            return false;
        }
    } catch (const std::exception& exception) {
        setError(error, exception.what());
        destroy(ctx);
        return false;
    }

    return true;
}

void SequenceCaptureTarget::destroy(const VulkanContext& ctx)
{
    const VkDevice device = ctx.getDevice();
    if (device == VK_NULL_HANDLE)
        return;

    if (framebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device, framebuffer_, nullptr);
    if (colorImageView_ != VK_NULL_HANDLE)
        vkDestroyImageView(device, colorImageView_, nullptr);
    if (colorImage_ != VK_NULL_HANDLE)
        vkDestroyImage(device, colorImage_, nullptr);
    if (colorMemory_ != VK_NULL_HANDLE)
        vkFreeMemory(device, colorMemory_, nullptr);
    if (depthImageView_ != VK_NULL_HANDLE)
        vkDestroyImageView(device, depthImageView_, nullptr);
    if (depthImage_ != VK_NULL_HANDLE)
        vkDestroyImage(device, depthImage_, nullptr);
    if (depthMemory_ != VK_NULL_HANDLE)
        vkFreeMemory(device, depthMemory_, nullptr);
    if (readbackBuffer_ != VK_NULL_HANDLE)
        vkDestroyBuffer(device, readbackBuffer_, nullptr);
    if (readbackMemory_ != VK_NULL_HANDLE)
        vkFreeMemory(device, readbackMemory_, nullptr);

    framebuffer_ = VK_NULL_HANDLE;
    colorImageView_ = VK_NULL_HANDLE;
    colorImage_ = VK_NULL_HANDLE;
    colorMemory_ = VK_NULL_HANDLE;
    depthImageView_ = VK_NULL_HANDLE;
    depthImage_ = VK_NULL_HANDLE;
    depthMemory_ = VK_NULL_HANDLE;
    readbackBuffer_ = VK_NULL_HANDLE;
    readbackMemory_ = VK_NULL_HANDLE;
    readbackBytes_ = 0;
    width_ = 0;
    height_ = 0;
    colorFormat_ = VK_FORMAT_UNDEFINED;
}

void SequenceCaptureTarget::recordReadback(
    VkCommandBuffer commandBuffer) const
{
    if (!isReady())
        return;

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = colorImage_;
    toTransfer.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1
    };
    copy.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(
        commandBuffer, colorImage_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        readbackBuffer_, 1, &copy);

    VkBufferMemoryBarrier toHost{};
    toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.buffer = readbackBuffer_;
    toHost.offset = 0;
    toHost.size = readbackBytes_;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &toHost, 0, nullptr);
}

bool SequenceCaptureTarget::readRgba(
    const VulkanContext& ctx,
    std::vector<uint8_t>& rgba,
    std::string* error) const
{
    if (error)
        error->clear();
    if (!isReady()) {
        setError(error, "Sequence capture target is not ready");
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(
            ctx.getDevice(), readbackMemory_, 0,
            readbackBytes_, 0, &mapped) != VK_SUCCESS) {
        setError(error, "Failed to map Sequence capture readback memory");
        return false;
    }

    const size_t byteCount = static_cast<size_t>(readbackBytes_);
    rgba.resize(byteCount);
    const auto* source = static_cast<const uint8_t*>(mapped);
    if (isBgraFormat(colorFormat_)) {
        for (size_t offset = 0; offset < byteCount; offset += 4u) {
            rgba[offset + 0u] = source[offset + 2u];
            rgba[offset + 1u] = source[offset + 1u];
            rgba[offset + 2u] = source[offset + 0u];
            rgba[offset + 3u] = source[offset + 3u];
        }
    } else {
        std::memcpy(rgba.data(), source, byteCount);
    }
    vkUnmapMemory(ctx.getDevice(), readbackMemory_);
    return true;
}
