#pragma once

#include "AnimationClip.hpp"

#include <string>
#include <vector>

/** @brief 状态机过渡条件。 */
struct TransitionCondition {
    std::string paramName;
    enum class Op { Greater, Less, Equal, NotEqual, True, False } op = Op::Equal;
    float threshold = 0.f;
};

/** @brief Animator 运行时参数。 */
struct AnimatorParam {
    std::string name;
    enum class Type { Float, Int, Bool, Trigger } type = Type::Float;

    union Value {
        float f;
        int i;
        bool b;

        constexpr Value() : f(0.f) {}
    } value;

    void setFloat(float v);
    void setInt(int v);
    void setBool(bool v);
    void setTrigger();

    /** @brief 按参数类型检查单个过渡条件。 */
    bool checkCondition(const TransitionCondition& condition) const;
};

/** @brief 单个动画状态。 */
struct AnimatorState {
    std::string name;
    std::string clipName;
    float speed = 1.f;
    bool loop = true;
};

/** @brief 两个动画状态之间的过渡。 */
struct AnimatorTransition {
    std::string fromState; ///< 空字符串表示 AnyState。
    std::string toState;
    float fadeDuration = 0.2f;
    bool hasExitTime = false;
    float exitTime = 1.f;
    std::vector<TransitionCondition> conditions;
};

/** @brief 驱动动画状态、过渡条件和交叉淡入淡出的运行时控制器。 */
class AnimatorController {
public:
    struct BlendCommand {
        const AnimationClip* clipA = nullptr;
        const AnimationClip* clipB = nullptr;
        float blendWeight = 0.f;
        float timeA = 0.f;
        float timeB = 0.f;
    };

    /** @brief 配置状态机定义，并重置到默认状态。 */
    void configure(std::vector<AnimatorState> states,
                   std::vector<AnimatorTransition> transitions,
                   std::vector<AnimatorParam> params,
                   const std::string& defaultState = {});

    /** @brief 根据动画片段生成无过渡的默认状态机。 */
    void configureFromClips(const std::vector<AnimationClip>& clips);

    void setFloat(const std::string& name, float v);
    void setInt(const std::string& name, int v);
    void setBool(const std::string& name, bool v);
    void setTrigger(const std::string& name);

    /** @brief 推进状态机并返回本帧动画采样指令。 */
    BlendCommand update(float dt, const std::vector<AnimationClip>& clips);

    void reset();
    bool hasStates() const { return !states_.empty(); }
    bool isTransitioning() const { return transitioning_; }
    float blendProgress() const { return blendT_; }
    const std::string& currentStateName() const { return currentState_; }
    const std::string& nextStateName() const { return nextState_; }
    const std::vector<AnimatorState>& states() const { return states_; }
    const std::vector<AnimatorTransition>& transitions() const { return transitions_; }
    const std::vector<AnimatorParam>& params() const { return params_; }

private:
    std::vector<AnimatorState> states_;
    std::vector<AnimatorTransition> transitions_;
    std::vector<AnimatorParam> params_;

    std::string defaultState_;
    std::string currentState_;
    std::string nextState_;
    float stateTime_ = 0.f;
    float nextStateTime_ = 0.f;
    float blendT_ = 0.f;
    float activeFadeDuration_ = 0.f;
    bool transitioning_ = false;

    bool checkAllConditions(const AnimatorTransition& transition) const;
    bool exitTimeReached(const AnimatorTransition& transition,
                         const AnimatorState& state,
                         const AnimationClip* clip,
                         float previousStateTime) const;
    void consumeTriggers(const AnimatorTransition& transition);

    AnimatorParam* findParam(const std::string& name);
    const AnimatorParam* findParam(const std::string& name) const;
    const AnimatorState* findState(const std::string& name) const;
    const AnimationClip* findClip(const std::string& clipName,
                                  const std::vector<AnimationClip>& clips) const;
    static float sampleTime(const AnimatorState& state,
                            const AnimationClip* clip,
                            float stateTime);
};
