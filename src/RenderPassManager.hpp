#pragma once
#include "VulkanContext.hpp"
#include "SwapChain.hpp"

class RenderPassManager {
public:
    void create(const VulkanContext& ctx, const SwapChain& swapChain);
    void destroy(const VulkanContext& ctx);

    VkRenderPass getMainRenderPass() const { return mainRenderPass_; }
    VkRenderPass getPickRenderPass() const { return pickRenderPass_; }
    VkRenderPass getPipRenderPass()  const { return pipRenderPass_; }
    // PiP color attachment 格式与 swapchain 一致，保证主管线可在 PiP renderpass 上复用
    VkFormat     getPipColorFormat() const { return pipColorFormat_; }

    VkFormat findDepthFormat(const VulkanContext& ctx) const;
    VkFormat findSupportedFormat(const VulkanContext& ctx,
        const std::vector<VkFormat>& candidates,
        VkImageTiling tiling, VkFormatFeatureFlags features) const;

private:
    VkRenderPass mainRenderPass_{};
    VkRenderPass pickRenderPass_{};
    VkRenderPass pipRenderPass_{};
    VkFormat     pipColorFormat_{};

    void createMainRenderPass(const VulkanContext& ctx, VkFormat colorFormat);
    void createPickRenderPass(const VulkanContext& ctx);
    void createPipRenderPass(const VulkanContext& ctx, VkFormat colorFormat);
};
