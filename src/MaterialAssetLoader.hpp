#pragma once
#include "MaterialManager.hpp"
#include <string>

// Description of a material asset loaded from a .ast (JSON) file.
// All path fields stored here are RELATIVE to the executable working dir
// (i.e. already prefixed with "res/"), so they can be passed straight to
// TextureManager / PipelineManager without any further rewriting.
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
    std::string    modelPath;           // resolved path to .obj/.gltf/.glb, may be empty

    // Sub-material paths (from .ast "subMaterials" array), relative to res/
    std::vector<std::string> subMaterialPaths;
};

class MaterialAssetLoader {
public:
    // The resource root: every relative path inside the .ast is resolved
    // against this directory.
    static constexpr const char* kResRoot = "res/";

    // Load a single material asset.
    //   astRelPath: path relative to res/, e.g. "materials/mainmodel.ast".
    //   out:        populated on success.
    //   err:        optional; set to a human-readable message on failure.
    // Returns true on success; on failure `out` is left untouched.
    static bool load(const std::string& astRelPath,
                     MaterialAssetDesc& out,
                     std::string* err = nullptr);
};
