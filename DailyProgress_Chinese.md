## 2026-07-13

- [ ] 修复 Animator Preview 被 Sequencer 误清理的问题：将条件从 `!seqHasAnimTrack || !seqPlayer_.isPlaying()` 改为 `seqPlayer_.isPlaying() && !seqHasAnimTrack`，避免每帧覆盖 Animator 面板的 Preview 按钮状态（Application.cpp）
- [ ] 修复 `+ Add State` / 拖拽创建 state 后 current state 被重置回旧 state 的问题：传入新 state 名作为 `configure()` 的 defaultState，不再使用 `ctrl.currentStateName()`，创建后自动激活新 state（IMGUIManager.cpp）
- [ ] 在 State Properties 编辑区增加 clipName 显示和编辑，用户可查看和修改 state 绑定的 clip（IMGUIManager.cpp）
- [ ] 修复跨 `.anim.ast` 文件的同名 clip 问题：加载时自动追加 `_1`、`_2` 后缀去重（AnimationAssetLoader.cpp）
- [ ] 去掉 Apply State 按钮：clipName/speed/loop 修改直接写入状态机，不再触发 `configure()` → `reset()`（IMGUIManager.cpp）
- [ ] 新增 Set Active State 按钮：直接切换当前激活 state，跳过 transition（AnimatorController.hpp/.cpp, IMGUIManager.cpp）
- [ ] 去掉 Apply Transition 按钮：所有 transition 属性（toState、fadeDuration、hasExitTime、exitTime、blendCurve、conditions）直接写入状态机（IMGUIManager.cpp）
- [ ] 新增 per-state Root Motion 支持：3 种模式（None / Locked / Follow），可配置根骨骼，Follow 模式将 root delta 累加到 entity transform（AnimatorController.hpp/.cpp, SceneManager.hpp, Application.cpp, IMGUIManager.cpp）
- [ ] 参数面板增加名称编辑和删除按钮（IMGUIManager.cpp）
- [ ] Condition 的 Param 从手动输入改为 Combo 下拉，根据参数类型动态显示 Op/Threshold（Float: Greater/Less/Equal/NotEqual + DragFloat, Bool: Equal/NotEqual/True/False, Trigger: True/False）（IMGUIManager.cpp）
- [ ] 新增 Animator Preview Mode 开关：ON（默认）— 状态机驱动动画；OFF — Sequencer 驱动动画，Animator 面板变为只读，`onAnimClipEval` 在预览模式下跳过（Application.hpp/.cpp, IMGUIManager.cpp）
- [ ] 缩小 Parameter 和 Condition 元素的宽度，改善布局紧凑性（IMGUIManager.cpp）

## 2026-07-12

- [ ] 实现 Sequencer ImGui 面板（TODO-026/027/028）：自适应秒数刻度的时间线、带 Mute/Solo 的轨道列表、clip 彩色矩形 + 关键帧菱形节点、片段属性编辑器、轨道管理（Add/Delete）、Load/Save .seq.json、相机路径录制控制、序列时长编辑（IMGUIManager.cpp）
- [ ] 实现 TransformKeyframe 关键帧系统（方案B）：TransformKeyframe 结构（time/position/rotation/scale/easeToNext）+ TransformKeyframeTrack（targetEntityId 绑定实体）+ evaluate(t) 前后关键帧插值求值（Sequence.hpp/.cpp）
- [ ] 新增 7 种混合模式：Linear、SmoothStep、EaseIn、EaseOut、EaseInOut、Cubic、Exponential，通过 applyEaseCurve() 通用曲线函数实现（Sequence.hpp）
- [ ] 扩展 SequencePlayer：新增 onTransformKeyframeEval callback + setSequenceRef() 引用外部 Sequence 实现实时同步 + totalDur<=0 时关键帧预览兜底（SequencePlayer.hpp/.cpp）
- [ ] 扩展 SequenceAssetLoader：支持 TransformKeyframeTrack 的 JSON 序列化（保存/加载后按 time 排序）（SequenceAssetLoader.cpp）
- [ ] 新增 Sequencer 预览机制：Application 维护 sequencerPreviewPending_ 标志，requestSequencerPreview() API，onTransformKeyframeEval 仅在播放或预览请求时执行 — 避免持续覆盖实体 Transform（Application.hpp/.cpp）
- [ ] 修复关键帧与时间轴对齐：统一 contentOriginX 坐标基准，移除子窗口边框确保 x 位置一致（IMGUIManager.cpp）
- [ ] 新增 UI 工作流按钮：Add Selected to Track（绑定 Camera 到关键帧轨道）、Add Keyframe（自动记录实体 Transform）、Update from Entity（写回 Transform）、Preview / Preview Here（单次预览）（IMGUIManager.cpp）
- [ ] 修复 seqPlayer_ 与 currentSequence_ 不同步：drawFrame 每帧调用 setSequenceRef() 引用外部 Sequence，不再使用过期的 owned 拷贝（Application.cpp, SequencePlayer.cpp）
- [ ] 修复点击时间轴不触发实时预览：点击现在调用 seek() + requestSequencerPreview() 立即更新实体 Transform（IMGUIManager.cpp）
- [ ] 时间轴刻度尺自适应：范围 = max(duration + 2, 10)，不再固定 0-6（IMGUIManager.cpp）
- [ ] 更新 TDD.md §4.12.5：记录 Sequencer Phase C 已完成，含关键帧系统、7种混合模式、预览机制、UI 工作流

## 2026-07-09

- [ ] 修复 PiP color/depth 输出为空且 PiP 显示与主视口一致的问题（TODO-034）：根因是两个叠加问题——(1) 所有管线使用静态 viewport/scissor 且未声明 VK_DYNAMIC_STATE_VIEWPORT/SCISSOR，PiP 的 vkCmdSetViewport(320×240) 被静默忽略，几何落在 framebuffer 外；(2) PiP renderpass 用 R8G8B8A8_UNORM 而主管线在 B8G8R8A8_SRGB 的 main renderpass 上创建，格式不兼容。修复：给 4 个管线 builder 启用动态 viewport/scissor（PipelineManager.cpp）；主 pass（Application.cpp）和 pick pass（PickSystem.cpp）begin 后补 vkCmdSetViewport/Scissor；PiP renderpass 与 color image 改用 swapchain 格式（RenderPassManager、FramebufferManager）
- [ ] 修复蒙皮模型在 PiP 中仍用主视角渲染的问题：createSkinnedMaterialFrom（MaterialManager.cpp）漏调 createPipResources，蒙皮材质的 pipDescSets 为空，getPipDescriptorSet 回退到主 descriptor set（绑定主视角 UBO）。修复：在 writeDescSets 后补 createPipResources 调用
- [ ] 修复 Camera 模型朝向与预览方向相差 Y -90° 的问题：ModelEntity 新增 modelRotationOffset 字段（SceneManager.hpp），createCameraEntity 中设为 Y -90°（SceneManager.cpp），在主 pass、PiP pass、pick pass 构建模型矩阵时右乘该偏移（Application.cpp、PickSystem.cpp）——cameraData.orientation（预览方向）不受影响，仅校正显示的模型几何体朝向
- [ ] 新增 Gizmo 世界/本地坐标系切换：按 4 键在 gizmoLocal_ 的 ImGuizmo::WORLD 与 ImGuizmo::LOCAL 间切换（IMGUIManager.hpp/.cpp、Application.cpp processInput）
- [ ] 修复 createCameraEntity 将四元数当欧拉角存储的问题 — `glm::degrees(glm::eulerAngles(orientation))` 产生的是度单位的 vec3，却被赋给 glm::quat 字段，导致旋转数据损坏（SceneManager.cpp）
- [ ] 修复 SequencerCamera 坐标系约定：forward() 从 -X 改回 -Z，与主 Camera 的约定一致（SequencerCamera.cpp）
- [ ] 修复 PiP 共享 mapped UBO 时序冲突：为 MaterialManager 增加每材质 PiP 专用 UBO + PiP descriptor set（MaterialManager.hpp/.cpp），PiP 渲染改用 updateAllPipUBOs + getPipDescriptorSet，不再覆盖/恢复主 UBO（Application.cpp）
- [ ] 修复 Camera 模型加载：loadCameraModelOnce 改用 FbxImporter::load() 加载 res/bin/mesh/Camera.fbx，不再错误地把 Camera.mesh.ast（JSON 描述文件）当原始二进制读取（SceneManager.cpp）
- [ ] 新增 PiP 渲染跳过选中 Camera 实体本身的逻辑，避免自身遮挡（Application.cpp）
- [ ] 修复 recreateSwapChain 未重置 pipTextureCreated_ 的问题 — ImGui_ImplVulkan_Shutdown 会销毁所有 texture（Application.cpp）
- [ ] 清理 PiP 渲染路径中的诊断 std::cout 输出
- [ ] **未解决 Bug**：PiP RenderPass 的 draw call 正常执行（RenderDoc 中 input texture 可见），但 color attachment 和 depth attachment 输出为空 — 没有片元写入。CPU 侧已验证：pipView != mainView、pipDescriptorSet != mainDescriptorSet、PiP UBO 写入正确、pipeline colorWriteMask = RGBA、viewport/scissor 在 beginRenderPass 之后设置。怀疑方向：pipeline 在主 renderpass（B8G8R8A8_SRGB）上创建但用于 PiP renderpass（R8G8B8A8_UNORM）— format 不匹配可能导致静默渲染失败。详见 TDD.md §16 完整调查记录。

## 2026-07-08

- [ ] ModelEntity 新增 Type 枚举 (Mesh/Camera) 和 Camera 专属字段 (cameraData, cameraPreviewEnabled, cameraPreviewScale)
- [ ] 新增 syncCameraFromTransform() 从 ModelEntity transform 同步位置/朝向到 SequencerCamera
- [ ] 新增 SceneManager::createCameraEntity() 创建 Camera Actor 实体，使用共享模型缓存
- [ ] 新增 Application::createCameraActor() 和 selectedCameraEntityId_ 跟踪选中的 Camera
- [ ] 扩展 tryPickMainModel() 在点击 Camera Actor 时设置 selectedCameraEntityId_
- [ ] OperationWindow 新增 "New Camera" 按钮，在主相机前方创建 Camera Actor
- [ ] SceneOutliner 新增 Camera Actor 显示，使用淡蓝色高亮
- [ ] 修改 PiP 渲染逻辑，使用选中 Camera Actor 的 cameraData 计算 view/proj 矩阵
- [ ] 新增主视口右下角 PiP 叠加渲染（通过 ImGui foreground draw list）
- [ ] 新增 pipTextureCreated_ 标志，避免每帧重复创建 PiP 纹理
- [ ] 更新 SequencerCamera::forward() 使用 -X 作为相机前方方向（符合 Camera Actor 约定）
- [ ] **未解决 Bug**：PiP 预览显示主视口而非选中 Camera Actor 的视角 — view 矩阵未正确从 Camera Actor transform 计算
- [ ] 更新 TDD.md §15 已知限制表，记录 Camera PiP bug 状态

## 2026-06-30

- [ ] 新增模型感知的 Animator Controller 资产浏览器：按 clip 兼容性递归筛选 `res/animators/`，提供 Refresh、New AnimController、Save Current 操作
- [ ] 重构 Animator clip 行：添加显式 Preview/Stop 和内联 Rename 控件，用 ASCII Active/State 标签替代不支持的 Unicode 图标
- [ ] 实现带验证的动画 clip 重命名：更新 `.anim.ast` 元数据不重写 `.anim.bin`，同步运行时 clips 和当前控制器 state/transition 引用
- [ ] 新增 per-entity animation/controller 资产路径追踪，glTF 和 FBX 模型的 UI 编辑能正确持久化
- [ ] 修复 Windows 动画资产替换失败：在事务性 temp/backup rename 前释放输入文件，使用字体安全错误码
- [ ] UI 验证中将 Scene 动画 clip 重命名为 Idle，同步 TDD.md，通过最终 x64-debug 构建

## 2026-06-29

- [ ] 实现 Phase B3 混合品质：新增 BlendCurve 枚举（Linear/SmoothStep/EaseIn/EaseOut）和 applyBlendCurve() 辅助函数；AnimatorTransition.blendCurve 字段；update() 对 blendWeight 输出应用当前曲线；旋转沿用 B2 的 glm::slerp
- [ ] 实现 Phase B4 状态机序列化：AnimatorController::saveToFile/loadFromFile 使用 nlohmann/json；MaterialAssetDesc 新增 animControllerPath 字段；MaterialAssetLoader 解析 .ast 的 "animController" 字段；Application::loadAndApplyMaterialAsset 自动加载控制器；创建 res/animators/example.animctrl.json；loadFromFile 对所有 JSON key 做 contains()+is_array() 校验
- [ ] 实现 Phase B5 Animator 面板（动画总控中心）：三区布局（左侧侧边栏 + 右侧上半双栏 + 底部双栏）；左侧上半显示当前控制器及 Load/Save 按钮；左侧下半列出动画 clip 带 Preview/Stop 和 Rename；右侧上半左栏 States 列表带当前标记和拖拽目标；右侧上半右栏 Outgoing Transitions 列表带 + Add Transition 按钮；底部左栏 Transition/State 编辑器；底部右栏状态信息 + Add Param/State 按钮 + Reset Controller
- [ ] 实现 Phase B5 预览模式：ModelEntity 新增 previewClipIndex/previewTime/previewSpeed 字段；Application::drawFrame 在 previewClipIndex >= 0 时绕过状态机直接播放
- [ ] 实现 Phase B5-6 事件驱动系统：AnimatorEvent 结构体（SetFloat/SetInt/SetBool/SetTrigger + paramName + value）和 AnimatorController::dispatchEvent/dispatchEvents 方法，为 Sequence 系统预留标准接口
- [ ] 修复 Animator 面板崩溃：ImGui BeginGroup/EndGroup 不匹配（函数顶部多了 BeginGroup 无对应 EndGroup）
- [ ] 修复实体回退逻辑：仅选择 hasSkin_ && skeleton 的实体，防止第一个模型实体无蒙皮数据时空指针解引用
- [ ] 修复 condition 编辑循环中 ImGui ID 冲突：所有控件 label 带 _%d 索引后缀
- [ ] 强化 loadFromFile：增加 is_object() 检查和 states/params/transitions/conditions 数组的 contains()+is_array() 守卫
- [ ] 更新 TDD.md §4.12.3 添加 B3-B5 细节、MaterialAssetDesc.animControllerPath 字段、"Known limitations" 表；更新 animation-system-plan.md B3/B4/B5 复选框为 [x] 并添加 B5-6
- [ ] 通过 x64-debug 构建，0 错误

## 2026-06-28

- [ ] 添加 ufbx 为 Git 子模块，集成 `ufbx.c` 和 include 路径到 CMake 构建
- [ ] 实现 `FbxImporter`：支持二进制/ASCII FBX 网格三角剖分、材质槽和 PBR 参数、外部/内嵌纹理、骨架/蒙皮权重、30 Hz 烘焙动画 clip；归一化轴/单位，导入边界翻转 FBX V 坐标
- [ ] 集成 FBX 到 Content Browser 导入对话框，分类 Mesh/Material/Anim 资产生成，ModelRegistry 格式分类，SceneManager 运行时加载/缓存，递归缩略图发现
- [ ] 修复导入 FBX 实体回退到 Default Mesh：用项目绝对路径 `res/` 根目录配置 `MaterialAssetLoader`，独立解析入口/子材质资产
- [ ] 修复无贴图材质渲染：中性 fallback 纹理保证 Base Color、Metallic、Roughness、Emissive Color、Emissive Intensity 在无纹理贴图时仍然有效
- [ ] 修复 Properties 纹理编辑：per-material 持久输入缓冲、绝对或 `res/` 相对路径解析、事务性 Albedo/Normal 替换、Material 类型支持、可见加载状态
- [ ] 通过 x64-debug 构建和 FBX 冒烟测试，覆盖二进制和 ASCII 几何、材质/纹理引用、蒙皮网格、有限动画矩阵；同步 TDD.md，完成 TODO-007/TODO-013

## 2026-06-27

- [ ] 实现 Phase B1：新 AnimatorController 模块中的 AnimatorState、AnimatorTransition、类型化参数、条件求值、一次性 Trigger 消耗
- [ ] 实现 Phase B2：per-entity 状态机更新，支持 AnyState/当前 state 过渡、归一化 exit time、零时长切换、BlendCommand 输出
- [ ] 新增 BoneLocalTransform TRS 采样和逐骨骼 cross-fade 混合：位移/缩放用 mix，旋转用 slerp
- [ ] 修复带动画的 glTF 鼠标选择：新增 skinned GPU pick shader/pipeline，复用 per-submesh 材质描述符和当前骨骼 palette
- [ ] 通过 x64-debug 构建、AnimatorController 条件/fade/Trigger/exit-time 冒烟测试和 SPIR-V 验证；同步 TDD.md 和 Phase B1-B2 计划复选框

## 2026-06-26

- [ ] **资源路径架构**：重构 `applicationResourceRoot()` 从 exe 路径向上查找项目根目录（含 `CMakeLists.txt` + `res/`），所有 res 读写以 `<project-root>/res/` 为唯一基准。移除 CMake `copy_directory` 构建后步骤（之前每次重建静默覆盖已保存场景）。回滚临时 `saveScene` 双写 hack 和 `ThumbnailRenderer` source-dir 搜索
- [ ] **场景路径迁移（Bug #3）**：SceneSerializer 新增 `resolveLegacyAstPath()`，递归搜索 `res/content/` 将旧 `materials/<stem>.ast` 引用重映射为新 `content/<stem>.mesh.ast`
- [ ] **子材质字段兼容**：`MaterialAssetLoader::load` 同时读取 `subMaterials`（旧）和 `materials`（新迁移格式）数组字段
- [ ] **多骨架动画实体（Bug #2）**：动画状态从 SceneManager 全局单例下沉到 per-ModelEntity 字段，drawFrame 逐实体独立求值，CachedModelResource 缓存 skeleton + clips 共享数据
- [ ] **缩略图骨架污染（Bug #1）**：临时模型加载的 RAII save/restore guard 在 Bug #2 修复后被移除

## 2026-06-18

- [✓] 外部 LLM 验证与运行时确认：修复 skeleton parse order、bone UBO identity init、recreate() vert→frag param、push constants layout、animation bind-pose fallback、multi-skin handling、per-slot material cloning、per-skin bone palettes、128-bone limit。构建通过（MSVC 2022, C++20），用户确认多部件角色/机器人动画渲染正确
- [✓] 记录验证经验：数据依赖顺序、Vulkan UBO 初始化、create/recreate 一致性、push-constant/descriptor-set 布局、部分动画通道处理、多 skin per-palette、骨骼容量
- [✓] 将已验证的 A3 DailyProgress 条目标记为 [✓]
- [✓] MaterialManager 新增 `createSkinnedMaterialFrom(src)` 从非蒙皮材质克隆为蒙皮版本
- [✓] 新增 `getMetallicRoughnessPath()` / `getAoPath()` / `getEmissivePath()` / `hasSkinning()` 访问器
- [✓] 新增 `Application::convertModelMaterialsToSkinned()` 辅助函数
- [✓] 实现多 skin skeleton 合并、mesh-node skin 绑定、全局 nodeToBone 动画解析、每 skin 局部 joint 计数
- [✓] GPU 骨骼 palette 容量提升至 `kMaxBones = 256`，重新编译 `skinned_vert.spv`
- [✓] 更新 TDD.md，标记 GPU skinning 完成

## 2026-06-17

- [✓] 扩展 Vertex 结构体：新增 boneIndices（ivec4）和 boneWeights（vec4）蒙皮字段
- [✓] 新增 `Vertex::getSkinnedAttributeDescriptions()` 7 属性布局（locations 6/7 为骨骼）
- [✓] 新增 `kMaxBones = 256` 常量和 `BoneMatricesUBO` 结构体
- [✓] 创建 `skinned_vert.glsl` 顶点着色器，4-bone weighted blend GPU skinning
- [✓] MaterialManager 实现 `createSkinnedMeshMaterial()`，per-swapchain-image `BoneMatricesUBO` 分配
- [✓] 新增 `updateBoneMatrices()` per-frame bone matrix 上传
- [✓] MaterialEntry 扩展 `hasSkinning_` 标志和 `boneUBOs/Memory/Mapped` 向量
- [✓] PipelineManager 新增 skinned descriptor set layout（binding 6）和 skinned mesh pipeline
- [✓] SceneManager::loadModelFromGltf 读取 JOINTS_0/WEIGHTS_0 顶点属性
- [✓] Application::recordCommandBuffer 动态 VkPipelineLayout 选择集成 skinned pipeline
- [✓] drawFrame per-frame 动画采样 + computeFinalMatrices + updateBoneMatrices
- [✓] 全部 7 个 A3 子任务完成，构建零错误
- [✓] 更新 TDD.md Phase A3 GPU skinning 文档