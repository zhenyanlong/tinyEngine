#pragma once
#include "VulkanContext.hpp"

class CommandManager {
public:
    void create(const VulkanContext& ctx);
    void allocateCommandBuffers(const VulkanContext& ctx, uint32_t count);
    void createSyncObjects(const VulkanContext& ctx, uint32_t imageCount);
    /** @brief Swapchain image 数量变化后重置逐图像 fence 归属，不重建帧同步对象。 */
    void resetImagesInFlight(uint32_t imageCount);
    void destroy(const VulkanContext& ctx);

    VkCommandPool   getCommandPool()    const { return commandPool_; }
    VkCommandBuffer getCommandBuffer(uint32_t index) const { return commandBuffers_[index]; }

    VkSemaphore getImageAvailableSemaphore(uint32_t frame) const { return imageAvailableSemaphores_[frame]; }
    VkSemaphore getRenderFinishedSemaphore(uint32_t frame) const { return renderFinishedSemaphores_[frame]; }
    VkFence&    getInFlightFence(uint32_t frame)  { return inFlightFences_[frame]; }
    VkFence     getInFlightFenceVal(uint32_t frame) const { return inFlightFences_[frame]; }
    VkFence&    getImageInFlight(uint32_t index)          { return imagesInFlight_[index]; }

    VkCommandBuffer beginSingleTimeCommands(const VulkanContext& ctx) const;
    void            endSingleTimeCommands(const VulkanContext& ctx, VkCommandBuffer cmd) const;

    void freeCommandBuffers(const VulkanContext& ctx);

private:
    VkCommandPool                commandPool_{};
    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore>     imageAvailableSemaphores_;
    std::vector<VkSemaphore>     renderFinishedSemaphores_;
    std::vector<VkFence>         inFlightFences_;
    std::vector<VkFence>         imagesInFlight_;
};
