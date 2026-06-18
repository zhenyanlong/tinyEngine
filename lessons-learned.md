# tinyEngine — Lessons Learned

> 记录外部验证过程中发现的问题、根因和教训，供未来开发参考。
> 由 `verify-fix` skill 自动维护，日期倒序排列。

---

## 2026-06-18 — #1

### 原始条目
- Extended Vertex struct with `boneIndices` (ivec4) and `boneWeights` (vec4) fields for skinning vertex data
- Implemented `createSkinnedMeshMaterial()` in `MaterialManager` with per-swapchain-image `BoneMatricesUBO` allocation
- Added `updateBoneMatrices()` to `MaterialManager` for per-frame bone matrix upload via `memcpy`
- Updated `SceneManager::loadModelFromGltf` to read `JOINTS_0` (uint8/uint16/uint32 via raw buffer) and `WEIGHTS_0` vertex attributes, set `ModelEntity::hasSkin_` flag

### 发现的问题
1. 骨骼解析（skeleton + animations）位于顶点读取循环 **之后**，导致 `skeleton_` 始终为 `nullptr`，`hasSkinData = false`，`hasSkin_` 从未被设置
2. 原始的 skin 解析 + animation 解析代码在顶点循环后面还 **重复了一份**（约 150 行重复逻辑）
3. JOINTS_0 索引无越界检查，骨骼数超过 kMaxBones 或 glTF 携带非骨骼 joint 时可能读到无效矩阵
4. 骨骼权重未归一化，非归一化权重会导致顶点位移异常

### 根因
- **顺序错误**：AI 实现时把骨骼解析放到了顶点加载之后，但顶点加载又需要 skeleton_ 来判断是否读取蒙皮数据 → 死循环
- **重复代码**：AI 在需要"先解析骨骼"后没有删除旧的解析代码块，导致逻辑残留
- **输入假设**：默认 glTF 数据的 joint 索引合法、weight 归一化，未考虑实际资产中非标准数据

### 修复
1. 将 skin 解析提前到 line 307（顶点读取之前），删除旧的重复解析块
2. 增加 `boneLimit = min(skeleton->bones.size(), kMaxBones)`，joint 越界时 clamp 到 0 并打印警告
3. 对 WEIGHTS_0 求和归一化：`sum > 1e-6 ? weights/sum : {1,0,0,0}`

### 教训
1. **数据依赖决定代码顺序**：如果顶点阅读需要 skeleton_ 非空，skeleton 的解析代码必须严格在顶点处理之前。检查 `if (skeleton_)`/`if (hasSkinData)` 这类判断前面是否有赋值其依赖变量的代码
2. **移动代码后必须删除原代码**：重构时 `SearchReplace` 容易创建副本，完成后务必手动检查并清理重复块
3. **外部数据输入必须有防御**：glTF 资产可能有非标准 joint 索引或未归一化 weight → 始终加 clamp + normalize + warn

---

## 2026-06-18 — #2

### 原始条目
- Implemented `createSkinnedMeshMaterial()` in `MaterialManager` with per-swapchain-image `BoneMatricesUBO` allocation
- Added `updateBoneMatrices()` to `MaterialManager` for per-frame bone matrix upload via `memcpy`
- Extended `MaterialEntry` with `hasSkinning_` flag and `boneUBOs/Memory/Mapped` vectors, updated `allocateDescSets` / `writeDescSets` / `destroyEntry` / `onSwapchainRecreate` for skinned material support

### 发现的问题
BoneMatricesUBO 在 `createSkinnedMeshMaterial`和 `onSwapchainRecreate` 中分配后 **未初始化**（未映射的内存是随机内容），导致在动画播放前骨骼矩阵包含垃圾值，模型塌陷到原点或出现爆炸网格。

### 根因
- 假设 `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` 分配的内存在 `vkMapMemory` 后会自动清零——实际 Vulkan spec 不保证初始内容
- `updateBoneMatrices` 用 `memcpy` 整个 `sizeof(BoneMatricesUBO)` 覆盖到 mapped buffer，但当 finalBoneMatrices 不足 128 根时，未被覆盖的骨骼是上一次的随机数据

### 修复
- 创建 `makeIdentityBoneMatricesUBO()` 辅助函数，生成全 identity mat4 的 UBO
- 在 `createSkinnedMeshMaterial`、`onSwapchainRecreate` 的 bone UBO 创建循环末尾 `memcpy(identity)`
- `updateBoneMatrices` 改为先 `makeIdentityBoneMatricesUBO()` 作为基线，再只覆盖传入的 finalBoneMatrices

### 教训
1. **GPU 可见的 UBO 必须显式初始化**：Vulkan 不保证 `VK_MEMORY_PROPERTY_HOST_VISIBLE` 内存的初始内容——永远在 `vkMapMemory` 后立刻写 identity/zero 兜底值
2. **部分更新策略：先填 identity 再覆盖**：不要只 `memcpy` 传入的数据 → 先 `memset` identity，再 memcpy 有效部分，确保剩余骨骼不会残留脏数据
3. **每个 "创建 → 映射 → 初始化 → unmap" 序列中，初始化步骤不可跳过**：尤其是 swapchain recreate 时重建的 buffer

---

## 2026-06-18 — #3

### 原始条目
- Added skinned descriptor set layout (binding 6 = BoneMatricesUBO, vertex stage) and skinned mesh pipeline (`skinnedMeshPipeline_`) to `PipelineManager`

### 发现的问题
`PipelineManager::recreate()` 中调用 `createSkinnedPipeline(ctx, rpMgr.getMainRenderPass(), dir + "skinned_vert.spv", vertSpv, extent)` — 第四个参数传入的是 **vertex shader 路径**（`vertSpv`），但该参数期望的是 **fragment shader 路径**（`fragSpv`）。导致 `buildSkinnedPipeline` 把 `vert.spv` 当作 fragment shader 编译，管线创建失败 → 实际使用 default fallback pipeline。

对比 `PipelineManager::create()` 中的调用是正确的：`createSkinnedPipeline(ctx, rpMgr.getMainRenderPass(), dir + "skinned_vert.spv", fragSpv, extent)`

### 根因
`recreate()` 是 copy-paste 自 `create()` 的修改版本。在 `create()` 中 `createSkinnedPipeline` 是最后新增的一行，参数正确。在 `recreate()` 中 copy-paste 时，错误地把同函数内的局部变量 `vertSpv` 用在了第三和第四两个参数位（而正确应该是 `fragSpv`）。

### 修复
将 `recreate()` 中的传参改为：`createSkinnedPipeline(ctx, rpMgr.getMainRenderPass(), dir + "skinned_vert.spv", fragSpv, extent)`

### 教训
1. **recreate() 和 create() 的代码重复是高风险区**：两函数逻辑几乎一样，新增功能时必须同时在两处修改并**逐参数核对**。更好的做法是重构为 `create()` 调用 `recreate()` 或反之，消除重复
2. **Shader pipeline 参数静默失败难以发现**：如果 `buildSkinnedPipeline` 只打 stderr 日志并返回 VK_NULL_HANDLE（不回退抛异常），错误可能被忽略。确保 pipeline 创建失败时有充分可见性
3. **命名 vs 类型：局部变量名相同但含义不同**：`create()` 中 `vertSpv` 是参数（主顶点 shader），但在 `recreate()` 中也是参数——copy-paste 时容易在"新增性行"中误用

---

## 2026-06-18 — #4

### 原始条目
- Integrated skinned pipeline routing in `Application::recordCommandBuffer` with dynamic `VkPipelineLayout` selection
- Auto-created skinned materials for skeletal entities in `initVulkan`, `recreateSwapChain`, `beginDragPlace`, and `loadAndApplyMaterialAsset`

### 发现的问题
1. `recordCommandBuffer` 中 `bindDescriptorSets` 可以动态选择 layout，但 `vkCmdPushConstants` 仍硬编码使用 `pipeMgr_.getMainPipelineLayout()` → 对于 skinned pipeline，push constants 写入了错误的 layout
2. `drawFrame` 中没有动画采样逻辑——`updateBoneMatrices` 被实现了但从未被调用，骨骼动画不播放
3. 每帧需要遍历所有 skinned entity，为其所有 skinned material 上传骨骼矩阵

### 根因
- **push constants layout**：AI 在实现"动态 layout 选择"时只更新了 `bindDescriptorSets`，忘记 `pushConstants` 也需要对应的 layout
- **缺失动画播放**：A3 是"蒙皮管线"而非"动画播放"——但验证 LLM 发现没有动画播放就不能验证蒙皮管线是否正确。这是 Phase 阶段之间的 implicit dependency：没有 A4 的 AnimationPlayer，A3 无法功能验证

### 修复
- `vkCmdPushConstants` 改为使用当前实际绑定的 `pipeLayout`（skinned/main 分支选择）
- `drawFrame` 中新增：每帧计算 `glfwGetTime() % clip.duration` → `evaluateBoneLocalTransform(..., fallbackLocalTransform)` → `computeFinalMatrices` → 遍历所有 skinned entity 调用 `updateBoneMatrices`
- 同一 entity 的多个 slot 共用同一份 bone matrix（去重）

### 教训
1. **push constants 和 descriptor sets 的 layout 必须始终保持一致**：如果根据 pipeline 切换了 bindDescriptorSets 的 layout，vkCmdPushConstants 也必须同步切换
2. **跨 phase 的验证依赖要提前埋 stub**：A3 无法独立验证 → 应该在 A3 末尾加一段临时动画播放代码（哪怕只放第一帧的 bind pose），确保蒙皮渲染至少有一个有效输入输出循环
3. **"被调用但从不被调用"是常见缺陷**：实现 `updateBoneMatrices` 后必须确认它在主循环的某个地方被实际调用

---

## 2026-06-18 — #5

### 原始条目
- (Cross-cutting A1/A2 fix) AnimationClip::evaluateBoneLocalTransform only accepts (boneIndex, t), but many glTF animations only animate rotation for some bones — bones without Translation/Scale channels defaulted to (0,0,0), collapsing the model to the origin for those bones

### 发现的问题
`evaluateBoneLocalTransform(int boneIndex, float t)` 对于通道缺失的骨骼 T/R/S 组件无法回退到绑定姿态的合理值。例如只有 rotation 通道的骨骼，平移和缩放在求值时默认为 `(0,0,0)` 和 `(1,1,1)`，而非该骨骼的 bind-pose 值 → 导致骨骼位置错误收缩到原点。

### 根因
AI 实现的求值逻辑假设每条骨骼都有完整的 T+R+S 三条通道——实际 glTF 资产可以只有 rotation 通道。`evalVec` lambda 内部 `ch.values.front()` 在 CubicSpline 模式下的布局是 `[inTan, value, outTan]` 三联组，偏移计算也可能出错。

### 修复
- 新增 `evaluateBoneLocalTransform(int boneIndex, float t, const glm::mat4& fallbackLocalTransform)` 重载
- 调用方（`drawFrame` 中）传入 `skeleton->bones[i].localBindTransform` 作为 fallback
- 重载内部先将 `fallbackLocalTransform` 分解为 T/R/S 分量，对通道中存在的属性用关键帧求值，缺失的属性用分解值填充

### 教训
1. **动画数据格式允许部分通道**：glTF animation 的每个 channel 独立存在——T/R/S 不是全有或全无。求值函数必须能处理任意通道组合，缺失的组件应从 bind-pose 或 identity 获取
2. **为 lookup 型函数提供 fallback 重载**：`evaluateBoneLocalTransform(bone, t)` 应该调用更通用的 `evaluateBoneLocalTransform(bone, t, fallback)`，默认传 identity/bind-pose
3. **CubicSpline 数据布局是三联组**：每个 keyframe 占用 3 个 vec4（inTangent, value, outTangent），索引计算为 `i*3 + 0/1/2`，不能与 Linear/Step 的 1:1 索引混合

---

## 2026-06-18 — #6

### 原始条目
- Multi-skin glTF support: merge multiple skins into one animation skeleton and render all submeshes correctly.

### 发现的问题
双部件模型包含两个 skin：机器人 skin 为 72 joints，人物 skin 为 191 joints。把顶点 `JOINTS_0` 改写成合并骨架的 global bone index 后，shader 仍按一个固定 bone palette 解释这些索引，导致人物部分网格读取到机器人 skin 的矩阵或被 clamp 到 bone 0。表现为人物上半身动画基本正确，但下半身跟随机器人 idle。

### 根因
glTF 的 `JOINTS_0` 是相对于当前 mesh 所属 skin 的局部 joint index，不是全文件唯一的 bone index。多 skin 模型可以共享一个动画求值骨架，但 GPU shader 每次 draw 需要的是当前 skin 的局部 palette。把顶点索引全局化会破坏这个约定。

### 修复
- `SceneManager` 合并所有 skin 的 joint 节点为统一 skeleton，但保存 `skinBoneIndices[skinIndex]` 作为 skin-local → global bone 的映射
- 通过 glTF node 的 `mesh` + `skin` 引用建立 mesh→skin 映射，并在 `SubMesh::skinIndex` 中记录
- 顶点 `JOINTS_0` 保持 skin-local，只按当前 skin 的 joint 数量 clamp
- `Application::drawFrame` 先计算全局 `finalBoneMatrices`，再按 `SubMesh::skinIndex` 生成 per-skin palette，上传给对应 skinned material

### 教训
1. **多 skin 的正确边界是 draw/submesh，而不是整个 glTF 文件**：shader 看到的 bone index 必须和当前 descriptor 中的 bone palette 使用同一个索引空间。
2. **不要把 glTF skin-local joint index 过早改写为全局索引**：全局骨架适合 CPU 动画求值，skin-local palette 适合 GPU 蒙皮。
3. **多部件模型要用真实资产验证**：单 skin 示例不会暴露跨 skin 索引串扰，必须覆盖人物 + 附属机器人/武器/道具这类资产。

---

## 2026-06-18 — #7

### 原始条目
- A3 GPU skinning used `kMaxBones = 128` for `BoneMatricesUBO`.

### 发现的问题
已验证资产的人物 skin 有 191 joints，实际顶点最大 `JOINTS_0` 为 189。即使改为 per-skin palette，`kMaxBones = 128` 仍会把下半身等高编号 joint clamp 到 0，造成错误动画。

### 根因
A3 计划里的 128 bones 只适合较小示例模型。真实角色模型经常超过 128 joints。当前实现使用 UBO，Vulkan 最小保证的 uniform buffer range 是 16KB，正好容纳 256 个 mat4。

### 修复
- 将 `VulkanTypes.hpp::kMaxBones` 从 128 提升到 256
- 将 `res/shaders/skinned_vert.glsl` 的 `BoneMatricesBlock` 数组从 128 提升到 256
- 重新编译 `res/shaders/skinned_vert.spv`
- 构建同步资源到输出目录，并通过用户运行时验证

### 教训
1. **bone palette 容量要按真实资产验证**：只看教程模型会低估上限。
2. **CPU 常量和 shader 常量必须同步**：改 `kMaxBones` 后必须同步 `.glsl` 并重新生成 `.spv`，否则运行时仍使用旧数组。
3. **优先按 skin 限制，而不是按合并骨架总数限制**：当前资产合并后 263 bones，但单个 skin 最大 191 bones；per-skin palette 让 256 上限足够覆盖真实 draw。
