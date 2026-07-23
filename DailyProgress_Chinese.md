## 2026-07-23

- [ ] Animator State 新增显式 Forward/Reverse 播放方向与非负 Play Rate，并使用归一化播放进度；version 1 负 speed 资产自动迁移为 Reverse + 绝对倍率
- [ ] 新增 SingleClip/BlendSpace1D State Motion，支持 Float 参数驱动的带位置 Sample、端点钳制、归一化时间同步，以及 BlendSpace 内部与 State Transition 外部两层 TRS 姿势混合
- [ ] Root Motion 改为方向与循环安全的逐 Clip delta 提取，并跨 BlendSpace Sample 和 State Transition 混合；Sequencer 非连续 seek 会重置逐实体 Clip 历史
- [ ] `.animctrl.json` 升级为 version 2 并保留 v1 加载；扩展 Animator UI、兼容 Controller 筛选、保存验证、Clip Rename、参数重命名传播、被引用参数删除保护及当前 Sequence 事件更新
- [ ] 更新 TDD.md 与 animation-system-plan.md，同步 Phase B6 架构、迁移、兼容性、数据流、UI、Root Motion 和验收标准
- [ ] 验证通过：x64-debug 构建、Animator v1/v2/倒放/BlendSpace/Transition C++ smoke test、Python MCP 测试 10/10 与 git diff 检查

## 2026-07-22

- [ ] 修复无损 Swapchain 重建：窗口 resize 保留全部场景实体/模型缓冲、Transform、选择状态、材质/纹理 ID 与逐 skin SkinBinding；仅重建依赖交换链的 UBO/Descriptor、Framebuffer、Pipeline、CommandBuffer、逐 image fence 归属、Pick、PiP 与 ImGui 纹理 Descriptor
- [ ] 时间尺支持连续拖动：开始拖动时暂停播放，鼠标离开时间尺后仍保持捕获，将时间限制在 Sequence 时长内，并持续 seek/预览 Transform 与 Animator 轨道
- [ ] AnimatorKeyframe 改为从 0 到目标时间的确定性求值：SetFloat/SetInt/SetBool 在固定步长与精确关键帧边界采样，SetTrigger 只在一个边界脉冲；正常播放、直接 seek 与反向拖动共用同一纯状态机求值
- [ ] 废弃独立 Sequencer Event/AnimationClip 的运行时执行，移除相关 callback 和全实体 Trigger 广播；新增显式 Group 父轨道，旧空 AnimationClip 父轨道自动迁移，非空旧轨道仅保留 JSON 兼容并禁用执行
- [ ] 验证通过：x64-debug 构建、Animator 时间流/旧资产迁移 C++ smoke test、隐藏窗口 resize 运行测试（3/3 实体签名保持）、Python MCP 测试 10/10 与 git diff 检查
- [ ] 修复 Mixamo 多 skin 角色眼球漂移：将运行时骨骼 UBO/DescriptorSet 从共享 MaterialId 中拆分为按实体/材质/skinIndex 独立的 SkinBinding；主视口、PiP、GPU Pick、资源缓存复用、场景重载与清理均使用匹配绑定（MaterialManager.hpp/.cpp、SceneManager.hpp/.cpp、Application.cpp、PickSystem.cpp、SceneSerializer.cpp）
- [ ] 新增递归 Sequence 资产选择弹窗，支持搜索、刷新、元数据/错误展示、双击加载；加载采用原子替换，解析失败时保留当前编辑内容（IMGUIManager.hpp/.cpp、Application.hpp/.cpp、SequenceAssetLoader.cpp）
- [ ] 主摄像机 yaw 改为绕世界 WorldUp 旋转，新增 Q/E 沿世界轴下降/上升，并在 ImGui 捕获键盘或鼠标时阻止相机输入（camera.hpp/.cpp、Application.cpp）
- [ ] 修复程序重启后 Sequence 轨道无法驱动原模型：场景持久化 entityId/displayName，模型与 Camera 共用统一 ID 分配器，Transform/Animator 轨道持久化 targetEntityId + targetAstRelPath + targetDisplayName（SceneManager.hpp/.cpp、SceneSerializer.cpp、Sequence.hpp、SequenceAssetLoader.cpp）
- [ ] Sequence 加载新增向后兼容目标恢复：依次尝试现有 ID、唯一稳定元数据、旧 `[T]/[A]` 轨道名称推断；无法唯一匹配时弹出 Rebind Sequence Tracks 手动重绑定窗口，并以红色 Target Missing 标记（Application.hpp/.cpp、IMGUIManager.hpp/.cpp）
- [ ] 修复多 Transform 轨道单次预览只有第一条生效：全部轨道求值后再清除 sequencerPreviewPending_；修复 setSequenceRef() 每帧清空 EventClip 去重状态（Application.cpp、SequencePlayer.cpp）
- [ ] New Camera 改为直接复制当前主摄像机的世界位置与朝向，不再生成在视线前方 5 个单位处（IMGUIManager.cpp）
- [ ] 更新 TDD.md：同步本次蒙皮、Sequencer、相机控制和 Camera Actor 的架构、兼容性、当前状态、迁移方式与验证记录
- [ ] 验证通过：x64-debug 构建、Python MCP/stdio 测试 10/10、Sequence 绑定元数据 C++ 往返测试、Camera/Sequence 专项 smoke test 与 git diff 检查

## 2026-07-19

- [ ] 扩展帧捕获的逐请求 `include_ui` 选择：`include_ui=false` 仅跳过被捕获帧的 ImGui 合成，下一帧自动恢复 UI（FrameCapture.hpp/.cpp、Application.cpp、mcp_server/server.py）
- [ ] 新增 `tiny_asset_list` 注册资产查询；结果仅包含 ModelRegistry 中具有有效 OBJ/glTF/GLB/FBX payload 的可放置资产（Application.cpp、mcp_server/server.py）
- [ ] 新增 Agent 场景搭建工具 `tiny_model_place`、`tiny_model_get_transform`、`tiny_model_set_transform`、`tiny_model_delete`，支持部分 TRS 更新、欧拉角/四元数旋转输入、有限数检查、缩放范围限制和结构化错误（Application.hpp/.cpp、mcp_server/server.py）
- [ ] 抽取 `Application::placeRegisteredModel()`，使 MCP 与 Content Browser 拖放复用同一套模型、材质、子材质、动画资产和蒙皮材质初始化流程，不再模拟鼠标输入（Application.hpp/.cpp）
- [ ] 修复首模型实体 Transform 双数据源问题：主渲染、PiP 渲染和旧 ImGuizmo 路径统一使用 `ModelEntity::transform`；`mainModelTransform` 仅保留为兼容镜像（Application.cpp、IMGUIManager.cpp）
- [ ] 为每个实体保存局部包围盒，并在 schema v2 场景快照中计算世界 AABB，计算包含实体 TRS 与 Camera 模型旋转偏移（SceneManager.hpp/.cpp、Mcp/SceneSnapshot.cpp）
- [ ] FastMCP Server 扩展至 10 个工具并新增场景工具协议测试；x64-debug 构建通过，Python/stdio 测试 10/10 通过（mcp_server/tests）
- [ ] 完成真实引擎验收：列出 9 个可放置资产，创建两个模型，更新并回读 Transform，确认世界包围盒非零，人工检查带 UI/无 UI 的 1280x720 截图；随后删除验收实体并优雅关闭引擎
- [ ] 将 MCP 实现状态、剩余手动重载步骤、架构、协议与验证证据同步到 mcp-control-plan.md 和 TDD.md

## 2026-07-18

- [ ] 审查现有 MCP Agent 计划并划定一晚可完成的 MVP 边界，区分 Codex 可实现部分与需要手动启动引擎/重载 Codex 的步骤（mcp-control-plan.md）
- [ ] 评估动画降帧导出与曲线拟合/优化方案，定义平移、旋转、缩放误差指标与分阶段交付预期，并保存到 animation-compression-plan.md
- [ ] 实现 tinyEngine MCP 基础设施：主线程 CommandBridge 队列、跨平台 localhost NDJSON IpcServer、结构化错误处理、可重连生命周期以及 `--mcp/--port/--exit-after` 启动参数（src/Mcp、Application.cpp）
- [ ] 新增 `tiny_ping`、`tiny_engine_status`、`tiny_scene_snapshot`、`tiny_engine_shutdown`，并在包含 6 个实体的真实引擎场景中完成验证（mcp_server/server.py、Mcp/SceneSnapshot.cpp）
- [ ] 实现异步 Swapchain 帧捕获：Vulkan layout transition、GPU→CPU 回读、BGRA/RGBA 转换、PNG 编码、截图根目录路径校验与 MCP ImageContent 直接输出（Mcp/FrameCapture.cpp、mcp_server/server.py）
- [ ] 新增项目级 Codex MCP 配置、Python 包配置与测试；初始 x64-debug 构建通过，MCP/IPC/截图测试 6/6 通过（.codex/config.toml、pyproject.toml、mcp_server/tests）

## 2026-07-15

- [ ] 重构 Sequencer 面板为双列布局：固定左标签列（##seqLabels，无滚动）+ 可滚动右时间线列（##seqTimeline，水平滚动），通过 seqTimelineScrollY_ 实现垂直滚动同步（IMGUIManager.hpp/.cpp, TDD.md）
- [ ] 左列轨道标签从 InvisibleButton+Dummy 混用改为 ImGui::Selectable，修复 hover/click 不稳定问题（IMGUIManager.cpp）
- [ ] 修复 isParentTrack 对空子轨道的误判：新增 `!isSubTrack(ti)` 守卫，确保新建的 [T]/[A] 子轨道不被识别为父轨道（IMGUIManager.cpp）
- [ ] 修复时间线列行 Y 光标漂移：用独立累加 rowCursorY 替代 GetCursorScreenPos()，将行布局与 clip/keyframe item 的 cursor 污染解耦（IMGUIManager.cpp）
- [ ] 修复双列垂直对齐累积漂移：两列统一使用显式 SetCursorScreenPos + rowCursorY 累加，绕过 ImGui ItemSpacing（IMGUIManager.cpp）
- [ ] 新增重复实体检测："Add Selected to Track" 在实体已在序列中拥有父轨道时跳过创建，代之以状态消息提示（IMGUIManager.cpp）
- [ ] 更新 TDD.md Sequencer C6 章节：单列布局文档替换为双列布局，移除"已知 Bug"章节（两个 Bug 已修复）
- [ ] 新增 SceneSerializer 对 animatorControllerPath 的持久化：.scene.json 保存/加载控制器绑定（SceneSerializer.cpp）
- [ ] 新增 Animator 面板 Clear Controller 按钮，用于解绑并回退到运行时控制器（IMGUIManager.cpp）
- [ ] 新增 State Properties 编辑区的 Set as Default State 按钮（AnimatorController.hpp, IMGUIManager.cpp）
- [ ] 新增 defaultStateName() getter 到 AnimatorController（AnimatorController.hpp）
- [ ] Animator 关键帧 Param 编辑器从 InputText 改为 Combo 下拉，自动从 Controller 参数列表推断类型（IMGUIManager.cpp）
- [ ] Animator 关键帧 Initial State 从 InputText 改为 Combo 下拉，从 Controller states 列表选择（IMGUIManager.cpp）
- [ ] 修复场景加载后 AnimatorController 绑定被覆盖：SceneManager::setEntityAnimationData() 增加 guard 条件，实体已有 animatorControllerPath 时跳过 configureFromClips()，不再覆盖从 .scene.json 恢复的 Controller。移除 loadScene() 中冗余的 Controller 重试代码。（SceneManager.cpp, Application.cpp）
- [ ] 修复 AnimatorKeyframe 时间轴预览时状态过渡不生效：根因是 onAnimatorKeyframeEval 调用 AnimatorController::update(t) 以大步进推进状态机，导致 exitTime 检测窗口跳过、trigger 消费与内部状态耦合。重构方案：AnimatorController 新增 computeBlendAtTime() 纯函数，以 0.05s 步进逐帧模拟从 0 到 t 的状态演进，使用本地参数副本，不修改内部状态。（AnimatorController.hpp/.cpp, Application.cpp）
- [ ] 新增 Animator 关键帧 event 编辑自动预览功能：所有 event 变更点（参数名 Combo/InputText、值编辑、插值模式、添加/删除事件、删除关键帧）均自动触发 seek(editTime) + requestSequencerPreview()。（IMGUIManager.cpp）
- [ ] 更新 TDD.md：§4.12.3 新增 computeBlendAtTime() 文档，§4.12.5 将两个已修复 Bug 从"已知 Bug"移至"已修复的 Bug"，更新 C7 运行时描述

## 2026-07-14

- [ ] 实现 AnimatorKeyframeTrack 系统：新增 TrackType::AnimatorKeyframe、AnimatorParamEvent/AnimatorKeyframe/AnimatorKeyframeTrack 结构体、ParamInterp 枚举（Step/Linear/SmoothStep/EaseIn/EaseOut/EaseInOut/Cubic/Exponential）（Sequence.hpp/.cpp）
- [ ] 实现 AnimatorKeyframeTrack::evaluate()：从 t=0 累积应用所有事件，SetFloat/SetInt 支持关键帧间插值，SetBool/SetTrigger 跳变（Sequence.cpp）
- [ ] 扩展 EventClip：从 eventName 字符串改为完整 AnimatorEvent 结构体，支持 SetFloat/SetInt/SetBool/SetTrigger（Sequence.hpp）
- [ ] 新增 onAnimatorKeyframeEval 回调到 SequencePlayer::FrameCallbacks（SequencePlayer.hpp/.cpp）
- [ ] 在 Application.cpp 实现 onAnimatorKeyframeEval：重置状态机→应用累积参数→update(t) 演进到当前帧；有 AnimatorKeyframe 轨道时 BlendCommand 由状态机计算（Application.cpp）
- [ ] 实现 AnimatorKeyframeTrack 完整序列化：.seq.json 保存事件/插值/初始状态；EventClip 序列化支持所有事件类型；新增 paramInterpToStr/strToParamInterp 辅助函数（SequenceAssetLoader.hpp/.cpp）
- [ ] 新增 UE 风格层级轨道：点击 "Add Selected to Track" 自动创建父轨道 + Transform 子轨道 + Animator 子轨道（实体有 AnimatorController 时）；轨道用实体 displayName 命名（IMGUIManager.cpp）
- [ ] 统一 "Add Keyframe" 按钮：自动检测当前选中轨道类型，添加 Transform 或 Animator 关键帧（IMGUIManager.cpp）
- [ ] 新增 Animator 关键帧编辑 UI：事件类型下拉 / 参数名输入 / 值控件 / 插值模式下拉（IMGUIManager.cpp）
- [ ] TransformTween 轨道类型在 UI 中标记为 "(Deprecated)"，颜色改为灰色（IMGUIManager.cpp）
- [ ] 移除 Mute/Solo 按钮和 trackMuted_/trackSoloed_ 成员变量（简化代码，功能从未实现）（IMGUIManager.hpp/.cpp）
- [ ] 移除冗余的 selectedKeyframeTrackIdx_ 状态变量，统一使用 selectedTrackIdx（IMGUIManager.hpp/.cpp）
- [ ] 重构 Sequencer 面板为单列布局：所有内容（刻度尺+轨道行）放在一个 ##timelineScroll 子窗口内，避免并排子窗口争抢鼠标焦点（IMGUIManager.cpp）
- [ ] **未解决 Bug**：单列布局中轨道标签的 InvisibleButton hover/点击不稳定。根因可能与同一窗口内 Dummy 占位区域与 InvisibleButton 的交互有关，需要新会话排查 ImGui item 交互逻辑
- [ ] **未解决 Bug**：关键帧与时间轴刻度尺的竖向对齐可能因水平滚动偏移而不一致

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
