#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

/** @brief 模型文件类型枚举 */
enum class ModelType { OBJ, GLTF, GLB, Unknown };

/** @brief 模型资产元数据（每个条目对应 res/materials/ 下一个 .ast 文件） */
struct ModelAsset {
    uint64_t    id;           ///< 唯一标识 == std::hash<astRelPath>
    std::string name;         ///< 显示名称（来自 .ast 的 name 字段或文件名 stem）
    std::string astRelPath;   ///< .ast 文件相对 res/ 的路径，如 "materials/viking_room.ast"
    std::string modelRelPath; ///< .ast 中 model 字段指向的模型路径，如 "models/viking_room.obj"；空表示纯材质
    std::string astType;      ///< .ast 中 type 字段: "Mesh" / "Box" / "Material"
    std::string subFolder;    ///< materials/ 下的子目录，如 "sci-fi"；平铺在 materials/ 下则为空
    ModelType   type;
    glm::vec3   boundsMin   = glm::vec3(0.f);  ///< AABB 最小值
    glm::vec3   boundsMax   = glm::vec3(0.f);  ///< AABB 最大值
    glm::vec3   displaySize = glm::vec3(0.f);  ///< boundsMax - boundsMin
    bool        hasThumbnail = false;           ///< 缩略图可用
};

/**
 * @brief 模型资产注册表：扫描 res/materials/*.ast，缓存模型元数据。
 *
 * 每个 .ast 文件对应一个"模型资产"（含材质参数 + 模型引用），在 initVulkan 中调用 scan() 初始化。
 * Content Browser 通过 getAll() / search() 查询。
 */
class ModelRegistry {
public:
    /** @brief 扫描 resRoot/materials/*.ast，填充 assets_ */
    void scan(const std::string& resRoot);

    /** @brief 重新扫描（运行时热刷新） */
    void refresh();

    /** @brief 返回所有已注册的模型资产 */
    const std::vector<ModelAsset>& getAll() const { return assets_; }

    /** @brief 按 .ast 相对路径查找 */
    const ModelAsset* findByPath(const std::string& astRelPath) const;

    /** @brief 按 id 查找 */
    const ModelAsset* findById(uint64_t id) const;

    /** @brief 名称不区分大小写的子串搜索（限定当前文件夹） */
    std::vector<const ModelAsset*> search(const std::string& keyword,
                                          const std::string& subFolder) const;

    /** @brief 已扫描模型数量（限定当前文件夹） */
    size_t size(const std::string& subFolder) const;

    /** @brief 返回已发现的所有子文件夹名（去重排序） */
    const std::vector<std::string>& getFolders() const { return folders_; }

    /** @brief 返回 resRoot（用于拼接完整路径） */
    const std::string& getResRoot() const { return resRoot_; }

private:
    std::vector<ModelAsset>                assets_;
    std::unordered_map<std::string, uint64_t> pathToId_;  // key = astRelPath
    std::vector<std::string>               folders_;
    std::string                            resRoot_;

    /** @brief 根据模型文件扩展名判断模型类型 */
    static ModelType classifyModelType(const std::string& modelRelPath);

    /** @brief 生成显示名称：去掉后缀，下划线替换空格 */
    static std::string makeDisplayName(const std::string& stem);
};
