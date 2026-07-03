#include "SequenceAssetLoader.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <glm/gtc/quaternion.hpp>

using nlohmann::json;

namespace {

std::filesystem::path gResRoot = SequenceAssetLoader::kResRoot;

std::string resolveResPath(const std::string& rel)
{
    const std::filesystem::path relPath(rel);
    if (relPath.is_absolute())
        return relPath.string();
    return (gResRoot / relPath).string();
}

const char* trackTypeName(TrackType t)
{
    switch (t) {
    case TrackType::AnimationClip:  return "AnimationClip";
    case TrackType::CameraPath:     return "CameraPath";
    case TrackType::TransformTween: return "TransformTween";
    case TrackType::Event:          return "Event";
    }
    return "AnimationClip";
}

TrackType trackTypeFromName(const std::string& s)
{
    if (s == "CameraPath")     return TrackType::CameraPath;
    if (s == "TransformTween") return TrackType::TransformTween;
    if (s == "Event")          return TrackType::Event;
    return TrackType::AnimationClip;
}

json vec3ToJson(const glm::vec3& v)
{
    return json::array({ v.x, v.y, v.z });
}

glm::vec3 vec3FromJson(const json& j)
{
    if (j.is_array() && j.size() >= 3)
        return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    return glm::vec3(0.f);
}

json quatToJson(const glm::quat& q)
{
    return json::array({ q.w, q.x, q.y, q.z });
}

glm::quat quatFromJson(const json& j)
{
    if (j.is_array() && j.size() >= 4)
        return glm::quat(j[0].get<float>(), j[1].get<float>(),
                         j[2].get<float>(), j[3].get<float>());
    return glm::quat(1.f, 0.f, 0.f, 0.f);
}

// ── CameraPath evaluate ────────────────────────────────────────────────
// Catmull-Rom 插值辅助
static float catmullRom1D(float p0, float p1, float p2, float p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.f * p1) +
                   (-p0 + p2) * t +
                   (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                   (-p0 + 3.f * p1 - 3.f * p2 + p3) * t3);
}

static glm::vec3 catmullRomVec3(const glm::vec3& p0, const glm::vec3& p1,
                                 const glm::vec3& p2, const glm::vec3& p3, float t)
{
    return glm::vec3(
        catmullRom1D(p0.x, p1.x, p2.x, p3.x, t),
        catmullRom1D(p0.y, p1.y, p2.y, p3.y, t),
        catmullRom1D(p0.z, p1.z, p2.z, p3.z, t));
}

} // anonymous namespace

// ── CameraPath ─────────────────────────────────────────────────────────

CameraPath::EvalResult CameraPath::evaluate(double t) const
{
    EvalResult result;
    if (keyframes.empty()) return result;

    // 夹紧到有效范围
    t = std::clamp(t, keyframes.front().time, keyframes.back().time);

    // 找到 t 所在区间
    size_t seg = 0;
    for (size_t i = 0; i + 1 < keyframes.size(); ++i) {
        if (t >= keyframes[i].time && t <= keyframes[i + 1].time) {
            seg = i;
            break;
        }
    }

    if (seg + 1 >= keyframes.size()) {
        // 超出范围，返回最后一个
        result = { keyframes.back().position, keyframes.back().orientation, keyframes.back().fovDeg };
        return result;
    }

    const auto& k0 = keyframes[seg];
    const auto& k1 = keyframes[seg + 1];
    const double segLen = k1.time - k0.time;
    const float localT = segLen > 0.0 ? static_cast<float>((t - k0.time) / segLen) : 0.f;

    if (interpolation == 1) {
        // Linear
        result.position = glm::mix(k0.position, k1.position, localT);
        result.orientation = glm::slerp(k0.orientation, k1.orientation, localT);
        result.fovDeg = glm::mix(k0.fovDeg, k1.fovDeg, localT);
    } else {
        // Catmull-Rom
        const auto& p0 = seg > 0 ? keyframes[seg - 1] : k0;
        const auto& p3 = seg + 2 < keyframes.size() ? keyframes[seg + 2] : k1;
        result.position = catmullRomVec3(p0.position, k0.position, k1.position, p3.position, localT);
        result.orientation = glm::slerp(k0.orientation, k1.orientation, localT);
        result.fovDeg = glm::mix(k0.fovDeg, k1.fovDeg, localT);
    }

    return result;
}

// ── SequenceAssetLoader ────────────────────────────────────────────────

void SequenceAssetLoader::setResRoot(const std::string& resRoot)
{
    gResRoot = std::filesystem::path(resRoot);
}

std::string SequenceAssetLoader::getResRoot()
{
    return gResRoot.string();
}

// ── saveSequence ───────────────────────────────────────────────────────

bool SequenceAssetLoader::saveSequence(const std::string& jsonRelPath,
                                       const Sequence& seq,
                                       std::string* err)
{
    const std::string fullPath = resolveResPath(jsonRelPath);

    json j;
    j["version"] = 1;
    j["name"] = seq.name;
    j["totalDuration"] = seq.totalDuration;

    auto& jTracks = j["tracks"];
    for (const auto& track : seq.tracks) {
        json jt;
        jt["name"] = track.name;
        jt["type"] = trackTypeName(track.type);

        auto& jAnimClips = jt["animClips"];
        for (const auto& c : track.animClips) {
            json jc;
            jc["name"] = c.name;
            jc["startTime"] = c.startTime;
            jc["duration"] = c.duration;
            jc["clipName"] = c.clipName;
            jc["clipOffset"] = c.clipOffset;
            jc["playSpeed"] = c.playSpeed;
            jAnimClips.push_back(std::move(jc));
        }

        auto& jCamClips = jt["cameraPathClips"];
        for (const auto& c : track.cameraPathClips) {
            json jc;
            jc["name"] = c.name;
            jc["startTime"] = c.startTime;
            jc["duration"] = c.duration;
            jc["pathAsset"] = c.pathAssetRelPath;
            jCamClips.push_back(std::move(jc));
        }

        auto& jTweenClips = jt["tweenClips"];
        for (const auto& c : track.tweenClips) {
            json jc;
            jc["name"] = c.name;
            jc["startTime"] = c.startTime;
            jc["duration"] = c.duration;
            jc["startPosition"] = vec3ToJson(c.startPosition);
            jc["startRotation"] = quatToJson(c.startRotation);
            jc["startScale"] = vec3ToJson(c.startScale);
            jc["endPosition"] = vec3ToJson(c.endPosition);
            jc["endRotation"] = quatToJson(c.endRotation);
            jc["endScale"] = vec3ToJson(c.endScale);
            jc["ease"] = easeToStr(c.ease);
            jTweenClips.push_back(std::move(jc));
        }

        auto& jEventClips = jt["eventClips"];
        for (const auto& c : track.eventClips) {
            json jc;
            jc["name"] = c.name;
            jc["startTime"] = c.startTime;
            jc["duration"] = c.duration;
            jc["eventName"] = c.eventName;
            jEventClips.push_back(std::move(jc));
        }

        jTracks.push_back(std::move(jt));
    }

    std::ofstream out(fullPath);
    if (!out.is_open()) {
        if (err) *err = "Cannot open file for writing: " + fullPath;
        return false;
    }
    out << j.dump(2) << '\n';
    if (!out.good()) {
        if (err) *err = "Write failed: " + fullPath;
        return false;
    }
    return true;
}

// ── loadSequence ───────────────────────────────────────────────────────

bool SequenceAssetLoader::loadSequence(const std::string& jsonRelPath,
                                       Sequence& out,
                                       std::string* err)
{
    const std::string fullPath = resolveResPath(jsonRelPath);
    std::ifstream in(fullPath);
    if (!in.is_open()) {
        if (err) *err = "Cannot open file: " + fullPath;
        return false;
    }

    json j;
    try { in >> j; } catch (...) {
        if (err) *err = "JSON parse error: " + fullPath;
        return false;
    }

    if (!j.is_object()) {
        if (err) *err = "Invalid JSON root (not object): " + fullPath;
        return false;
    }

    out.name = j.value("name", std::string{});
    out.totalDuration = j.value("totalDuration", 0.0);
    out.tracks.clear();

    if (j.contains("tracks") && j["tracks"].is_array()) {
        for (const auto& jt : j["tracks"]) {
            SequenceTrack track;
            track.name = jt.value("name", std::string{});
            track.type = trackTypeFromName(jt.value("type", std::string{"AnimationClip"}));

            // animClips
            if (jt.contains("animClips") && jt["animClips"].is_array()) {
                for (const auto& jc : jt["animClips"]) {
                    AnimTrackClip c;
                    c.name = jc.value("name", std::string{});
                    c.startTime = jc.value("startTime", 0.0);
                    c.duration = jc.value("duration", 0.0);
                    c.clipName = jc.value("clipName", std::string{});
                    c.clipOffset = jc.value("clipOffset", 0.0);
                    c.playSpeed = jc.value("playSpeed", 1.0);
                    track.animClips.push_back(std::move(c));
                }
            }

            // cameraPathClips
            if (jt.contains("cameraPathClips") && jt["cameraPathClips"].is_array()) {
                for (const auto& jc : jt["cameraPathClips"]) {
                    CameraPathClip c;
                    c.name = jc.value("name", std::string{});
                    c.startTime = jc.value("startTime", 0.0);
                    c.duration = jc.value("duration", 0.0);
                    c.pathAssetRelPath = jc.value("pathAsset", std::string{});
                    track.cameraPathClips.push_back(std::move(c));
                }
            }

            // tweenClips
            if (jt.contains("tweenClips") && jt["tweenClips"].is_array()) {
                for (const auto& jc : jt["tweenClips"]) {
                    TransformTweenClip c;
                    c.name = jc.value("name", std::string{});
                    c.startTime = jc.value("startTime", 0.0);
                    c.duration = jc.value("duration", 0.0);
                    if (jc.contains("startPosition")) c.startPosition = vec3FromJson(jc["startPosition"]);
                    if (jc.contains("startRotation")) c.startRotation = quatFromJson(jc["startRotation"]);
                    if (jc.contains("startScale"))    c.startScale = vec3FromJson(jc["startScale"]);
                    if (jc.contains("endPosition"))   c.endPosition = vec3FromJson(jc["endPosition"]);
                    if (jc.contains("endRotation"))   c.endRotation = quatFromJson(jc["endRotation"]);
                    if (jc.contains("endScale"))      c.endScale = vec3FromJson(jc["endScale"]);
                    c.ease = strToEase(jc.value("ease", std::string{"SmoothStep"}));
                    track.tweenClips.push_back(std::move(c));
                }
            }

            // eventClips
            if (jt.contains("eventClips") && jt["eventClips"].is_array()) {
                for (const auto& jc : jt["eventClips"]) {
                    EventClip c;
                    c.name = jc.value("name", std::string{});
                    c.startTime = jc.value("startTime", 0.0);
                    c.duration = jc.value("duration", 0.0);
                    c.eventName = jc.value("eventName", std::string{});
                    track.eventClips.push_back(std::move(c));
                }
            }

            out.tracks.push_back(std::move(track));
        }
    }

    return true;
}

// ── saveCameraPath ─────────────────────────────────────────────────────

bool SequenceAssetLoader::saveCameraPath(const std::string& jsonRelPath,
                                         const CameraPath& path,
                                         std::string* err)
{
    const std::string fullPath = resolveResPath(jsonRelPath);

    json j;
    j["version"] = 1;
    j["name"] = path.name;
    j["interpolation"] = path.interpolation;

    auto& jKeys = j["keyframes"];
    for (const auto& kf : path.keyframes) {
        json jk;
        jk["time"] = kf.time;
        jk["position"] = vec3ToJson(kf.position);
        jk["orientation"] = quatToJson(kf.orientation);
        jk["fovDeg"] = kf.fovDeg;
        jKeys.push_back(std::move(jk));
    }

    std::ofstream out(fullPath);
    if (!out.is_open()) {
        if (err) *err = "Cannot open file for writing: " + fullPath;
        return false;
    }
    out << j.dump(2) << '\n';
    if (!out.good()) {
        if (err) *err = "Write failed: " + fullPath;
        return false;
    }
    return true;
}

// ── loadCameraPath ─────────────────────────────────────────────────────

bool SequenceAssetLoader::loadCameraPath(const std::string& jsonRelPath,
                                         CameraPath& out,
                                         std::string* err)
{
    const std::string fullPath = resolveResPath(jsonRelPath);
    std::ifstream in(fullPath);
    if (!in.is_open()) {
        if (err) *err = "Cannot open file: " + fullPath;
        return false;
    }

    json j;
    try { in >> j; } catch (...) {
        if (err) *err = "JSON parse error: " + fullPath;
        return false;
    }

    if (!j.is_object()) {
        if (err) *err = "Invalid JSON root (not object): " + fullPath;
        return false;
    }

    out.name = j.value("name", std::string{});
    out.interpolation = j.value("interpolation", 0);
    out.keyframes.clear();

    if (j.contains("keyframes") && j["keyframes"].is_array()) {
        for (const auto& jk : j["keyframes"]) {
            CameraKeyframe kf;
            kf.time = jk.value("time", 0.0);
            if (jk.contains("position"))    kf.position = vec3FromJson(jk["position"]);
            if (jk.contains("orientation")) kf.orientation = quatFromJson(jk["orientation"]);
            kf.fovDeg = jk.value("fovDeg", 60.f);
            out.keyframes.push_back(std::move(kf));
        }
    }

    // 按时间排序
    std::sort(out.keyframes.begin(), out.keyframes.end(),
              [](const CameraKeyframe& a, const CameraKeyframe& b) {
                  return a.time < b.time;
              });

    return true;
}

// ── 工具函数 ───────────────────────────────────────────────────────────

std::string SequenceAssetLoader::easeToStr(TweenEase e)
{
    switch (e) {
    case TweenEase::Linear:     return "Linear";
    case TweenEase::SmoothStep: return "SmoothStep";
    case TweenEase::EaseIn:     return "EaseIn";
    case TweenEase::EaseOut:    return "EaseOut";
    }
    return "SmoothStep";
}

TweenEase SequenceAssetLoader::strToEase(const std::string& s)
{
    if (s == "Linear")     return TweenEase::Linear;
    if (s == "EaseIn")     return TweenEase::EaseIn;
    if (s == "EaseOut")    return TweenEase::EaseOut;
    return TweenEase::SmoothStep;
}