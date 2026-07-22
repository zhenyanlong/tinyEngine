#pragma once

#include "VulkanContext.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

class FrameCapture {
public:
    void configure(const VulkanContext& ctx, bool transferSrcSupported,
                   VkFormat format, VkExtent2D extent,
                   const std::string& captureDirectory);
    void destroy(const VulkanContext& ctx);

    nlohmann::json request(int currentFrameCount, bool includeUi = true);
    nlohmann::json query(uint64_t jobId) const;
    bool shouldRenderUi() const;

    bool record(VkCommandBuffer commandBuffer, VkImage swapChainImage,
                int currentFrameCount);
    bool isSubmitted() const;
    void complete(const VulkanContext& ctx);

private:
    enum class Status { Idle, Pending, Submitted, Ready, Failed };

    bool createReadbackBuffer(const VulkanContext& ctx);
    void destroyReadbackBuffer(const VulkanContext& ctx);
    void failActiveJob(std::string code, std::string message);
    static const char* statusName(Status status);

    VkBuffer       readbackBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
    VkDeviceSize   readbackBytes_ = 0;
    VkFormat       format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent_{};
    std::string    captureDirectory_;
    bool           available_ = false;
    std::string    unavailableCode_ = "capture_unavailable";
    std::string    unavailableMessage_ = "Frame capture is not configured";

    Status         status_ = Status::Idle;
    uint64_t       nextJobId_ = 1;
    uint64_t       jobId_ = 0;
    int            requestedFrame_ = 0;
    int            capturedFrame_ = 0;
    bool           includeUi_ = true;
    std::string    outputPath_;
    uint64_t       outputBytes_ = 0;
    std::string    errorCode_;
    std::string    errorMessage_;
};
