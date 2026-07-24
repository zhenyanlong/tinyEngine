#pragma once

#include "AnimationClip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

/** @brief 状态的播放方向。播放进度始终正向累积，方向只影响采样时间映射。 */
enum class PlaybackDirection {
    Forward,
    Reverse
};

/** @brief 状态使用的运动类型。 */
enum class StateMotionType {
    SingleClip,
    BlendSpace1D
};

/** @brief BlendSpace1D 中的一个动画采样点。 */
struct BlendSpace1DSample {
    std::string clipName;
    float position = 0.f;
};

/** @brief 单个动画状态。 */
struct AnimatorState {
    std::string name;
    StateMotionType motionType = StateMotionType::SingleClip;
    std::string clipName;
    std::string blendParameter;
    std::vector<BlendSpace1DSample> blendSamples;
    float playRate = 1.f;
    PlaybackDirection direction = PlaybackDirection::Forward;
    bool loop = true;

    // ── Root Motion ──
    enum class RootMotionMode { None, Locked, Follow };
    RootMotionMode rootMotion = RootMotionMode::None;
    std::string rootBoneName;           ///< 用于 root motion 的骨骼名称，空字符串 = bones[0]
};

/** @brief blend 权重曲线类型。 */
enum class BlendCurve {
    Linear,
    SmoothStep,
    EaseIn,
    EaseOut
};

/** @brief 将一个归一化 t ∈ [0,1] 按曲线重映射。 */
inline float applyBlendCurve(float t, BlendCurve curve) {
    switch (curve) {
    case BlendCurve::Linear:     return t;
    case BlendCurve::SmoothStep: return t * t * (3.f - 2.f * t);
    case BlendCurve::EaseIn:     return t * t;
    case BlendCurve::EaseOut:    return 1.f - (1.f - t) * (1.f - t);
    }
    return t;
}

/** @brief 两个动画状态之间的过渡。 */
struct AnimatorTransition {
    std::string fromState; ///< 空字符串表示 AnyState。
    std::string toState;
    float fadeDuration = 0.2f;
    bool hasExitTime = false;
    float exitTime = 1.f; ///< 累计归一化进度；Loop 状态允许 >1 表示多轮播放
    BlendCurve blendCurve = BlendCurve::SmoothStep;
    std::vector<TransitionCondition> conditions;
};

/** @brief 动画事件：用于 Sequence 或其他外部系统触发 AnimatorController 参数变化。 */
struct AnimatorEvent {
    enum class Type { SetFloat, SetInt, SetBool, SetTrigger };
    Type type = Type::SetFloat;
    std::string paramName;
    float floatValue = 0.f;
    int intValue = 0;
    bool boolValue = false;
};

/**
 * @brief Sequencer 在一个确定时间点提供给 Animator 的输入快照。
 *
 * params 只包含该时间点已有关键帧覆盖的持续参数；未出现的参数继续使用
 * AnimatorController 默认值。triggers 是只在该采样点有效一次的脉冲。
 */
struct AnimatorTimelineSample {
    float time = 0.f;
    std::vector<AnimatorParam> params;
    std::vector<std::string> triggers;
};

/** @brief 驱动动画状态、过渡条件和交叉淡入淡出的运行时控制器。 */
class AnimatorController {
public:
    /** @brief State Motion 中一个实际参与求值的动画片段。 */
    struct ClipPoseSample {
        const AnimationClip* clip = nullptr;
        float sampleTime = 0.f;
        float weight = 1.f;
    };

    /** @brief 一个 State 内部解析后的姿势指令，BlendSpace1D 最多包含两个片段。 */
    struct StatePoseCommand {
        const AnimatorState* state = nullptr;
        std::array<ClipPoseSample, 2> samples{};
        uint32_t sampleCount = 0;
    };

    /**
     * @brief Animator 最终输出的两层姿势指令。
     *
     * poseA/poseB 分别表示当前和目标 State 的内部 SingleClip/BlendSpace 姿势，
     * blendWeight 再表示两个 State 之间的交叉淡入淡出权重。
     */
    struct BlendCommand {
        const AnimatorState* stateA = nullptr;
        const AnimatorState* stateB = nullptr;
        StatePoseCommand poseA;
        StatePoseCommand poseB;
        float blendWeight = 0.f;
    };

    /** @brief 配置状态机定义，并重置到默认状态。 */
    void configure(std::vector<AnimatorState> states,
                   std::vector<AnimatorTransition> transitions,
                   std::vector<AnimatorParam> params,
                   const std::string& defaultState = {});

    /** @brief 根据动画片段生成无过渡的默认状态机。 */
    void configureFromClips(const std::vector<AnimationClip>& clips);

    /** @brief 将当前状态机保存为 .json 文件。 */
    bool saveToFile(const std::string& jsonPath) const;

    /** @brief 从 .json 文件加载状态机定义，返回 false 表示解析失败。 */
    bool loadFromFile(const std::string& jsonPath);

    void setFloat(const std::string& name, float v);
    void setInt(const std::string& name, int v);
    void setBool(const std::string& name, bool v);
    void setTrigger(const std::string& name);

    /** @brief 分发一个动画事件（直接修改对应参数）。 */
    void dispatchEvent(const AnimatorEvent& event);

    /** @brief 批量分发事件。 */
    void dispatchEvents(const std::vector<AnimatorEvent>& events);

    /** @brief 推进状态机并返回本帧动画采样指令。 */
    BlendCommand update(float dt, const std::vector<AnimationClip>& clips);

    /**
     * @brief 纯函数：在指定时间点计算状态机的 BlendCommand，不修改内部状态。
     *
     * 从 initialState 开始，应用给定的参数值，模拟在时间 t 秒内的状态演进，
     * 返回该时间点的 BlendCommand。不修改 params_ / stateProgress_ 等内部成员。
     *
     * @param t          模拟的时间点（秒），从 0 开始
     * @param clips      动画 clip 列表
     * @param paramValues 要应用到状态机的参数值（覆盖 params_ 中的默认值）
     * @param initialState 初始状态名（空字符串 = defaultState_）
     * @return BlendCommand 在时间 t 处的混合指令
     */
    BlendCommand computeBlendAtTime(float t,
                                    const std::vector<AnimationClip>& clips,
                                    const std::vector<AnimatorParam>& paramValues = {},
                                    const std::string& initialState = {}) const;

    /** @brief 按时间顺序应用 Sequencer 输入并纯函数求值状态机。 */
    BlendCommand computeBlendAtTime(float t,
                                    const std::vector<AnimationClip>& clips,
                                    const std::vector<AnimatorTimelineSample>& timeline,
                                    const std::string& initialState) const;

    void reset();
    /** @brief 直接切换到指定 state，跳过 transition。如果 name 不存在则忽略。 */
    void setActiveState(const std::string& name);
    bool hasStates() const { return !states_.empty(); }
    bool isTransitioning() const { return transitioning_; }
    float blendProgress() const { return blendT_; }
    const std::string& currentStateName() const { return currentState_; }
    const std::string& nextStateName() const { return nextState_; }
    const std::vector<AnimatorState>& states() const { return states_; }
    const std::vector<AnimatorTransition>& transitions() const { return transitions_; }
    const std::vector<AnimatorParam>& params() const { return params_; }
    const std::string& defaultStateName() const { return defaultState_; }
    uint64_t definitionRevision() const { return definitionRevision_; }

    /** @brief 不重置运行时播放状态地替换单个 State/Transition 定义。 */
    bool updateState(size_t index, AnimatorState state);
    bool updateTransition(size_t index, AnimatorTransition transition);
    /** @brief 标记通过受控外部编辑完成的一次定义变化。 */
    void markDefinitionChanged() { bumpDefinitionRevision(); }

    /** @brief 查询 Sequencer 等外部系统持有的名称引用是否仍有效。 */
    bool hasState(const std::string& name) const { return findState(name) != nullptr; }
    bool hasParameter(const std::string& name, AnimatorParam::Type type) const;

    /**
     * @brief 从同一资产的另一个实例同步状态机定义。
     *
     * preserveRuntimeValues=true 时按 name/type 保留本实例参数值，并尽量保留
     * 当前 State/Transition 进度；定义已不兼容时回退到 reset()。
     */
    void replaceDefinitionFrom(const AnimatorController& source,
                               bool preserveRuntimeValues = true);

    /** @brief 将参数及其 Controller 内部引用原子重命名。 */
    bool renameParameter(const std::string& oldName, const std::string& newName);
    /** @brief 原子重命名 State，并保留运行时播放/过渡进度。 */
    bool renameState(const std::string& oldName, const std::string& newName);

private:
    std::vector<AnimatorState> states_;
    std::vector<AnimatorTransition> transitions_;
    std::vector<AnimatorParam> params_;

    std::string defaultState_;
    std::string currentState_;
    std::string nextState_;
    float stateProgress_ = 0.f;
    float nextStateProgress_ = 0.f;
    float blendT_ = 0.f;
    float activeFadeDuration_ = 0.f;
    BlendCurve activeBlendCurve_ = BlendCurve::SmoothStep;
    bool transitioning_ = false;
    uint64_t definitionRevision_ = 0;

    bool checkAllConditions(const AnimatorTransition& transition) const;
    bool exitTimeReached(const AnimatorTransition& transition,
                         const AnimatorState& state,
                         float previousProgress) const;
    void consumeTriggers(const AnimatorTransition& transition);

    AnimatorParam* findParam(const std::string& name);
    const AnimatorParam* findParam(const std::string& name) const;
    const AnimatorState* findState(const std::string& name) const;
    const AnimationClip* findClip(const std::string& clipName,
                                  const std::vector<AnimationClip>& clips) const;
    StatePoseCommand resolveStatePose(const AnimatorState& state,
                                      const std::vector<AnimationClip>& clips,
                                      const std::vector<AnimatorParam>& params,
                                      float progress) const;
    float effectiveDuration(const AnimatorState& state,
                            const std::vector<AnimationClip>& clips,
                            const std::vector<AnimatorParam>& params,
                            float progress) const;
    bool hasValidMotion(const AnimatorState& state,
                        const std::vector<AnimationClip>& clips) const;
    static float sampleTime(const AnimatorState& state,
                            const AnimationClip* clip,
                            float progress);
    void bumpDefinitionRevision();
};
