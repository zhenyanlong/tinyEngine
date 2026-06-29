#include "MaterialAssetLoader.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>

using nlohmann::json;

namespace {

std::filesystem::path& configuredResRoot()
{
    static std::filesystem::path root = std::filesystem::absolute("res");
    return root;
}

std::filesystem::path stripResPrefix(std::filesystem::path path)
{
    if (!path.empty() && path.begin()->string() == "res")
        return path.lexically_relative("res");
    return path;
}

glm::vec4 readVec4(const json& arr, const glm::vec4& fallback)
{
    if (!arr.is_array() || arr.size() < 4) return fallback;
    return glm::vec4{
        arr[0].get<float>(), arr[1].get<float>(),
        arr[2].get<float>(), arr[3].get<float>()
    };
}

std::string resolveRel(const std::string& rel)
{
    if (rel.empty()) return {};
    std::filesystem::path path(rel);
    if (path.is_absolute()) return path.lexically_normal().string();
    return (configuredResRoot() / stripResPrefix(path)).lexically_normal().string();
}

} // namespace

void MaterialAssetLoader::setResRoot(const std::string& resRoot)
{
    if (resRoot.empty()) return;
    configuredResRoot() = std::filesystem::absolute(std::filesystem::path(resRoot)).lexically_normal();
}

std::string MaterialAssetLoader::getResRoot()
{
    return configuredResRoot().string();
}

bool MaterialAssetLoader::load(const std::string& astRelPath,
                               MaterialAssetDesc& out,
                               std::string* err)
{
    std::filesystem::path astPath(astRelPath);
    if (!astPath.is_absolute())
        astPath = configuredResRoot() / stripResPrefix(astPath);
    astPath = astPath.lexically_normal();
    const std::string fullPath = astPath.string();
    std::ifstream f(astPath);
    if (!f.is_open()) {
        if (err) *err = "cannot open file: " + fullPath;
        return false;
    }

    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        if (err) *err = std::string("json parse error: ") + e.what();
        return false;
    }

    MaterialAssetDesc desc;

    desc.name = j.value("name", std::string{});

    const std::string typeStr = j.value("type", std::string("Mesh"));
    if (typeStr == "Material")      desc.type = MaterialType::Material;
    else if (typeStr == "Box")      desc.type = MaterialType::Box;
    else                            desc.type = MaterialType::Mesh;

    if (j.contains("shader") && j["shader"].is_object()) {
        const auto& s = j["shader"];
        desc.vertSpv = resolveRel(s.value("vert", std::string{}));
        desc.fragSpv = resolveRel(s.value("frag", std::string{}));
    }

    if (j.contains("params") && j["params"].is_object()) {
        const auto& p = j["params"];
        if (p.contains("baseColor"))
            desc.params.baseColor = readVec4(p["baseColor"], desc.params.baseColor);
        desc.params.roughness         = p.value("roughness",         desc.params.roughness);
        desc.params.metallic          = p.value("metallic",          desc.params.metallic);
        desc.params.emissiveIntensity = p.value("emissiveIntensity", desc.params.emissiveIntensity);
        if (p.contains("emissiveColor"))
            desc.params.emissiveColor = readVec4(p["emissiveColor"], desc.params.emissiveColor);
    }

    if (j.contains("textures") && j["textures"].is_object()) {
        const auto& t = j["textures"];
        desc.albedoPath            = resolveRel(t.value("albedo",            std::string{}));
        desc.normalPath            = resolveRel(t.value("normal",            std::string{}));
        desc.metallicRoughnessPath = resolveRel(t.value("metallicRoughness", std::string{}));
        desc.aoPath                = resolveRel(t.value("ao",                std::string{}));
        desc.emissivePath          = resolveRel(t.value("emissive",          std::string{}));
    }

    // Optional: model field. Either a plain string "models/xxx.obj"
    // or { "path": "models/xxx.obj" } for forward compatibility.
    if (j.contains("model")) {
        const auto& m = j["model"];
        if (m.is_string()) {
            desc.modelPath = resolveRel(m.get<std::string>());
        } else if (m.is_object()) {
            desc.modelPath = resolveRel(m.value("path", std::string{}));
        }
    }

    // Optional: subMaterials array of relative .ast paths
    // 兼容两种字段名：旧格式 "subMaterials" 和新格式 "materials"
    // （迁移脚本 migrate_assets_to_content.py 将 subMaterials 重命名为 materials）
    auto readMaterialArray = [&](const char* key) {
        if (j.contains(key) && j[key].is_array()) {
            for (const auto& item : j[key]) {
                if (item.is_string())
                    desc.subMaterialPaths.push_back(item.get<std::string>());
            }
        }
    };
    readMaterialArray("subMaterials");
    if (desc.subMaterialPaths.empty())
        readMaterialArray("materials");

    // Optional: animController reference for skinned models
    desc.animControllerPath = resolveRel(j.value("animController", std::string{}));

    out = std::move(desc);
    return true;
}
