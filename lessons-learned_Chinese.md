# tinyEngine — 经验教训

> 记录外部验证过程中发现的问题、根因和教训，供未来开发参考。
> 由 `verify-fix` skill 自动维护，日期倒序排列。

---

## 2026-06-18 — #1

### 原始条目
- 扩展 Vertex 结构体，新增 boneIndices 和 boneWeights 用于蒙皮顶点数据
- 在 MaterialManager 中实现 createSkinnedMeshMaterial，为每个交换链镜像分配 BoneMatricesUBO
- 在 MaterialManager 中新增 updateBoneMatrices，通过 memcpy 实现每帧骨骼矩阵上传
- 更新 SceneManager::loadModelFromGltf，读取 JOINTS_0/WEIGHTS_0 顶点属性，设置 ModelEntity::hasSkin_ 标记

### 发现的问题
1. 骨骼解析位于顶点读取循环之后，导致 skeleton_ 始终为 nullptr，hasSkin_ 从未被设置
2. 原始皮肤解析代码在顶点循环后面还重复了一份（约 150 行重复逻辑）
3. JOINTS_0 索引无越界检查，可能读到无效骨骼矩阵
4. 骨骼权重未归一化

### 根因
- 顺序错误：AI 把骨骼解析放到了顶点加载之后，但顶点加载又需要 skeleton_ 判断是否读取蒙皮数据 → 死循环
- 重复代码：AI 移动代码后未删除原位置残留
- 输入假设：默认 glTF 数据合法，未考虑非标准资产

### 修复
1. 将 skin 解析提前到顶点读取之前，删除旧的重复解析块
2. 增加 boneLimit 限制，joint 越界时 clamp 到 0 并打印警告
3. 对 WEIGHTS_0 求和归一化

### 教训
1. 数据依赖决定代码顺序：被依赖变量必须在检查它的 if 条件前完成赋值
2. 移动代码后必须删除原代码：重构完成后手动检查并清理重复块
3. 外部数据输入必须有防御：glTF 资产可能不符合规范 → 始终加 clamp + normalize + warn

---

## 2026-06-18 — #2

### 原始条目
- 在 MaterialManager 中实现 createSkinnedMeshMaterial，为每个交换链镜像分配 BoneMatricesUBO
- 在 MaterialManager 中新增 updateBoneMatrices，通过 memcpy 实现每帧骨骼矩阵上传
- 扩展 MaterialEntry 添加 hasSkinning_ 标志和 boneUBOs/Memory/Mapped 向量

### 发现的问题
BoneMatricesUBO 在创建和 swapchain 重建后未初始化，包含随机内存数据，导致模型塌陷或网格爆炸。

### 根因
- 假设 Vulkan HOST_VISIBLE 内存在 vkMapMemory 后会自动清零——实际 Vulkan 规范不保证初始内容
- updateBoneMatrices 直接 memcpy 整个 UBO，但当 finalBoneMatrices 不足 128 根时，未被覆盖的骨骼包含随机数据

### 修复
- 创建 makeIdentityBoneMatricesUBO() 辅助函数，生成全 identity mat4 的 UBO
- 在创建和重建 bone UBO 后立刻 memcpy identity
- updateBoneMatrices 改为先填 identity 再覆盖有效部分

### 教训
1. GPU 可见的 UBO 必须显式初始化：Vulkan 不保证 HOST_VISIBLE 内存的初始内容
2. 部分更新策略：先填 identity 再覆盖有效骨骼，确保剩余骨骼不会残留脏数据
3. 创建 → 映射 → 初始化 → unmap 序列中，初始化步骤不可跳过

---

## 2026-06-18 — #3

### 原始条目
- 在 PipelineManager 中新增蒙皮描述符集布局和蒙皮网格管线

### 发现的问题
PipelineManager::recreate() 中 createSkinnedPipeline 的第四参数错误传入了 vertSpv（顶点着色器路径），而非 fragSpv（片段着色器路径），导致 skinned pipeline 创建失败后静默回退到默认管线。

### 根因
recreate() 是 create() 的 copy-paste 版本。create() 中新增的 createSkinnedPipeline 参数正确（fragSpv），recreate() 中 copy-paste 时误用了局部变量 vertSpv。

### 修复
将 recreate() 第四参数改为 fragSpv。

### 教训
1. recreate() 和 create() 的代码重复是高风险区：新增功能时必须同时在两处修改并逐参数核对
2. Shader pipeline 参数静默失败难以发现：确保 pipeline 创建失败时有充分的可见性（日志 + fallback）
3. 同名局部变量在不同函数中含义可能不同，copy-paste 时容易误用

---

## 2026-06-18 — #4

### 原始条目
- 在 Application::recordCommandBuffer 中集成蒙皮管线路由，根据管线类型动态选择 VkPipelineLayout
- 为骨骼实体自动创建蒙皮材质

### 发现的问题
1. bindDescriptorSets 动态选择了 layout，但 vkCmdPushConstants 仍硬编码使用 mainPipelineLayout，导致 skinned pipeline 的 push constants 写错
2. drawFrame 中没有动画采样逻辑——updateBoneMatrices 实现了但从未被调用
3. 每帧需要遍历所有 skinned entity 上传骨骼矩阵

### 根因
- AI 实现"动态 layout 选择"时只更新了 bindDescriptorSets，忘记 pushConstants 也需要同步
- A3 和 A4 之间的 implicit dependency：没有动画播放就无法验证蒙皮管线

### 修复
- vkCmdPushConstants 改为使用当前实际绑定的 pipeLayout
- drawFrame 中新增每帧动画采样 → evaluateBoneLocalTransform → computeFinalMatrices → updateBoneMatrices

### 教训
1. push constants 和 descriptor sets 的 layout 必须始终保持一致
2. 跨 phase 的验证依赖要提前埋 stub：实现蒙皮管线后至少放一帧 bind pose 验证
3. 实现方法后必须确认它在主循环中被实际调用

---

## 2026-06-18 — #5

### 原始条目
- （A1/A2 交叉修复）AnimationClip::evaluateBoneLocalTransform 只接受 (boneIndex, t)，但许多 glTF 动画只对部分骨骼设置 rotation 通道——缺少 Translation/Scale 通道的骨骼默认塌陷到原点

### 发现的问题
evaluateBoneLocalTransform 假设每条骨骼都有完整的 T+R+S 三条通道。实际 glTF 资产可以只有 rotation 通道。缺失的 T/S 分量默认为零值而非 bind-pose 值，导致骨骼缩到原点。

### 根因
AI 实现的求值逻辑没有处理部分通道场景。CubicSpline 数据布局为三联组（inTan, value, outTan），索引计算也需修正。

### 修复
- 新增 evaluateBoneLocalTransform(boneIndex, t, fallbackLocalTransform) 重载
- drawFrame 中传入 skeleton->bones[i].localBindTransform 作为 fallback
- 内部先分解 fallback 为 T/R/S，对存在的通道用关键帧值，缺失的用分解值

### 教训
1. 动画数据格式允许部分通道：求值函数必须能处理任意 T/R/S 组合，缺失组件从 bind-pose 获取
2. 为 lookup 型函数提供 fallback 重载：单参数版本应委托到更通用的多参数版本
3. CubicSpline 数据布局是三联组：每个 keyframe 占 3 个 vec4，索引 = i*3 + 0/1/2

---

## 2026-06-18 — #6

### 原始条目
- 多 skin glTF 支持：将多个 skin 合并到统一动画骨架中，并正确渲染所有 submesh。

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
1. 多 skin 的正确边界是 draw/submesh，而不是整个 glTF 文件：shader 看到的 bone index 必须和当前 descriptor 中的 bone palette 使用同一个索引空间。
2. 不要把 glTF skin-local joint index 过早改写为全局索引：全局骨架适合 CPU 动画求值，skin-local palette 适合 GPU 蒙皮。
3. 多部件模型要用真实资产验证：单 skin 示例不会暴露跨 skin 索引串扰，必须覆盖人物 + 附属机器人/武器/道具这类资产。

---

## 2026-06-18 — #7

### 原始条目
- A3 GPU 蒙皮使用 `kMaxBones = 128` 作为 `BoneMatricesUBO` 上限。

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
1. bone palette 容量要按真实资产验证：只看教程模型会低估上限。
2. CPU 常量和 shader 常量必须同步：改 `kMaxBones` 后必须同步 `.glsl` 并重新生成 `.spv`，否则运行时仍使用旧数组。
3. 优先按 skin 限制，而不是按合并骨架总数限制：当前资产合并后 263 bones，但单个 skin 最大 191 bones；per-skin palette 让 256 上限足够覆盖真实 draw。
