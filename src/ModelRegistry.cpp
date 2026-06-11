#include "ModelRegistry.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

ModelType ModelRegistry::classifyModelType(const std::string& modelRelPath)
{
    const std::string lower = modelRelPath;
    auto endsWith = [&](const char* suffix) {
        const size_t slen = std::strlen(suffix);
        return lower.size() >= slen && lower.compare(lower.size() - slen, slen, suffix) == 0;
    };
    if (endsWith(".obj"))  return ModelType::OBJ;
    if (endsWith(".glb"))  return ModelType::GLB;
    if (endsWith(".gltf")) return ModelType::GLTF;
    return ModelType::Unknown;
}

std::string ModelRegistry::makeDisplayName(const std::string& stem)
{
    std::string name = stem;
    // 下划线替换空格
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

void ModelRegistry::scan(const std::string& resRoot)
{
    assets_.clear();
    pathToId_.clear();
    resRoot_ = resRoot;

    refresh();
}

void ModelRegistry::refresh()
{
    assets_.clear();
    pathToId_.clear();

    const std::filesystem::path materialsDir =
        std::filesystem::path(resRoot_) / "materials";
    std::error_code ec;
    if (!std::filesystem::is_directory(materialsDir, ec))
        return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(materialsDir, ec)) {
        if (!entry.is_regular_file(ec))
            continue;

        if (entry.path().extension() != ".ast")
            continue;

        // 构建 .ast 相对路径：materials/xxx.ast
        const std::filesystem::path rel = std::filesystem::relative(entry.path(), resRoot_, ec);
        const std::string astRelPath = rel.generic_string();

        // 读取 .ast 提取 name 和 model 字段
        std::string displayName = makeDisplayName(entry.path().stem().string());
        std::string modelRelPath;
        ModelType   type = ModelType::Unknown;

        {
            std::ifstream f(entry.path());
            if (f.is_open()) {
                try {
                    nlohmann::json j;
                    f >> j;
                    // 优先使用 .ast 中的 name 字段
                    if (j.contains("name") && j["name"].is_string())
                        displayName = j["name"].get<std::string>();
                    // 读取 model 字段
                    if (j.contains("model")) {
                        const auto& m = j["model"];
                        if (m.is_string())
                            modelRelPath = m.get<std::string>();
                        else if (m.is_object() && m.contains("path"))
                            modelRelPath = m["path"].get<std::string>();
                        type = classifyModelType(modelRelPath);
                    }
                } catch (...) {
                    // JSON 解析失败，保持 fallback 值
                }
            }
        }

        ModelAsset asset;
        asset.id           = std::hash<std::string>{}(astRelPath);
        asset.name         = displayName;
        asset.astRelPath   = astRelPath;
        asset.modelRelPath = modelRelPath;
        asset.type         = type;

        // 检测缩略图：res/thumbnails/<stem>.png
        const std::string stem = entry.path().stem().string();
        const std::filesystem::path thumbPath =
            std::filesystem::path(resRoot_) / "thumbnails" / (stem + ".png");
        if (std::filesystem::is_regular_file(thumbPath))
            asset.hasThumbnail = true;

        assets_.push_back(std::move(asset));
    }

    // 按名称排序
    std::sort(assets_.begin(), assets_.end(),
              [](const ModelAsset& a, const ModelAsset& b) { return a.name < b.name; });

    // 重建查找表：key = astRelPath
    for (const auto& a : assets_)
        pathToId_[a.astRelPath] = a.id;
}

const ModelAsset* ModelRegistry::findByPath(const std::string& relPath) const
{
    const auto it = pathToId_.find(relPath);
    if (it == pathToId_.end())
        return nullptr;
    return findById(it->second);
}

const ModelAsset* ModelRegistry::findById(uint64_t id) const
{
    for (const auto& a : assets_) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

std::vector<const ModelAsset*> ModelRegistry::search(const std::string& keyword) const
{
    std::vector<const ModelAsset*> result;
    if (keyword.empty()) {
        result.reserve(assets_.size());
        for (const auto& a : assets_)
            result.push_back(&a);
        return result;
    }

    // 不区分大小写
    std::string lowerKeyword = keyword;
    std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::tolower);

    for (const auto& a : assets_) {
        std::string lowerName = a.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName.find(lowerKeyword) != std::string::npos)
            result.push_back(&a);
    }
    return result;
}
