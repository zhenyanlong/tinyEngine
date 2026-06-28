#pragma once
#include "MaterialManager.hpp"
#include <string>

// Description of a material asset loaded from a .ast (JSON) file.
// All path fields are resolved against the configured project res/ root and
// can be passed straight to TextureManager / PipelineManager.
struct MaterialAssetDesc {
    std::string    name;
    MaterialType   type = MaterialType::Mesh;
    std::string    vertSpv;             // resolved path, may be empty -> use default pipeline
    std::string    fragSpv;             // resolved path, may be empty
    MaterialParams params;
    std::string    albedoPath;          // resolved path, may be empty -> use 1x1 fallback
    std::string    normalPath;          // resolved path, may be empty
    std::string    metallicRoughnessPath;// glTF convention: g=roughness, b=metallic
    std::string    aoPath;              // grayscale; r channel sampled
    std::string    emissivePath;        // sRGB color
    std::string    modelPath;           // resolved path to .obj/.gltf/.glb/.fbx, may be empty

    // Sub-material paths (from .ast "subMaterials" array), relative to res/
    std::vector<std::string> subMaterialPaths;
};

class MaterialAssetLoader {
public:
    /** @brief 设置项目唯一的 res/ 根目录。 */
    static void setResRoot(const std::string& resRoot);

    /** @brief 返回当前 res/ 根目录。 */
    static std::string getResRoot();

    // Load a single material asset.
    //   astRelPath: path relative to res/, e.g. "materials/mainmodel.ast".
    //   out:        populated on success.
    //   err:        optional; set to a human-readable message on failure.
    // Returns true on success; on failure `out` is left untouched.
    static bool load(const std::string& astRelPath,
                     MaterialAssetDesc& out,
                     std::string* err = nullptr);
};
