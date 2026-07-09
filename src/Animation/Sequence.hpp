#pragma once

#include "AnimationClip.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

/** @brief 轨道类型 */
enum class TrackType {
    AnimationClip,  ///< 引用一段 .anim.ast 动画片段
    CameraPath,     ///< 相机路径（播放期间屏蔽右键相机控制）
    TransformTween, ///< 场景对象 position/rotation/scale 补间
    Event           ///< 时间点触发 AnimatorEvent
};

/** @brief 插值模式 */
enum class TweenEase {
    Linear,
    SmoothStep,
    EaseIn,
    EaseOut
};

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

/** @brief Transform 补间片段，对场景对象做 TRS 关键帧插值 */
struct TransformTweenClip : SequenceClipBase {
    // 起始 pose
    glm::vec3 startPosition{0.f};
    glm::quat startRotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 startScale{1.f};

    // 结束 pose
    glm::vec3 endPosition{0.f};
    glm::quat endRotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 endScale{1.f};

    TweenEase ease = TweenEase::SmoothStep;

    struct EvalResult {
        glm::vec3 position{0.f};
        glm::quat rotation{1.f, 0.f, 0.f, 0.f};
        glm::vec3 scale{1.f};
    };

    /** @brief 在 localT（从 startTime 起的绝对秒数）处求值 */
    EvalResult evaluate(double localT) const;
};

/** @brief 事件片段，到达时间点时触发一个 AnimatorEvent */
struct EventClip : SequenceClipBase {
    std::string eventName;  ///< 事件名称，供 Sequencer 查询并触发 dispatchEvent
};

/** @brief 一条轨道，内部按 startTime 升序存放同一类型的片段 */
struct SequenceTrack {
    std::string name;
    TrackType   type = TrackType::AnimationClip;

    std::vector<AnimTrackClip>      animClips;
    std::vector<CameraPathClip>     cameraPathClips;
    std::vector<TransformTweenClip> tweenClips;
    std::vector<EventClip>          eventClips;

    /** @brief 获取该轨道的总时长（最长 clip 的 startTime + duration） */
    double totalDuration() const;
};

/** @brief 完整的时间轴 Sequence */
struct Sequence {
    std::string name;
    double      totalDuration = 0.0;  ///< 可覆盖，否则自动计算
    std::vector<SequenceTrack> tracks;

    /** @brief 计算所有轨道中最大的 endTime */
    double computeTotalDuration() const;
};