#include "ModelRegistry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

ModelType ModelRegistry::classifyModelType(const std::string& modelRelPath)
{
    std::string lower = modelRelPath;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto endsWith = [&](const char* suffix) {
        const size_t slen = std::strlen(suffix);
        return lower.size() >= slen && lower.compare(lower.size() - slen, slen, suffix) == 0;
    };
    if (endsWith(".obj"))  return ModelType::OBJ;
    if (endsWith(".glb"))  return ModelType::GLB;
    if (endsWith(".gltf")) return ModelType::GLTF;
    if (endsWith(".fbx"))  return ModelType::FBX;
    return ModelType::Unknown;
}

std::string ModelRegistry::makeDisplayName(const std::string& stem)
{
    std::string name = stem;
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
    folders_.clear();

    std::error_code ec;
    std::filesystem::path assetDir = std::filesystem::path(resRoot_) / "content";
    std::string assetRootName = "content";

    if (!std::filesystem::is_directory(assetDir, ec)) {
        assetDir = std::filesystem::path(resRoot_) / "materials";
        assetRootName = "materials";
    }
    if (!std::filesystem::is_directory(assetDir, ec))
        return;

    auto addFolder = [&](const std::string& folder) {
        if (folder.empty()) return;
        if (std::find(folders_.begin(), folders_.end(), folder) == folders_.end())
            folders_.push_back(folder);
    };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetDir, ec)) {
        if (entry.is_directory(ec)) {
            const std::filesystem::path relDir = std::filesystem::relative(entry.path(), assetDir, ec);
            if (!ec && !relDir.empty() && relDir.generic_string() != ".")
                addFolder(relDir.generic_string());
            continue;
        }

        if (!entry.is_regular_file(ec) || entry.path().extension() != ".ast")
            continue;

        const std::filesystem::path rel = std::filesystem::relative(entry.path(), resRoot_, ec);
        if (ec) continue;
        const std::string astRelPath = rel.generic_string();

        std::string subFolder;
        {
            const std::filesystem::path astP(astRelPath);
            const std::string parentStr = astP.parent_path().generic_string();
            const std::string prefix = assetRootName + "/";
            if (parentStr != assetRootName && parentStr.size() > prefix.size()
                && parentStr.compare(0, prefix.size(), prefix) == 0) {
                subFolder = parentStr.substr(prefix.size());
            }
        }

        std::string displayName = makeDisplayName(entry.path().stem().string());
        std::string modelRelPath;
        std::string astType = "Mesh";
        ModelType type = ModelType::Unknown;

        {
            std::ifstream f(entry.path());
            if (f.is_open()) {
                try {
                    nlohmann::json j;
                    f >> j;
                    if (j.contains("name") && j["name"].is_string())
                        displayName = j["name"].get<std::string>();
                    if (j.contains("type") && j["type"].is_string())
                        astType = j["type"].get<std::string>();
                    if (j.contains("model")) {
                        const auto& m = j["model"];
                        if (m.is_string())
                            modelRelPath = m.get<std::string>();
                        else if (m.is_object() && m.contains("path"))
                            modelRelPath = m["path"].get<std::string>();
                        type = classifyModelType(modelRelPath);
                    } else if (j.contains("binary") && j["binary"].is_string()) {
                        modelRelPath = j["binary"].get<std::string>();
                    }
                } catch (...) {
                    // Keep fallback metadata for malformed assets.
                }
            }
        }

        ModelAsset asset;
        asset.id = std::hash<std::string>{}(astRelPath);
        asset.name = displayName;
        asset.astRelPath = astRelPath;
        asset.astType = astType;
        asset.subFolder = subFolder;
        asset.modelRelPath = modelRelPath;
        asset.type = type;

        const std::string stem = entry.path().stem().string();
        const std::filesystem::path thumbPath =
            std::filesystem::path(resRoot_) / "thumbnails" / (stem + ".png");
        if (std::filesystem::is_regular_file(thumbPath))
            asset.hasThumbnail = true;

        assets_.push_back(std::move(asset));
    }

    std::sort(assets_.begin(), assets_.end(),
              [](const ModelAsset& a, const ModelAsset& b) { return a.name < b.name; });

    for (const auto& a : assets_) {
        if (!a.subFolder.empty())
            addFolder(a.subFolder);
    }
    std::sort(folders_.begin(), folders_.end());

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

std::vector<const ModelAsset*> ModelRegistry::search(const std::string& keyword,
                                                      const std::string& subFolder) const
{
    std::vector<const ModelAsset*> result;

    auto matchesFolder = [&](const ModelAsset& a) {
        return subFolder.empty() ? a.subFolder.empty() : (a.subFolder == subFolder);
    };

    if (keyword.empty()) {
        result.reserve(assets_.size());
        for (const auto& a : assets_) {
            if (matchesFolder(a))
                result.push_back(&a);
        }
        return result;
    }

    std::string lowerKeyword = keyword;
    std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::tolower);

    for (const auto& a : assets_) {
        if (!matchesFolder(a)) continue;
        std::string lowerName = a.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName.find(lowerKeyword) != std::string::npos)
            result.push_back(&a);
    }
    return result;
}

size_t ModelRegistry::size(const std::string& subFolder) const
{
    size_t cnt = 0;
    for (const auto& a : assets_) {
        if (subFolder.empty()) {
            if (a.subFolder.empty()) ++cnt;
        } else if (a.subFolder == subFolder) {
            ++cnt;
        }
    }
    return cnt;
}
