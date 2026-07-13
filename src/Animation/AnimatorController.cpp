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
    const float previousStateTime = stateTime_;
    bool stateChangedThisFrame = false;
    stateTime_ += safeDt * std::max(current->speed, 0.f);

    if (transitioning_) {
        const AnimatorState* next = findState(nextState_);
        if (!next) {
            transitioning_ = false;
            nextState_.clear();
            blendT_ = 0.f;
        } else {
            nextStateTime_ += safeDt * std::max(next->speed, 0.f);
            blendT_ = activeFadeDuration_ <= kFloatEpsilon
                ? 1.f
                : std::min(1.f, blendT_ + safeDt / activeFadeDuration_);

            if (blendT_ >= 1.f) {
                currentState_ = nextState_;
                stateTime_ = nextStateTime_;
                nextState_.clear();
                nextStateTime_ = 0.f;
                activeFadeDuration_ = 0.f;
                blendT_ = 0.f;
                transitioning_ = false;
                current = findState(currentState_);
                stateChangedThisFrame = true;
            }
        }
    }

    if (!transitioning_ && current && !stateChangedThisFrame) {
        const AnimationClip* currentClip = findClip(current->clipName, clips);
        for (const auto& transition : transitions_) {
            if (!transition.fromState.empty() && transition.fromState != currentState_)
                continue;
            if (transition.toState == currentState_)
                continue;

            const AnimatorState* target = findState(transition.toState);
            if (!target || !findClip(target->clipName, clips))
                continue;
            if (!exitTimeReached(transition, *current, currentClip, previousStateTime)
                || !checkAllConditions(transition))
                continue;

            nextState_ = target->name;
            nextStateTime_ = 0.f;
            blendT_ = 0.f;
            activeFadeDuration_ = std::max(transition.fadeDuration, 0.f);
            activeBlendCurve_ = transition.blendCurve;
            transitioning_ = true;
            consumeTriggers(transition);

            if (activeFadeDuration_ <= kFloatEpsilon) {
                currentState_ = nextState_;
                stateTime_ = 0.f;
                nextState_.clear();
                nextStateTime_ = 0.f;
                activeFadeDuration_ = 0.f;
                transitioning_ = false;
                current = findState(currentState_);
            }
            break;
        }
    }

    current = findState(currentState_);
    if (!current) return command;

    command.clipA = findClip(current->clipName, clips);
    command.timeA = sampleTime(*current, command.clipA, stateTime_);

    if (transitioning_) {
        const AnimatorState* next = findState(nextState_);
        if (next) {
            command.clipB = findClip(next->clipName, clips);
            command.timeB = sampleTime(*next, command.clipB, nextStateTime_);
            const float raw = std::clamp(blendT_, 0.f, 1.f);
            command.blendWeight = applyBlendCurve(raw, activeBlendCurve_);
        }
    }
    return command;
}

void AnimatorController::reset() {
    currentState_ = defaultState_;
    nextState_.clear();
    stateTime_ = 0.f;
    nextStateTime_ = 0.f;
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
    stateTime_ = 0.f;
    nextStateTime_ = 0.f;
    blendT_ = 0.f;
    activeFadeDuration_ = 0.f;
    transitioning_ = false;
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
                                         const AnimationClip* clip,
                                         float previousStateTime) const {
    if (!transition.hasExitTime) return true;
    if (!clip || clip->duration <= kFloatEpsilon) return false;

    const float targetTime = std::clamp(transition.exitTime, 0.f, 1.f) * clip->duration;
    if (!state.loop) return stateTime_ >= targetTime;
    if (targetTime <= kFloatEpsilon) return true;

    const float elapsed = std::max(0.f, stateTime_ - previousStateTime);
    if (elapsed >= clip->duration) return true;

    const float previousWrapped = std::fmod(std::max(previousStateTime, 0.f), clip->duration);
    const float currentWrapped = std::fmod(std::max(stateTime_, 0.f), clip->duration);
    const bool wrapped = currentWrapped < previousWrapped;
    return wrapped
        ? targetTime > previousWrapped || targetTime <= currentWrapped
        : previousWrapped < targetTime && currentWrapped >= targetTime;
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

const AnimationClip* AnimatorController::findClip(
    const std::string& clipName, const std::vector<AnimationClip>& clips) const {
    const auto it = std::find_if(clips.begin(), clips.end(),
                                 [&clipName](const AnimationClip& clip) {
                                     return clip.name == clipName;
                                 });
    return it == clips.end() ? nullptr : &*it;
}

float AnimatorController::sampleTime(const AnimatorState& state,
                                     const AnimationClip* clip,
                                     float stateTime) {
    if (!clip || clip->duration <= kFloatEpsilon) return 0.f;
    if (!state.loop) return std::clamp(stateTime, 0.f, clip->duration);

    const float wrapped = std::fmod(stateTime, clip->duration);
    return wrapped < 0.f ? wrapped + clip->duration : wrapped;
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
    j["version"] = 1;
    j["defaultState"] = defaultState_;

    auto& jStates = j["states"];
    for (const auto& s : states_) {
        nlohmann::json js;
        js["name"] = s.name;
        js["clipName"] = s.clipName;
        js["speed"] = s.speed;
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

    std::vector<AnimatorState> states;
    if (j.contains("states") && j["states"].is_array()) {
        for (const auto& js : j["states"]) {
            AnimatorState s;
            s.name = js.value("name", std::string{});
            s.clipName = js.value("clipName", std::string{});
            s.speed = js.value("speed", 1.f);
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
