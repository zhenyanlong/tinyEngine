#pragma once

#include "VulkanContext.hpp"

#include <cstdint>
#include <string>
#include <vector>

class FramebufferManager;
class RenderPassManager;

/**
 * Fixed-resolution offscreen target used by Sequence video recording.
 *
 * The target is deliberately separate from the window swapchain. Its GPU
 * resources may be recreated around a swapchain rebuild while the encoder and
 * fixed-step frame index remain alive.
 */
class SequenceCaptureTarget {
public:
    bool create(const VulkanContext& ctx,
                const FramebufferManager& framebufferManager,
                const RenderPassManager& renderPassManager,
                uint32_t width, uint32_t height,
                std::string* error = nullptr);
    void destroy(const VulkanContext& ctx);

    bool isReady() const {
        return framebuffer_ != VK_NULL_HANDLE
            && colorImage_ != VK_NULL_HANDLE
            && readbackBuffer_ != VK_NULL_HANDLE;
    }

    VkFramebuffer framebuffer() const { return framebuffer_; }
    VkImage colorImage() const { return colorImage_; }
    VkExtent2D extent() const { return {width_, height_}; }

    /**
     * Record a color-image -> host-visible buffer copy after the offscreen
     * render pass has ended.
     */
    void recordReadback(VkCommandBuffer commandBuffer) const;

    /**
     * Copy the completed readback buffer into tightly packed RGBA8 bytes.
     * The caller must wait for the submitting fence before calling this.
     */
    bool readRgba(const VulkanContext& ctx,
                  std::vector<uint8_t>& rgba,
                  std::string* error = nullptr) const;

private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;

    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorImageView_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
    VkDeviceSize readbackBytes_ = 0;
};
