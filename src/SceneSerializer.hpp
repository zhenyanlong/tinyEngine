#pragma once

#include <string>

class Application;
class SceneManager;
class BufferManager;
class MaterialManager;
class CommandManager;
class FramebufferManager;
class PipelineManager;
class Camera;
class VulkanContext;

/**
 * @brief 场景序列化器：将场景实体/材质/相机状态保存为 .scene.json 或从中加载。
 */
class SceneSerializer {
public:
    /**
     * @brief 保存当前场景到 JSON 文件。
     * @param path     目标文件完整路径
     * @param sceneMgr 场景管理器（包含实体列表和 Box 数据）
     * @param matMgr   材质管理器（用于对比 .ast 参考值与当前材质差异）
     * @param camera   相机（位置和朝向）
     * @return true 成功
     */
    static bool save(const std::string& path,
                     const SceneManager& sceneMgr,
                     const MaterialManager& matMgr,
                     const Camera& camera);

    /**
     * @brief 从 JSON 文件加载场景。
     * @param path     源文件完整路径
     * @param app      应用实例（用于调用 createModelEntity / addBox）
     * @param sceneMgr 场景管理器
     * @param matMgr   材质管理器（用于加载 .ast 材质 + 应用 materialOverride）
     * @param bufMgr   缓冲管理器
     * @param ctx      Vulkan 上下文（材质创建需要）
     * @param cmdMgr   命令管理器（材质纹理上传需要）
     * @param fbMgr    帧缓冲管理器（材质纹理上传需要）
     * @param pipeMgr  管线管理器（材质描述符集分配需要）
     * @param camera   相机（位置和朝向恢复）
     * @param resRoot  资源根目录（拼接模型路径）
     * @return true 成功
     */
    static bool load(const std::string& path,
                     Application& app,
                     SceneManager& sceneMgr,
                     MaterialManager& matMgr,
                     BufferManager& bufMgr,
                     VulkanContext& ctx,
                     CommandManager& cmdMgr,
                     FramebufferManager& fbMgr,
                     PipelineManager& pipeMgr,
                     Camera& camera,
                     const std::string& resRoot);
};
