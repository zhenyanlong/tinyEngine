#include "AnimatorController.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <utility>

namespace {

constexpr float kFloatEpsilon = 1e-5f;

bool nearlyEqual(float a, float b) {
    return std::abs(a - b) <= kFloatEpsilon;
}

bool exitTimeReachedForProgress(
    const AnimatorTransition& transition,
    const AnimatorState& state,
    float previousProgress,
    float currentProgress)
{
    if (!transition.hasExitTime) return true;

    const float requestedProgress = std::max(transition.exitTime, 0.f);
    if (!state.loop) {
        // A non-looping clip holds its final pose after normalized progress 1.
        return currentProgress >= std::min(requestedProgress, 1.f);
    }
    if (requestedProgress <= kFloatEpsilon) return true;

    // Values above one are cumulative: 2.0 means two complete loops, 3.0
    // means three. Once reached, keep the exit gate open for conditions.
    if (requestedProgress > 1.f)
        return currentProgress >= requestedProgress;

    // Values in (0, 1] are phase gates and are checked again every loop.
    const float elapsed = std::max(0.f, currentProgress - previousProgress);
    if (elapsed >= 1.f) return true;

    const float previousWrapped =
        previousProgress - std::floor(previousProgress);
    const float currentWrapped =
        currentProgress - std::floor(currentProgress);
    const bool wrapped = currentWrapped < previousWrapped;
    return wrapped
        ? requestedProgress > previousWrapped
            || requestedProgress <= currentWrapped
        : previousWrapped < requestedProgress
            && currentWrapped >= requestedProgress;
}

} // namespace

void AnimatorParam::setFloat(float v) {
    if (type == Type::Float) value.f = v;
}

void AnimatorParam::setInt(int v) {
    if (type == Type::Int) value.i = v;
}

void AnimatorParam::setBool(bool v) {
    if (type == Type::Bool) value.b = v;
}

void AnimatorParam::setTrigger() {
    if (type == Type::Trigger) value.b = true;
}

bool AnimatorParam::checkCondition(const TransitionCondition& condition) const {
    switch (type) {
    case Type::Float:
        switch (condition.op) {
        case TransitionCondition::Op::Greater:  return value.f > condition.threshold;
        case TransitionCondition::Op::Less:     return value.f < condition.threshold;
        case TransitionCondition::Op::Equal:    return nearlyEqual(value.f, condition.threshold);
        case TransitionCondition::Op::NotEqual: return !nearlyEqual(value.f, condition.threshold);
        case TransitionCondition::Op::True:     return !nearlyEqual(value.f, 0.f);
        case TransitionCondition::Op::False:    return nearlyEqual(value.f, 0.f);
        }
        break;
    case Type::Int: {
        const int threshold = static_cast<int>(condition.threshold);
        switch (condition.op) {
        case TransitionCondition::Op::Greater:  return value.i > threshold;
        case TransitionCondition::Op::Less:     return value.i < threshold;
        case TransitionCondition::Op::Equal:    return value.i == threshold;
        case TransitionCondition::Op::NotEqual: return value.i != threshold;
        case TransitionCondition::Op::True:     return value.i != 0;
        case TransitionCondition::Op::False:    return value.i == 0;
        }
        break;
    }
    case Type::Bool:
    case Type::Trigger: {
        const bool threshold = !nearlyEqual(condition.threshold, 0.f);
        switch (condition.op) {
        case TransitionCondition::Op::Equal:    return value.b == threshold;
        case TransitionCondition::Op::NotEqual: return value.b != threshold;
        case TransitionCondition::Op::True:     return value.b;
        case TransitionCondition::Op::False:    return !value.b;
        case TransitionCondition::Op::Greater:
        case TransitionCondition::Op::Less:     return false;
        }
        break;
    }
    }
    return false;
}

void AnimatorController::configure(std::vector<AnimatorState> states,
                                   std::vector<AnimatorTransition> transitions,
                                   std::vector<AnimatorParam> params,
                                   const std::string& defaultState) {
    states_ = std::move(states);
    transitions_ = std::move(transitions);
    params_ = std::move(params);

    defaultState_ = defaultState;
    if (!findState(defaultState_) && !states_.empty()) {
        defaultState_ = states_.front().name;
    }
    reset();
    bumpDefinitionRevision();
}

void AnimatorController::configureFromClips(const std::vector<AnimationClip>& clips) {
    std::vector<AnimatorState> states;
    states.reserve(clips.size());
    for (const auto& clip : clips) {
        AnimatorState state;
        state.name = clip.name.empty() ? "Clip" + std::to_string(states.size()) : clip.name;
        state.clipName = clip.name;
        states.push_back(std::move(state));
    }
    configure(std::move(states), {}, {});
}

void AnimatorController::setFloat(const std::string& name, float v) {
    if (AnimatorParam* param = findParam(name)) param->setFloat(v);
}

void AnimatorController::setInt(const std::string& name, int v) {
    if (AnimatorParam* param = findParam(name)) param->setInt(v);
}

void AnimatorController::setBool(const std::string& name, bool v) {
    if (AnimatorParam* param = findParam(name)) param->setBool(v);
}

void AnimatorController::setTrigger(const std::string& name) {
    if (AnimatorParam* param = findParam(name)) param->setTrigger();
}

void AnimatorController::dispatchEvent(const AnimatorEvent& event) {
    switch (event.type) {
    case AnimatorEvent::Type::SetFloat:   setFloat(event.paramName, event.floatValue); break;
    case AnimatorEvent::Type::SetInt:     setInt(event.paramName, event.intValue); break;
    case AnimatorEvent::Type::SetBool:    setBool(event.paramName, event.boolValue); break;
    case AnimatorEvent::Type::SetTrigger: setTrigger(event.paramName); break;
    }
}

void AnimatorController::dispatchEvents(const std::vector<AnimatorEvent>& events) {
    for (const auto& e : events) dispatchEvent(e);
}

AnimatorController::BlendCommand AnimatorController::update(
    float dt, const std::vector<AnimationClip>& clips) {
    BlendCommand command;
    if (states_.empty()) return command;

    const AnimatorState* current = findState(currentState_);
    if (!current) {
        reset();
        current = findState(currentState_);
        if (!current) return command;
    }

    const float safeDt = std::max(dt, 0.f);
    const float previousProgress = stateProgress_;
    bool stateChangedThisFrame = false;
    const float currentDuration = effectiveDuration(*current, clips, params_, stateProgress_);
    if (currentDuration > kFloatEpsilon) {
        stateProgress_ += safeDt * std::max(current->playRate, 0.f) / currentDuration;
    }

    if (transitioning_) {
        const AnimatorState* next = findState(nextState_);
        if (!next) {
            transitioning_ = false;
            nextState_.clear();
            blendT_ = 0.f;
        } else {
            const float nextDuration = effectiveDuration(*next, clips, params_, nextStateProgress_);
            if (nextDuration > kFloatEpsilon) {
                nextStateProgress_ += safeDt * std::max(next->playRate, 0.f) / nextDuration;
            }
            blendT_ = activeFadeDuration_ <= kFloatEpsilon
                ? 1.f
                : std::min(1.f, blendT_ + safeDt / activeFadeDuration_);

            if (blendT_ >= 1.f) {
                currentState_ = nextState_;
                stateProgress_ = nextStateProgress_;
                nextState_.clear();
                nextStateProgress_ = 0.f;
                activeFadeDuration_ = 0.f;
                blendT_ = 0.f;
                transitioning_ = false;
                current = findState(currentState_);
                stateChangedThisFrame = true;
            }
        }
    }

    if (!transitioning_ && current && !stateChangedThisFrame) {
        for (const auto& transition : transitions_) {
            if (!transition.fromState.empty() && transition.fromState != currentState_)
                continue;
            if (transition.toState == currentState_)
                continue;

            const AnimatorState* target = findState(transition.toState);
            if (!target || !hasValidMotion(*target, clips))
                continue;
            if (transition.hasExitTime && !hasValidMotion(*current, clips))
                continue;
            if (!exitTimeReached(transition, *current, previousProgress)
                || !checkAllConditions(transition))
                continue;

            nextState_ = target->name;
            nextStateProgress_ = 0.f;
            blendT_ = 0.f;
            activeFadeDuration_ = std::max(transition.fadeDuration, 0.f);
            activeBlendCurve_ = transition.blendCurve;
            transitioning_ = true;
            consumeTriggers(transition);

            if (activeFadeDuration_ <= kFloatEpsilon) {
                currentState_ = nextState_;
                stateProgress_ = 0.f;
                nextState_.clear();
                nextStateProgress_ = 0.f;
                activeFadeDuration_ = 0.f;
                transitioning_ = false;
                current = findState(currentState_);
            }
            break;
        }
    }

    current = findState(currentState_);
    if (!current) return command;

    command.stateA = current;
    command.poseA = resolveStatePose(*current, clips, params_, stateProgress_);

    if (transitioning_) {
        const AnimatorState* next = findState(nextState_);
        if (next) {
            command.stateB = next;
            command.poseB = resolveStatePose(*next, clips, params_, nextStateProgress_);
            const float raw = std::clamp(blendT_, 0.f, 1.f);
            command.blendWeight = applyBlendCurve(raw, activeBlendCurve_);
        }
    }
    return command;
}

AnimatorController::BlendCommand AnimatorController::computeBlendAtTime(
    float t, const std::vector<AnimationClip>& clips,
    const std::vector<AnimatorParam>& paramValues,
    const std::string& initialState) const
{
    AnimatorTimelineSample start;
    start.time = 0.f;
    start.params = paramValues;
    std::vector<AnimatorTimelineSample> timeline{start};
    if (t > 0.f) {
        AnimatorTimelineSample end = start;
        end.time = t;
        timeline.push_back(std::move(end));
    }
    return computeBlendAtTime(t, clips, timeline, initialState);
}

AnimatorController::BlendCommand AnimatorController::computeBlendAtTime(
    float t, const std::vector<AnimationClip>& clips,
    const std::vector<AnimatorTimelineSample>& inputTimeline,
    const std::string& initialState) const
{
    BlendCommand command;
    if (states_.empty()) return command;
    t = std::max(t, 0.f);

    // 确定起始状态
    std::string curState = initialState.empty() ? defaultState_ : initialState;
    const AnimatorState* current = findState(curState);
    if (!current) {
        // 回退到第一个状态
        if (states_.empty()) return command;
        curState = states_.front().name;
        current = &states_.front();
    }

    // 状态机和参数全部使用本地副本，seek/拖动不会污染运行时 Animator。
    std::vector<AnimatorParam> localParams = params_;

    // 模拟状态机随时间演进，从 0 到 t
    float stateProgress = 0.f;
    float nextStateProgress = 0.f;
    float blendT = 0.f;
    float activeFadeDuration = 0.f;
    BlendCurve activeBlendCurve = BlendCurve::SmoothStep;
    bool transitioning = false;
    std::string nextState;
    bool consumed = false;  // 是否已检查过本轮的过渡条件

    std::vector<AnimatorTimelineSample> timeline;
    timeline.reserve(inputTimeline.size() + 2);
    for (const auto& sample : inputTimeline) {
        if (sample.time >= 0.f && sample.time <= t + kFloatEpsilon)
            timeline.push_back(sample);
    }
    std::stable_sort(timeline.begin(), timeline.end(),
                     [](const AnimatorTimelineSample& a, const AnimatorTimelineSample& b) {
                         return a.time < b.time;
                     });
    if (timeline.empty() || timeline.front().time > kFloatEpsilon)
        timeline.insert(timeline.begin(), AnimatorTimelineSample{});
    if (timeline.back().time < t - kFloatEpsilon) {
        AnimatorTimelineSample end;
        end.time = t;
        timeline.push_back(std::move(end));
    }

    auto applySample = [&](const AnimatorTimelineSample& sample) {
        for (auto& param : localParams) {
            if (param.type == AnimatorParam::Type::Trigger)
                param.value.b = false;
        }
        for (const auto& value : sample.params) {
            for (auto& param : localParams) {
                if (param.name == value.name && param.type == value.type) {
                    param.value = value.value;
                    break;
                }
            }
        }
        for (const auto& triggerName : sample.triggers) {
            for (auto& param : localParams) {
                if (param.name == triggerName && param.type == AnimatorParam::Type::Trigger) {
                    param.value.b = true;
                    break;
                }
            }
        }
    };

    float elapsed = 0.f;
    for (const auto& sample : timeline) {
        const float targetTime = std::clamp(sample.time, elapsed, t);
        const float step = targetTime - elapsed;
        const float safeDt = std::max(step, 0.f);
        const float previousProgress = stateProgress;
        bool stateChangedThisStep = false;

        current = findState(curState);
        if (!current) break;

        // 播放进度始终正向累积；Forward/Reverse 只影响最终采样时间。
        const float currentDuration = effectiveDuration(
            *current, clips, localParams, stateProgress);
        if (currentDuration > kFloatEpsilon) {
            stateProgress += safeDt * std::max(current->playRate, 0.f) / currentDuration;
        }

        if (transitioning) {
            const AnimatorState* nextSt = findState(nextState);
            if (!nextSt) {
                transitioning = false;
                nextState.clear();
                blendT = 0.f;
            } else {
                const float nextDuration = effectiveDuration(
                    *nextSt, clips, localParams, nextStateProgress);
                if (nextDuration > kFloatEpsilon) {
                    nextStateProgress += safeDt * std::max(nextSt->playRate, 0.f) / nextDuration;
                }
                blendT = activeFadeDuration <= kFloatEpsilon
                    ? 1.f
                    : std::min(1.f, blendT + safeDt / activeFadeDuration);

                if (blendT >= 1.f) {
                    // 过渡完成
                    curState = nextState;
                    stateProgress = nextStateProgress;
                    nextState.clear();
                    nextStateProgress = 0.f;
                    activeFadeDuration = 0.f;
                    blendT = 0.f;
                    transitioning = false;
                    consumed = false;
                    stateChangedThisStep = true;
                }
            }
        }

        // 持续参数先应用，Trigger 随后作为单次脉冲，再检查过渡。
        applySample(sample);

        // 检查新的过渡（仅在非过渡状态或过渡刚完成时）
        if (!transitioning && !consumed && !stateChangedThisStep) {
            current = findState(curState);
            if (!current) break;

            for (const auto& transition : transitions_) {
                if (!transition.fromState.empty() && transition.fromState != curState)
                    continue;
                if (transition.toState == curState)
                    continue;

                const AnimatorState* target = findState(transition.toState);
                if (!target || !hasValidMotion(*target, clips))
                    continue;

                // Exit Time uses forward accumulated progress even for Reverse
                // sampling. The transition-completion step is skipped above so
                // previousProgress always belongs to this same current state.
                const bool exitOk =
                    (!transition.hasExitTime
                     || hasValidMotion(*current, clips))
                    && exitTimeReachedForProgress(
                        transition, *current,
                        previousProgress, stateProgress);

                if (!exitOk) continue;

                // 检查条件（使用本地参数副本）
                bool allConditionsMet = true;
                for (const auto& cond : transition.conditions) {
                    const AnimatorParam* p = nullptr;
                    for (const auto& lp : localParams) {
                        if (lp.name == cond.paramName) { p = &lp; break; }
                    }
                    if (!p || !p->checkCondition(cond)) {
                        allConditionsMet = false;
                        break;
                    }
                }
                if (!allConditionsMet) continue;

                // 触发过渡
                nextState = target->name;
                nextStateProgress = 0.f;
                blendT = 0.f;
                activeFadeDuration = std::max(transition.fadeDuration, 0.f);
                activeBlendCurve = transition.blendCurve;
                transitioning = true;

                // 消费 trigger（在本地参数副本中）
                for (const auto& cond : transition.conditions) {
                    for (auto& lp : localParams) {
                        if (lp.name == cond.paramName && lp.type == AnimatorParam::Type::Trigger) {
                            lp.value.b = false;
                            break;
                        }
                    }
                }

                // 零时长过渡
                if (activeFadeDuration <= kFloatEpsilon) {
                    curState = nextState;
                    stateProgress = 0.f;
                    nextState.clear();
                    nextStateProgress = 0.f;
                    activeFadeDuration = 0.f;
                    blendT = 0.f;
                    transitioning = false;
                }
                break;
            }
            consumed = true;
        }

        // 如果不在过渡中，下个步进可以继续检查过渡
        if (!transitioning) consumed = false;

        // Trigger 只对当前时间边界的一次过渡判断有效，不能累积为永久 true。
        for (auto& param : localParams) {
            if (param.type == AnimatorParam::Type::Trigger)
                param.value.b = false;
        }

        elapsed = targetTime;
        if (elapsed >= t - kFloatEpsilon) break;
    }

    // 构建 BlendCommand
    current = findState(curState);
    if (!current) return command;

    command.stateA = current;
    command.poseA = resolveStatePose(*current, clips, localParams, stateProgress);

    if (transitioning) {
        const AnimatorState* nextSt = findState(nextState);
        if (nextSt) {
            command.stateB = nextSt;
            command.poseB = resolveStatePose(
                *nextSt, clips, localParams, nextStateProgress);
            const float raw = std::clamp(blendT, 0.f, 1.f);
            command.blendWeight = applyBlendCurve(raw, activeBlendCurve);
        }
    }

    return command;
}

void AnimatorController::reset() {
    currentState_ = defaultState_;
    nextState_.clear();
    stateProgress_ = 0.f;
    nextStateProgress_ = 0.f;
    blendT_ = 0.f;
    activeFadeDuration_ = 0.f;
    activeBlendCurve_ = BlendCurve::SmoothStep;
    transitioning_ = false;

    for (auto& param : params_) {
        if (param.type == AnimatorParam::Type::Trigger) param.value.b = false;
    }
}

void AnimatorController::setActiveState(const std::string& name) {
    if (!findState(name)) return;
    currentState_ = name;
    nextState_.clear();
    stateProgress_ = 0.f;
    nextStateProgress_ = 0.f;
    blendT_ = 0.f;
    activeFadeDuration_ = 0.f;
    transitioning_ = false;
}

bool AnimatorController::updateState(size_t index, AnimatorState state) {
    if (index >= states_.size()) return false;
    // State rename needs to update transitions/default/Sequence references
    // atomically and is intentionally handled by a dedicated workflow.
    if (state.name != states_[index].name) return false;
    states_[index] = std::move(state);

    if (!findState(defaultState_) && !states_.empty())
        defaultState_ = states_.front().name;
    if (!findState(currentState_))
        reset();
    else if (transitioning_ && !findState(nextState_)) {
        nextState_.clear();
        nextStateProgress_ = 0.f;
        blendT_ = 0.f;
        activeFadeDuration_ = 0.f;
        transitioning_ = false;
    }

    bumpDefinitionRevision();
    return true;
}

bool AnimatorController::updateTransition(
    size_t index, AnimatorTransition transition) {
    if (index >= transitions_.size()) return false;
    transitions_[index] = std::move(transition);
    bumpDefinitionRevision();
    return true;
}

bool AnimatorController::hasParameter(
    const std::string& name, AnimatorParam::Type type) const {
    const AnimatorParam* param = findParam(name);
    return param && param->type == type;
}

void AnimatorController::replaceDefinitionFrom(
    const AnimatorController& source, bool preserveRuntimeValues) {
    const std::vector<AnimatorParam> oldParams = params_;
    const std::string oldCurrentState = currentState_;
    const std::string oldNextState = nextState_;
    const float oldStateProgress = stateProgress_;
    const float oldNextStateProgress = nextStateProgress_;
    const float oldBlendT = blendT_;
    const float oldFadeDuration = activeFadeDuration_;
    const BlendCurve oldBlendCurve = activeBlendCurve_;
    const bool oldTransitioning = transitioning_;

    states_ = source.states_;
    transitions_ = source.transitions_;
    params_ = source.params_;
    defaultState_ = source.defaultState_;

    if (preserveRuntimeValues) {
        for (auto& param : params_) {
            const auto it = std::find_if(
                oldParams.begin(), oldParams.end(),
                [&](const AnimatorParam& oldParam) {
                    return oldParam.name == param.name
                        && oldParam.type == param.type;
                });
            if (it != oldParams.end() && param.type != AnimatorParam::Type::Trigger)
                param.value = it->value;
        }
    }

    const bool canPreserveCurrent =
        preserveRuntimeValues && findState(oldCurrentState);
    const bool canPreserveTransition =
        canPreserveCurrent && oldTransitioning && findState(oldNextState);
    if (canPreserveCurrent) {
        currentState_ = oldCurrentState;
        stateProgress_ = oldStateProgress;
        nextState_ = canPreserveTransition ? oldNextState : std::string{};
        nextStateProgress_ = canPreserveTransition ? oldNextStateProgress : 0.f;
        blendT_ = canPreserveTransition ? oldBlendT : 0.f;
        activeFadeDuration_ = canPreserveTransition ? oldFadeDuration : 0.f;
        activeBlendCurve_ = canPreserveTransition
            ? oldBlendCurve : BlendCurve::SmoothStep;
        transitioning_ = canPreserveTransition;
    } else {
        reset();
    }

    bumpDefinitionRevision();
}

void AnimatorController::bumpDefinitionRevision() {
    ++definitionRevision_;
    if (definitionRevision_ == 0)
        definitionRevision_ = 1;
}

bool AnimatorController::checkAllConditions(const AnimatorTransition& transition) const {
    return std::all_of(
        transition.conditions.begin(), transition.conditions.end(),
        [this](const TransitionCondition& condition) {
            const AnimatorParam* param = findParam(condition.paramName);
            return param && param->checkCondition(condition);
        });
}

bool AnimatorController::exitTimeReached(const AnimatorTransition& transition,
                                         const AnimatorState& state,
                                         float previousProgress) const {
    return exitTimeReachedForProgress(
        transition, state, previousProgress, stateProgress_);
}

void AnimatorController::consumeTriggers(const AnimatorTransition& transition) {
    for (const auto& condition : transition.conditions) {
        AnimatorParam* param = findParam(condition.paramName);
        if (param && param->type == AnimatorParam::Type::Trigger) {
            param->value.b = false;
        }
    }
}

AnimatorParam* AnimatorController::findParam(const std::string& name) {
    const auto it = std::find_if(params_.begin(), params_.end(),
                                 [&name](const AnimatorParam& param) {
                                     return param.name == name;
                                 });
    return it == params_.end() ? nullptr : &*it;
}

const AnimatorParam* AnimatorController::findParam(const std::string& name) const {
    const auto it = std::find_if(params_.begin(), params_.end(),
                                 [&name](const AnimatorParam& param) {
                                     return param.name == name;
                                 });
    return it == params_.end() ? nullptr : &*it;
}

const AnimatorState* AnimatorController::findState(const std::string& name) const {
    const auto it = std::find_if(states_.begin(), states_.end(),
                                 [&name](const AnimatorState& state) {
                                     return state.name == name;
                                 });
    return it == states_.end() ? nullptr : &*it;
}

bool AnimatorController::renameParameter(const std::string& oldName,
                                         const std::string& newName) {
    if (oldName.empty() || newName.empty() || oldName == newName) return false;
    AnimatorParam* param = findParam(oldName);
    if (!param || findParam(newName)) return false;

    param->name = newName;
    for (auto& transition : transitions_) {
        for (auto& condition : transition.conditions) {
            if (condition.paramName == oldName) condition.paramName = newName;
        }
    }
    for (auto& state : states_) {
        if (state.blendParameter == oldName) state.blendParameter = newName;
    }
    bumpDefinitionRevision();
    return true;
}

bool AnimatorController::renameState(const std::string& oldName,
                                     const std::string& newName) {
    if (oldName.empty() || newName.empty() || oldName == newName
        || newName == "AnyState") {
        return false;
    }

    auto stateIt = std::find_if(
        states_.begin(), states_.end(),
        [&](const AnimatorState& state) { return state.name == oldName; });
    if (stateIt == states_.end() || findState(newName))
        return false;

    stateIt->name = newName;
    for (auto& transition : transitions_) {
        if (transition.fromState == oldName)
            transition.fromState = newName;
        if (transition.toState == oldName)
            transition.toState = newName;
    }
    if (defaultState_ == oldName)
        defaultState_ = newName;
    if (currentState_ == oldName)
        currentState_ = newName;
    if (nextState_ == oldName)
        nextState_ = newName;

    bumpDefinitionRevision();
    return true;
}

const AnimationClip* AnimatorController::findClip(
    const std::string& clipName, const std::vector<AnimationClip>& clips) const {
    const auto it = std::find_if(clips.begin(), clips.end(),
                                 [&clipName](const AnimationClip& clip) {
                                     return clip.name == clipName;
                                 });
    return it == clips.end() ? nullptr : &*it;
}

AnimatorController::StatePoseCommand AnimatorController::resolveStatePose(
    const AnimatorState& state,
    const std::vector<AnimationClip>& clips,
    const std::vector<AnimatorParam>& params,
    float progress) const {
    StatePoseCommand pose;
    pose.state = &state;

    if (state.motionType == StateMotionType::SingleClip) {
        const AnimationClip* clip = findClip(state.clipName, clips);
        if (!clip) return pose;
        pose.samples[0] = ClipPoseSample{clip, sampleTime(state, clip, progress), 1.f};
        pose.sampleCount = 1;
        return pose;
    }

    struct ResolvedSample {
        const AnimationClip* clip = nullptr;
        float position = 0.f;
    };
    std::vector<ResolvedSample> resolved;
    resolved.reserve(state.blendSamples.size());
    for (const auto& sample : state.blendSamples) {
        if (const AnimationClip* clip = findClip(sample.clipName, clips)) {
            resolved.push_back({clip, sample.position});
        }
    }
    if (resolved.empty()) return pose;
    std::stable_sort(resolved.begin(), resolved.end(),
                     [](const ResolvedSample& a, const ResolvedSample& b) {
                         return a.position < b.position;
                     });

    float axis = 0.f;
    const auto paramIt = std::find_if(
        params.begin(), params.end(), [&](const AnimatorParam& param) {
            return param.name == state.blendParameter
                && param.type == AnimatorParam::Type::Float;
        });
    if (paramIt != params.end()) axis = paramIt->value.f;

    auto setSingle = [&](const ResolvedSample& sample) {
        pose.samples[0] = ClipPoseSample{
            sample.clip, sampleTime(state, sample.clip, progress), 1.f};
        pose.sampleCount = 1;
    };

    if (resolved.size() == 1 || axis <= resolved.front().position) {
        setSingle(resolved.front());
        return pose;
    }
    if (axis >= resolved.back().position) {
        setSingle(resolved.back());
        return pose;
    }

    for (size_t i = 0; i + 1 < resolved.size(); ++i) {
        const auto& lower = resolved[i];
        const auto& upper = resolved[i + 1];
        if (axis > upper.position) continue;

        const float span = upper.position - lower.position;
        if (span <= kFloatEpsilon) {
            setSingle(upper);
            return pose;
        }
        const float upperWeight = std::clamp(
            (axis - lower.position) / span, 0.f, 1.f);
        if (upperWeight <= kFloatEpsilon) {
            setSingle(lower);
        } else if (upperWeight >= 1.f - kFloatEpsilon) {
            setSingle(upper);
        } else {
            pose.samples[0] = ClipPoseSample{
                lower.clip, sampleTime(state, lower.clip, progress),
                1.f - upperWeight};
            pose.samples[1] = ClipPoseSample{
                upper.clip, sampleTime(state, upper.clip, progress),
                upperWeight};
            pose.sampleCount = 2;
        }
        return pose;
    }

    setSingle(resolved.back());
    return pose;
}

float AnimatorController::effectiveDuration(
    const AnimatorState& state,
    const std::vector<AnimationClip>& clips,
    const std::vector<AnimatorParam>& params,
    float progress) const {
    const StatePoseCommand pose = resolveStatePose(state, clips, params, progress);
    if (pose.sampleCount == 0) return 0.f;

    float duration = 0.f;
    for (uint32_t i = 0; i < pose.sampleCount; ++i) {
        if (pose.samples[i].clip) {
            duration += std::max(pose.samples[i].clip->duration, 0.f)
                      * pose.samples[i].weight;
        }
    }
    return duration;
}

bool AnimatorController::hasValidMotion(
    const AnimatorState& state,
    const std::vector<AnimationClip>& clips) const {
    if (state.motionType == StateMotionType::SingleClip) {
        return findClip(state.clipName, clips) != nullptr;
    }
    return std::any_of(
        state.blendSamples.begin(), state.blendSamples.end(),
        [&](const BlendSpace1DSample& sample) {
            return findClip(sample.clipName, clips) != nullptr;
        });
}

float AnimatorController::sampleTime(const AnimatorState& state,
                                     const AnimationClip* clip,
                                     float progress) {
    if (!clip || clip->duration <= kFloatEpsilon) return 0.f;
    float phase = state.loop
        ? progress - std::floor(progress)
        : std::clamp(progress, 0.f, 1.f);
    if (state.direction == PlaybackDirection::Reverse) phase = 1.f - phase;
    return std::clamp(phase, 0.f, 1.f) * clip->duration;
}

// ── 序列化辅助 ─────────────────────────────────────────────────────

namespace {

std::string curveToStr(BlendCurve c) {
    switch (c) {
    case BlendCurve::Linear:     return "Linear";
    case BlendCurve::SmoothStep: return "SmoothStep";
    case BlendCurve::EaseIn:     return "EaseIn";
    case BlendCurve::EaseOut:    return "EaseOut";
    }
    return "SmoothStep";
}

BlendCurve strToCurve(const std::string& s) {
    if (s == "Linear")     return BlendCurve::Linear;
    if (s == "EaseIn")     return BlendCurve::EaseIn;
    if (s == "EaseOut")    return BlendCurve::EaseOut;
    return BlendCurve::SmoothStep;
}

std::string directionToStr(PlaybackDirection direction) {
    return direction == PlaybackDirection::Reverse ? "Reverse" : "Forward";
}

PlaybackDirection strToDirection(const std::string& value) {
    return value == "Reverse"
        ? PlaybackDirection::Reverse
        : PlaybackDirection::Forward;
}

std::string motionTypeToStr(StateMotionType type) {
    return type == StateMotionType::BlendSpace1D
        ? "BlendSpace1D"
        : "SingleClip";
}

StateMotionType strToMotionType(const std::string& value) {
    return value == "BlendSpace1D"
        ? StateMotionType::BlendSpace1D
        : StateMotionType::SingleClip;
}

std::string rootMotionModeToStr(AnimatorState::RootMotionMode m) {
    switch (m) {
    case AnimatorState::RootMotionMode::None:   return "None";
    case AnimatorState::RootMotionMode::Locked: return "Locked";
    case AnimatorState::RootMotionMode::Follow: return "Follow";
    }
    return "None";
}

AnimatorState::RootMotionMode strToRootMotionMode(const std::string& s) {
    if (s == "Locked") return AnimatorState::RootMotionMode::Locked;
    if (s == "Follow") return AnimatorState::RootMotionMode::Follow;
    return AnimatorState::RootMotionMode::None;
}

std::string paramTypeToStr(AnimatorParam::Type t) {
    switch (t) {
    case AnimatorParam::Type::Float:   return "Float";
    case AnimatorParam::Type::Int:     return "Int";
    case AnimatorParam::Type::Bool:    return "Bool";
    case AnimatorParam::Type::Trigger: return "Trigger";
    }
    return "Float";
}

AnimatorParam::Type strToParamType(const std::string& s) {
    if (s == "Int")     return AnimatorParam::Type::Int;
    if (s == "Bool")    return AnimatorParam::Type::Bool;
    if (s == "Trigger") return AnimatorParam::Type::Trigger;
    return AnimatorParam::Type::Float;
}

std::string condOpToStr(TransitionCondition::Op op) {
    switch (op) {
    case TransitionCondition::Op::Greater:  return "Greater";
    case TransitionCondition::Op::Less:     return "Less";
    case TransitionCondition::Op::Equal:    return "Equal";
    case TransitionCondition::Op::NotEqual: return "NotEqual";
    case TransitionCondition::Op::True:     return "True";
    case TransitionCondition::Op::False:    return "False";
    }
    return "Equal";
}

TransitionCondition::Op strToCondOp(const std::string& s) {
    if (s == "Greater")  return TransitionCondition::Op::Greater;
    if (s == "Less")     return TransitionCondition::Op::Less;
    if (s == "Equal")    return TransitionCondition::Op::Equal;
    if (s == "NotEqual") return TransitionCondition::Op::NotEqual;
    if (s == "True")     return TransitionCondition::Op::True;
    if (s == "False")    return TransitionCondition::Op::False;
    return TransitionCondition::Op::Equal;
}

} // namespace

bool AnimatorController::saveToFile(const std::string& jsonPath) const {
    nlohmann::json j;
    j["version"] = 2;
    j["defaultState"] = defaultState_;

    auto& jStates = j["states"];
    for (const auto& s : states_) {
        nlohmann::json js;
        js["name"] = s.name;
        js["motionType"] = motionTypeToStr(s.motionType);
        if (s.motionType == StateMotionType::BlendSpace1D) {
            js["blendParameter"] = s.blendParameter;
            auto& samples = js["samples"];
            for (const auto& sample : s.blendSamples) {
                samples.push_back({
                    {"clipName", sample.clipName},
                    {"position", sample.position}
                });
            }
        } else {
            js["clipName"] = s.clipName;
        }
        js["direction"] = directionToStr(s.direction);
        js["playRate"] = std::max(s.playRate, 0.f);
        js["loop"] = s.loop;
        js["rootMotion"] = rootMotionModeToStr(s.rootMotion);
        if (!s.rootBoneName.empty())
            js["rootBoneName"] = s.rootBoneName;
        jStates.push_back(std::move(js));
    }

    auto& jParams = j["params"];
    for (const auto& p : params_) {
        nlohmann::json jp;
        jp["name"] = p.name;
        jp["type"] = paramTypeToStr(p.type);
        switch (p.type) {
        case AnimatorParam::Type::Float:   jp["defaultValue"] = p.value.f; break;
        case AnimatorParam::Type::Int:     jp["defaultValue"] = p.value.i; break;
        case AnimatorParam::Type::Bool:
        case AnimatorParam::Type::Trigger: jp["defaultValue"] = p.value.b; break;
        }
        jParams.push_back(std::move(jp));
    }

    auto& jTrans = j["transitions"];
    for (const auto& t : transitions_) {
        nlohmann::json jt;
        jt["from"] = t.fromState;
        jt["to"] = t.toState;
        jt["fadeDuration"] = t.fadeDuration;
        jt["hasExitTime"] = t.hasExitTime;
        jt["exitTime"] = t.exitTime;
        jt["blendCurve"] = curveToStr(t.blendCurve);
        auto& jConds = jt["conditions"];
        for (const auto& c : t.conditions) {
            nlohmann::json jc;
            jc["param"] = c.paramName;
            jc["op"] = condOpToStr(c.op);
            jc["threshold"] = c.threshold;
            jConds.push_back(std::move(jc));
        }
        jTrans.push_back(std::move(jt));
    }

    std::ofstream out(jsonPath);
    if (!out.is_open()) return false;
    out << j.dump(2) << '\n';
    return out.good();
}

bool AnimatorController::loadFromFile(const std::string& jsonPath) {
    std::ifstream in(jsonPath);
    if (!in.is_open()) return false;

    nlohmann::json j;
    try { in >> j; } catch (...) { return false; }

    if (!j.is_object()) return false;
    const int version = j.value("version", 1);

    std::vector<AnimatorState> states;
    if (j.contains("states") && j["states"].is_array()) {
        for (const auto& js : j["states"]) {
            AnimatorState s;
            s.name = js.value("name", std::string{});
            s.clipName = js.value("clipName", std::string{});
            s.motionType = strToMotionType(
                js.value("motionType", std::string{"SingleClip"}));
            s.blendParameter = js.value("blendParameter", std::string{});
            if (js.contains("samples") && js["samples"].is_array()) {
                for (const auto& jSample : js["samples"]) {
                    if (!jSample.is_object()) continue;
                    BlendSpace1DSample sample;
                    sample.clipName = jSample.value("clipName", std::string{});
                    sample.position = jSample.value("position", 0.f);
                    s.blendSamples.push_back(std::move(sample));
                }
            }

            if (version <= 1) {
                const float legacySpeed = js.value("speed", 1.f);
                s.playRate = std::abs(legacySpeed);
                s.direction = legacySpeed < 0.f
                    ? PlaybackDirection::Reverse
                    : PlaybackDirection::Forward;
                s.motionType = StateMotionType::SingleClip;
            } else {
                s.playRate = std::max(js.value("playRate", 1.f), 0.f);
                s.direction = strToDirection(
                    js.value("direction", std::string{"Forward"}));
            }
            s.loop = js.value("loop", true);
            s.rootMotion = strToRootMotionMode(js.value("rootMotion", std::string{"None"}));
            s.rootBoneName = js.value("rootBoneName", std::string{});
            states.push_back(std::move(s));
        }
    }

    std::vector<AnimatorParam> params;
    if (j.contains("params") && j["params"].is_array()) {
        for (const auto& jp : j["params"]) {
            AnimatorParam p;
            p.name = jp.value("name", std::string{});
            p.type = strToParamType(jp.value("type", std::string{"Float"}));
            switch (p.type) {
            case AnimatorParam::Type::Float:   p.value.f = jp.value("defaultValue", 0.f); break;
            case AnimatorParam::Type::Int:     p.value.i = jp.value("defaultValue", 0); break;
            case AnimatorParam::Type::Bool:    p.value.b = jp.value("defaultValue", false); break;
            case AnimatorParam::Type::Trigger: p.value.b = false; break;
            }
            params.push_back(std::move(p));
        }
    }

    std::vector<AnimatorTransition> transitions;
    if (j.contains("transitions") && j["transitions"].is_array()) {
        for (const auto& jt : j["transitions"]) {
            AnimatorTransition t;
            t.fromState = jt.value("from", std::string{});
            t.toState = jt.value("to", std::string{});
            t.fadeDuration = jt.value("fadeDuration", 0.2f);
            t.hasExitTime = jt.value("hasExitTime", false);
            t.exitTime = jt.value("exitTime", 1.f);
            t.blendCurve = strToCurve(jt.value("blendCurve", std::string{"SmoothStep"}));
            if (jt.contains("conditions") && jt["conditions"].is_array()) {
                for (const auto& jc : jt["conditions"]) {
                    TransitionCondition c;
                    c.paramName = jc.value("param", std::string{});
                    c.op = strToCondOp(jc.value("op", std::string{"Equal"}));
                    c.threshold = jc.value("threshold", 0.f);
                    t.conditions.push_back(std::move(c));
                }
            }
            transitions.push_back(std::move(t));
        }
    }

    configure(std::move(states), std::move(transitions), std::move(params),
              j.value("defaultState", std::string{}));
    return true;
}
