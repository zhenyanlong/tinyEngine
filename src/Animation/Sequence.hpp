#pragma once

#include "AnimationClip.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <string>
#include <vector>

/** @brief 轨道类型 */
enum class TrackType {
    AnimationClip,      ///< 引用一段 .anim.ast 动画片段
    CameraPath,         ///< 相机路径（播放期间屏蔽右键相机控制）
    TransformTween,     ///< 场景对象 position/rotation/scale 补间（旧版，保留兼容）
    TransformKeyframe,  ///< 关键帧轨道（新版）
    Event               ///< 时间点触发 AnimatorEvent
};

/** @brief 插值模式 */
enum class TweenEase {
    Linear,             ///< 线性插值
    SmoothStep,         ///< 平滑插值（Hermite）
    EaseIn,             ///< 缓入（二次方）
    EaseOut,            ///< 缓出（二次方）
    EaseInOut,          ///< 缓入缓出组合
    Cubic,              ///< 三次方插值
    Exponential         ///< 指数插值
};

/** @brief 获取 TweenEase 的显示名称 */
inline const char* tweenEaseToString(TweenEase ease) {
    switch (ease) {
    case TweenEase::Linear:      return "Linear";
    case TweenEase::SmoothStep:  return "SmoothStep";
    case TweenEase::EaseIn:      return "EaseIn";
    case TweenEase::EaseOut:     return "EaseOut";
    case TweenEase::EaseInOut:   return "EaseInOut";
    case TweenEase::Cubic:       return "Cubic";
    case TweenEase::Exponential: return "Exponential";
    }
    return "Unknown";
}

/** @brief 应用混合曲线，输入 t 范围 [0, 1]，返回结果范围 [0, 1] */
inline double applyEaseCurve(double t, TweenEase ease) {
    t = (t < 0.0) ? 0.0 : (t > 1.0) ? 1.0 : t;
    switch (ease) {
    case TweenEase::Linear:
        return t;
    case TweenEase::SmoothStep:
        return t * t * (3.0 - 2.0 * t);
    case TweenEase::EaseIn:
        return t * t;
    case TweenEase::EaseOut:
        return 1.0 - (1.0 - t) * (1.0 - t);
    case TweenEase::EaseInOut:
        return (t < 0.5)
            ? 2.0 * t * t
            : 1.0 - (-2.0 * t + 2.0) * (-2.0 * t + 2.0) / 4.0;
    case TweenEase::Cubic:
        return t * t * t;
    case TweenEase::Exponential:
        return (t <= 0.0) ? 0.0 : std::pow(2.0, 10.0 * (t - 1.0));
    }
    return t;
}

/** @brief 基类，所有 SequenceClip 的公共头部 */
struct SequenceClipBase {
    std::string name;
    double      startTime = 0.0;  ///< 在 Sequence 时间轴上的起始时间（秒）
    double      duration  = 0.0;  ///< 播放时长（秒）
};

/** @brief 动画片段轨道，引用一个 AnimationClip */
struct AnimTrackClip : SequenceClipBase {
    std::string clipName;    ///< 引用 entity->animationClips[i].name
    double      clipOffset = 0.0;  ///< 从 AnimationClip 的哪个时间点开始
    double      playSpeed  = 1.0;  ///< 播放倍速
};

/** @brief 相机路径片段，引用一个 CameraPath 资产 */
struct CameraPathClip : SequenceClipBase {
    std::string pathAssetRelPath;  ///< 相对于 res/ 的 .campath.json 路径
};

/** @brief Transform 补间片段，对场景对象做 TRS 关键帧插值（旧版，保留兼容） */
struct TransformTweenClip : SequenceClipBase {
    glm::vec3 startPosition{0.f};
    glm::quat startRotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 startScale{1.f};

    glm::vec3 endPosition{0.f};
    glm::quat endRotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 endScale{1.f};

    TweenEase ease = TweenEase::SmoothStep;

    struct EvalResult {
        glm::vec3 position{0.f};
        glm::quat rotation{1.f, 0.f, 0.f, 0.f};
        glm::vec3 scale{1.f};
    };

    EvalResult evaluate(double localT) const;
};

/** @brief 事件片段，到达时间点时触发一个 AnimatorEvent */
struct EventClip : SequenceClipBase {
    std::string eventName;
};

/** @brief 关键帧：记录某一时刻的 Transform 状态 */
struct TransformKeyframe {
    double    time = 0.0;          ///< 在序列时间轴上的位置（秒）
    glm::vec3 position{0.f};
    glm::quat rotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 scale{1.f};
    TweenEase easeToNext = TweenEase::Linear;  ///< 到下一个关键帧的混合模式
};

/** @brief 关键帧轨道：包含多个关键帧，绑定到特定实体 */
struct TransformKeyframeTrack {
    std::string  name;
    uint64_t     targetEntityId = 0;  ///< 关联的场景实体 ID
    std::vector<TransformKeyframe> keyframes;  ///< 按 time 升序排列

    struct EvalResult {
        glm::vec3 position{0.f};
        glm::quat rotation{1.f, 0.f, 0.f, 0.f};
        glm::vec3 scale{1.f};
        bool valid = false;  ///< 是否有有效求值结果
    };

    /** @brief 在指定时间 t 求值，返回混合后的 Transform */
    EvalResult evaluate(double t) const;

    /** @brief 获取轨道总时长（最后一个关键帧的时间） */
    double totalDuration() const;
};

/** @brief 一条轨道，内部按 startTime 升序存放同一类型的片段 */
struct SequenceTrack {
    std::string name;
    TrackType   type = TrackType::AnimationClip;

    std::vector<AnimTrackClip>      animClips;
    std::vector<CameraPathClip>     cameraPathClips;
    std::vector<TransformTweenClip> tweenClips;
    std::vector<EventClip>          eventClips;

    TransformKeyframeTrack          keyframeTrack;  ///< 新版关键帧轨道

    double totalDuration() const;
};

/** @brief 完整的时间轴 Sequence */
struct Sequence {
    std::string name;
    double      totalDuration = 0.0;  ///< 可覆盖，否则自动计算
    std::vector<SequenceTrack> tracks;

    double computeTotalDuration() const;
};