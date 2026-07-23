# tinyEngine 动画系统建设计划

---

## 整体架构概览



- todo 0 : 支持多模型导入

```
动画系统
├── A. 基础骨骼动画（Skeletal Animation）
│   ├── A1. Skeleton / Bone 数据结构
│   ├── A2. cgltf 动画数据解析
│   ├── A3. 蒙皮 Shader（GPU Skinning）
│   ├── A4. AnimationClip 运行时求值
│   └── A5. 动画资产化（Anim .ast + .anim.bin）
│
├── B. 状态机动画系统（Animator / State Machine）
│   ├── B1. AnimatorState / AnimatorTransition 数据结构
│   ├── B2. AnimatorController 状态机逻辑
│   ├── B3. 动画混合（Cross-fade Blend）
│   ├── B4. 状态机资产序列化（.animctrl.json）
│   └── B5. ImGui 状态机编辑器面板
│
├── C. Sequencer 时序动画系统
│   ├── C1. SequenceTrack / SequenceClip 数据结构
│   ├── C2. Sequence 播放控制器
│   ├── C3. 相机路径轨道（Camera Path Track）
│   ├── C4. 对象变换轨道（TransformTween + TransformKeyframe）
│   ├── C5. Sequence 资产序列化（.seq.json）
│   └── C6. ImGui Sequencer 编辑器面板
│
└── D. 资产系统扩充
    ├── D1. 动画资产扫描与注册
    ├── D2. .ast 文件扩展（引用骨骼/动画控制器）
    └── D3. ImGui 资产浏览器面板
```

## Phase A — 基础骨骼动画

### A1. Skeleton / Bone 数据结构

**目标：** 建立引擎内的骨骼层级表示，供后续蒙皮和动画求值使用。

**新建文件：** `src/Animation/Skeleton.hpp`

**需要定义的结构体：**

```cpp
// 单根骨骼（关节）
struct Bone {
    std::string name;
    int         parentIndex;          // -1 表示根骨骼
    glm::mat4   inverseBindMatrix;    // glTF accessor skin.inverseBindMatrices[i]
    glm::mat4   localBindTransform;   // 绑定姿势下的局部 TRS（from glTF node）
};

// 整个骨架
struct Skeleton {
    std::vector<Bone>     bones;           // 按深度优先顺序排列，父骨骼下标总 < 子骨骼
    std::unordered_map<std::string, int> boneNameToIndex;

    // 根据当前每根骨骼的局部变换，递推出 finalBoneMatrices[i] = globalTransform[i] * inverseBindMatrix[i]
    void computeFinalMatrices(
        const std::vector<glm::mat4>& localTransforms,   // 输入：每帧由动画求值系统填入
        std::vector<glm::mat4>&       finalBoneMatrices  // 输出：上传到 GPU
    ) const;
};
```

**具体任务：**

- [x] **A1-1** 创建 `src/Animation/` 目录，新建 `Skeleton.hpp` / `Skeleton.cpp`
- [x] **A1-2** 实现 `Skeleton::computeFinalMatrices`：按父→子顺序遍历（`parentIndex < i` 保证），`global[i] = global[parent] * local[i]`，`final[i] = global[i] * inverseBindMatrix[i]`
- [x] **A1-3** 在 `Skeleton.cpp` 写单元测试函数 `testSkeleton()`（不对外暴露，仅 debug 用）：构造一个两骨骼链，验证 final matrix 计算正确

**验收标准：** 能构造一个 3 骨骼链式骨架，调用 `computeFinalMatrices` 后 `finalBoneMatrices[2]` 与手算结果一致。

---

### A2. cgltf 动画数据解析

**目标：** 在 `SceneManager::loadModelFromGltf` 中解析 glTF 皮肤（skin）和动画（animations），填充 Skeleton 和 AnimationClip。

**前置依赖：** A1 完成。

**新建文件：** `src/Animation/AnimationClip.hpp`

```cpp
// 一个通道（channel）对应一根骨骼的一个属性（T/R/S）
struct AnimChannel {
    int         boneIndex;
    enum class Target { Translation, Rotation, Scale } target;
    std::vector<float>      times;      // keyframe 时间戳（秒）
    std::vector<glm::vec4>  values;     // vec3 for T/S，quat(xyzw) for R，统一用 vec4 存储
    enum class Interpolation { Linear, Step, CubicSpline } interp;
};

struct AnimationClip {
    std::string             name;
    float                   duration;   // max(channel.times.back())
    std::vector<AnimChannel> channels;

    // 对单根骨骼求值某一时间点的 TRS（返回 4x4 局部矩阵）
    glm::mat4 evaluateBoneLocalTransform(int boneIndex, float t) const;
};
```

**具体任务：**

- [x] **A2-1** 新建 `src/Animation/AnimationClip.hpp` / `.cpp`，实现上述结构体
- [x] **A2-2** 实现 `AnimationClip::evaluateBoneLocalTransform`：
  - 找到该骨骼的 Translation / Rotation / Scale 三条 channel（可能缺失，缺失则用骨骼的 bindTransform 值）
  - 对每个 channel 做二分查找找到 `[t0, t1]` 区间
  - `Linear`：`glm::mix` 或 `glm::slerp`；`Step`：取 `t0` 值；`CubicSpline`：三次 Hermite 插值
  - 将 T/R/S 组合成 `T * R * S` 的 mat4
- [x] **A2-3** 在 `SceneManager.hpp` 中新增成员：
  ```cpp
  std::shared_ptr<Skeleton>               skeleton_;
  std::vector<AnimationClip>              animationClips_;
  ```
- [x] **A2-4** 在 `SceneManager::loadModelFromGltf` 末尾新增皮肤解析：
  - 遍历 `data->skins`（取第一个 skin，或后续多皮肤支持）
  - 读取 `skin->joints` 数组，为每个 joint 从 `cgltf_node` 的 `matrix` / `translation/rotation/scale` 中提取 `localBindTransform`
  - 读取 `skin->inverse_bind_matrices` accessor，填充 `Bone::inverseBindMatrix`
  - 维护 `cgltf_node* → boneIndex` 的临时 map，供动画解析使用
- [x] **A2-5** 继续解析 `data->animations`：
  - 遍历每个 `cgltf_animation`，创建对应 `AnimationClip`
  - 遍历 `anim->channels`：找到 target node 对应的 boneIndex（用上一步的 map）
  - 遍历 `anim->samplers`：读取 input（时间）和 output（值）accessor，填充 `AnimChannel::times` 和 `values`
  - 计算 `clip.duration = max(channel.times.back())`
- [x] **A2-6** 新增 `SceneManager::getAnimationClips()` / `getSkeleton()` 公开访问器，供 Application 层使用
- [x] **A2-7** 修改 `SceneManager::loadModelFromObj`：皮肤和动画置空（OBJ 不支持骨骼），不影响现有渲染路径

**验收标准：** 加载一个带骨骼动画的 glTF 文件（如 Khronos 官方的 `CesiumMan.glb`），能在 debug 日志中打印出骨骼数量、动画数量、第一条动画的时长。

---

### A3. 蒙皮 Shader（GPU Skinning）

**目标：** 新增支持骨骼蒙皮的 Vertex Shader 和对应 Vulkan 管线，将最终骨骼矩阵通过 UBO 传入 GPU。

**前置依赖：** A2 完成（能获取骨骼数量）。

**新建/修改文件：** `shaders/skinned_vert.glsl`（新建）、`VulkanTypes.hpp`（扩展）、`PipelineManager`（新增管线）、`MaterialManager`（新增 UBO slot）

**具体任务：**

- [x] **A3-1** 在 `vectex.hpp` 扩展 `Vertex` 结构体，新增蒙皮属性：
  ```cpp
  glm::ivec4 boneIndices = {0,0,0,0};  // location=6：影响该顶点的最多4根骨骼索引
  glm::vec4  boneWeights = {1,0,0,0};  // location=7：对应权重（归一化）
  ```
  同时更新 `Vertex::getAttributeDescriptions()` 增加这两个 attribute。
  > **注意：** 现有无蒙皮模型的顶点数据不含这两个字段，需要在加载时区分填充（glTF skinned mesh 填实际值，OBJ/无皮肤 glTF 填默认值 `{0,0,0,0}` / `{1,0,0,0}`）。

- [x] **A3-2** 在 `VulkanTypes.hpp` 新增骨骼矩阵 UBO：
  ```cpp
  constexpr int kMaxBones = 256;
  struct BoneMatricesUBO {
      glm::mat4 bones[kMaxBones];  // finalBoneMatrices，按骨骼索引排列
  };
  ```

- [x] **A3-3** 新建 `shaders/skinned_vert.glsl`：
  - 输入 layout 与 `vert.glsl` 一致，额外加 `layout(location=6) in ivec4 boneIndices` 和 `layout(location=7) in vec4 boneWeights`
  - 新增 `layout(set=0, binding=6) uniform BoneMatricesBlock { mat4 bones[256]; } boneUBO;`
  - skinMatrix = `boneWeights.x * bones[boneIndices.x] + boneWeights.y * bones[boneIndices.y] + ...`
  - `gl_Position = proj * view * model * skinMatrix * vec4(pos, 1.0)`
  - 法线变换：`normalMatrix * mat3(skinMatrix) * normal`
  - 编译命令加入 `compile_shaders.ps1`

- [x] **A3-4** 在 `MaterialManager` 中新增 `createSkinnedMeshMaterial`：
  - 额外分配一个 `BoneMatricesUBO` 大小的 VkBuffer（per swapchain image）
  - Descriptor set layout 增加 binding 6（UBO，vertex stage）
  - 新增 `updateBoneMatrices(materialId, imageIndex, const std::vector<glm::mat4>&)` 方法：`memcpy` 到对应 mapped UBO

- [x] **A3-5** 在 `PipelineManager` 中新增 skinned mesh 管线：
  - 复用现有 mesh 管线的所有状态
  - 切换 vert shader 为 `skinned_vert.spv`
  - 使用新的 descriptor set layout（含 binding 6）
  - 新增 `VkPipeline skinnedMeshPipeline_`

- [x] **A3-6** 在 `SceneManager::loadModelFromGltf` 中，当 `skeleton_ != nullptr` 时，将顶点的 `JOINTS_0` 和 `WEIGHTS_0` accessor 数据读入 `Vertex::boneIndices` / `boneWeights`

- [x] **A3-7** 在 `Application::recordCommandBuffer` 中，检测当前主模型是否有骨骼，若有则绑定 skinned 管线（代替默认 mesh 管线）

**验收标准：** `CesiumMan.glb` 在绑定姿势（t=0）下渲染正确，网格不出现扭曲/爆炸。

**验证记录（2026-06-18）：** A3 已通过 `cmake --build --preset x64-debug`，并经用户运行时确认：双 skin 模型（机器人 72 joints、人物 191 joints）在动画播放时不再拉伸，人物下半身不再错误跟随机器人 idle。最终实现采用 glTF node→skin 绑定、`SubMesh::skinIndex`、skin-local JOINTS_0、per-skin bone palette 上传，以及 `kMaxBones = 256`。

---

### A4. AnimationClip 运行时求值（Animator 求值层）

**目标：** 实现每帧驱动动画播放的求值层，将 AnimationClip + 时间 → 最终骨骼矩阵上传 GPU。

**前置依赖：** A2、A3 完成。

**新建文件：** `src/Animation/AnimationPlayer.hpp` / `.cpp`

```cpp
class AnimationPlayer {
public:
    // 绑定骨架和材质（用于 updateBoneMatrices 回调）
    void bind(std::shared_ptr<Skeleton> skeleton, MaterialId skinnedMat);

    // 设置当前播放的 clip 和循环模式
    void play(const AnimationClip* clip, bool loop = true);
    void stop();
    void pause();

    // 每帧调用，dt 为帧时间（秒），imageIndex 供 UBO 更新
    void update(float dt, uint32_t imageIndex, MaterialManager& matMgr);

    float currentTime() const { return currentTime_; }
    bool  isPlaying()   const { return playing_; }

private:
    std::shared_ptr<Skeleton>  skeleton_;
    const AnimationClip*       currentClip_ = nullptr;
    float                      currentTime_ = 0.f;
    bool                       playing_     = false;
    bool                       loop_        = true;
    MaterialId                 skinnedMatId_;
    std::vector<glm::mat4>     localTransforms_;  // 每根骨骼的当前局部矩阵
    std::vector<glm::mat4>     finalBoneMatrices_; // 上传 GPU 用
};
```

**具体任务：**

- [x] **A4-1** 新建 `AnimationPlayer.hpp` / `AnimationPlayer.cpp`，实现上述接口
  > 实际实现：未引入独立 AnimationPlayer 类，动画求值逻辑直接集成在 `Application::drawFrame` 的逐实体循环中。功能等价，且更适合每实体独立 animation state 的架构。
- [x] **A4-2** 实现 `AnimationPlayer::update`：
  > 实际实现：`drawFrame` 中逐实体执行 `currentTime = fmod(glfwGetTime(), clip.duration)` → `evaluateBoneLocalTransform` → `computeFinalMatrices` → `updateBoneMatrices`。
  1. `currentTime_ += dt`；若 `loop_` 则对 `clip->duration` 取模，否则 clamp
  2. 遍历所有骨骼 index，调用 `clip->evaluateBoneLocalTransform(i, currentTime_)` 填入 `localTransforms_[i]`
  3. 调用 `skeleton_->computeFinalMatrices(localTransforms_, finalBoneMatrices_)`
  4. 调用 `matMgr.updateBoneMatrices(skinnedMatId_, imageIndex, finalBoneMatrices_)`
- [x] **A4-3** 在 `Application` 中增加 `std::unique_ptr<AnimationPlayer> animPlayer_` 成员
  > 实际实现：animation state 存储在每个 `ModelEntity::skeleton` / `animationClips`，无需全局 animPlayer_ 成员。
- [x] **A4-4** 在 `Application::gameLoop` 中，于 `camera_.UpdataCameraPosition` 之后调用 `animPlayer_->update(dt, imageIndex, matMgr_)`
  > 实际实现：`drawFrame` 内逐实体求值，等价于每帧 update。
- [x] **A4-5** 在 `Application::initVulkan` 末尾，若模型有骨骼动画，自动调用 `animPlayer_->bind(...)` 并播放第一条 clip
  > 实际实现：`initVulkan`/`loadScene`/`beginDragPlace` 通过 `ensureAnimationAssetForMeshAst` 按实体绑定 skeleton/clips，`drawFrame` 自动播放 clips[0]。

**验收标准：** `CesiumMan.glb` 能在屏幕上实时播放行走动画，关节正确驱动网格变形。

---

### A5. 动画资产化（Anim .ast + .anim.bin）

**目标：** 将 Skeleton + AnimationClips 保存为引擎自有资产格式，下次直接加载，不依赖原始 glTF。`.ast` 作为可读的资产 Header/Meta，实际关键帧和矩阵重数据写入 `res/bin/anim/*.anim.bin`。

**新建文件：** `src/Animation/AnimationAssetLoader.hpp` / `.cpp`

**文件格式设计（`res/content/<folder>/<name>.anim.ast`）：**

```json
{
  "version": 1,
  "type": "Anim",
  "name": "Robot_Walk",
  "rootModel": "content/Characters/Robot/Robot.mesh.ast",
  "binary": "bin/anim/Robot_Walk.anim.bin",
  "skeleton": {
    "boneCount": 191,
    "bones": [
      {
        "name": "Hips",
        "parentIndex": -1
      }
    ],
    "skins": [
      { "boneIndices": [0, 1, 2] }
    ]
  },
  "clips": [
    {
      "name": "Walk",
      "duration": 1.033,
      "channels": [
        {
          "boneIndex": 0,
          "target": "Translation",
          "interpolation": "Linear",
          "timeCount": 32,
          "valueCount": 32
        }
      ]
    }
  ]
}
```

**具体任务：**

- [x] **A5-1** 新建 `AnimationAssetLoader.hpp` / `.cpp`
- [x] **A5-2** 定义 `type="Anim"` 的 `.ast` 元数据格式：记录 `rootModel`、`binary`、bone name/parent、skin palette remap、clip/channel 描述
- [x] **A5-3** 定义 `.anim.bin` 二进制布局：写入 magic/version、bone matrices、channel times、channel values
- [x] **A5-4** 实现 `AnimationAssetLoader::save(astPath, binPath, rootModel, skeleton, clips)`：
  - `.ast` 写 Header/Meta，不写大型 keyframe 数组
  - `.anim.bin` 写 `inverseBindMatrix` / `localBindTransform` / `globalBindTransform` / `times` / `values`
- [x] **A5-5** 实现 `AnimationAssetLoader::load(astPath)`：
  - 读取 `.ast` 元数据和 `.anim.bin`
  - 返回 `AnimationAsset { skeleton, clips }` 结构体
- [x] **A5-6** 在导入带动画 glTF 时，生成 `res/content/<当前目录>/<模型名>/<模型名>.anim.ast` 和 `res/bin/anim/<模型名>.anim.bin`
- [x] **A5-7** Mesh `.ast` 新增 `animations` 字段，引用一个或多个 Anim `.ast`
- [x] **A5-8** Anim `.ast` 新增 `rootModel` 字段，指回所属 Mesh `.ast`
- [x] **A5-9** Content Browser 扫描 `res/content/**/*.ast`，支持 `Anim` 类型显示与过滤；二进制重数据按 ast type 存放到 `res/bin/<type>/`
- [x] **A5-10** 提供复制式迁移工具：将旧 `res/materials/**/*.ast`、`res/models`、`res/textures` 复制到 `res/content` / `res/bin` 新结构，保留旧文件不删除

**验收标准：** 导入带动画 glTF 后生成 Mesh/Material/Anim `.ast` 和 `.anim.bin`；直接从 Mesh `.ast` 加载时能通过 `animations` 字段恢复 Skeleton + AnimationClip，骨骼数、skin palette、clip/channel 数、duration 与原始 glTF 一致。

---

## Phase B — 状态机动画系统（Animator）

### B1. AnimatorState / AnimatorTransition 数据结构

**目标：** 定义状态机的基础单元。

**新建文件：** `src/Animation/AnimatorController.hpp`

```cpp
// 状态机参数（类似 Unity Animator Parameters）
struct AnimatorParam {
    std::string name;
    enum class Type { Float, Int, Bool, Trigger } type;
    union { float f; int i; bool b; } value;
};

// 单个状态（绑定一个 AnimationClip）
struct AnimatorState {
    std::string  name;
    std::string  clipName;      // 对应 AnimationClip::name
    float        speed = 1.0f;  // 播放速度倍率
    bool         loop  = true;
};

// 过渡条件（单个 condition）
struct TransitionCondition {
    std::string           paramName;
    enum class Op { Greater, Less, Equal, NotEqual, True, False } op;
    float                 threshold = 0.f;
};

// 两个状态之间的过渡
struct AnimatorTransition {
    std::string                     fromState;  // "" 表示 AnyState
    std::string                     toState;
    float                           fadeDuration = 0.2f;   // 秒
    bool                            hasExitTime  = false;
    float                           exitTime     = 1.0f;   // 归一化 [0,1]
    std::vector<TransitionCondition> conditions;
};
```

**具体任务：**

- [x] **B1-1** 新建 `src/Animation/AnimatorController.hpp`，定义上述所有结构体
- [x] **B1-2** 实现 `AnimatorParam` 的辅助方法：`setFloat/setInt/setBool/setTrigger`，以及 `checkCondition(const TransitionCondition&) const`

---

### B2. AnimatorController 状态机逻辑

**目标：** 实现状态机的核心驱动逻辑。

**新建文件：** `src/Animation/AnimatorController.cpp`

```cpp
class AnimatorController {
public:
    // 加载状态机定义
    void loadFromAsset(const std::string& path);

    // 运行时参数控制
    void setFloat(const std::string& name, float v);
    void setInt  (const std::string& name, int v);
    void setBool (const std::string& name, bool v);
    void setTrigger(const std::string& name);

    // 每帧更新，返回当前需要传给 AnimationPlayer 的混合指令
    struct BlendCommand {
        const AnimationClip* clipA = nullptr;  // 当前状态 clip
        const AnimationClip* clipB = nullptr;  // 目标状态 clip（过渡中才有）
        float                blendWeight = 0.f; // clipB 的权重 [0,1]
        float                timeA = 0.f;
        float                timeB = 0.f;
    };
    BlendCommand update(float dt, const std::vector<AnimationClip>& clips);

    const std::string& currentStateName() const { return currentState_; }

private:
    std::vector<AnimatorState>      states_;
    std::vector<AnimatorTransition> transitions_;
    std::vector<AnimatorParam>      params_;

    std::string  currentState_;
    std::string  nextState_;         // 过渡目标状态
    float        stateTime_    = 0.f; // 当前状态播放时间
    float        nextStateTime_= 0.f; // 目标状态已播放时间（提前起跑）
    float        blendT_       = 0.f; // 当前过渡进度 [0,1]
    bool         transitioning_= false;

    bool checkAllConditions(const AnimatorTransition& t) const;
    const AnimationClip* findClip(const std::string& clipName,
                                   const std::vector<AnimationClip>& clips) const;
};
```

**具体任务：**

- [x] **B2-1** 实现 `AnimatorController::update`：
  1. 推进 `stateTime_` += `dt * currentState.speed`
  2. 若正在过渡：推进 `blendT_` += `dt / fadeDuration`；到 1.0 时切换完成（`currentState_ = nextState_`）
  3. 若未在过渡：遍历所有 `fromState == currentState_` 或 `fromState == ""` 的 transitions；对每个 transition 调用 `checkAllConditions`；若满足则开始过渡（`transitioning_ = true`, 记录 `nextState_`, 清零 `blendT_/nextStateTime_`）
  4. 返回 `BlendCommand`（携带两个 clip 指针和时间）
- [x] **B2-2** 扩展 `AnimationPlayer`，新增 `updateWithBlend(const BlendCommand& cmd, uint32_t imageIndex, MaterialManager& matMgr)`：
  - 若 `clipB == nullptr`：退化为单 clip 求值（已有逻辑）
  - 若 `clipB != nullptr`：分别对两个 clip 在 `timeA`/`timeB` 求值，对 `localTransforms_[i]` 做逐骨骼插值（平移 `mix`，旋转 `slerp`，缩放 `mix`），权重为 `blendWeight`
- [x] **B2-3** 在 `Application` 中增加 `std::unique_ptr<AnimatorController> animCtrl_` 成员
- [x] **B2-4** 在 `Application::gameLoop` 中替换 `animPlayer_->update(dt, ...)` 为：
  ```cpp
  auto cmd = animCtrl_->update(dt, sceneMgr_.getAnimationClips());
  animPlayer_->updateWithBlend(cmd, imageIndex, matMgr_);
  ```

> **实际实现：** 延续 A4 的逐实体架构，`AnimatorController` 存放于每个 `ModelEntity`；`Application::drawFrame(dt)` 直接消费 `BlendCommand` 并执行逐骨骼 TRS 混合，不再引入全局 `AnimationPlayer` / `animCtrl_` 单例，功能对应 B2-2 至 B2-4。

**验收标准：** 能在 Idle / Walk 两个状态间切换，切换时有平滑混合过渡（无跳变）。

---

### B3. 动画混合（Cross-fade Blend）

**目标：** 骨骼混合算法正确性与品质保证。

**具体任务：**

- [x] **B3-1** 旋转插值使用 `glm::slerp`（非 `mix`），防止骨骼在大角度过渡时路径走直线（弦插值）
- [x] **B3-2** 混合权重曲线：支持三种 ease 模式（Linear、SmoothStep、EaseIn/Out），在 `AnimatorTransition` 中以 `blendCurve` 枚举存储
- [x] **B3-3** 验证极端情况：①两 clip 时长差异极大时的时间归一化；②pitch 超过 90° 骨骼不翻转；③权重 = 0.0 和 1.0 时与单 clip 求值结果完全一致

---

### B4. 状态机资产序列化（.animctrl.json）

**目标：** 状态机定义可保存、可加载，编辑器改动后重启引擎仍然有效。

**文件格式（`res/animators/<name>.animctrl.json`）：**

```json
{
  "version": 1,
  "defaultState": "Idle",
  "states": [
    { "name": "Idle",  "clipName": "Idle",  "speed": 1.0, "loop": true },
    { "name": "Walk",  "clipName": "Walk",  "speed": 1.0, "loop": true },
    { "name": "Run",   "clipName": "Run",   "speed": 1.2, "loop": true }
  ],
  "params": [
    { "name": "Speed",    "type": "Float", "defaultValue": 0.0 },
    { "name": "IsJumping","type": "Bool",  "defaultValue": false }
  ],
  "transitions": [
    {
      "from": "Idle", "to": "Walk",
      "fadeDuration": 0.2, "hasExitTime": false,
      "conditions": [{ "param": "Speed", "op": "Greater", "threshold": 0.1 }]
    },
    {
      "from": "Walk", "to": "Idle",
      "fadeDuration": 0.3, "hasExitTime": false,
      "conditions": [{ "param": "Speed", "op": "Less", "threshold": 0.1 }]
    },
    {
      "from": "", "to": "Run",
      "fadeDuration": 0.15, "hasExitTime": false,
      "conditions": [{ "param": "Speed", "op": "Greater", "threshold": 0.8 }]
    }
  ]
}
```

**具体任务：**

- [x] **B4-1** 在 `AnimatorController` 中实现 `saveToFile(path)` 和 `loadFromFile(path)`（用 nlohmann/json）
- [x] **B4-2** 扩展 `.ast` 文件格式，新增可选字段：
  ```json
  "animController": "animators/character.animctrl.json"
  ```
- [x] **B4-3** 在 `Application::loadAndApplyMaterialAsset` 中，若 `.ast` 含 `animController` 字段，自动加载对应 `.animctrl.json` 并赋值给 `animCtrl_`
- [x] **B4-4** 在 `res/animators/` 目录下放置一个 `example.animctrl.json` 作为示例模板

**验收标准：** 关闭引擎，删除运行时对象，重启后从 `.animctrl.json` 加载，状态机行为与关闭前完全一致。

---

### B5. ImGui 状态机编辑器面板

**目标：** 在 ImGui 中提供可视化的状态机查看与基本编辑能力。

**新增 ImGui 面板 `"Animator"` 在 `UIManager` 的 `prepareFrame()` 中：**

**具体任务：**

> **实际实现说明：** B5 经设计讨论后扩展为"动画总控面板"，采用三区布局（左侧侧边栏 + 右侧上半双栏 + 底部双栏），并新增 B5-6 事件驱动系统。以下任务编号对应原始 plan，实际实现细节见 TDD 4.12.3。

- [x] **B5-1** 在 `IMGUIManager.hpp` 中新增 `bool showAnimatorPanel_ = false`，在主面板顶部加一个 `Checkbox("Animator", &showAnimatorPanel_)` 按钮
- [x] **B5-2** 实现 `drawAnimatorPanel()`：
  - 左侧列表：所有状态（`ImGui::Selectable`），当前状态高亮绿色
  - 右侧参数区：每个 `AnimatorParam` 对应一个控件（float → SliderFloat，bool → Checkbox，trigger → Button）
  - 底部：当前状态名 + 播放进度条（`ImGui::ProgressBar`）
  - 若正在过渡：同时显示目标状态名和混合进度
- [x] **B5-3** 实现状态编辑功能：
  - 双击某个状态 → 弹出 `ImGui::BeginPopup`：可修改 clipName（从下拉列表中选）、speed、loop
  - 每次修改后调用 `animCtrl_->saveToFile(path)` 自动保存
- [x] **B5-4** 实现过渡编辑：
  - 选中某状态后，右侧显示"Transitions from this state"列表
  - 每条 transition 展示 from → to + conditions 摘要
  - 提供 "Add Transition" 和 "Delete Transition" 按钮
- [x] **B5-5** 底部新增 "Save .animctrl.json" 按钮，调用 `animCtrl_->saveToFile`
- [x] **B5-6**（新增）事件驱动系统：`AnimatorEvent` 结构体 + `dispatchEvent`/`dispatchEvents` 方法，为 Sequence 系统预留接口

---

### B6. 倒放与 BlendSpace1D State Motion

**目标：** 让 State 显式配置播放方向，并在保持 State Transition、Sequencer、Root Motion、多实体蒙皮和旧资产兼容的前提下支持一维混合空间。

**具体任务：**

- [x] **B6-1** 将有符号 speed 拆分为非负 `playRate` 与 `PlaybackDirection`，使用始终正向累积的归一化播放进度实现 Forward/Reverse、Loop 和方向无关的 Exit Time
- [x] **B6-2** 新增 `StateMotionType::SingleClip/BlendSpace1D`、Float blendParameter 与带 position 的 Sample 列表；区间外钳制到端点，区间内在相邻 Sample 间线性混合并按归一化相位同步不同时长 Clip
- [x] **B6-3** 将 `BlendCommand` 重构为 State 内部 Sample 混合 + State 间 Cross-fade 两层结构，支持 Single↔Single、Single↔BlendSpace 和 BlendSpace↔BlendSpace
- [x] **B6-4** 将 `.animctrl.json` 升级为 version 2，并向后迁移 version 1 的 clipName/speed；负 speed 迁移为 Reverse + abs(speed)
- [x] **B6-5** Root Motion 改为按 Clip 计算循环安全的正放/倒放 delta，再按 BlendSpace 与 Transition 权重混合；seek/反向时间跳变清空历史
- [x] **B6-6** Animator 面板支持 Motion Type、Direction、Play Rate、Float 参数和 Sample 编辑；兼容资产筛选、Clip Rename、参数 Rename/删除保护与保存前验证覆盖新引用结构
- [x] **B6-7** Sequencer `computeBlendAtTime()` 与实时 Animator 共用 Motion/播放时钟解析，AnimatorKeyframe SetFloat 可确定性驱动 BlendSpace
- [x] **B6-8** 通过 x64-debug 构建、v1/v2/Reverse/BlendSpace/Transition C++ smoke test、10 个 Python MCP 测试和 diff 检查

**验收标准：** 负 speed 旧资产重启后保持倒放；BlendSpace 在端点/中间/越界参数下姿势正确；四种 State Motion 过渡组合可工作；Sequence seek 与播放姿势一致；Root Motion 在倒放循环和 BlendSpace 权重变化时不产生瞬移；主视口、PiP、GPU Pick 与多 SkinBinding 接口不变。

---

## Phase C — Sequencer 时序动画系统

### C1. SequenceTrack / SequenceClip 数据结构

**目标：** 建立 Sequencer 的基础数据模型，支持时间轴上的多轨道多片段。

**新建文件：** `src/Animation/Sequence.hpp`

```cpp
// 轨道类型枚举
enum class TrackType {
    AnimationClip,      // 播放某段骨骼动画
    CameraPath,         // 相机沿路径运动
    TransformTween,     // 对象 Transform 补间（旧版，保留兼容）
    TransformKeyframe,  // 关键帧轨道（新版，绑定实体）
    Event,              // 触发一个命名事件（供逻辑层响应）
};

// 插值模式（7种混合曲线）
enum class TweenEase {
    Linear, SmoothStep, EaseIn, EaseOut, EaseInOut, Cubic, Exponential
};

// 时间轴上的一个片段（通用基类）
struct SequenceClipBase {
    std::string name;
    double      startTime;   // 秒
    double      duration;    // 秒
};

// 骨骼动画片段
struct AnimTrackClip : SequenceClipBase {
    std::string clipName;   // AnimationClip::name
    double      clipOffset; // 从 clip 内的哪个时间开始播
    double      playSpeed;  // 1.0 = 正常速度
};

// 相机路径片段（见 C3 详述）
struct CameraPathClip : SequenceClipBase {
    std::string pathAssetRelPath;  // 指向 .campath.json
};

// Transform 补间片段（旧版）
struct TransformTweenClip : SequenceClipBase {
    glm::vec3 startPosition, endPosition;
    glm::quat startRotation, endRotation;
    glm::vec3 startScale, endScale;
    TweenEase ease;
};

// 事件片段
struct EventClip : SequenceClipBase {
    std::string eventName;
};

// 关键帧：记录某一时刻的 Transform 状态（新版）
struct TransformKeyframe {
    double    time;           // 在序列时间轴上的位置（秒）
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
    TweenEase easeToNext;     // 到下一个关键帧的混合模式
};

// 关键帧轨道：绑定到特定实体
struct TransformKeyframeTrack {
    std::string  name;
    uint64_t     targetEntityId;  // 关联的场景实体 ID
    std::vector<TransformKeyframe> keyframes;  // 按 time 升序

    struct EvalResult { glm::vec3 position; glm::quat rotation; glm::vec3 scale; bool valid; };
    EvalResult evaluate(double t) const;
    double totalDuration() const;
};

// 单条轨道（持有若干同类型片段）
struct SequenceTrack {
    std::string name;
    TrackType   type;

    std::vector<AnimTrackClip>       animClips;
    std::vector<CameraPathClip>      cameraPathClips;
    std::vector<TransformTweenClip>  tweenClips;
    std::vector<EventClip>           eventClips;
    TransformKeyframeTrack           keyframeTrack;  // 新版关键帧轨道

    double totalDuration() const;
};

// 整个 Sequence（时间轴）
struct Sequence {
    std::string              name;
    double                   totalDuration = 0.0;
    std::vector<SequenceTrack> tracks;

    double computeTotalDuration() const;
};
```

**具体任务：**

- [x] **C1-1** 新建 `src/Animation/Sequence.hpp`，定义上述所有结构体
- [x] **C1-2** 实现 `Sequence::computeTotalDuration()`：遍历所有 track 取最大 endTime（含 keyframeTrack）
- [x] **C1-3** 实现各 clip 类型的 `evaluate(localT)` 方法：
  - `TransformTweenClip::evaluate(localT)`：按 ease 曲线插值 start→end TRS
  - `TransformKeyframeTrack::evaluate(t)`：查找前后关键帧，按 `easeToNext` 插值混合
  - `applyEaseCurve(t, ease)`：通用曲线求值函数，支持 7 种混合模式

---

### C2. Sequence 播放控制器

**目标：** 驱动 Sequence 按时间轴执行各轨道片段。

**新建文件：** `src/Animation/SequencePlayer.hpp` / `.cpp`

```cpp
class SequencePlayer {
public:
    void load(const Sequence& seq);

    void play(bool loop = false);
    void pause();
    void stop();
    void seek(float t);  // 跳转到指定时间

    // 每帧调用
    // callbacks 用于通知上层执行具体操作
    struct FrameCallbacks {
        std::function<void(float t, const std::string& pathAsset)> onCameraPathEval;
        std::function<void(const std::string& clipName, float t)>  onAnimClipEval;
        std::function<void(const std::string& entity, const glm::mat4& m)> onTransformEval;
        std::function<void(const std::string& event)>              onEvent;
    };
    void update(float dt, const FrameCallbacks& cb);

    float currentTime()   const { return currentTime_; }
    float totalDuration() const { return seq_ ? seq_->totalDuration : 0.f; }
    bool  isPlaying()     const { return playing_; }

private:
    const Sequence* seq_         = nullptr;
    float           currentTime_ = 0.f;
    bool            playing_     = false;
    bool            loop_        = false;
    std::set<int>   firedEvents_; // 本次播放中已触发的事件 clip 索引，防重复触发
};
```

**具体任务：**

- [x] **C2-1** 新建 `SequencePlayer.hpp` / `SequencePlayer.cpp`，实现上述接口
- [x] **C2-2** 实现 `SequencePlayer::update`：
  1. `currentTime_ += dt`；若超出 `totalDuration` 则 loop 或 stop
  2. 对每条 track（若 enabled && !muted）：
     - 遍历该 track 的所有 clip，判断 `currentTime_` 是否在 `[startTime, endTime)` 内
     - 在区间内：计算 `localT = (currentTime_ - startTime) / duration`，调用对应 callback
     - Event clip 特殊处理：只在 `localT > 0` 第一次进入时触发，用 `firedEventClipHashes_` 去重
- [x] **C2-3** 在 `Application` 中增加 `std::unique_ptr<SequencePlayer> seqPlayer_` 成员和 `Sequence currentSequence_`
- [x] **C2-4** 在 `Application::gameLoop` 中调用 `seqPlayer_->update(dt, callbacks)`，并在 callbacks 中连接相机路径、动画、变换的实际执行逻辑
- [x] **C2-5** Sequencer 播放期间，屏蔽右键拖拽相机（`mouseCallback` 中检测 `sequenceCameraActive_` 时跳过）

---

### C3. 相机路径轨道（Camera Path Track）

**目标：** 支持录制相机运动路径，并在 Sequencer 中回放，实现电影级摄像机运动。

**新建文件：** `src/Animation/CameraPath.hpp` / `.cpp`

**数据结构：**

```cpp
// 相机路径关键帧
struct CameraKeyframe {
    float     time;        // 秒
    glm::vec3 position;
    glm::quat orientation; // 相机朝向四元数（直接存 camera_.orientation_）
    float     fovDeg;      // 支持 FOV 动画
};

// 插值方式
enum class CameraPathInterp { Linear, CatmullRom, Bezier };

// 相机路径资产
struct CameraPath {
    std::string          name;
    CameraPathInterp     interp = CameraPathInterp::CatmullRom;
    std::vector<CameraKeyframe> keyframes;  // 按 time 排序

    // 在时间 t 处求值，返回相机位置、朝向、FOV
    struct EvalResult { glm::vec3 pos; glm::quat orient; float fovDeg; };
    EvalResult evaluate(float t) const;
};
```

**具体任务：**

- [x] **C3-1** 新建 `src/Animation/CameraPath.hpp` / `.cpp`，实现上述结构（已在 SequenceAssetLoader 中实现）
- [x] **C3-2** 实现 `CameraPath::evaluate(t)`：
  - 二分查找 `t` 所在的 `[k0, k1]` 区间
  - **Catmull-Rom 插值**（推荐默认值）：对 position 使用四点 Catmull-Rom 曲线（`k_{i-1}, k_i, k_{i+1}, k_{i+2}`），对 orientation 使用 `glm::slerp`，对 fovDeg 使用线性插值
  - `Linear`：position 和 fovDeg 线性插值，orientation `slerp`
- [x] **C3-3** 在 `Application` 中新增相机录制功能：
  ```cpp
  bool         recordingCameraPath_ = false;
  CameraPath   recordingPath_;
  double       recordingTime_  = 0.0;
  double       recordInterval_ = 0.1;
  double       recordTimer_    = 0.0;
  ```
- [x] **C3-4** 在 `Application::gameLoop` 录制模式下：每帧 `recordTimer_ += dt`，若 `>= recordInterval_` 则自动抓取当前相机状态插入 `recordingPath_.keyframes`，重置 `recordTimer_`
- [x] **C3-5** 在 `SequencePlayer::update` 的 `onCameraPathEval` callback 中，调用 `path.evaluate(t)` → `camera_.Position = result.pos; camera_.SetOrientation(result.orient); camera_.FovDeg = result.fovDeg`（已在 TODO-023 中实现）
- [x] **C3-6** 实现 `CameraPath::saveToFile(path)` / `loadFromFile(path)`（已在 SequenceAssetLoader 中实现）

---

### C4. 对象变换轨道（TransformTween + TransformKeyframe）

**目标：** 支持对场景中的对象（主模型、box、Camera Actor）做关键帧变换动画。

**两种实现方式：**

**C4-A TransformTweenClip（旧版，保留兼容）**

- [x] **C4-1** 在 `Sequence.hpp` 中确认 `TransformTweenClip` 结构已包含 start/end TRS 和 ease 模式
- [x] **C4-2** 实现 7 种 ease 函数（`applyEaseCurve(t, ease)`）：
  - Linear、SmoothStep、EaseIn、EaseOut、EaseInOut、Cubic、Exponential
- [x] **C4-3** 在 `SequencePlayer::update` 的 `onTransformTweenEval` callback 中，根据 `entity` 名称找到对应对象并更新其 `ObjectTransform`：
  - `"main"` → 更新 `Application::mainModelTransform`
  - `"box:<id>"` → 调用 `sceneMgr_.setBoxPosition(id, pos)`（当前 box 只支持位移，旋转和缩放为后续扩展）
- [x] **C4-4** 在 Sequencer ImGui 面板中，对 TransformTween clip 提供 "Record Start" 和 "Record End" 按钮（从当前对象的实际 Transform 抓取值）
  > **实际实现**：通过 **Update from Entity** 按钮将当前实体 Transform 写回选中的关键帧，等价于 Record Start/End

**C4-B TransformKeyframeTrack（新版，推荐使用）**

- [x] **C4-5** 新增 `TransformKeyframe` 结构体（time / position / rotation / scale / easeToNext）
- [x] **C4-6** 新增 `TransformKeyframeTrack` 结构体（name / targetEntityId / keyframes[] + evaluate(t)）
  - `evaluate(t)` 自动查找前后关键帧，按 `easeToNext` 插值混合
  - 边界处理：t < 首帧返回首帧值，t > 末帧返回末帧值
- [x] **C4-7** 在 `SequencePlayer` 中新增 `onTransformKeyframeEval` callback：
  - 参数为 (t, entityId, EvalResult)
  - Application 中根据 entityId 查找实体并更新 Transform + syncCameraFromTransform()
- [x] **C4-8** 实时预览机制：
  - Application 维护 `sequencerPreviewPending_` 标志
  - `requestSequencerPreview()` 设置标志，下一帧执行一次后清除
  - 非播放状态下不持续覆盖实体 Transform，允许用户通过 Gizmo 自由调整
  - 播放状态下持续驱动 Transform 更新

---

### C5. Sequence 资产序列化（.seq.json）

**目标：** 完整的 Sequence 时间轴可保存和加载。

**文件格式（`res/sequences/<name>.seq.json`）：**

```json
{
  "version": 1,
  "name": "CinematicShot01",
  "totalDuration": 8.0,
  "tracks": [
    {
      "name": "Camera",
      "type": "CameraPath",
      "cameraPathClips": [
        {
          "name": "Shot01",
          "startTime": 0.0,
          "duration": 8.0,
          "pathAsset": "sequences/paths/shot01_cam.campath.json"
        }
      ]
    },
    {
      "name": "Character",
      "type": "AnimationClip",
      "animClips": [
        { "name": "Idle", "startTime": 0.0, "duration": 3.0, "clipName": "Idle",  "clipOffset": 0.0, "playSpeed": 1.0 },
        { "name": "Walk", "startTime": 3.0, "duration": 5.0, "clipName": "Walk",  "clipOffset": 0.0, "playSpeed": 1.0 }
      ]
    },
    {
      "name": "PropMove",
      "type": "TransformTween",
      "tweenClips": [
        {
          "name": "Move01",
          "startTime": 2.0, "duration": 1.5,
          "startPosition": [0,0,0], "endPosition": [2,0,0],
          "startRotation": [1,0,0,0], "endRotation": [1,0,0,0],
          "startScale": [1,1,1], "endScale": [1,1,1],
          "ease": "SmoothStep"
        }
      ]
    },
    {
      "name": "CameraActorMove",
      "type": "TransformKeyframe",
      "keyframeTrack": {
        "name": "CameraActorMove",
        "targetEntityId": 12345,
        "keyframes": [
          {
            "time": 0.0,
            "position": [0,0,5], "rotation": [1,0,0,0], "scale": [1,1,1],
            "easeToNext": "SmoothStep"
          },
          {
            "time": 15.0,
            "position": [3,2,8], "rotation": [0.9,0.1,0,0.4], "scale": [1,1,1],
            "easeToNext": "Linear"
          }
        ]
      }
    }
  ]
}
```

**相机路径单独保存（`res/sequences/paths/<name>.campath.json`）：**

```json
{
  "version": 1,
  "name": "shot01_cam",
  "interpolation": 0,
  "keyframes": [
    { "time": 0.0, "position": [0,-4,4], "orientation": [1,0,0,0], "fovDeg": 45.0 },
    { "time": 2.0, "position": [3,-2,3], "orientation": [0.9,0.1,0,0.4], "fovDeg": 40.0 }
  ]
}
```

**具体任务：**

- [x] **C5-1** 新建 `src/Animation/SequenceAssetLoader.hpp` / `.cpp`
- [x] **C5-2** 实现 `SequenceAssetLoader::saveSequence(path, seq)` 和 `loadSequence(path, seq)` — nlohmann/json 实现，支持所有 5 种 TrackType（含 TransformKeyframeTrack）
- [x] **C5-3** 实现 `SequenceAssetLoader::saveCameraPath(path, camPath)` 和 `loadCameraPath(path, camPath)` — 加载后按 time 排序
- [x] **C5-4** 实现 7 种 TweenEase 的双向字符串转换（`easeToStr` / `strToEase`）
- [x] **C5-5** TransformKeyframeTrack 完整序列化：targetEntityId + keyframes[]（time/position/rotation/scale/easeToNext），加载后按 time 排序
- [x] **C5-6** 在 `res/sequences/` 目录下提供一个示例 `.seq.json` 文件

---

### C6. ImGui Sequencer 编辑器面板

**目标：** 提供可视化的时间轴编辑器，支持录制、播放、轨道管理、关键帧编辑。

**实际实现：** `src/IMGUIManager.cpp::drawSequencerPanel()`

**具体任务：**

- [x] **C6-1** 在 `IMGUIManager.hpp` 增加 `bool showSequencerPanel_ = false`，主面板顶部加 Checkbox 开关
- [x] **C6-2** 实现 `drawSequencerPanel()`：
  - **顶部工具栏：** Play / Pause / Stop 按钮、Loop 勾选框、Zoom 缩放、序列时长编辑（Dur 输入框）、时间显示 `"03.24 / 08.00"`、Load/Save .seq.json 按钮
  - **时间线刻度：** 基于 duration 自适应显示秒数刻度（`max(duration + 2, 10)`），用 `ImGui::GetWindowDrawList()->AddLine` 手绘刻度 + 数字标签
  - **当前时间指示线：** 红色竖线（播放位置）+ 黄色竖线（编辑位置 sequencerEditTime_）
  - **点击时间轴：** 设置编辑位置并触发实时预览（`seek()` + `requestSequencerPreview()`）
  - **轨道列表（左列）：** 每条 track 一行，显示名字 + Mute/Solo 按钮，按轨道类型显示不同颜色
  - **片段区域（右列）：** clip 绘制为彩色矩形，关键帧绘制为黄色菱形节点（选中高亮为白色），帧间连线
- [x] **C6-3** 实现片段属性编辑器（Sequencer 底部区域）：
  - 选中 `AnimTrackClip` → 显示 clipName 输入、offset、speed
  - 选中 `CameraPathClip` → 显示 path 路径输入
  - 选中 `TransformTweenClip` → 显示 start/end TRS 的 InputFloat3 和 ease 下拉（7种模式）
  - 选中 `TransformKeyframe` → 显示 time/position/rotation/scale/easeToNext 编辑
- [x] **C6-4** 实现录制控制区域（位于底部）：
  - "Start Recording" / "Stop Recording" 按钮（触发 `Application::recordingCameraPath_`）
  - 录制中显示红色 "Recording..." + 已录制时长
  - Path Name 输入框用于保存 .campath.json
- [x] **C6-5** 实现添加/删除轨道：
  - "Add Track" 按钮 → 在 `seq.tracks` 中插入新 track（默认 AnimationClip 类型）
  - "Delete Track" 按钮 → 删除选中 track
  - Track Type 下拉切换轨道类型
- [x] **C6-6** 实现加载/保存：
  - 面板顶部右侧：`InputText` 输入序列名 + Load / Save 按钮
  - 调用 `SequenceAssetLoader::loadSequence` / `saveSequence`

**新增 UI 工作流按钮（关键帧系统）：**

- [x] **C6-7** **Add Selected to Track**：将选中实体（Camera Actor）创建为 TransformKeyframe 轨道（绑定 targetEntityId）
- [x] **C6-8** **Add Keyframe**：在当前编辑时间点添加关键帧，自动记录选中实体的当前 Transform
- [x] **C6-9** **Update from Entity**：将当前实体 Transform 写回选中的关键帧（替代旧版的 Record Start/End）
- [x] **C6-10** **Preview** / **Preview Here**：触发单次预览求值（`requestSequencerPreview()`），不持续覆盖实体 Transform

---

## Phase D — 资产系统扩充

### D1. 动画资产扫描与注册

**目标：** 引擎启动时自动扫描 `res/` 目录下所有动画相关资产，注册到可访问的资产列表，供 ImGui 面板和 Application 直接引用。

**具体任务：**

- [ ] **D1-1** 新建 `src/Animation/AnimationAssetRegistry.hpp` / `.cpp`：
  ```cpp
  struct AnimAssetRegistry {
      std::vector<std::string> animJsonPaths;       // .anim.ast 列表
      std::vector<std::string> animCtrlPaths;       // .animctrl.json 列表
      std::vector<std::string> sequencePaths;       // .seq.json 列表
      std::vector<std::string> cameraPathPaths;     // .campath.json 列表
  
      void scan(const std::string& resRoot = "res/");
  };
  ```
- [ ] **D1-2** 实现 `AnimAssetRegistry::scan`：使用 `std::filesystem::recursive_directory_iterator` 遍历 `res/` 下所有文件，按扩展名分类填入对应 vector
- [ ] **D1-3** 在 `Application` 中增加 `AnimAssetRegistry assetRegistry_`，在 `initVulkan` 末尾调用 `assetRegistry_.scan()`
- [ ] **D1-4** ImGui 资产面板（见 D3）读取 `assetRegistry_` 展示内容

---

### D2. .ast 文件格式扩展

**目标：** 现有 `.ast` 材质资产文件支持引用骨骼动画和状态机，使一次 "Load .ast" 操作能完整还原模型+材质+动画绑定。

**修改 `MaterialAssetDesc` 结构体（`MaterialAssetLoader.hpp`）：**

```cpp
struct MaterialAssetDesc {
    // ... 现有字段 ...
    std::string animationAssetPath;    // 指向 .anim.json（可选）
    std::string animControllerPath;    // 指向 .animctrl.json（可选）
};
```

**具体任务：**

- [ ] **D2-1** 更新 `MaterialAssetLoader::load` 解析新增的两个字段（字段缺失时为空字符串，向后兼容）
- [ ] **D2-2** 更新 `SceneManager::dumpGltfMaterialAst`：在自动生成的 `.ast` 文件中，若当前模型有骨骼动画，自动填入 `"animation"` 字段（指向刚生成的 `.anim.json`）
- [ ] **D2-3** 更新 `Application::loadAndApplyMaterialAsset`：检测并加载 `animationAssetPath` 和 `animControllerPath`
- [ ] **D2-4** 更新 `.ast` 文件格式文档（写在 `res/materials/README.md` 或本文档的"资产格式参考"章节）

---

### D3. ImGui 资产浏览器面板

**目标：** 在主 ImGui 窗口中新增一个"Assets"标签页，集中管理所有动画相关资产。

**具体任务：**

- [ ] **D3-1** 在 `UIManager` 的主面板内新增 `TabBar`，将现有控件整理为 `"Scene"` 标签，新增 `"Assets"` 和 `"Animator"` 和 `"Sequencer"` 标签
- [ ] **D3-2** `"Assets"` 标签内容：
  - **Animation Files (.anim.json)**：列表 + 双击加载 → 调用 `Application::loadAnimationAsset`
  - **Animator Controllers (.animctrl.json)**：列表 + 双击加载
  - **Sequences (.seq.json)**：列表 + 双击加载到 `seqPlayer_`
  - **Camera Paths (.campath.json)**：列表 + 预览（显示关键帧数量和时长）
  - 顶部 "Refresh" 按钮重新调用 `assetRegistry_.scan()`
- [ ] **D3-3** 资产列表支持拖拽到轨道：将 `.anim.json` 拖到 AnimationClip 轨道上自动创建 clip（使用 `ImGui::SetDragDropPayload` / `ImGui::AcceptDragDropPayload`）

---

## 各 Phase 依赖关系与推荐开发顺序

```
A1 → A2 → A3 → A4 → A5
          ↓
          B1 → B2 → B3 → B4 → B5
               ↓
               C1 → C2 → C3 → C4 → C5 → C6
                              ↓
                              D1 → D2 → D3
```

**推荐分阶段里程碑：**

| 里程碑 | 完成条件 | 状态 |
|--------|----------|------|
| M1 基础骨骼播放 | CesiumMan.glb 实时播放行走动画 | ✅ 已完成 |
| M2 骨骼资产化 | 从 Anim .ast + .anim.bin 加载后动画正常 | ✅ 已完成 |
| M3 状态机 | Idle ↔ Walk 按参数自动切换 | ✅ 已完成 |
| M4 状态机编辑器 | 在 ImGui 里改状态机并保存 | ✅ 已完成 |
| M5 Sequencer 基础 | 时间轴驱动动画片段和 Transform 补间 | ✅ 已完成 |
| M6 相机路径 | 录制+回放相机路径 | ✅ 已完成 |
| M7 完整编辑器 | Sequencer ImGui 面板可用（含关键帧系统） | ✅ 已完成 |
| M8 资产系统 | 一键加载 .ast 还原全部状态 | ⏳ 待实现（Phase D） |

---

## 关键实现风险与注意事项

### 顶点格式兼容性（⚠️ 高风险）
新增 `boneIndices` / `boneWeights` 字段会改变 `Vertex` 的 stride，导致现有 OBJ 和无皮肤 glTF 的顶点缓冲与旧 pipeline 不兼容。
**解决方案：** 定义两个独立的 VkPipeline（现有 mesh pipeline 保持不变，新增 skinned mesh pipeline）；SceneManager 标记 `hasSkin_` 标志，Application 按此标志选择 pipeline。两套 VkBuffer（无皮肤用旧顶点格式，有皮肤用新顶点格式）。

### 骨骼矩阵 UBO 大小（⚠️ 中风险）
`256 * sizeof(mat4) = 16384 bytes`，等于 Vulkan 最小保证的 UBO 范围，覆盖已验证资产中 191-joint 的人物 skin。多 skin 模型不上传合并后骨架总数，而是按 `SubMesh::skinIndex` 上传当前 skin 的局部 palette。
**解决方案：** `kMaxBones` 作为单 skin 上限检查；超过时打印警告。多 skin glTF 使用 `skinBoneIndices` 将 skin-local palette 映射到合并骨架的最终矩阵。

### Sequencer 与游戏循环的时序（⚠️ 中风险）
Sequencer 播放期间，多个系统（AnimationPlayer、Camera、ObjectTransform）同时被驱动，需要明确执行顺序以避免一帧延迟（transform 写完后 UBO 更新才能读到）。
**解决方案：** 严格执行顺序：`seqPlayer_->update → animPlayer_->updateWithBlend → matMgr_.updateAllUBOs → recordCommandBuffer`。

### 动画资产二进制兼容性（⚠️ 中风险）
骨骼动画数据量大（1000 帧 * 50 骨骼 * 3 channel），不再把关键帧数组写进 JSON；`.ast` 只保存 Header/Meta，`.anim.bin` 保存矩阵、times 和 values。
**解决方案：** `.anim.bin` 写入 magic/version，Anim `.ast` 记录 `binary` 路径、bone/channel 计数和 `rootModel`，加载时校验格式版本与数据数量。

---

## 新增文件清单

```
src/Animation/
├── Skeleton.hpp / .cpp
├── AnimationClip.hpp / .cpp
├── AnimatorController.hpp / .cpp
├── AnimationRetargeter.hpp / .cpp
├── Sequence.hpp / .cpp
├── SequencePlayer.hpp / .cpp
├── SequencerCamera.hpp / .cpp
├── AnimationAssetLoader.hpp / .cpp
├── SequenceAssetLoader.hpp / .cpp
└── AnimationAssetRegistry.hpp / .cpp  （Phase D1，待实现）

shaders/
└── skinned_vert.glsl  （+ 编译为 skinned_vert.spv）

res/
├── content/             (.ast 资产描述文件根目录)
├── bin/
│   ├── mesh/            (Mesh payload: .obj/.gltf/.glb)
│   ├── anim/            (Animation payload: .anim.bin)
│   └── texture/         (Texture payload)
├── animators/           (.animctrl.json 存放目录)
└── sequences/
    └── paths/           (.campath.json 存放目录)
```

**修改现有文件：**

| 文件 | 修改内容摘要 |
|------|-------------|
| `vectex.hpp` | 新增 `boneIndices`/`boneWeights` 字段，有皮肤 vs 无皮肤需两套 attribute descriptor |
| `VulkanTypes.hpp` | 新增 `BoneMatricesUBO` 结构体 |
| `SceneManager.hpp/.cpp` | 新增 `skeleton_`/`animationClips_` 成员；`loadModelFromGltf` 解析动画；`hasSkin_` 标志 |
| `MaterialManager.hpp/.cpp` | 新增 `createSkinnedMeshMaterial` 和 `updateBoneMatrices` |
| `PipelineManager.hpp/.cpp` | 新增 skinned mesh pipeline 和对应 descriptor set layout |
| `Application.hpp/.cpp` | 新增 animPlayer_、animCtrl_、seqPlayer_、录制相关成员；gameLoop 接入各系统更新 |
| `IMGUIManager.hpp/.cpp` | 重构为 TabBar；新增 Animator / Sequencer / Assets 面板 |
| `MaterialAssetLoader.hpp/.cpp` | 扩展 `MaterialAssetDesc` 和 parse 逻辑 |
