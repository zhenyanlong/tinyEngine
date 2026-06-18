#include "MaterialAssetLoader.hpp"
#include "nlohmann/json.hpp"
#include <fstream>

using nlohmann::json;

namespace {

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
    return std::string(MaterialAssetLoader::kResRoot) + rel;
}

} // namespace

bool MaterialAssetLoader::load(const std::string& astRelPath,
                               MaterialAssetDesc& out,
                               std::string* err)
{
    const std::string fullPath = std::string(kResRoot) + astRelPath;
    std::ifstream f(fullPath);
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
    if (j.contains("subMaterials") && j["subMaterials"].is_array()) {
        for (const auto& item : j["subMaterials"]) {
            if (item.is_string())
                desc.subMaterialPaths.push_back(item.get<std::string>());
        }
    }

    out = std::move(desc);
    return true;
}
