#include "CommandManager.hpp"
#include <stdexcept>

void CommandManager::create(const VulkanContext& ctx)
{
    QueueFamilyIndices indices = ctx.findQueueFamilies(ctx.getPhysicalDevice());

    VkCommandPoolCreateInfo pi{};
    pi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = indices.graphicsFamily.value();

    if (vkCreateCommandPool(ctx.getDevice(), &pi, nullptr, &commandPool_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool!");
}

void CommandManager::allocateCommandBuffers(const VulkanContext& ctx, uint32_t count)
{
    freeCommandBuffers(ctx);
    commandBuffers_.resize(count);

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = commandPool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = count;

    if (vkAllocateCommandBuffers(ctx.getDevice(), &ai, commandBuffers_.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate command buffers!");
}

void CommandManager::createSyncObjects(const VulkanContext& ctx, uint32_t imageCount)
{
    imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);
    imagesInFlight_.resize(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(ctx.getDevice(), &si, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(ctx.getDevice(), &si, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(ctx.getDevice(), &fi, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create sync objects!");
    }
}

void CommandManager::resetImagesInFlight(uint32_t imageCount)
{
    imagesInFlight_.assign(imageCount, VK_NULL_HANDLE);
}

void CommandManager::destroy(const VulkanContext& ctx)
{
    freeCommandBuffers(ctx);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(ctx.getDevice(), renderFinishedSemaphores_[i], nullptr);
        vkDestroySemaphore(ctx.getDevice(), imageAvailableSemaphores_[i], nullptr);
        vkDestroyFence(ctx.getDevice(), inFlightFences_[i], nullptr);
    }
    imageAvailableSemaphores_.clear();
    renderFinishedSemaphores_.clear();
    inFlightFences_.clear();
    imagesInFlight_.clear();

    vkDestroyCommandPool(ctx.getDevice(), commandPool_, nullptr);
    commandPool_ = VK_NULL_HANDLE;
}

void CommandManager::freeCommandBuffers(const VulkanContext& ctx)
{
    if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(ctx.getDevice(), commandPool_,
            static_cast<uint32_t>(commandBuffers_.size()), commandBuffers_.data());
        commandBuffers_.clear();
    }
}

VkCommandBuffer CommandManager::beginSingleTimeCommands(const VulkanContext& ctx) const
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = commandPool_;
    ai.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx.getDevice(), &ai, &cb);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    return cb;
}

void CommandManager::endSingleTimeCommands(const VulkanContext& ctx, VkCommandBuffer cb) const
{
    vkEndCommandBuffer(cb);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cb;

    vkQueueSubmit(ctx.getGraphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.getGraphicsQueue());
    vkFreeCommandBuffers(ctx.getDevice(), commandPool_, 1, &cb);
}
