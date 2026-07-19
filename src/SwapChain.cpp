#include "SwapChain.hpp"
#include <algorithm>
#include <stdexcept>

void SwapChain::create(const VulkanContext& ctx, GLFWwindow* window)
{
    createSwapChain(ctx, window);
    createImageViews(ctx);
}

void SwapChain::recreate(const VulkanContext& ctx, GLFWwindow* window)
{
    destroyImageViews(ctx);
    vkDestroySwapchainKHR(ctx.getDevice(), swapChain_, nullptr);
    swapChain_ = VK_NULL_HANDLE;
    createSwapChain(ctx, window);
    createImageViews(ctx);
}

void SwapChain::destroy(const VulkanContext& ctx)
{
    destroyImageViews(ctx);
    vkDestroySwapchainKHR(ctx.getDevice(), swapChain_, nullptr);
    swapChain_ = VK_NULL_HANDLE;
}

void SwapChain::createSwapChain(const VulkanContext& ctx, GLFWwindow* window)
{
    SwapChainSupportDetails support = ctx.querySwapChainSupport(ctx.getPhysicalDevice());
    VkSurfaceFormatKHR surfFmt  = chooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR   pMode    = chooseSwapPresentMode(support.presentModes);
    VkExtent2D         ext      = chooseSwapExtent(support.capabilities, window);

    uint32_t imgCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        imgCount > support.capabilities.maxImageCount)
        imgCount = support.capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = ctx.getSurface();
    ci.minImageCount    = imgCount;
    ci.imageFormat      = surfFmt.format;
    ci.imageColorSpace  = surfFmt.colorSpace;
    ci.imageExtent      = ext;
    ci.imageArrayLayers = 1;
    transferSrcSupported_ =
        (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (transferSrcSupported_)
        ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    QueueFamilyIndices idx = ctx.findQueueFamilies(ctx.getPhysicalDevice());
    uint32_t families[]   = { idx.graphicsFamily.value(), idx.presentFamily.value() };

    if (idx.graphicsFamily != idx.presentFamily) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform   = support.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = pMode;
    ci.clipped        = VK_TRUE;

    if (vkCreateSwapchainKHR(ctx.getDevice(), &ci, nullptr, &swapChain_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain!");

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(ctx.getDevice(), swapChain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(ctx.getDevice(), swapChain_, &count, images_.data());

    imageFormat_ = surfFmt.format;
    extent_      = ext;
}

void SwapChain::createImageViews(const VulkanContext& ctx)
{
    imageViews_.resize(images_.size());
    for (uint32_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image    = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = imageFormat_;
        vi.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel   = 0;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(ctx.getDevice(), &vi, nullptr, &imageViews_[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create swapchain image view!");
    }
}

void SwapChain::destroyImageViews(const VulkanContext& ctx)
{
    for (auto v : imageViews_)
        vkDestroyImageView(ctx.getDevice(), v, nullptr);
    imageViews_.clear();
}

VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return formats.front();
}

VkPresentModeKHR SwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes)
{
    for (const auto& m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* window)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;

    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    VkExtent2D actual{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    actual.width  = std::max(caps.minImageExtent.width,  std::min(caps.maxImageExtent.width,  actual.width));
    actual.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, actual.height));
    return actual;
}
