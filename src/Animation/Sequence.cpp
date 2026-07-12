#include "Sequence.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

double SequenceTrack::totalDuration() const
{
    double maxEnd = 0.0;
    auto endTime = [](const SequenceClipBase& c) { return c.startTime + c.duration; };

    for (const auto& c : animClips)
        maxEnd = std::max(maxEnd, endTime(c));
    for (const auto& c : cameraPathClips)
        maxEnd = std::max(maxEnd, endTime(c));
    for (const auto& c : tweenClips)
        maxEnd = std::max(maxEnd, endTime(c));
    for (const auto& c : eventClips)
        maxEnd = std::max(maxEnd, endTime(c));

    if (type == TrackType::TransformKeyframe) {
        maxEnd = std::max(maxEnd, keyframeTrack.totalDuration());
    }

    return maxEnd;
}

double Sequence::computeTotalDuration() const
{
    double maxEnd = 0.0;
    for (const auto& track : tracks)
        maxEnd = std::max(maxEnd, track.totalDuration());
    return maxEnd > 0.0 ? maxEnd : 0.0;
}

TransformTweenClip::EvalResult TransformTweenClip::evaluate(double localT) const
{
    EvalResult result;
    double dur = duration > 0.0 ? duration : 1.0;
    double rawT = std::clamp(localT / dur, 0.0, 1.0);
    double t = applyEaseCurve(rawT, ease);

    result.position = glm::mix(startPosition, endPosition, static_cast<float>(t));
    result.rotation = glm::normalize(glm::slerp(startRotation, endRotation, static_cast<float>(t)));
    result.scale = glm::mix(startScale, endScale, static_cast<float>(t));

    return result;
}

double TransformKeyframeTrack::totalDuration() const
{
    if (keyframes.empty()) return 0.0;
    return keyframes.back().time;
}

TransformKeyframeTrack::EvalResult TransformKeyframeTrack::evaluate(double t) const
{
    EvalResult result;

    if (keyframes.empty()) {
        result.valid = false;
        return result;
    }

    if (keyframes.size() == 1) {
        result.position = keyframes[0].position;
        result.rotation = keyframes[0].rotation;
        result.scale = keyframes[0].scale;
        result.valid = true;
        return result;
    }

    if (t <= keyframes.front().time) {
        result.position = keyframes.front().position;
        result.rotation = keyframes.front().rotation;
        result.scale = keyframes.front().scale;
        result.valid = true;
        return result;
    }

    if (t >= keyframes.back().time) {
        result.position = keyframes.back().position;
        result.rotation = keyframes.back().rotation;
        result.scale = keyframes.back().scale;
        result.valid = true;
        return result;
    }

    size_t prevIdx = 0;
    for (size_t i = 1; i < keyframes.size(); ++i) {
        if (keyframes[i].time > t) {
            prevIdx = i - 1;
            break;
        }
    }

    const TransformKeyframe& prev = keyframes[prevIdx];
    const TransformKeyframe& next = keyframes[prevIdx + 1];

    const double timeSpan = next.time - prev.time;
    double normalizedT = (timeSpan > 0.0) ? (t - prev.time) / timeSpan : 0.0;
    normalizedT = applyEaseCurve(normalizedT, prev.easeToNext);

    result.position = glm::mix(prev.position, next.position, static_cast<float>(normalizedT));
    result.rotation = glm::normalize(glm::slerp(prev.rotation, next.rotation, static_cast<float>(normalizedT)));
    result.scale = glm::mix(prev.scale, next.scale, static_cast<float>(normalizedT));
    result.valid = true;

    return result;
}