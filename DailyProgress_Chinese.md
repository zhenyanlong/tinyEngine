## 2026-07-03

- [ ] 制定了剩余 7 天开发路线图：Phase C（Sequencer）+ Phase D（资产系统），以及两个新功能：Content Browser 离屏缩略图修复和 Sequencer 摄像机 + PiP 小窗
- [ ] 更新 TDD.md：新增 §4.12.5 Sequencer 系统规范（C1-C6）、§4.12.6 AnimationAssetRegistry 规范、更新目录结构（animators/、sequences/、icons/）、更新初始化顺序（缩略图生成和资产扫描）、扩展 §15 已知限制表
- [ ] 更新 todolist.md 7 天计划：第1天（C1+C5+缩略图修复）、第2天（C2+C4）、第3天（C3+摄像机PiP）、第4天（C6面板上半）、第5天（C6面板下半+D1）、第6天（D2/D3+摄像机模型）、第7天（集成测试+构建）
- [ ] 记录 VS 2022 CMake 路径供后续构建使用：C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

## 2026-07-02

- [ ] 修复 `ensureAnimationAssetForMeshAst` 的回退逻辑：当传入的 `.ast` 路径是非 mesh 类型（如 `.material.ast`）且不含 `"animations"` 数组时，函数现在会检查目标实体的 `astRelPath`，回退到读取 `.mesh.ast` 以找到所有 `.anim.ast` 引用
- [ ] 修复 `loadAndApplyMaterialAsset`：当通过 `.mesh.ast` 交换模型时，将 `astRelPath` 记录到实体上，使后续的 `ensureAnimationAssetForMeshAst` 调用能正确找到动画资产
- [ ] 完成 TODO-017：Remy 模型现在能正确加载全部 3 个 `.anim.ast` clip，而非仅 1 个

## 2026-06-30

- [ ] 新增 `FbxImporter::loadAnimationOnly()` 从场景节点层级解析无蒙皮（Mixamo 风格）FBX 为虚拟骨架。
- [ ] 新增 `AnimationRetargeter` 模块，支持 `buildMapping()`、`retargetClip()` 和 `retargetPose()`，实现基于名称的源/目标骨骼重映射。
- [ ] 新增 `Application::importAnimationFbx()` 编排完整动画重定向管线：加载目标 Mesh 骨架 → 解析源 FBX 动画 → 重定向 clip → 持久化为 `.anim.ast` + `.anim.bin` 并更新目标 Mesh `.ast` 的 animations 字段。
- [ ] 将动画重定向集成到导入模型流程，使含动画的 FBX 模型同样经过重定向管线。
- [ ] 在 Content Browser UI 新增 Import Anim... 按钮和目标 Mesh 选择弹窗，用于导入无蒙皮动画 FBX 文件。
- [ ] 修复 `ensureAnimationAssetForMeshAst` 中 `existingAnimAsts` 变量作用域 bug（在块内声明但后续在块外使用导致编译错误）。
- [ ] 修复 `contentPrefix` 双写路径 bug：动画资产路径误写为 `content/content/...` 应改为 `content/...`。
- [ ] 移除未使用的 `FbxAnimationOnlyResult::clipName` 字段及相关代码。
- [ ] 在 UI 验证中将选中动画 clip 从 Scene 重命名为 Idle，同步更新 TDD.md，并通过最终 x64-debug 构建。

- [ ] 新增面向当前模型的 Animator Controller 资产浏览器，递归扫描 `res/animators/` 并按 clip 兼容性筛选，同时提供 Refresh、New AnimController 和 Save Current 操作。
- [ ] 重构 Animator 动画条目，增加明确的 Preview/Stop 与行内 Rename 控件，并以 ASCII Active/State 标签替换字体不支持的 Unicode 状态图标。
- [ ] 实现带校验的动画 clip 重命名：仅更新 `.anim.ast` 元数据而不重写 `.anim.bin`，并同步运行时 clip 及当前控制器的状态/过渡引用。
- [ ] 为 glTF 与 FBX 模型增加逐实体动画资产/控制器资产路径记录，使 UI 编辑能够持久化到正确资产。
- [ ] 修复 Windows 动画资产替换失败：事务式临时文件/备份 rename 前释放输入文件，并使用字体安全的错误码。
- [ ] 在 UI 验证中将选中动画 clip 从 Scene 重命名为 Idle，同步更新 TDD.md，并通过最终 x64-debug 构建。

## 2026-06-29

- [ ] 实现 Phase B3 混合品质保障：新增 BlendCurve 枚举（Linear/SmoothStep/EaseIn/EaseOut）与 applyBlendCurve() 工具函数；AnimatorTransition.blendCurve 字段；update() 输出 blendWeight 时应用当前过渡的 ease 曲线；旋转已在 B2 使用 glm::slerp。
- [ ] 实现 Phase B4 状态机序列化：AnimatorController::saveToFile/loadFromFile 使用 nlohmann/json；MaterialAssetDesc 新增 animControllerPath 字段；MaterialAssetLoader 解析 .ast 的 animController 字段；Application::loadAndApplyMaterialAsset 自动加载控制器；创建 res/animators/example.animctrl.json 示例文件；loadFromFile 对所有 JSON key 做 contains()+is_array() 检查避免异常。
- [ ] 实现 Phase B5 动画总控面板：三区布局（左侧侧边栏 + 右侧上半双栏 + 底部双栏）；左侧上半显示当前 controller 与 Load/Save 按钮，支持 DND_ANIMCTRL 拖拽源；左侧下半列出动画 clip，每项有 [▶] 预览 / [■] 停止按钮，支持 DND_ANIMCLIP 拖拽源；右侧上半左栏 States 列表用 ● 标记当前状态，接受拖拽创建 state；右侧上半右栏 Outgoing Transitions 列表 + Add Transition 按钮；底部左栏 Transition/State 编辑器（toState 下拉、fade/exitTime/blendCurve/conditions，condition 控件 label 带 _%d 索引避免 ID 冲突）；底部右栏状态信息 + Add Param/State 按钮 + Reset Controller。
- [ ] 实现 Phase B5 预览模式：ModelEntity 新增 previewClipIndex/previewTime/previewSpeed 字段；Application::drawFrame 在 previewClipIndex >= 0 时绕开状态机直接播放 animationClips[previewClipIndex]。
- [ ] 实现 Phase B5-6 事件驱动系统：AnimatorEvent 结构体（SetFloat/SetInt/SetBool/SetTrigger + paramName + 值）与 AnimatorController::dispatchEvent/dispatchEvents 方法，为未来 Sequence 系统驱动状态机过渡预留标准接口。
- [ ] 修复 Animator 面板崩溃：ImGui BeginGroup/EndGroup 不匹配（函数顶部多了一个 BeginGroup 无对应 EndGroup，触发 ErrorRecoveryTryToRecoverWindowState 中的 Missing EndGroup() assert）。
- [ ] 修复 entity fallback 逻辑：只选择 hasSkin_ && skeleton 的实体，避免第一个模型实体无蒙皮数据时空指针解引用。
- [ ] 修复 condition 编辑循环的 ImGui ID 冲突：所有控件 label 追加 _%d 索引后缀（Param/Op/Threshold/X）。
- [ ] 加固 loadFromFile 异常安全：is_object() 检查 + states/params/transitions/conditions 数组的 contains()+is_array() 守卫。
- [ ] 更新 TDD.md 4.12.3 章节补充 B3-B5 细节、MaterialAssetDesc.animControllerPath 字段和"已知限制"表；更新 animation-system-plan.md B3/B4/B5 checkbox 为 [x] 并新增 B5-6；todolist.md 新增 TODO-016 并标记 TODO-014 完成。
- [ ] x64-debug 构建通过，0 error。

## 2026-06-28

- [ ] 以 Git Submodule 方式加入 ufbx，并将 `ufbx.c` 与 include 路径接入 CMake 构建。
- [ ] 实现 `FbxImporter`，支持二进制/ASCII FBX 的网格三角化、材质槽与 PBR 参数、外部/内嵌纹理、骨骼/蒙皮权重和 30 Hz 烘焙动画片段；统一坐标轴/单位，并在 FBX 导入边界翻转 V 坐标。
- [ ] 将 FBX 接入 Content Browser 导入对话框、分类的 Mesh/Material/Anim 资产生成、ModelRegistry 格式识别、SceneManager 运行时加载/缓存和递归缩略图发现。
- [ ] 通过为 `MaterialAssetLoader` 配置项目绝对 `res/` 根并使入口/子材质资产不依赖进程工作目录完成解析，修复导入 FBX 实体回退为 Default Mesh 的问题。
- [ ] 使用中性 fallback 纹理修复无贴图材质渲染，使 Base Color、Metallic、Roughness、Emissive Color 和 Emissive Intensity 在缺少纹理时仍然生效。
- [ ] 修复 Properties 纹理编辑：增加逐材质持久输入缓冲、绝对或相对 `res/` 的路径解析、事务式 Albedo/Normal 替换、Material 类型支持和可见加载状态。
- [ ] x64-debug 构建及覆盖二进制/ASCII 几何、材质/纹理引用、蒙皮网格和有限动画矩阵的 FBX smoke tests 均通过；已同步 TDD.md 并完成 TODO-007/TODO-013。

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
