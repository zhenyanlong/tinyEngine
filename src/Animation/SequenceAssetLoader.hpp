#pragma once

#include "Sequence.hpp"

#include <memory>
#include <string>
#include <vector>

struct CameraKeyframe {
    double      time;               ///< 时间（秒）
    glm::vec3   position{0.f};
    glm::quat   orientation{1.f, 0.f, 0.f, 0.f};
    float       fovDeg = 60.f;
};

struct CameraPath {
    std::string name;
    int         interpolation = 0;  ///< 0=CatmullRom, 1=Linear
    std::vector<CameraKeyframe> keyframes;

    /** @brief 在时间 t 求值相机姿态 */
    struct EvalResult {
        glm::vec3 position{0.f};
        glm::quat orientation{1.f, 0.f, 0.f, 0.f};
        float     fovDeg = 60.f;
    };
    EvalResult evaluate(double t) const;
};

class SequenceAssetLoader {
public:
    static constexpr const char* kResRoot = "res/";

    static void setResRoot(const std::string& resRoot);
    static std::string getResRoot();

    // ── Sequence (.seq.json) ────────────────────────────────────────────
    static bool saveSequence(const std::string& jsonRelPath,
                             const Sequence& seq,
                             std::string* err = nullptr);

    static bool loadSequence(const std::string& jsonRelPath,
                             Sequence& out,
                             std::string* err = nullptr);

    // ── CameraPath (.campath.json) ─────────────────────────────────────
    static bool saveCameraPath(const std::string& jsonRelPath,
                               const CameraPath& path,
                               std::string* err = nullptr);

    static bool loadCameraPath(const std::string& jsonRelPath,
                               CameraPath& out,
                               std::string* err = nullptr);

    // ── 工具 ──────────────────────────────────────────────────────────────
    static std::string easeToStr(TweenEase e);
    static TweenEase strToEase(const std::string& s);

private:
    static std::string trackTypeToStr(TrackType t);
    static TrackType strToTrackType(const std::string& s);
};