#pragma once
#include "VulkanContext.hpp"

class SwapChain {
public:
    void create(const VulkanContext& ctx, GLFWwindow* window);
    void recreate(const VulkanContext& ctx, GLFWwindow* window);
    void destroy(const VulkanContext& ctx);

    VkSwapchainKHR             getSwapChain()       const { return swapChain_; }
    VkFormat                   getImageFormat()     const { return imageFormat_; }
    VkExtent2D                 getExtent()          const { return extent_; }
    bool                       supportsTransferSrc() const { return transferSrcSupported_; }
    uint32_t                   getImageCount()      const { return static_cast<uint32_t>(images_.size()); }
    const std::vector<VkImage>&     getImages()     const { return images_; }
    const std::vector<VkImageView>& getImageViews() const { return imageViews_; }

private:
    VkSwapchainKHR             swapChain_{};
    std::vector<VkImage>       images_;
    std::vector<VkImageView>   imageViews_;
    VkFormat                   imageFormat_{};
    VkExtent2D                 extent_{};
    bool                       transferSrcSupported_ = false;

    void createSwapChain(const VulkanContext& ctx, GLFWwindow* window);
    void createImageViews(const VulkanContext& ctx);
    void destroyImageViews(const VulkanContext& ctx);

    static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    static VkPresentModeKHR   chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes);
    static VkExtent2D         chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* window);
};
