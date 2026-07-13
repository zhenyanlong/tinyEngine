## 2026-06-27

- [ ] 在新的 AnimatorController 模块中实现 Phase B1 的 AnimatorState、AnimatorTransition、类型化参数、条件求值和一次性 Trigger 消费。
- [ ] 实现 Phase B2 的逐实体状态机更新，支持 AnyState/当前状态过渡、归一化 exit time、零时长切换和 BlendCommand 输出。
- [ ] 新增 BoneLocalTransform TRS 采样和逐骨骼 cross-fade，在最终矩阵求值前使用平移/缩放 mix 与四元数 slerp 进行混合。
- [ ] 新增复用逐 submesh 材质描述符和当前骨骼 palette 的蒙皮 GPU 拾取 Shader/管线，修复动画 glTF 无法通过鼠标正确选中的问题。
- [ ] x64-debug 构建、AnimatorController 条件/fade/Trigger/exit-time smoke test 和 SPIR-V 校验均通过，并已同步 TDD.md 与 Phase B1-B2 计划复选框。

## 2026-06-26

- [ ] **资源路径架构重构**：重构 `applicationResourceRoot()`，从 exe 路径向上查找项目根（同时含 `CMakeLists.txt` 和 `res/` 的目录）。所有 res 读写（mesh/anim/scene/texture/shader/thumbnail）统一以 `<项目根>/res/` 为唯一基准。移除 CMake 的 `copy_directory` post-build 步骤（此前每次构建把源 `res/` 覆盖到 exe 旁，导致保存的场景被静默覆盖）。回滚了临时的 `saveScene` 双写 hack 和 `ThumbnailRenderer` 向上查找源目录逻辑，两者不再需要。
- [ ] **场景路径迁移（Bug #3）**：在 `SceneSerializer` 新增 `resolveLegacyAstPath()`，将旧 `materials/<stem>.ast` 引用递归搜索 `res/content/` 重映射到新 `content/<stem>.mesh.ast`。在 `load()` 的 `.ast` model 字段查找和 `ent->astRelPath` 赋值两处应用，使保存的场景在下次保存时自动迁移到新布局。
- [ ] **子材质字段兼容**：`MaterialAssetLoader::load` 现在同时读取 `subMaterials`（旧格式）和 `materials`（迁移脚本新格式）数组字段。迁移脚本将 `subMaterials` 重命名为 `materials`，但加载器只读旧名，导致 `loadScene` 跳过子材质加载（所有槽位回退为默认白纹理），而 `beginDragPlace` 因有 `autoAstPaths` 回退而正常。修复方式：`subMaterials` 为空时读取 `materials` 作为回退。
- [ ] **多骨架动画实体（Bug #2）**：将动画状态从 `SceneManager` 全局单例（`skeleton_` / `animationClips_`）下沉到每个 `ModelEntity` 的字段（`ent.skeleton` / `ent.animationClips`）。`loadModelFromObj`/`loadModelFromGltf` 现在填充实体自己的 skeleton/clips。`drawFrame` 重写为逐实体遍历并独立求值动画。新增 `setEntityAnimationData`/`getEntitySkeleton`/`getEntityAnimationClips` 接口接收 `entityId`。`ensureAnimationAssetForMeshAst` 增加 `entityId` 参数。`CachedModelResource` 现在缓存 skeleton（shared_ptr）+ clips，重复拖拽同模型共享动画数据。移除了 `ThumbnailRenderer` 的 RAII skeleton save/restore guard（不再需要：临时实体加载不再污染其他实体的状态）。不同骨架的动画模型现在可以同场景共存，不再互相覆盖。
- [ ] **缩略图 skeleton 污染（Bug #1，被 Bug #2 修复取代）**：最初在 `ThumbnailRenderer::renderAndSave` 加了 RAII guard，在临时模型加载前后保存/恢复全局 `skeleton_`/`animationClips_`。该 guard 后来在 Bug #2 把动画状态下沉到每实体后移除，全局污染已不可能发生。

## 2026-06-18

- [✓] 外部 LLM 验证与运行时验证完成：修复骨骼解析顺序、bone UBO identity 初始化、recreate() vert→frag 参数、push constants layout、动画 bind-pose fallback、多 skin 处理、逐槽位材质克隆、per-skin 骨骼矩阵 palette，以及 128 骨骼上限问题。编译通过（MSVC 2022，C++20），用户确认双部件人物 + 机器人模型动画已正确渲染。
- [✓] 记录验证经验：数据依赖顺序、Vulkan UBO 初始化、create/recreate 一致性、push-constant/descriptor-set layout 一致性、动画部分通道处理、多 skin glTF 的 per-skin palette，以及骨骼矩阵容量。
- [✓] 经过源码审计、构建验证、资产检查和用户运行时确认后，已将 A3 DailyProgress 中验证通过的条目标记为 [✓]。
- [✓] 新增 `createSkinnedMaterialFrom(src)` 方法，从已有非蒙皮材质克隆为蒙皮版本，保留所有纹理路径和 MaterialParams。
- [✓] 新增 `getMetallicRoughnessPath()` / `getAoPath()` / `getEmissivePath()` / `hasSkinning()` 查询方法。
- [✓] 新增 `Application::convertModelMaterialsToSkinned()` 辅助方法，遍历所有 sub-mesh 槽位逐个克隆 PBR 材质。
- [✓] 将 initVulkan / recreateSwapChain / beginDragPlace / loadAndApplyMaterialAsset 中重复的蒙皮材质创建逻辑替换为 `convertModelMaterialsToSkinned()`。
- [✓] 实现 SceneManager 多 skin 骨架合并：遍历全部 skin，通过 globalNodeToBone 构建统一骨架，并为每个 skin 构建 `skinBoneIndices` 供 palette 生成使用。
- [✓] 新增 glTF mesh-node skin 绑定：按 node 引用的 skin 记录每个 `SubMesh::skinIndex`，顶点 `JOINTS_0` 保持 skin-local 索引供 shader 使用。
- [✓] 更新动画解析使用 globalNodeToBone 查找通道 boneIndex，不再仅依赖 skin[0]。
- [✓] joint clamp 改为使用当前 skin 的局部 joint 数量；每顶点权重归一化。
- [✓] 将 GPU 骨骼 palette 容量提升到 `kMaxBones = 256` 并重新编译 `skinned_vert.spv`，覆盖已验证资产中人物 skin 的 191 根 joint。
- [✓] 更新 TDD.md §4.10/§4.11/§4.12.3/§7.1/§15，记录最终多 skin、per-skin palette、材质克隆、256 骨骼 palette 和验证教训；GPU 蒙皮渲染标记为已完成。
- [✓] **运行时问题已解决**：per-skin palette 上传和 256 骨骼 shader/UBO 容量修复后，双部件人物 + 机器人模型的蒙皮材质与动画已正确渲染。

## 2026-06-17

- [✓] 扩展 Vertex 结构体，新增 `boneIndices`（ivec4）和 `boneWeights`（vec4）字段用于蒙皮顶点数据 — 已验证，管线渲染正常
- [✓] 新增 `Vertex::getSkinnedAttributeDescriptions()` 返回 7 属性布局（骨骼数据在 location 6/7） — 已验证，管线使用蒙皮属性描述
- [✓] 更新 `Vertex::operator==` 和 `VertexHash`，将蒙皮属性纳入比较和哈希计算 — 已验证，含蒙皮数据的网格加载正常
- [✓] 在 `VulkanTypes.hpp` 中新增 `kMaxBones = 256` 常量和 `BoneMatricesUBO` 结构体（256 × mat4，16384 字节） — 已验证，覆盖人物 skin 的 191 根 joint；见 lessons-learned.md #1
- [✓] 创建 `skinned_vert.glsl` 顶点着色器，实现四骨骼加权混合的 GPU 蒙皮计算，并编译为 `skinned_vert.spv` — 已验证，管线编译正确；见 lessons-learned.md #3
- [✓] 在 `MaterialManager` 中实现 `createSkinnedMeshMaterial()`，为每个交换链镜像分配 `BoneMatricesUBO` — 已验证，identity 初始化修复已应用；见 lessons-learned.md #2
- [✓] 在 `MaterialManager` 中新增 `updateBoneMatrices()` 方法，通过 `memcpy` 实现每帧骨骼矩阵上传 — 已验证，identity 填充修复 + 已在 drawFrame 中调用；见 lessons-learned.md #2, #4
- [✓] 扩展 `MaterialEntry` 添加 `hasSkinning_` 标志和 `boneUBOs/Memory/Mapped` 向量，更新 `allocateDescSets` / `writeDescSets` / `destroyEntry` / `onSwapchainRecreate` 支持蒙皮材质 — 已验证，重建时 identity 初始化修复已应用；见 lessons-learned.md #2
- [✓] 在 `PipelineManager` 中新增蒙皮描述符集布局（binding 6 = BoneMatricesUBO，vertex stage）和蒙皮网格管线（`skinnedMeshPipeline_`） — 已验证，recreate() vert/frag 参数修复已应用；见 lessons-learned.md #3
- [✓] 更新 `SceneManager::loadModelFromGltf`，通过原始 buffer 读取 `JOINTS_0`（兼容 uint8/uint16/uint32）和 `WEIGHTS_0` 顶点属性，设置 `ModelEntity::hasSkin_` 标记 — 已验证，解析顺序 + joint clamp + weight normalize 修复已应用；见 lessons-learned.md #1
- [✓] 在 `Application::recordCommandBuffer` 中集成蒙皮管线路由，根据管线类型动态选择 `VkPipelineLayout` — 已验证，push constants layout 修复已应用；见 lessons-learned.md #4
- [✓] 在 `initVulkan`、`recreateSwapChain`、`beginDragPlace` 和 `loadAndApplyMaterialAsset` 中为骨骼实体自动创建蒙皮材质 — 已验证，drawFrame 中动画采样 + 骨骼矩阵上传已接入；见 lessons-learned.md #4
- [✓] 每帧动画采样：取 AnimationClip 0 以 bind-pose 回退求值 → computeFinalMatrices → 为所有蒙皮实体调用 updateBoneMatrices — 已验证，AnimationClip bind-pose fallback 重载已添加；见 lessons-learned.md #5
- [✓] A3 全部 7 个子任务完成，项目零错误编译通过（MSVC 2022，C++20） — 已验证，cmake --build --preset x64-debug 通过
- [✓] 更新 TDD.md，记录 Phase A3 GPU 蒙皮渲染：顶点布局（location 6/7）、BoneMatricesUBO、蒙皮描述符集布局、蒙皮管线规格、skinned_vert.glsl 着色器详情、材质自动创建流程、数据流扩展，并将 GPU 蒙皮渲染标记为已完成
- [ ] 更新 `session-init` skill 新增 DailyProgress 阅读步骤、更新 `daily-progress` skill 新增验证标记规范、为所有 DailyProgress 条目添加 `[ ]` 标记
- [ ] 更新 `session-init` skill 新增步骤 4：扫描 `.codex/skills/` 目录，在摘要输出中新增 Codex 技能清单，并记录对 `todo-sync-completed` / `todo-normalize-inbox` 的感知
- [ ] 创建 `verify-fix` skill：处理外部 LLM 验证反馈，分析根因，将教训写入 `lessons-learned.md`，并将已验证条目标记为 `[✓]`
- [ ] 创建 `mark-verified` skill：简单手动将 DailyProgress 中用户确认完成的条目从 `[ ]` 切换为 `[✓]`
- [ ] 创建 `lessons-learned.md` 和 `lessons-learned_Chinese.md` 模板，用于积累验证驱动的编码经验
- [ ] 更新 `session-init` skill：步骤 2 新增 `lessons-learned.md` 阅读，注意事项中列出全部 6 个 `.trae/skills/`
