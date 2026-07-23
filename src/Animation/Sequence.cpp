#include "Sequence.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <glm/gtc/quaternion.hpp>

double SequenceTrack::totalDuration() const
{
    if (type == TrackType::Group || type == TrackType::AnimationClip || type == TrackType::Event)
        return 0.0;
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
    if (type == TrackType::AnimatorKeyframe) {
        maxEnd = std::max(maxEnd, animatorTrack.totalDuration());
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

double AnimatorKeyframeTrack::totalDuration() const
{
    if (keyframes.empty()) return 0.0;
    return keyframes.back().time;
}

AnimatorKeyframeTrack::EvalResult AnimatorKeyframeTrack::evaluate(double t) const
{
    EvalResult result;
    result.initialState = initialState;
    result.evalTime = std::max(0.0, t);

    constexpr double kStep = 0.05;
    constexpr double kTimeEpsilon = 1e-6;
    std::vector<double> sampleTimes{0.0, result.evalTime};
    for (double sampleTime = kStep; sampleTime < result.evalTime; sampleTime += kStep)
        sampleTimes.push_back(sampleTime);
    for (const auto& kf : keyframes) {
        if (kf.time >= 0.0 && kf.time <= result.evalTime)
            sampleTimes.push_back(kf.time);
    }
    std::sort(sampleTimes.begin(), sampleTimes.end());
    sampleTimes.erase(std::unique(sampleTimes.begin(), sampleTimes.end(),
        [](double a, double b) { return std::abs(a - b) <= kTimeEpsilon; }), sampleTimes.end());

    auto findNextMatching = [&](double afterTime, const AnimatorParamEvent& source,
                                double& nextTime) -> const AnimatorParamEvent* {
        for (const auto& kf : keyframes) {
            if (kf.time <= afterTime + kTimeEpsilon) continue;
            for (const auto& candidate : kf.events) {
                if (candidate.paramName == source.paramName && candidate.type == source.type) {
                    nextTime = kf.time;
                    return &candidate;
                }
            }
        }
        return nullptr;
    };

    auto samplePersistentParams = [&](double sampleTime) {
        struct TimedEventRef { double time = 0.0; const AnimatorParamEvent* event = nullptr; };
        std::unordered_map<std::string, TimedEventRef> latest;
        for (const auto& kf : keyframes) {
            if (kf.time > sampleTime + kTimeEpsilon) break;
            for (const auto& ev : kf.events) {
                if (ev.type != AnimatorEvent::Type::SetTrigger && !ev.paramName.empty())
                    latest[ev.paramName] = TimedEventRef{kf.time, &ev};
            }
        }

        std::vector<AnimatorParam> params;
        params.reserve(latest.size());
        for (const auto& [name, timed] : latest) {
            const auto& ev = *timed.event;
            AnimatorParam param;
            param.name = name;
            switch (ev.type) {
            case AnimatorEvent::Type::SetFloat: {
                param.type = AnimatorParam::Type::Float;
                param.value.f = ev.floatValue;
                double nextTime = 0.0;
                if (ev.interp != ParamInterp::Step) {
                    if (const auto* next = findNextMatching(timed.time, ev, nextTime)) {
                        const double span = nextTime - timed.time;
                        const double alpha = span > kTimeEpsilon
                            ? applyParamInterpCurve((sampleTime - timed.time) / span, ev.interp)
                            : 0.0;
                        param.value.f = static_cast<float>(ev.floatValue
                            + (next->floatValue - ev.floatValue) * alpha);
                    }
                }
                break;
            }
            case AnimatorEvent::Type::SetInt: {
                param.type = AnimatorParam::Type::Int;
                param.value.i = ev.intValue;
                double nextTime = 0.0;
                if (ev.interp != ParamInterp::Step) {
                    if (const auto* next = findNextMatching(timed.time, ev, nextTime)) {
                        const double span = nextTime - timed.time;
                        const double alpha = span > kTimeEpsilon
                            ? applyParamInterpCurve((sampleTime - timed.time) / span, ev.interp)
                            : 0.0;
                        param.value.i = static_cast<int>(std::lround(ev.intValue
                            + (next->intValue - ev.intValue) * alpha));
                    }
                }
                break;
            }
            case AnimatorEvent::Type::SetBool:
                param.type = AnimatorParam::Type::Bool;
                param.value.b = ev.boolValue;
                break;
            case AnimatorEvent::Type::SetTrigger:
                continue;
            }
            params.push_back(param);
        }
        std::sort(params.begin(), params.end(),
                  [](const AnimatorParam& a, const AnimatorParam& b) { return a.name < b.name; });
        return params;
    };

    for (const double sampleTime : sampleTimes) {
        AnimatorTimelineSample sample;
        sample.time = static_cast<float>(sampleTime);
        sample.params = samplePersistentParams(sampleTime);
        for (const auto& kf : keyframes) {
            if (std::abs(kf.time - sampleTime) > kTimeEpsilon) continue;
            for (const auto& ev : kf.events) {
                if (ev.type == AnimatorEvent::Type::SetTrigger && !ev.paramName.empty())
                    sample.triggers.push_back(ev.paramName);
            }
        }
        result.timeline.push_back(std::move(sample));
    }

    if (!result.timeline.empty())
        result.params = result.timeline.back().params;
    return result;
}
