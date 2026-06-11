#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdint>

class VulkanContext;
class CommandManager;
class PipelineManager;
class MaterialManager;
class SceneManager;
class BufferManager;

/**
 * @brief 离屏渲染一个模型到小纹理并回读到 CPU 保存为 PNG。
 *
 * 用于为 Content Browser 自动生成缩略图。
 *
 * 使用流程：
 *   1. create() — 创建 128x128 渲染目标（render pass + framebuffer + readback buffer）
 *   2. renderModel(modelPath) — 加载模型、渲染、回读、编码 PNG 到 res/thumbnails/
 *   3. destroy() — 释放所有资源
 */
class ThumbnailRenderer {
public:
    static constexpr uint32_t kThumbSize = 128;

    ThumbnailRenderer() = default;

    /** @brief 创建离屏渲染资源 */
    void create(const VulkanContext& ctx,
                const CommandManager& cmdMgr,
                const PipelineManager& pipeMgr,
                MaterialManager& matMgr,
                SceneManager& sceneMgr,
                const BufferManager& bufMgr,
                const std::string& resRoot);

    /** @brief 销毁所有资源 */
    void destroy(const VulkanContext& ctx, MaterialManager& matMgr, SceneManager& sceneMgr);

    /**
     * @brief 渲染一个模型文件并保存缩略图 PNG 到 res/thumbnails/<name>.png
     * @param modelPath 模型文件完整路径
     * @param pngOutputPath 输出 PNG 路径
     * @return true 成功
     */
    bool renderAndSave(const std::string& modelPath, const std::string& pngOutputPath);

    /**
     * @brief 扫描 res/models/ 生成所有缺失缩略图
     * @return 生成的缩略图数量
     */
    int generateAll(const std::string& resRoot);

private:
    VulkanContext*      ctx_ = nullptr;
    const CommandManager* cmdMgr_ = nullptr;
    const PipelineManager* pipeMgr_ = nullptr;
    MaterialManager*    matMgr_ = nullptr;
    SceneManager*       sceneMgr_ = nullptr;
    const BufferManager* bufMgr_ = nullptr;
    std::string         resRoot_;

    // 渲染目标
    VkRenderPass    renderPass_ {};
    VkImage         colorImage_ {};
    VkDeviceMemory  colorMemory_ {};
    VkImageView     colorView_ {};
    VkImage         depthImage_ {};
    VkDeviceMemory  depthMemory_ {};
    VkImageView     depthView_ {};
    VkFramebuffer   framebuffer_ {};

    // 回读缓冲
    VkBuffer        readbackBuf_ {};
    VkDeviceMemory  readbackMem_ {};

    // 零时渲染命令缓冲
    VkCommandPool   cmdPool_ {};
    VkCommandBuffer cmdBuf_ {};

    void createRenderPass(VkFormat colorFormat);
    void createFramebuffer(VkFormat colorFormat, VkFormat depthFormat);
    void createReadbackBuffer();
    void createThumbCommandBuffer();
};
