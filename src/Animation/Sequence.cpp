#include "Sequence.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

static double applyEaseImpl(double t, TweenEase ease)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (ease) {
    case TweenEase::Linear:
        return t;
    case TweenEase::SmoothStep:
        return t * t * (3.0 - 2.0 * t);
    case TweenEase::EaseIn:
        return t * t;
    case TweenEase::EaseOut:
        return 1.0 - (1.0 - t) * (1.0 - t);
    }
    return t;
}

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
    double t = applyEaseImpl(rawT, ease);

    result.position = glm::mix(startPosition, endPosition, static_cast<float>(t));
    result.rotation = glm::normalize(glm::slerp(startRotation, endRotation, static_cast<float>(t)));
    result.scale = glm::mix(startScale, endScale, static_cast<float>(t));

    return result;
}