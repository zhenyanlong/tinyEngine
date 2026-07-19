#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

class Camera;
class SceneManager;

/**
 * @brief 将当前场景的可验证状态序列化为稳定 JSON 快照。
 */
class SceneSnapshot {
public:
    /**
     * @brief 捕获模型实体、Box 和相机状态。
     * @param sceneManager 场景数据来源，必须在主线程访问
     * @param camera 当前编辑器相机
     * @param selectedBoxEntityId 当前选中的 Box，0 表示无
     * @return 可直接通过 MCP IPC 返回的 JSON 对象
     */
    static nlohmann::json capture(const SceneManager& sceneManager,
                                  const Camera& camera,
                                  uint64_t selectedBoxEntityId);
};
