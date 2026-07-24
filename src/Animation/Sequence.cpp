#include "Sequence.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <glm/gtc/quaternion.hpp>

namespace {

constexpr double kKeyTimeEpsilon = 1e-8;
constexpr float kVectorEpsilonSq = 1e-10f;
constexpr float kMaxAutoTangentFactor = 1.5f;
constexpr glm::vec3 kLocalForwardAxis{0.0f, 0.0f, 1.0f};

bool isFinite(const glm::vec3& value)
{
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

glm::vec3 normalizedOr(const glm::vec3& value, const glm::vec3& fallback)
{
    const float lengthSq = glm::dot(value, value);
    if (isFinite(value) && lengthSq > kVectorEpsilonSq)
        return value / std::sqrt(lengthSq);

    const float fallbackLengthSq = glm::dot(fallback, fallback);
    if (isFinite(fallback) && fallbackLengthSq > kVectorEpsilonSq)
        return fallback / std::sqrt(fallbackLengthSq);

    return kLocalForwardAxis;
}

glm::vec3 positionChordAt(
    const std::vector<TransformKeyframe>& keys,
    size_t keyIndex)
{
    if (keys.size() < 2)
        return kLocalForwardAxis;
    if (keyIndex == 0)
        return keys[1].position - keys[0].position;
    if (keyIndex + 1 >= keys.size())
        return keys[keyIndex].position - keys[keyIndex - 1].position;
    return keys[keyIndex + 1].position - keys[keyIndex - 1].position;
}

glm::vec3 autoTangentDirection(
    const std::vector<TransformKeyframe>& keys,
    size_t keyIndex)
{
    const glm::vec3 chordDirection =
        normalizedOr(positionChordAt(keys, keyIndex), kLocalForwardAxis);

    const glm::quat& rotation = keys[keyIndex].rotation;
    const float rotationLengthSq = glm::dot(rotation, rotation);
    if (!std::isfinite(rotationLengthSq)
        || rotationLengthSq <= kVectorEpsilonSq) {
        return chordDirection;
    }

    glm::vec3 rotationAxis =
        normalizedOr(
            glm::normalize(rotation) * kLocalForwardAxis,
            chordDirection);

    // Imported models commonly use +Z as forward while cameras use -Z.
    // Pick the sign that follows the local motion chord so Cubic Auto is
    // independent of that convention and cannot create a backwards loop.
    if (glm::dot(rotationAxis, chordDirection) < 0.0f)
        rotationAxis = -rotationAxis;
    return rotationAxis;
}

float segmentSpeed(
    const TransformKeyframe& first,
    const TransformKeyframe& second)
{
    const double timeSpan = second.time - first.time;
    if (timeSpan <= kKeyTimeEpsilon)
        return 0.0f;

    const float distance = glm::length(second.position - first.position);
    if (!std::isfinite(distance))
        return 0.0f;
    return distance / static_cast<float>(timeSpan);
}

float curvatureFactor(const glm::vec3& first, const glm::vec3& second)
{
    const float dotValue = glm::clamp(glm::dot(first, second), -1.0f, 1.0f);
    const float angle = std::acos(dotValue);
    const float cosQuarterAngle = std::cos(angle * 0.25f);
    const float denominator = cosQuarterAngle * cosQuarterAngle;
    if (!std::isfinite(denominator) || denominator <= 1e-4f)
        return kMaxAutoTangentFactor;
    return glm::clamp(1.0f / denominator, 1.0f, kMaxAutoTangentFactor);
}

glm::vec3 autoTangentVelocity(
    const std::vector<TransformKeyframe>& keys,
    size_t keyIndex)
{
    float speedSum = 0.0f;
    int speedCount = 0;
    if (keyIndex > 0) {
        speedSum += segmentSpeed(keys[keyIndex - 1], keys[keyIndex]);
        ++speedCount;
    }
    if (keyIndex + 1 < keys.size()) {
        speedSum += segmentSpeed(keys[keyIndex], keys[keyIndex + 1]);
        ++speedCount;
    }
    if (speedCount == 0)
        return glm::vec3(0.0f);

    const glm::vec3 direction = autoTangentDirection(keys, keyIndex);
    float factorSum = 0.0f;
    int factorCount = 0;
    if (keyIndex > 0) {
        factorSum += curvatureFactor(
            autoTangentDirection(keys, keyIndex - 1), direction);
        ++factorCount;
    }
    if (keyIndex + 1 < keys.size()) {
        factorSum += curvatureFactor(
            direction, autoTangentDirection(keys, keyIndex + 1));
        ++factorCount;
    }

    const float averageSpeed = speedSum / static_cast<float>(speedCount);
    const float factor = factorCount > 0
        ? factorSum / static_cast<float>(factorCount)
        : 1.0f;
    return direction * averageSpeed * factor;
}

glm::vec3 cubicAutoPosition(
    const std::vector<TransformKeyframe>& keys,
    size_t firstIndex,
    float normalizedTime)
{
    const TransformKeyframe& first = keys[firstIndex];
    const TransformKeyframe& second = keys[firstIndex + 1];
    const float timeSpan =
        static_cast<float>(std::max(second.time - first.time, 0.0));

    const glm::vec3 tangent0 =
        autoTangentVelocity(keys, firstIndex) * timeSpan;
    const glm::vec3 tangent1 =
        autoTangentVelocity(keys, firstIndex + 1) * timeSpan;

    const float t2 = normalizedTime * normalizedTime;
    const float t3 = t2 * normalizedTime;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + normalizedTime;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;

    const glm::vec3 position =
        h00 * first.position + h10 * tangent0
        + h01 * second.position + h11 * tangent1;
    if (!isFinite(position))
        return glm::mix(first.position, second.position, normalizedTime);
    return position;
}

} // namespace

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
    if (cameraShotTrack)
        maxEnd = std::max(maxEnd, cameraShotTrack->totalDuration());
    return maxEnd > 0.0 ? maxEnd : 0.0;
}

const CameraShotKeyframe* CameraShotTrack::evaluate(double t) const
{
    if (keyframes.empty() || t < keyframes.front().time)
        return nullptr;

    const auto next = std::upper_bound(
        keyframes.begin(), keyframes.end(), t,
        [](double value, const CameraShotKeyframe& key) {
            return value < key.time;
        });
    return next == keyframes.begin() ? nullptr : &*std::prev(next);
}

double CameraShotTrack::totalDuration() const
{
    return keyframes.empty() ? 0.0 : keyframes.back().time;
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
    normalizedT = std::clamp(normalizedT, 0.0, 1.0);

    if (prev.easeToNext == TweenEase::Cubic) {
        const float cubicT = static_cast<float>(normalizedT);
        const float smoothT =
            cubicT * cubicT * (3.0f - 2.0f * cubicT);
        result.position = cubicAutoPosition(keyframes, prevIdx, cubicT);
        result.rotation = glm::normalize(
            glm::slerp(prev.rotation, next.rotation, smoothT));
        result.scale = glm::mix(prev.scale, next.scale, smoothT);
    } else {
        normalizedT = applyEaseCurve(normalizedT, prev.easeToNext);
        result.position = glm::mix(
            prev.position, next.position, static_cast<float>(normalizedT));
        result.rotation = glm::normalize(
            glm::slerp(
                prev.rotation, next.rotation,
                static_cast<float>(normalizedT)));
        result.scale = glm::mix(
            prev.scale, next.scale, static_cast<float>(normalizedT));
    }
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
