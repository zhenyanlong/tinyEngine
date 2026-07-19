# tinyEngine 动画帧率压缩与误差控制计划

> **状态：** 待批准，尚未开始实现  
> **目标：** 将导入动画按目标帧率重采样并导出，在明确的误差标准下删除冗余关键帧，导出后打印压缩率、误差和最差样本等报告。  
> **实施建议：** 先完成一晚可交付的 FBX Linear 动画 MVP；梯度下降、CUBICSPLINE 优化和存储量化作为后续阶段。

---

## 1. 结论与范围判断

该功能适合加入现有动画资产导入链路，但不应把“梯度下降调整曲线”作为第一版的核心算法。

原因如下：

- 关键帧是否保留是离散选择问题，普通梯度下降不直接适合解决。
- 四元数旋转位于单位球面，直接优化分量需要额外约束和归一化。
- 只优化均方误差可能降低平均误差，却增大某个骨骼、某个时间点的最大误差。
- 导出功能需要可重复、可解释，并能用硬门槛判断 PASS/FAIL；误差约束关键帧删减比梯度下降更适合作为首版。

### 时间判断

- **一晚可完成：** FBX 动画专用导入路径、Linear 输出、目标帧率配置、误差约束删帧、骨架姿态验证、控制台报告及核心测试。
- **一晚不可可靠完成：** 梯度下降曲线优化、完整 CUBICSPLINE 切线优化、glTF/FBX 全路径统一、二进制量化、高级 UI、自动多轮回填和完整性能调优。
- **完整版本预计：** 在 MVP 之后增加约 2～4 个工作日。

---

## 2. 当前实现审查

### 2.1 已有能力

- `FbxImporter` 能使用 ufbx 烘焙 FBX 动画。
- `AnimationClip` 支持 Translation、Rotation、Scale 通道。
- 运行时支持 Step、Linear、CubicSpline 插值。
- Linear 旋转使用 `glm::slerp`，CubicSpline 旋转结果会归一化。
- `AnimationAssetLoader` 能把动画写入 `.anim.ast` 和 `.anim.bin`。
- 动画专用 FBX 导入已经支持骨骼重定向，并打印 matched/unmatched 骨骼数量。

### 2.2 当前问题

#### P1：参考动画已在导入阶段提前丢失精度

`FbxImporter.cpp` 的普通 FBX 和动画专用 FBX 两条路径目前均使用：

```cpp
bakeOpts.resample_rate = 30.0;
bakeOpts.key_reduction_enabled = true;
bakeOpts.key_reduction_rotation = true;
```

因此进入 `AnimationClip` 的数据已经是 30 Hz 且经过 ufbx 删帧的结果，无法再准确衡量“导出动画相对原始 FBX 曲线”的误差。

**处理要求：** 压缩路径必须禁用预删帧，并生成更高采样率的未压缩参考动画；MVP 默认参考帧率为 120 Hz。

#### P1：局部通道误差不足以评价视觉误差

父骨骼很小的旋转误差可能沿层级放大，在手、脚或武器挂点形成明显位移。因此不能只统计局部 Translation/Rotation/Scale 差异。

**处理要求：** 最大骨架姿态空间误差是主要硬门槛，局部通道误差只作为诊断信息。

#### P2：插值类型不能使用同一种删帧规则

- Linear：使用运行时相同的线性插值或四元数 slerp 重建。
- Step：只能合并连续相同值，不能跨越跳变时间拟合。
- CubicSpline：输出值包含入切线、关键值和出切线，不能按普通 Linear 通道处理。

**MVP 范围：** 当前 FBX 烘焙输出统一为 Linear，先只支持 Linear 压缩；Step/CubicSpline 放入后续阶段。

#### P2：当前导出不是事务式写入

动画二进制、动画 AST 和目标 Mesh AST 按顺序直接写入。发生磁盘或序列化错误时，可能产生不完整文件、孤立文件或被截断的引用文件。

**处理要求：** 验证必须在写入前完成；输出先写临时文件，全部成功后再替换正式文件，最后更新 Mesh 引用。

#### P3：缺少压缩配置、报告和资产元数据

当前动画导入 UI 只能选择目标 Mesh，不能配置目标帧率、误差预算或质量档位；`.anim.ast` 也未保存压缩参数和验证结果。

---

## 3. 术语与功能边界

本计划区分三类压缩：

1. **帧率压缩（Resampling）**  
   将参考动画按 15/24/30/60 Hz 等目标帧率均匀采样。

2. **关键帧压缩（Key Reduction）**  
   在目标采样序列中删除可由相邻关键帧重建、且误差不超过预算的关键帧。

3. **数值压缩（Quantization）**  
   使用 16 bit、定点数、范围编码等降低单个关键值的存储大小。

一晚 MVP 只实现前两项，不修改 `.anim.bin` 的数值布局和版本。

---

## 4. 压缩误差标准

### 4.1 比较基准

压缩误差比较对象定义为：

```text
未压缩且已完成重定向的参考动画
                 vs
相同骨架、相同时长的压缩动画
```

这样可以把“骨骼重定向误差”和“动画压缩误差”分离。重定向的 matched/unmatched 统计继续单独报告。

MVP 中的“原动画误差”实际指相对 **120 Hz 未删帧参考烘焙动画** 的误差。报告必须明确标记为 `referenceBakeFps=120`，不能宣称是对连续 FBX 曲线的数学精确比较。

### 4.2 主要姿态误差

对骨骼 `b` 和时间 `t`：

```text
E_pose(t, b) = length(
    G_reference(t, b)  * p_b
  - G_compressed(t, b) * p_b
)
```

- `G`：从根骨骼累计到当前骨骼的全局/模型空间变换。
- `p_b`：骨骼末端测试点；叶骨骼没有自然长度时，使用按角色高度生成的探针点。
- 硬门槛使用所有骨骼和验证时间中的最大值：`maxPoseError`。

### 4.3 辅助通道误差

#### 平移误差

```text
E_translation = length(T_reference - T_compressed)
```

#### 旋转误差

```text
E_rotation = 2 * acos(clamp(abs(dot(q_reference, q_compressed)), 0, 1))
```

使用 `abs(dot)` 保证 `q` 与 `-q` 被视为同一旋转。报告时转换为角度。

#### 缩放误差

```text
E_scale = max(abs(S_reference - S_compressed) / max(abs(S_reference), epsilon))
```

### 4.4 默认 Balanced 质量标准

- `poseErrorTolerance = 0.1% × characterHeight`
- 对 2 米角色约为 2 mm。
- 动画时长必须一致。
- 首尾姿态必须保留。
- 不允许 NaN、Inf、零长度四元数或无效缩放。
- `maxPoseError <= poseErrorTolerance` 才能导出。
- RMS、P95、P99 用于质量诊断，不代替最大误差硬门槛。

建议后续提供三个预设：

| 档位 | 最大姿态误差 | 适用场景 |
|---|---:|---|
| High | 角色高度的 0.05% | 近景角色、面部以外的高质量骨骼动画 |
| Balanced | 角色高度的 0.10% | 默认游戏角色动画 |
| Aggressive | 角色高度的 0.25% | 远景角色、大批量背景动画 |

### 4.5 验证采样时间

验证集合取以下时间点的并集，并去重排序：

- 参考动画的全部关键时间；
- 压缩动画的全部关键时间；
- 每个压缩区间的中点；
- 按 `validationFps = max(120, 4 × targetFps)` 生成的均匀时间点；
- 动画起点和终点。

---

## 5. 推荐算法

### 5.1 总体流程

```text
FBX 原始曲线
  → 禁用 ufbx key reduction
  → 120 Hz 参考烘焙
  → 骨骼重定向
  → 目标帧率重采样
  → 误差约束递归删帧
  → 骨架姿态空间验证
  → PASS：生成报告并导出
  → FAIL：拒绝导出并打印最差样本
```

### 5.2 目标帧率重采样

- 采样时间为 `0, 1/targetFps, 2/targetFps, ... duration`。
- 无论最后一个规则采样点是否等于 `duration`，都必须显式包含终点。
- 使用 `AnimationClip` 运行时相同的求值方式生成目标值。
- MVP 输出统一使用 Linear 通道。

### 5.3 误差约束递归删帧

对每个 Linear 通道：

1. 保留区间首尾关键帧。
2. 使用首尾值和运行时插值器重建区间内所有候选关键帧。
3. 计算每个候选点的通道误差，找到误差最大的关键帧。
4. 如果最大误差不超过预算，删除区间内部关键帧。
5. 如果超过预算，保留最差关键帧，并对左右区间递归。
6. 四元数通道预先统一符号连续性，每次插值和输出后归一化。

通道预算用于初筛，最终是否通过仍由骨架姿态空间验证决定。

### 5.4 梯度下降的后续定位

梯度优化只作为可选第二阶段：

- 先由确定性算法决定关键帧数量和时间。
- 固定关键时间，仅优化保留关键帧的值或 CubicSpline 切线。
- 目标函数同时包含 RMS 姿态误差和最大误差惩罚。
- 每轮更新后归一化旋转四元数。
- 优化结果只有在关键帧数不增加、最大误差不恶化且通过硬门槛时才接受。

不允许用梯度优化结果绕过最终姿态验证。

---

## 6. 建议代码结构

### 6.1 新建文件

```text
src/Animation/AnimationCompression.hpp
src/Animation/AnimationCompression.cpp
```

建议数据结构：

```cpp
enum class AnimationCompressionQuality {
    High,
    Balanced,
    Aggressive,
    Custom,
};

struct AnimationCompressionOptions {
    float targetFps = 30.0f;
    float referenceFps = 120.0f;
    float validationFps = 120.0f;
    float poseErrorToleranceMeters = 0.002f;
    float translationToleranceMeters = 0.0005f;
    float rotationToleranceDegrees = 0.25f;
    float scaleRelativeTolerance = 0.001f;
    bool preserveEndpoints = true;
    bool failOnThresholdExceeded = true;
};

struct AnimationCompressionReport {
    bool passed = false;
    uint64_t originalKeyCount = 0;
    uint64_t compressedKeyCount = 0;
    uint64_t originalPayloadBytes = 0;
    uint64_t compressedPayloadBytes = 0;
    float maxPoseErrorMeters = 0.0f;
    float rmsPoseErrorMeters = 0.0f;
    float p95PoseErrorMeters = 0.0f;
    float p99PoseErrorMeters = 0.0f;
    float maxTranslationErrorMeters = 0.0f;
    float maxRotationErrorDegrees = 0.0f;
    float maxScaleRelativeError = 0.0f;
    std::string worstClip;
    std::string worstBone;
    float worstTimeSeconds = 0.0f;
    double compressionTimeMs = 0.0;
    double validationTimeMs = 0.0;
};

struct AnimationCompressionResult {
    std::vector<AnimationClip> clips;
    AnimationCompressionReport report;
};
```

### 6.2 需要修改的现有位置

- `src/FbxImporter.hpp/.cpp`
  - 新增 FBX 动画烘焙选项。
  - 取消两条代码路径中写死的 30 Hz。
  - 压缩路径禁用 ufbx 预删帧。

- `src/Application.cpp::importAnimationFbx`
  - 在重定向完成后、`AnimationAssetLoader::save()` 前执行压缩和验证。
  - 验证失败时不修改目标 Mesh AST。

- `src/Animation/AnimationAssetLoader.cpp`
  - 保持 `.anim.bin` version 1 通道布局不变。
  - 在 `.anim.ast` 中增加可选 `compression` 元数据。
  - 使用临时文件和最终替换，避免半完成输出。

- `src/IMGUIManager.hpp/.cpp`
  - MVP 可以先使用默认 Balanced 配置。
  - 后续增加目标 FPS、质量预设、Advanced 参数和结果摘要。

- `CMakeLists.txt`
  - 注册新增压缩实现和测试文件。

---

## 7. 导出报告规范

### 7.1 控制台摘要

```text
[AnimCompress] PASS
  source:              walk.fbx
  target fps:          30
  reference fps:       120
  clips:               1
  keys:                18420 -> 3260 (-82.30%)
  payload:             312.5 KiB -> 57.1 KiB (-81.73%)
  max pose error:      1.42 mm / 2.00 mm
  RMS / P95 / P99:     0.18 / 0.63 / 0.94 mm
  max rotation error:  0.17 deg
  worst sample:        Walk / RightHand / 1.733 s
  compress / validate: 21.4 / 13.8 ms
  output:              ...anim.ast + ...anim.bin
```

失败时输出：

```text
[AnimCompress] FAIL: max pose error exceeded
  measured: 3.61 mm
  allowed:  2.00 mm
  worst:    Walk / LeftFoot / 0.867 s
  output:   not written
```

### 7.2 `.anim.ast` 可选元数据

```json
{
  "compression": {
    "algorithm": "error_bounded_key_reduction_v1",
    "targetFps": 30.0,
    "referenceFps": 120.0,
    "validationFps": 120.0,
    "poseToleranceMeters": 0.002,
    "originalKeyCount": 18420,
    "compressedKeyCount": 3260,
    "maxPoseErrorMeters": 0.00142,
    "rmsPoseErrorMeters": 0.00018,
    "passed": true
  }
}
```

该字段仅为描述性元数据，不改变当前 `.anim.bin` 读取方式。

---

## 8. 分阶段任务清单

## Phase AC0 — 配置与参考动画

**目标：** 获得未被 ufbx 预删帧污染的参考动画，并消除写死的 30 Hz。

- [ ] **AC0-1** 定义 `AnimationCompressionOptions` 和质量预设。
- [ ] **AC0-2** 为 FBX 烘焙增加 `resampleRate`、`keyReductionEnabled` 配置。
- [ ] **AC0-3** 合并普通 FBX 与动画专用 FBX 中重复的 bake options 构造逻辑。
- [ ] **AC0-4** 在动画专用导入中生成 120 Hz、未删帧参考动画。
- [ ] **AC0-5** 明确参考动画在重定向完成后复制并保留到验证结束。

**验收标准：** 同一 FBX 可分别生成 120 Hz 参考动画和指定目标帧率动画；日志显示实际配置，且参考路径未启用 ufbx key reduction。

---

## Phase AC1 — 重采样与关键帧删减

**目标：** 实现确定性、误差有上界的 Linear 动画压缩。

- [ ] **AC1-1** 实现目标时间序列生成，保证包含首尾。
- [ ] **AC1-2** 实现 Translation/Scale Linear 重采样。
- [ ] **AC1-3** 实现 Rotation slerp 重采样、四元数符号连续性和归一化。
- [ ] **AC1-4** 实现递归最差点保留算法。
- [ ] **AC1-5** 统计每个 clip/channel 压缩前后的关键帧数量。
- [ ] **AC1-6** 不修改源 `AnimationClip`，失败时能够完整回退。

**验收标准：** 常量曲线和严格线性曲线只保留必要关键帧；非线性曲线在配置的通道误差内稳定删帧，多次运行结果一致。

---

## Phase AC2 — 骨架姿态误差验证

**目标：** 使用最终视觉相关的空间误差决定是否允许导出。

- [ ] **AC2-1** 生成验证时间集合。
- [ ] **AC2-2** 在同一时间求值参考动画与压缩动画的局部 TRS。
- [ ] **AC2-3** 沿骨架父子层级组合模型空间矩阵。
- [ ] **AC2-4** 生成骨骼末端点和叶骨骼探针点。
- [ ] **AC2-5** 计算 max/RMS/P95/P99 姿态误差。
- [ ] **AC2-6** 记录最差 clip、bone、time 和通道辅助误差。
- [ ] **AC2-7** 检查 NaN、Inf、四元数和动画时长等结构性错误。

**验收标准：** 人工构造的“父骨骼小角度误差、子骨骼长距离放大”测试能被 `maxPoseError` 检出；超过门槛时结果为 FAIL。

---

## Phase AC3 — 导出集成与报告

**目标：** 把压缩接入动画专用 FBX 导出，并确保失败不污染正式资产。

- [ ] **AC3-1** 在 `importAnimationFbx` 重定向后执行压缩。
- [ ] **AC3-2** 只有验证 PASS 才调用资产保存。
- [ ] **AC3-3** 使用临时文件保存动画 AST/BIN，成功后替换正式文件。
- [ ] **AC3-4** 动画资产成功后再更新目标 Mesh AST 引用。
- [ ] **AC3-5** 输出总体和逐 clip 压缩报告。
- [ ] **AC3-6** 在 `.anim.ast` 写入可选 compression 元数据。

**验收标准：** PASS 时动画可保存并重新加载；FAIL 时正式动画和 Mesh AST 不发生变化，控制台能定位最差骨骼与时间。

---

## Phase AC4 — 测试与一晚 MVP 验收

- [ ] **AC4-1** 常量通道零误差删减测试。
- [ ] **AC4-2** 线性通道只保留端点测试。
- [ ] **AC4-3** 非线性曲线门槛测试。
- [ ] **AC4-4** `q` 与 `-q` 旋转零误差测试。
- [ ] **AC4-5** 四元数输出单位长度测试。
- [ ] **AC4-6** 骨架层级误差放大测试。
- [ ] **AC4-7** 首尾关键帧和动画时长保持测试。
- [ ] **AC4-8** `.anim.ast/.anim.bin` 保存、加载、再验证测试。
- [ ] **AC4-9** `cmake --build --preset x64-debug` 构建通过。
- [ ] **AC4-10** 使用至少一个真实 FBX 动画完成导入冒烟测试。

**一晚 MVP 完成定义：** AC0～AC4 全部通过；只支持动画专用 FBX 导入和 Linear 输出是允许的，但必须在日志与文档中明确限制。

---

## Phase AC5 — 后续增强

- [ ] **AC5-1** UI 增加目标帧率与 High/Balanced/Aggressive 预设。
- [ ] **AC5-2** Advanced UI 显示通道预算与验证采样率。
- [ ] **AC5-3** 支持 Step 通道，仅合并连续相同值并保留跳变。
- [ ] **AC5-4** 支持 CubicSpline 切线保持与压缩。
- [ ] **AC5-5** 支持 glTF 动画导入压缩。
- [ ] **AC5-6** 姿态验证失败时，在最差时间自动恢复关键帧并重新验证。
- [ ] **AC5-7** 固定关键时间后的可选梯度/L-BFGS 曲线微调。
- [ ] **AC5-8** 16 bit 时间、旋转和向量量化。
- [ ] **AC5-9** 生成 JSON/CSV 压缩报告，支持批量资产分析。
- [ ] **AC5-10** 通过 MCP 暴露压缩配置、执行、进度和报告查询工具。

---

## 9. 一晚实施排期

严格按 MVP 范围估算：

| 时间 | 工作内容 |
|---:|---|
| 0.5～1.0 h | AC0：配置、参考烘焙、移除写死参数 |
| 1.5～2.0 h | AC1：重采样与递归删帧 |
| 1.5～2.0 h | AC2：骨架姿态验证和统计 |
| 1.0～1.5 h | AC3：导出集成、报告、失败保护 |
| 1.0～1.5 h | AC4：单元测试、构建、真实 FBX 冒烟测试 |
| 0.5～1.0 h | 修复缓冲与文档同步 |

**总计：约 6～9 小时。**

如果真实 FBX 冒烟测试暴露骨骼重定向或单位制问题，应优先保证正确性，停止增加 UI 或梯度优化，不应为了满足“一晚”而降低误差验证标准。

---

## 10. 风险与决策规则

| 风险 | 影响 | 应对 |
|---|---|---|
| 参考烘焙帧率不足 | 高频运动误差被漏检 | 默认 120 Hz，并允许提升；报告明确参考 FPS |
| 角色尺寸差异大 | 固定毫米门槛过严或过松 | 使用角色高度百分比并显示换算毫米值 |
| 叶骨骼无长度 | 旋转误差无法由端点位移体现 | 使用按角色尺度生成的叶骨骼探针 |
| 局部误差通过但层级误差失败 | 导出被拒绝 | 姿态空间最大误差拥有最终决定权 |
| 压缩收益过低 | 功能正确但价值不足 | 报告收益，不放宽硬门槛；后续增加回填和优化算法 |
| 写文件中途失败 | 资产损坏 | 临时文件、验证先行、Mesh 引用最后更新 |
| 一晚范围膨胀 | 测试不足、实现不稳定 | 严格限定 FBX animation-only + Linear MVP |

### 执行决策门

在用户明确批准前，本计划仅作为设计文档，不开始修改实现。

建议批准口径：

```text
按 animation-compression-plan.md 的一晚 MVP（AC0～AC4）执行，AC5 暂缓。
```

