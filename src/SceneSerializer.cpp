#include "SceneSerializer.hpp"
#include "Application.hpp"
#include "SceneManager.hpp"
#include "MaterialManager.hpp"
#include "MaterialAssetLoader.hpp"
#include "BufferManager.hpp"
#include "CommandManager.hpp"
#include "FramebufferManager.hpp"
#include "PipelineManager.hpp"
#include "VulkanContext.hpp"
#include "Transform.hpp"
#include "camera.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

using json = nlohmann::json;

namespace {

// 把 MaterialParams 中与 .ast 参考值不同的字段写到 JSON object
// 返回写入的字段数；0 表示没有改动
int writeParamOverride(json& ov, const MaterialParams& cur, const MaterialParams& ref)
{
    int n = 0;
    if (cur.baseColor != ref.baseColor)         { ov["baseColor"]         = {cur.baseColor.x, cur.baseColor.y, cur.baseColor.z, cur.baseColor.w}; ++n; }
    if (cur.roughness  != ref.roughness)         { ov["roughness"]         = cur.roughness;          ++n; }
    if (cur.metallic   != ref.metallic)           { ov["metallic"]          = cur.metallic;           ++n; }
    if (cur.emissiveIntensity != ref.emissiveIntensity) { ov["emissiveIntensity"] = cur.emissiveIntensity; ++n; }
    if (cur.emissiveColor != ref.emissiveColor)   { ov["emissiveColor"]    = {cur.emissiveColor.x, cur.emissiveColor.y, cur.emissiveColor.z, cur.emissiveColor.w}; ++n; }
    return n;
}

void applyParamOverride(const json& ov, MaterialParams& p)
{
    if (ov.contains("baseColor") && ov["baseColor"].is_array() && ov["baseColor"].size() >= 4)
        p.baseColor = { ov["baseColor"][0].get<float>(), ov["baseColor"][1].get<float>(),
                        ov["baseColor"][2].get<float>(), ov["baseColor"][3].get<float>() };
    if (ov.contains("roughness"))         p.roughness         = ov["roughness"].get<float>();
    if (ov.contains("metallic"))          p.metallic          = ov["metallic"].get<float>();
    if (ov.contains("emissiveIntensity")) p.emissiveIntensity = ov["emissiveIntensity"].get<float>();
    if (ov.contains("emissiveColor") && ov["emissiveColor"].is_array() && ov["emissiveColor"].size() >= 3) {
        p.emissiveColor = { ov["emissiveColor"][0].get<float>(), ov["emissiveColor"][1].get<float>(),
                            ov["emissiveColor"][2].get<float>(), 1.f };
    }
}

} // namespace

static bool writeJson(const std::string& path, const json& j)
{
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "[SceneSerializer] Cannot write: " << path << "\n";
        return false;
    }
    out << j.dump(2);
    return true;
}

static bool readJson(const std::string& path, json& j)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[SceneSerializer] Cannot read: " << path << "\n";
        return false;
    }
    try { in >> j; } catch (const std::exception& e) {
        std::cerr << "[SceneSerializer] JSON parse error: " << e.what() << "\n";
        return false;
    }
    return true;
}

bool SceneSerializer::save(const std::string& path,
                           const SceneManager& sceneMgr,
                           const MaterialManager& matMgr,
                           const Camera& camera)
{
    json j;
    j["version"] = 1;

    // ── Entities ─────────────────────────────────────────────────────────
    auto ents = json::array();
    for (const auto& ent : sceneMgr.getModelEntities()) {
        json e;

        // 优先保存 .ast 路径（新设计），回退到 displayName（兼容旧实体）
        if (!ent.astRelPath.empty()) {
            e["astRelPath"] = ent.astRelPath;
        } else if (!ent.displayName.empty()) {
            e["displayName"] = ent.displayName;
        } else {
            continue;
        }

        e["position"]    = { ent.transform.position.x,
                             ent.transform.position.y,
                             ent.transform.position.z };
        e["rotation"]    = { ent.transform.rotation.x,
                             ent.transform.rotation.y,
                             ent.transform.rotation.z,
                             ent.transform.rotation.w };
        e["scale"]       = { ent.transform.scale.x,
                             ent.transform.scale.y,
                             ent.transform.scale.z };
        e["visible"]     = ent.visible;

        // 有 astRelPath 的实体：对比当前材质与 .ast 参考值，如有修改则存 materialOverride
        if (!ent.astRelPath.empty() && matMgr.isValid(ent.materialId)) {
            MaterialAssetDesc ref;
            if (MaterialAssetLoader::load(ent.astRelPath, ref)) {
                json ov;
                const MaterialParams& cur = matMgr.getParams(ent.materialId);
                int paramCount = writeParamOverride(ov, cur, ref.params);

                // 纹理路径对比
                const std::string curAlbedo = matMgr.getAlbedoPath(ent.materialId);
                const std::string curNormal = matMgr.getNormalPath(ent.materialId);
                if (curAlbedo != ref.albedoPath && (!curAlbedo.empty() || !ref.albedoPath.empty())) {
                    ov["albedoPath"] = curAlbedo; ++paramCount;
                }
                if (curNormal != ref.normalPath && (!curNormal.empty() || !ref.normalPath.empty())) {
                    ov["normalPath"] = curNormal; ++paramCount;
                }

                if (paramCount > 0)
                    e["materialOverride"] = ov;

                // ── 子材质覆写：对比 subMeshMaterials 与 subMaterials 参考 ──
                if (!ent.subMeshMaterials.empty() && !ref.subMaterialPaths.empty()) {
                    json smo = json::array();
                    const size_t slotCount = std::min(ent.subMeshMaterials.size(), ref.subMaterialPaths.size());
                    for (size_t si = 0; si < slotCount; ++si) {
                        const uint32_t slotMatId = ent.subMeshMaterials[si];
                        if (slotMatId == 0 || !matMgr.isValid(slotMatId)) continue;

                        // 加载该槽位的 .ast 参考
                        const std::string& slotAstRel = ref.subMaterialPaths[si];
                        if (slotAstRel.empty()) continue;

                        MaterialAssetDesc slotRef;
                        json slotOv;
                        int overrideCount = 0;

                        if (MaterialAssetLoader::load(slotAstRel, slotRef)) {
                            const MaterialParams& slotCur = matMgr.getParams(slotMatId);
                            overrideCount = writeParamOverride(slotOv, slotCur, slotRef.params);

                            const std::string sAlbedo = matMgr.getAlbedoPath(slotMatId);
                            const std::string sNormal = matMgr.getNormalPath(slotMatId);
                            if (sAlbedo != slotRef.albedoPath && (!sAlbedo.empty() || !slotRef.albedoPath.empty())) {
                                slotOv["albedoPath"] = sAlbedo; ++overrideCount;
                            }
                            if (sNormal != slotRef.normalPath && (!sNormal.empty() || !slotRef.normalPath.empty())) {
                                slotOv["normalPath"] = sNormal; ++overrideCount;
                            }
                        }

                        json entry;
                        entry["slot"] = static_cast<int>(si);
                        entry["astRelPath"] = slotAstRel;
                        if (overrideCount > 0)
                            entry["override"] = slotOv;
                        smo.push_back(entry);
                    }
                    if (!smo.empty())
                        e["subMaterialOverrides"] = smo;
                }
            }
        } else if (ent.astRelPath.empty()) {
            // 无 .ast 的实体：保存 materialId
            e["materialId"] = ent.materialId;
        }

        ents.push_back(e);
    }
    j["entities"] = ents;

    // ── Boxes ────────────────────────────────────────────────────────────
    auto boxes = json::array();
    for (const auto& kv : sceneMgr.getBoxes()) {
        json b;
        b["entityId"] = kv.first;
        b["position"] = { kv.second.x, kv.second.y, kv.second.z };
        boxes.push_back(b);
    }
    j["boxes"] = boxes;

    // ── Camera ───────────────────────────────────────────────────────────
    json cam;
    cam["position"]    = { camera.Position.x, camera.Position.y, camera.Position.z };
    const auto& orient = camera.GetOrientation();
    cam["orientation"] = { orient.x, orient.y, orient.z, orient.w };
    cam["speed"]       = camera.SPEED;
    cam["fov"]         = camera.FovDeg;
    j["camera"]        = cam;

    return writeJson(path, j);
}

bool SceneSerializer::load(const std::string& path,
                           Application& app,
                           SceneManager& sceneMgr,
                           MaterialManager& matMgr,
                           BufferManager& bufMgr,
                           VulkanContext& ctx,
                           CommandManager& cmdMgr,
                           FramebufferManager& fbMgr,
                           PipelineManager& pipeMgr,
                           Camera& camera,
                           const std::string& resRoot)
{
    json j;
    if (!readJson(path, j)) return false;

    // ── 清空现有场景 ────────────────────────────────────────────────────
    {
        // Collect ids first to avoid modifying while iterating
        std::vector<uint64_t> ids;
        for (const auto& ent : sceneMgr.getModelEntities())
            ids.push_back(ent.entityId);
        for (uint64_t eid : ids)
            sceneMgr.removeModelEntity(eid, ctx);
    }

    // ── 加载实体 ────────────────────────────────────────────────────────
    if (j.contains("entities")) {
        for (const auto& ej : j["entities"]) {
            // 优先通过 astRelPath 加载（新设计），回退到 displayName 扫描（兼容旧场景文件）
            std::string modelPath;

            if (ej.contains("astRelPath") && ej["astRelPath"].is_string()) {
                const std::string astRel = ej["astRelPath"].get<std::string>();
                const std::string fullAstPath = resRoot + "/" + astRel;

                // 读取 .ast 获取 model 字段
                std::ifstream af(fullAstPath);
                if (af.is_open()) {
                    try {
                        nlohmann::json aj;
                        af >> aj;
                        if (aj.contains("model")) {
                            const auto& m = aj["model"];
                            std::string relModel;
                            if (m.is_string())
                                relModel = m.get<std::string>();
                            else if (m.is_object() && m.contains("path"))
                                relModel = m["path"].get<std::string>();
                            if (!relModel.empty())
                                modelPath = resRoot + "/" + relModel;
                        }
                    } catch (...) {}
                }
                if (modelPath.empty()) {
                    std::cerr << "[SceneSerializer] Cannot find model in .ast: " << astRel << "\n";
                    continue;
                }
            } else if (ej.contains("displayName") && ej["displayName"].is_string()) {
                // 兼容旧格式：按 displayName 扫描 res/models/
                const std::string dname = ej["displayName"].get<std::string>();
                if (dname.empty()) continue;

                const std::filesystem::path modelsDir = resRoot + "/models";
                std::error_code ec;
                if (std::filesystem::is_directory(modelsDir, ec)) {
                    for (const auto& entry : std::filesystem::directory_iterator(modelsDir, ec)) {
                        if (!entry.is_regular_file(ec)) continue;
                        const std::string stem = entry.path().stem().string();
                        if (stem == dname) {
                            modelPath = entry.path().string();
                            break;
                        }
                    }
                }
                if (modelPath.empty()) {
                    std::cerr << "[SceneSerializer] Cannot find model for: " << dname << "\n";
                    continue;
                }
            } else {
                continue;
            }

            glm::vec3 pos(0.f);
            if (ej.contains("position"))
                pos = { ej["position"][0].get<float>(),
                        ej["position"][1].get<float>(),
                        ej["position"][2].get<float>() };

            uint64_t eid = sceneMgr.createModelEntity(modelPath, pos, bufMgr);
            auto* ent = sceneMgr.getModelEntity(eid);
            if (!ent) continue;

            // 恢复 .ast 资产路径（如果来自 astRelPath 加载）
            if (ej.contains("astRelPath") && ej["astRelPath"].is_string())
                ent->astRelPath = ej["astRelPath"].get<std::string>();

            // Restore transform
            if (ej.contains("rotation")) {
                ent->transform.rotation = glm::quat(
                    ej["rotation"][3].get<float>(),  // w
                    ej["rotation"][0].get<float>(),  // x
                    ej["rotation"][1].get<float>(),  // y
                    ej["rotation"][2].get<float>()   // z
                );
                sceneMgr.setEntityTransform(eid, ent->transform);
            }
            if (ej.contains("scale")) {
                ent->transform.scale = {
                    ej["scale"][0].get<float>(),
                    ej["scale"][1].get<float>(),
                    ej["scale"][2].get<float>()
                };
                sceneMgr.setEntityTransform(eid, ent->transform);
            }
            if (ej.contains("visible"))
                ent->visible = ej["visible"].get<bool>();
            if (ej.contains("materialId") && ej["materialId"].get<uint32_t>() != 0) {
                ent->materialId = ej["materialId"].get<uint32_t>();
                sceneMgr.setEntityMaterial(eid, ent->materialId);
            }

            // 从 .ast 加载材质，如有 materialOverride 则叠加
            if (!ent->astRelPath.empty()) {
                const MaterialId mid = matMgr.loadMaterialFromAsset(
                    ent->astRelPath, ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
                if (mid != kInvalidMaterialId) {
                    ent->materialId = mid;
                    for (auto& slotMat : ent->subMeshMaterials)
                        slotMat = mid;

                    // 应用主材质覆写
                    if (ej.contains("materialOverride") && ej["materialOverride"].is_object()) {
                        const auto& ov = ej["materialOverride"];
                        applyParamOverride(ov, matMgr.getParamsMut(mid));

                        if (ov.contains("albedoPath") && ov["albedoPath"].is_string())
                            matMgr.setAlbedoPath(mid, ov["albedoPath"].get<std::string>(),
                                                  ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
                        if (ov.contains("normalPath") && ov["normalPath"].is_string())
                            matMgr.setNormalPath(mid, ov["normalPath"].get<std::string>(),
                                                  ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
                    }

                    // ── 加载子材质（subMaterials）───────────────────────
                    // 读取入口 .ast 获取 subMaterials 列表
                    {
                        MaterialAssetDesc entryDesc;
                        if (MaterialAssetLoader::load(ent->astRelPath, entryDesc)
                            && !entryDesc.subMaterialPaths.empty()) {
                            if (ent->subMeshMaterials.size() < entryDesc.subMaterialPaths.size())
                                ent->subMeshMaterials.resize(entryDesc.subMaterialPaths.size(), mid);
                            for (size_t si = 0; si < entryDesc.subMaterialPaths.size()
                                              && si < ent->subMeshMaterials.size(); ++si) {
                                const MaterialId smMid = matMgr.loadMaterialFromAsset(
                                    entryDesc.subMaterialPaths[si],
                                    ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
                                if (smMid != kInvalidMaterialId)
                                    ent->subMeshMaterials[si] = smMid;
                            }
                        }
                    }

                    // ── 应用子材质覆写（来自场景文件）───────────────────
                    if (ej.contains("subMaterialOverrides") && ej["subMaterialOverrides"].is_array()) {
                        for (const auto& se : ej["subMaterialOverrides"]) {
                            if (!se.contains("slot")) continue;
                            const int slot = se["slot"].get<int>();
                            if (slot < 0 || static_cast<size_t>(slot) >= ent->subMeshMaterials.size())
                                continue;
                            const uint32_t smId = ent->subMeshMaterials[slot];
                            if (smId == 0 || !matMgr.isValid(smId)) continue;

                            if (se.contains("override") && se["override"].is_object()) {
                                const auto& sov = se["override"];
                                applyParamOverride(sov, matMgr.getParamsMut(smId));
                                if (sov.contains("albedoPath") && sov["albedoPath"].is_string())
                                    matMgr.setAlbedoPath(smId, sov["albedoPath"].get<std::string>(),
                                                          ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
                                if (sov.contains("normalPath") && sov["normalPath"].is_string())
                                    matMgr.setNormalPath(smId, sov["normalPath"].get<std::string>(),
                                                          ctx, cmdMgr, bufMgr, fbMgr, pipeMgr);
                            }
                        }
                    }
                }
            }
        }
    }

    // ── 加载 Box ────────────────────────────────────────────────────────
    if (j.contains("boxes")) {
        for (const auto& bj : j["boxes"]) {
            glm::vec3 pos(0.f);
            if (bj.contains("position"))
                pos = { bj["position"][0].get<float>(),
                        bj["position"][1].get<float>(),
                        bj["position"][2].get<float>() };
            app.addBox(pos);
        }
    }

    // ── 恢复相机 ────────────────────────────────────────────────────────
    if (j.contains("camera")) {
        const auto& cj = j["camera"];
        if (cj.contains("position"))
            camera.Position = { cj["position"][0].get<float>(),
                                cj["position"][1].get<float>(),
                                cj["position"][2].get<float>() };
        if (cj.contains("orientation") && cj["orientation"].is_array() && cj["orientation"].size() >= 4) {
            glm::quat q{
                cj["orientation"][3].get<float>(),  // w
                cj["orientation"][0].get<float>(),  // x
                cj["orientation"][1].get<float>(),  // y
                cj["orientation"][2].get<float>()   // z
            };
            camera.SetOrientation(q);
        } else {
            // 兼容旧格式：pitch/yaw
            float pitch = cj.value("pitch", 0.f);
            float yaw   = cj.value("yaw", 0.f);
            camera.SetPitchYaw(pitch, yaw);
        }
        if (cj.contains("speed")) camera.SPEED = cj["speed"].get<float>();
        if (cj.contains("fov"))   camera.FovDeg = cj["fov"].get<float>();
    }

    std::cout << "[SceneSerializer] Loaded scene: " << path << "\n";
    return true;
}
