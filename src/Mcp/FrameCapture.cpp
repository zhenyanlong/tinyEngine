#include "FrameCapture.hpp"

#include <stb_image_write.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

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

void FrameCapture::configure(const VulkanContext& ctx, bool transferSrcSupported,
                             VkFormat format, VkExtent2D extent,
                             const std::string& captureDirectory)
{
    if (status_ == Status::Pending || status_ == Status::Submitted) {
        failActiveJob("swapchain_recreated",
                      "Swapchain changed before the capture completed");
    }

    destroyReadbackBuffer(ctx);
    available_ = false;
    format_ = format;
    extent_ = extent;
    captureDirectory_ = std::filesystem::absolute(captureDirectory).string();

    if (!transferSrcSupported) {
        unavailableCode_ = "capture_unsupported";
        unavailableMessage_ = "The current Vulkan surface does not support swapchain transfer-source images";
        return;
    }
    if (!isSupportedColorFormat(format_)) {
        unavailableCode_ = "capture_format_unsupported";
        unavailableMessage_ = "The current swapchain color format is not supported by the screenshot MVP";
        return;
    }
    if (extent_.width == 0 || extent_.height == 0) {
        unavailableCode_ = "capture_extent_invalid";
        unavailableMessage_ = "The current swapchain extent is empty";
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(captureDirectory_, ec);
    if (ec) {
        unavailableCode_ = "capture_directory_error";
        unavailableMessage_ = "Cannot create capture directory: " + ec.message();
        return;
    }

    if (!createReadbackBuffer(ctx))
        return;

    available_ = true;
    unavailableCode_.clear();
    unavailableMessage_.clear();
}

void FrameCapture::destroy(const VulkanContext& ctx)
{
    destroyReadbackBuffer(ctx);
    available_ = false;
}

nlohmann::json FrameCapture::request(int currentFrameCount, bool includeUi)
{
    if (status_ == Status::Pending || status_ == Status::Submitted) {
        return {{"error", {{"code", "capture_busy"},
                            {"message", "A frame capture is already in progress"}}}};
    }
    if (!available_) {
        return {{"error", {{"code", unavailableCode_},
                            {"message", unavailableMessage_}}}};
    }

    jobId_ = nextJobId_++;
    requestedFrame_ = currentFrameCount;
    capturedFrame_ = 0;
    includeUi_ = includeUi;
    outputPath_.clear();
    outputBytes_ = 0;
    errorCode_.clear();
    errorMessage_.clear();
    status_ = Status::Pending;

    return {
        {"jobId", jobId_},
        {"status", statusName(status_)},
        {"requestedFrame", requestedFrame_},
        {"includeUi", includeUi_}
    };
}

nlohmann::json FrameCapture::query(uint64_t jobId) const
{
    if (jobId == 0 || jobId != jobId_) {
        return {{"error", {{"code", "capture_job_not_found"},
                            {"message", "Unknown capture job id"}}}};
    }

    nlohmann::json result = {
        {"jobId", jobId_},
        {"status", statusName(status_)},
        {"requestedFrame", requestedFrame_},
        {"capturedFrame", capturedFrame_},
        {"includeUi", includeUi_},
        {"width", extent_.width},
        {"height", extent_.height}
    };
    if (status_ == Status::Ready) {
        result["path"] = outputPath_;
        result["mimeType"] = "image/png";
        result["bytes"] = outputBytes_;
    } else if (status_ == Status::Failed) {
        result["error"] = {
            {"code", errorCode_},
            {"message", errorMessage_}
        };
    }
    return result;
}

bool FrameCapture::shouldRenderUi() const
{
    return status_ != Status::Pending || includeUi_;
}

bool FrameCapture::record(VkCommandBuffer commandBuffer, VkImage swapChainImage,
                          int currentFrameCount)
{
    if (status_ != Status::Pending || !available_)
        return false;

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = swapChainImage;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = {extent_.width, extent_.height, 1};
    vkCmdCopyImageToBuffer(commandBuffer, swapChainImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuffer_, 1, &copy);

    VkImageMemoryBarrier toPresent{};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = swapChainImage;
    toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toPresent);

    capturedFrame_ = currentFrameCount;
    status_ = Status::Submitted;
    return true;
}

bool FrameCapture::isSubmitted() const
{
    return status_ == Status::Submitted;
}

bool FrameCapture::isBusy() const
{
    return status_ == Status::Pending || status_ == Status::Submitted;
}

void FrameCapture::complete(const VulkanContext& ctx)
{
    if (status_ != Status::Submitted)
        return;

    void* mapped = nullptr;
    if (vkMapMemory(ctx.getDevice(), readbackMemory_, 0, readbackBytes_, 0, &mapped) != VK_SUCCESS) {
        failActiveJob("capture_map_failed", "Failed to map the Vulkan readback buffer");
        return;
    }

    const size_t pixelBytes = static_cast<size_t>(extent_.width)
                            * static_cast<size_t>(extent_.height) * 4u;
    std::vector<unsigned char> rgba(pixelBytes);
    const auto* source = static_cast<const unsigned char*>(mapped);
    if (isBgraFormat(format_)) {
        for (size_t i = 0; i < pixelBytes; i += 4) {
            rgba[i + 0] = source[i + 2];
            rgba[i + 1] = source[i + 1];
            rgba[i + 2] = source[i + 0];
            rgba[i + 3] = source[i + 3];
        }
    } else {
        std::memcpy(rgba.data(), source, pixelBytes);
    }
    vkUnmapMemory(ctx.getDevice(), readbackMemory_);

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::filesystem::path path = std::filesystem::path(captureDirectory_)
        / ("tiny_capture_" + std::to_string(now) + "_" + std::to_string(jobId_) + ".png");

    if (stbi_write_png(path.string().c_str(), static_cast<int>(extent_.width),
                       static_cast<int>(extent_.height), 4, rgba.data(),
                       static_cast<int>(extent_.width * 4u)) == 0) {
        failActiveJob("capture_png_failed", "Failed to encode the captured frame as PNG");
        return;
    }

    std::error_code ec;
    outputBytes_ = std::filesystem::file_size(path, ec);
    if (ec) {
        failActiveJob("capture_file_error", "PNG was written but its size could not be read: " + ec.message());
        return;
    }

    outputPath_ = std::filesystem::absolute(path).string();
    status_ = Status::Ready;
}

bool FrameCapture::createReadbackBuffer(const VulkanContext& ctx)
{
    readbackBytes_ = static_cast<VkDeviceSize>(extent_.width)
                   * static_cast<VkDeviceSize>(extent_.height) * 4u;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = readbackBytes_;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx.getDevice(), &bufferInfo, nullptr, &readbackBuffer_) != VK_SUCCESS) {
        unavailableCode_ = "capture_buffer_create_failed";
        unavailableMessage_ = "Failed to create the Vulkan screenshot readback buffer";
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(ctx.getDevice(), readbackBuffer_, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    try {
        allocation.memoryTypeIndex = ctx.findMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    } catch (const std::exception& error) {
        unavailableCode_ = "capture_memory_type_unavailable";
        unavailableMessage_ = error.what();
        destroyReadbackBuffer(ctx);
        return false;
    }

    if (vkAllocateMemory(ctx.getDevice(), &allocation, nullptr, &readbackMemory_) != VK_SUCCESS) {
        unavailableCode_ = "capture_memory_allocate_failed";
        unavailableMessage_ = "Failed to allocate Vulkan screenshot readback memory";
        destroyReadbackBuffer(ctx);
        return false;
    }
    if (vkBindBufferMemory(ctx.getDevice(), readbackBuffer_, readbackMemory_, 0) != VK_SUCCESS) {
        unavailableCode_ = "capture_memory_bind_failed";
        unavailableMessage_ = "Failed to bind Vulkan screenshot readback memory";
        destroyReadbackBuffer(ctx);
        return false;
    }
    return true;
}

void FrameCapture::destroyReadbackBuffer(const VulkanContext& ctx)
{
    if (readbackBuffer_ != VK_NULL_HANDLE)
        vkDestroyBuffer(ctx.getDevice(), readbackBuffer_, nullptr);
    if (readbackMemory_ != VK_NULL_HANDLE)
        vkFreeMemory(ctx.getDevice(), readbackMemory_, nullptr);
    readbackBuffer_ = VK_NULL_HANDLE;
    readbackMemory_ = VK_NULL_HANDLE;
    readbackBytes_ = 0;
}

void FrameCapture::failActiveJob(std::string code, std::string message)
{
    errorCode_ = std::move(code);
    errorMessage_ = std::move(message);
    status_ = Status::Failed;
}

const char* FrameCapture::statusName(Status status)
{
    switch (status) {
    case Status::Idle: return "idle";
    case Status::Pending: return "pending";
    case Status::Submitted: return "submitted";
    case Status::Ready: return "ready";
    case Status::Failed: return "failed";
    }
    return "failed";
}
