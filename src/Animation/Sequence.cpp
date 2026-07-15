#include "Sequence.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
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
    result.evalTime = t;

    if (keyframes.empty()) {
        return result;
    }

    std::unordered_map<std::string, AnimatorParam> paramMap;

    for (const auto& kf : keyframes) {
        if (kf.time > t) break;

        for (const auto& ev : kf.events) {
            AnimatorParam param;
            param.name = ev.paramName;

            switch (ev.type) {
            case AnimatorEvent::Type::SetFloat: {
                param.type = AnimatorParam::Type::Float;
                auto it = paramMap.find(ev.paramName);
                float prevVal = (it != paramMap.end() && it->second.type == AnimatorParam::Type::Float)
                                    ? it->second.value.f : 0.f;

                size_t nextKfIdx = 0;
                for (size_t i = 0; i < keyframes.size(); ++i) {
                    if (keyframes[i].time > kf.time) { nextKfIdx = i; break; }
                }

                bool hasInterp = (nextKfIdx > 0 && ev.interp != ParamInterp::Step);
                float nextVal = ev.floatValue;

                if (hasInterp && nextKfIdx < keyframes.size()) {
                    const auto& nextKf = keyframes[nextKfIdx];
                    for (const auto& nextEv : nextKf.events) {
                        if (nextEv.paramName == ev.paramName && nextEv.type == AnimatorEvent::Type::SetFloat) {
                            nextVal = nextEv.floatValue;
                            double span = nextKf.time - kf.time;
                            if (span > 0.0) {
                                double localT = (t - kf.time) / span;
                                localT = applyParamInterpCurve(localT, ev.interp);
                                param.value.f = static_cast<float>(prevVal + (nextVal - prevVal) * localT);
                            } else {
                                param.value.f = ev.floatValue;
                            }
                            break;
                        }
                    }
                } else {
                    param.value.f = ev.floatValue;
                }
                break;
            }
            case AnimatorEvent::Type::SetInt: {
                param.type = AnimatorParam::Type::Int;
                auto it = paramMap.find(ev.paramName);
                int prevVal = (it != paramMap.end() && it->second.type == AnimatorParam::Type::Int)
                                  ? it->second.value.i : 0;

                size_t nextKfIdx = 0;
                for (size_t i = 0; i < keyframes.size(); ++i) {
                    if (keyframes[i].time > kf.time) { nextKfIdx = i; break; }
                }

                bool hasInterp = (nextKfIdx > 0 && ev.interp != ParamInterp::Step);
                int nextVal = ev.intValue;

                if (hasInterp && nextKfIdx < keyframes.size()) {
                    const auto& nextKf = keyframes[nextKfIdx];
                    for (const auto& nextEv : nextKf.events) {
                        if (nextEv.paramName == ev.paramName && nextEv.type == AnimatorEvent::Type::SetInt) {
                            nextVal = nextEv.intValue;
                            double span = nextKf.time - kf.time;
                            if (span > 0.0) {
                                double localT = (t - kf.time) / span;
                                localT = applyParamInterpCurve(localT, ev.interp);
                                param.value.i = static_cast<int>(prevVal + (nextVal - prevVal) * localT);
                            } else {
                                param.value.i = ev.intValue;
                            }
                            break;
                        }
                    }
                } else {
                    param.value.i = ev.intValue;
                }
                break;
            }
            case AnimatorEvent::Type::SetBool:
                param.type = AnimatorParam::Type::Bool;
                param.value.b = ev.boolValue;
                break;
            case AnimatorEvent::Type::SetTrigger:
                param.type = AnimatorParam::Type::Trigger;
                param.value.b = true;
                break;
            }

            paramMap[ev.paramName] = param;
        }
    }

    for (const auto& [name, param] : paramMap) {
        result.params.push_back(param);
    }

    return result;
}