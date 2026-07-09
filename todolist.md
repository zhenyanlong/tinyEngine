# 待办功能清单

> 记录今天想做但暂时搁置、留待以后实现的功能点。每个条目使用稳定前缀，方便对话中按编号引用。

---

## 记录格式

每个条目包含以下信息：

```markdown
- [ ] TODO-001 【描述】一句话概括功能点
  - 上下文：为什么想做 / 阻塞原因 / 依赖项
  - 日期：YYYY-MM-DD
```

已完成条目移动到「已完成」，并补充完成日期和完成依据。

---

## 手动收集

> 在这里随手写不规范需求。之后调用 `$todo-normalize-inbox`，将本区内容整理为正式待办项。

（当前为空）

---

## 7 天开发计划（2026-07-03 ~ 2026-07-09）

> 基于当前进度：Phase A/B 全部完成，Phase C（Sequencer）和 Phase D（资产系统）待实现。加入两个新功能规划。

### 第 1 天（2026-07-03）— Sequencer 数据结构 + Content Browser 缩略图修复

- [x] TODO-018 【Sequencer】实现 C1：SequenceTrack / SequenceClip 数据结构
  - 上下文：Phase C1 基础。新建 `src/Animation/Sequence.hpp`，定义 SequenceClipBase、TrackType、AnimTrackClip、CameraPathClip、TransformTweenClip、EventClip、SequenceTrack、Sequence 等结构体
  - 日期：2026-07-03
  - **完成**：Sequence.hpp 已定义所有结构体（TrackType/TweenEase/SequenceClipBase/各Clip类型/SequenceTrack/Sequence），Sequence.cpp 已实现 totalDuration() 和 computeTotalDuration()，x64-debug 构建通过

- [x] TODO-019 【Sequencer】实现 C5 序列化：SequenceAssetLoader
  - 上下文：Phase C5 的序列化部分。新建 `src/Animation/SequenceAssetLoader.hpp/.cpp`，实现 Sequence 的 `saveSequence/loadSequence`（.seq.json）和 CameraPath 的 `saveCameraPath/loadCameraPath`（.campath.json）
  - 日期：2026-07-03
  - **完成**：SequenceAssetLoader.hpp/.cpp 已完整实现 saveSequence/loadSequence/saveCameraPath/loadCameraPath 以及 CameraPath::evaluate（支持 CatmullRom/Linear 插值），x64-debug 构建通过

- [ ] TODO-020 【Content Browser】修复离屏渲染缩略图 icon
  - 上下文：当前 Content Browser 中资产使用共享的 `res/icons/model.png` 占位符，而非实际模型的离屏渲染缩略图。`ThumbnailRenderer` 已能生成 `.png` 到 `res/thumbnails/`，但 Content Browser 的 `drawContentBrowser` 在缩略图不存在时回退到占位图标，且 `ThumbnailRenderer::generateAll` 只在 `res/bin/mesh/` 和 `res/models/` 下扫描，未覆盖新 `res/content/` 下的模型。修复：(1) 在 `Application::initVulkan` 末尾或模型导入后自动调用 `ThumbnailRenderer::generateAll`；(2) `generateAll` 扫描路径扩展至 `res/content/**/*.mesh.ast` 中引用的模型；(3) Content Browser 中缺失缩略图时显示"生成中"状态而非空白方块
  - 日期：2026-07-03
  - **部分完成**：ThumbnailRenderer pipeline 已修复（使用专用 thumbnail render pass，支持 per-subMesh 材质加载），但仍有三个渲染 bug 未解决（材质未显示、相机角度错误、skinned 模型全灰），Content Browser 显示逻辑尚未接入

### 第 2 天（2026-07-04）— Sequencer 播放控制器 + 基础轨道

- [x] TODO-021 【Sequencer】实现 C2：SequencePlayer 播放控制器
  - 上下文：Phase C2。新建 `src/Animation/SequencePlayer.hpp/.cpp`，实现 play/pause/stop/seek 和 `update(dt, FrameCallbacks)`，按时间轴驱动各轨道片段，支持 loop、event clip 防重复触发
  - 依赖：TODO-018（C1 数据结构）
  - 日期：2026-07-04
  - **完成**：SequencePlayer.hpp/.cpp 已完整实现，支持 play/pause/stop/seek/update，通过 FrameCallbacks 驱动 AnimationClip/CameraPath/TransformTween/Event 四种轨道，event clip 使用 hash 去重，支持 loop。x64-debug 构建通过

- [x] TODO-022 【Sequencer】实现 C4：TransformTween 轨道
  - 上下文：Phase C4。实现 TransformTweenClip 的 evaluate 和 ease 函数（Linear/SmoothStep/EaseIn/EaseOut），在 `SequencePlayer::update` 的 `onTransformTweenEval` callback 中更新场景对象的 position/rotation/scale
  - 依赖：TODO-021
  - 日期：2026-07-04
  - **完成**：TransformTweenClip::evaluate(localT) 已在 Sequence.cpp 中实现，支持 Linear/SmoothStep/EaseIn/EaseOut 四种 ease 模式，返回 EvalResult（position/rotation/scale）。x64-debug 构建通过

- [x] TODO-023 【Sequencer】将 SequencePlayer 接入 Application 主循环
  - 上下文：Phase C2-3/4。在 Application 中增加 `seqPlayer_` 和 `currentSequence_`，在 `drawFrame` 中调用 `seqPlayer_->update(dt, callbacks)`，Sequencer 播放期间屏蔽右键拖拽相机
  - 依赖：TODO-021
  - 日期：2026-07-04
  - **完成**：Application 已集成 seqPlayer_/currentSequence_/sequenceCameraActive_；drawFrame 中 Sequencer 驱动动画（设置 previewClipIndex/previewTime）、相机路径（加载 .campath.json 后写入 camera_ 状态并设置 sequenceCameraActive_ 屏蔽鼠标）、TransformTween（写入 entity.transform）、事件（触发 AnimatorEvent SetTrigger）；Sequencer 停止播放时自动恢复实体到正常模式。x64-debug 构建通过

### 第 3 天（2026-07-05）— Sequencer 相机路径 + 摄像机类

- [x] TODO-024 【Sequencer】实现 C3：CameraPath 轨道
  - 上下文：Phase C3。新建 `src/Animation/CameraPath.hpp/.cpp`，实现 CameraKeyframe、CameraPath 结构和 CatmullRom/Linear 插值 evaluate(t)，接入 Application 的录制功能（recordingCameraPath_ / recordingPath_）
  - 依赖：TODO-021
  - 日期：2026-07-05
  - **完成**：CameraKeyframe/CameraPath 数据结构、CatmullRom/Linear 插值 evaluate(t) 以及 saveCameraPath/loadCameraPath 已在 SequenceAssetLoader.hpp/.cpp 中实现；Application 新增录制 API（`beginCameraPathRecording`/`endCameraPathRecording`/`isRecordingCameraPath`/`getRecordingPath`/`setRecordInterval`）和 gameLoop 中的逐帧采样逻辑（每 `recordInterval_` 秒插入一个关键帧）；Sequencer 回放通过 SequencePlayer 的 `onCameraPathEval` callback 已在 TODO-023 中接入。创建示例文件 `res/sequences/paths/example_shot.campath.json`。x64-debug 构建通过

- [x] TODO-025 【Sequencer】实现 Sequencer 摄像机类与 PiP 小窗
  - 上下文：新增一个 SequencerCamera 类，封装独立的相机状态（Position、orientation、Fov）。在场景中添加一个可视化的摄像机模型（如三角锥 + 镜头框），选中后可在右下角打开一个小窗口（Picture-in-Picture），显示该摄像机视角的渲染画面。类似 UE 的"在视口中查看所选摄像机"功能。需要：(1) 新建 `SequencerCamera` 数据结构（独立的 Position/orientation/Fov/AspectRatio）; (2) 创建摄像机模型的 .mesh.ast 资产（可用简单的三角锥网格）; (3) 在 Content Browser 中可拖入场景; (4) 选中摄像机实体后，右下角出现 PiP 子窗口，通过第二个 Camera/Viewport 渲染场景（离屏 framebuffer → ImGui Image）
  - 日期：2026-07-05
  - **完成**：(1) `src/Animation/SequencerCamera.hpp/.cpp` 已实现 `SequencerCamera` 数据结构（position/orientation/fovDeg/aspectRatio/nearPlane/farPlane）和 `getViewMatrix()/getProjMatrix()/forward()/right()/up()` 方法；(2) RenderPassManager 新增 PiP RenderPass（VK_FORMAT_R8G8B8A8_UNORM + finalLayout=SHADER_READ_ONLY_OPTIMAL）；(3) FramebufferManager 新增 PiP 离屏资源（320x240 Color+Depth Image + VkSampler）；(4) Application 在 `recordCommandBuffer` 中使用 PiP RenderPass + Framebuffer 离屏渲染所有 modelEntities，并通过 `ImGui_ImplVulkan_AddTexture` 转换为 ImTextureID；(5) UIManager 新增 `showPipWindow_` Checkbox 和 `drawPipWindow()` 方法，使用 `ImGui::Image` 显示 PiP 纹理并提供相机参数编辑控件。**摄像机模型资产、Content Browser 拖入、G 键切换主视角**等扩展功能尚未实现（属后续优化）。x64-debug 构建通过

### 第 4 天（2026-07-06）— Sequencer ImGui 编辑器面板（上）

- [ ] TODO-026 【Sequencer】实现 C6 ImGui 面板：时间线 + 轨道列表
  - 上下文：Phase C6-1/2。在 `IMGUIManager` 中增加 `showSequencerPanel_` 开关，实现 `drawSequencerPanel()` 的顶部工具栏（Play/Pause/Stop、时间显示、Loop 勾选）、时间线刻度尺（手绘刻度 + 数字标签）、当前时间指示线（可拖动红色竖线触发 seek）、轨道列表（左列：名称 + Mute/Solo 图标）、片段区域（右列：各 track 的 clip 彩色矩形）
  - 依赖：TODO-018（C1 数据结构）
  - 日期：2026-07-06

- [ ] TODO-027 【Sequencer】实现 C6 ImGui 面板：片段属性编辑器 + 录制控制
  - 上下文：Phase C6-3/4。选中 clip 后底部显示属性编辑（AnimTrackClip：clipName 下拉、offset、speed；CameraPathClip：path 路径；TransformTweenClip：start/end TRS InputFloat3 + ease 下拉）。录制控制区域（Record Camera Path 开始/结束按钮、录制中红点指示、结束后自动保存 .campath.json）
  - 依赖：TODO-026
  - 日期：2026-07-06

### 第 5 天（2026-07-07）— Sequencer ImGui 面板（下）+ 资产系统 D1

- [ ] TODO-028 【Sequencer】实现 C6 ImGui 面板：轨道管理 + 加载/保存
  - 上下文：Phase C6-5/6。"Add Track"按钮 → Combo 选轨道类型 → 插入新 track；右键 track → 上下文菜单"Delete Track"。面板顶部右侧 InputText + Load/Save 按钮，实现 .seq.json 的加载与保存
  - 依赖：TODO-027
  - 日期：2026-07-07

- [ ] TODO-029 【资产系统】实现 D1：AnimationAssetRegistry
  - 上下文：Phase D1。新建 `src/Animation/AnimationAssetRegistry.hpp/.cpp`，实现 `scan(resRoot)` 扫描 res/ 下所有 .anim.ast / .animctrl.json / .seq.json / .campath.json 并分类注册。在 `Application::initVulkan` 末尾调用 `assetRegistry_.scan()`
  - 日期：2026-07-07

### 第 6 天（2026-07-08）— 资产系统 D2/D3 + 摄像机模型完善

- [ ] TODO-030 【资产系统】实现 D2：.ast 文件扩展 + D3：资产浏览器面板
  - 上下文：Phase D2/D3。D2：`MaterialAssetLoader` 解析 `animationAssetPath` / `animControllerPath`，`.ast` 自动填入动画引用；D3：在 `UIManager` 主面板新增 `TabBar`，将现有控件整理为 Scene/Assets/Animator/Sequencer 标签，Assets 标签展示动画资产列表（.anim.ast / .animctrl.json / .seq.json / .campath.json）+ 双击加载 + 拖拽到轨道
  - 依赖：TODO-029
  - 日期：2026-07-08

- [ ] TODO-031 【摄像机】完善摄像机模型与 PiP 渲染管线
  - 上下文：在 TODO-025 的基础上完善：(1) 摄像机模型在场景中显示为三角锥 + 视锥线框（用简单的顶点/索引数据或 box 组合）；(2) PiP 小窗口的离屏渲染使用独立的 RenderPass + Framebuffer，分辨率可调（默认 320x240），每帧渲染后通过 ImGui Image 显示；(3) PiP 窗口支持拖拽调整大小、右键关闭；(4) 选中摄像机实体时，主视口可切换为该摄像机视角（按 G 键切换）
  - 依赖：TODO-025
  - 日期：2026-07-08

### 第 7 天（2026-07-09）— 整合、调试、构建验证

- [ ] TODO-032 【整合】端到端集成测试：完整的 Sequencer 工作流
  - 上下文：验证以下完整流程：(1) 导入带骨骼动画的模型 → (2) 在 Animator 面板创建状态机（Idle/Walk 切换）→ (3) 在 Sequencer 面板创建 Sequence，添加 AnimationClip 轨道和 CameraPath 轨道 → (4) 播放 Sequence，验证动画 + 相机路径同时驱动 → (5) 保存 .seq.json 重启后恢复一致。修复集成过程中发现的 bug
  - 依赖：TODO-018 ~ TODO-031
  - 日期：2026-07-09

- [ ] TODO-033 【构建】x64-debug 构建通过 + smoke test
  - 上下文：完成所有 Phase C/D 代码后的最终构建验证。使用 VS 2022 CMake 执行 `cmake --build --preset x64-debug`，修复所有编译/链接错误。运行 smoke test：加载模型、播放动画、Sequencer 时间轴驱动、PiP 窗口渲染均无崩溃
  - 日期：2026-07-09

---

## 待办（长期 / 低优先级）

- [ ] TODO-006 【引擎/调试】支持 Agent 截图场景来调试和验证功能点
  - 上下文：从手动收集区整理；调试时需手动截图上传到外部 LLM 分析，流程繁琐。在 tinyEngine 中内置截图/帧捕获功能（如 RenderDoc 触发、离屏渲染到文件、或 Vulkan 帧缓冲导出），使 agent 可直接获取渲染画面进行分析和验证
  - 日期：2026-06-18
  - 来源：手动收集

- [ ] TODO-010 【Content Browser】添加删除资产文件功能，联动清理无引用的 bin payload
  - 上下文：从手动收集区整理；当前 Content Browser 无删除功能，废弃 .ast 文件需手动到文件系统删除。需求：在 Content Browser 中选中资产后提供删除按钮，删除 .ast 同时检查其引用的 bin/mesh、bin/texture、bin/anim 文件是否被其他 .ast 引用，若无引用则一并删除
  - 日期：2026-06-26
  - 来源：手动收集

- [ ] TODO-012 【动画压缩】加入 AI 压缩动画序列的功能
  - 上下文：当前动画序列占用空间较大，需要 AI 驱动的压缩方案减少资源体积
  - 日期：2026-06-26

- [ ] TODO-015 【方案展示】与老师讨论展示动画状态机和 Sequencer 系统的方案
  - 上下文：需要对外展示阶段性成果，确定合适的演示方式和内容
  - 日期：2026-06-27

---

## 已完成

- [x] TODO-034 【PiP 渲染】修复 PiP RenderPass color/depth attachment 输出为空的 Bug
  - 上下文：选中 Camera Actor 后 PiP 小窗应显示该摄像机视角，但画面始终与主视口一致。RenderDoc 抓帧确认 draw call 正常执行（input texture 可见），但 color attachment 和 depth attachment 输出为空（无片元写入）。
  - 日期：2026-07-09
  - 来源：PiP bug 调查会话
  - 完成日期：2026-07-09
  - 完成依据：根因是两个叠加问题——(1) 所有管线用静态 viewport（未声明 VK_DYNAMIC_STATE_VIEWPORT/SCISSOR），PiP 的 vkCmdSetViewport(320×240) 被静默忽略，几何落在 framebuffer 外；(2) PiP renderpass 用 R8G8B8A8_UNORM 而主管线在 B8G8R8A8_SRGB 的 main renderpass 上创建，格式不兼容。修复：给 4 个管线 builder 启用动态 viewport/scissor；主/pick pass begin 后补 vkCmdSetViewport；PiP renderpass 与 color image 改用 swapchain 格式。另修复蒙皮材质漏调 createPipResources 导致 PiP 用主视角的问题。详见 TDD.md §16.6。

- [x] TODO-017 【动画/加载】修复多个 .anim.ast 文件中的 clips 无法正确合并加载的问题
  - 上下文：Remy 模型有 3 个 .anim.ast 文件（通过 mesh.ast 的 animations 数组引用），每个包含 1 个 clip。当前加载后只显示 1 个 clip。
  - 日期：2026-07-02
  - 完成日期：2026-07-02
  - 完成依据：根因是 `loadAndApplyMaterialAsset` 调用 `ensureAnimationAssetForMeshAst` 时传入的 `astRelPath` 可能是 `.material.ast` 路径（不含 `animations` 数组），导致函数无法从该文件中读取动画引用。修复方案：(1) 在 `ensureAnimationAssetForMeshAst` 中增加回退逻辑：如果打开的 `.ast` 文件没有 `animations` 数组，则检查目标实体已有的 `astRelPath`（即 `.mesh.ast` 路径），若存在则以该路径重新读取；(2) 在 `loadAndApplyMaterialAsset` 中，当通过 `.mesh.ast` 交换模型时，将 `astRelPath` 记录到实体上，供回退逻辑使用。

- [x] TODO-016 【动画状态机】Phase B3-B5 完整实现
  - 上下文：B1-B2 运行时核心已就绪，需完成 B3 混合品质保障、B4 序列化、B5 ImGui 编辑器面板
  - 日期：2026-06-29
  - 完成日期：2026-06-29
  - 完成依据：
    - B3：BlendCurve 枚举（Linear/SmoothStep/EaseIn/EaseOut）+ applyBlendCurve() 工具函数；AnimatorTransition.blendCurve 字段；update() 输出 blendWeight 时应用 ease 曲线；旋转已使用 glm::slerp（B2 已实现）
    - B4：AnimatorController::saveToFile/loadFromFile 使用 nlohmann/json；MaterialAssetDesc 新增 animControllerPath 字段；MaterialAssetLoader 解析 .ast 的 animController 字段；Application::loadAndApplyMaterialAsset 自动加载；示例文件 res/animators/example.animctrl.json；loadFromFile 对所有 JSON key 做 contains()+is_array() 检查
    - B5：UIManager::drawAnimatorPanel() 三区布局（左侧侧边栏 | 右侧上半双栏 | 底部双栏）；左侧上半状态机资产列表 + Load/Save 按钮；左侧下半动画资产列表 + [▶] 预览按钮 + 拖拽 DND_ANIMCLIP；右侧上半 States | Outgoing Transitions 双栏；底部 Transition/State 编辑器 + 状态信息 + Add Param/State 按钮；ModelEntity.previewClipIndex 预览模式绕开状态机直接播放 clip
    - B5-6：AnimatorEvent 结构体 + dispatchEvent/dispatchEvents 方法，为 Sequence 系统预留事件驱动接口
    - 修复：ImGui BeginGroup/EndGroup 不匹配导致的崩溃；condition 循环 ImGui ID 冲突；entity fallback 只选有蒙皮的模型
    - x64-debug 构建通过

- [x] TODO-014 【测试准备】准备一套模型和动画用于测试动画状态机系统
  - 上下文：动画状态机系统开发需要测试素材验证功能正确性
  - 日期：2026-06-27
  - 完成日期：2026-06-29
  - 完成依据：B3-B5 实现过程中使用已有 FBX/glTF 蒙皮模型验证状态机、过渡、预览功能；TODO-013 的 FBX 导入已提供测试素材路径

- [x] TODO-007 【动画/资产】将动画模型资产和动画序列资产分离为不同文件
  - 上下文：从手动收集区整理；当前动画数据随 glTF 一起加载，动画片段无独立文件格式存储和复用。参考 Phase A5 计划，需要实现 .anim.json 或 .anim 格式的独立动画资产文件，支持序列化/反序列化 AnimationClip
  - 日期：2026-06-18
  - 来源：手动收集
  - 完成日期：2026-06-28
  - 完成依据：Phase A5 的 AnimationAssetLoader 已实现 `.anim.ast` Header 与 `.anim.bin` 二进制序列化/反序列化；Mesh `.ast` 通过 `animations` 引用独立动画资产，glTF 与 FBX 导入均可生成并在运行时恢复 Skeleton/AnimationClip

- [x] TODO-013 【FBX 兼容】使项目兼容 FBX 格式的 model 和动画加载
  - 上下文：FBX 是业界通用格式，大量模型和动画资源为 FBX 格式，目前仅支持 glTF
  - 日期：2026-06-27
  - 完成日期：2026-06-28
  - 完成依据：以 Git Submodule 引入 ufbx；FbxImporter 已支持二进制/ASCII FBX 的网格、材质槽、PBR 参数、外部/内嵌纹理、蒙皮骨骼与烘焙动画解析，并接入 Content Browser、Mesh/Material/Anim 资产生成、SceneManager、ModelRegistry、缩略图与运行时缓存；x64-debug 构建及静态/材质/蒙皮动画 smoke tests 通过

- [x] TODO-001 【Content Browser】新增文件夹功能，import 的 .ast 放在当前浏览的文件夹下
  - 上下文：目前所有 .ast 都平铺在 materials/ 下，无组织
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：Content Browser 支持创建并切换文件夹；导入入口 .ast 与 glTF 子材质 .ast 均写入当前文件夹；x64-debug 构建通过

- [x] TODO-002 【Content Browser】新增 filter，根据 .ast 的 type 字段过滤显示
  - 上下文：Mesh 和 Material 类型混在一起，需要分类查看
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：ModelRegistry 读取 .ast 的 type 字段，Content Browser 提供 All/Mesh/Box/Material 筛选；x64-debug 构建通过

- [x] TODO-003 【.ast 格式】新增 "Material" 类型（type="Material"），不含 model 字段
  - 上下文：纯材质资产（不关联模型）需要独立表示；Material 类型的 ast 不能拖入场景
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：MaterialAssetLoader 解析 type="Material"，MaterialManager 保留运行时 Material 类型并继续使用 Mesh 渲染路径；x64-debug 构建通过

- [x] TODO-004 【Content Browser】Material 类型的 .ast 禁止拖拽放置到场景
  - 上下文：Material ast 无 model 引用，拖拽无意义
  - 依赖：.ast 新增 Material 类型
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：Content Browser 不为 Material 资产创建拖拽源，Application::beginDragPlace 也拒绝 Material 类型资产；x64-debug 构建通过

- [x] TODO-005 【SceneSerializer】支持含 subMaterials 字段的 .ast 文件
  - 上下文：import 生成的 .ast 带有 subMaterials 数组，保存/加载场景时需要处理
  - 日期：2026-06-11
  - 完成日期：2026-06-17
  - 完成依据：SceneSerializer 保存/加载 subMaterialOverrides，加载入口 .ast 的 subMaterials，并放宽槽位数量边界；x64-debug 构建通过

- [x] TODO-008 【动画/加载】修复重新打开 scene 后 mesh 渲染异常（动画重复加载）
  - 上下文：手动收集区整理；重新打开 scene 后 mesh 渲染异常，但删掉重新拖拽出来恢复正常
  - 日期：2026-06-26
  - 来源：手动收集
  - 完成日期：2026-06-26
  - 完成依据：根因是迁移脚本将 .mesh.ast 的 subMaterials 重命名为 materials，但 MaterialAssetLoader 只读 subMaterials，导致 loadScene 跳过子材质加载。修复 MaterialAssetLoader::load 兼容 materials 字段（subMaterials 为空时回退）；x64-debug 构建通过，用户确认重新打开场景后 mesh 渲染正常

- [x] TODO-011 【引擎/构建】res 文件夹不再编译时复制，以项目根 res/ 为唯一基准
  - 上下文：手动收集区整理；此前 CMake post-build 把 res/ 复制到 exe 旁，导致保存的场景被源 res/ 覆盖，且存在双份 res 造成路径混乱
  - 日期：2026-06-26
  - 来源：手动收集
  - 完成日期：2026-06-26
  - 完成依据：重构 applicationResourceRoot() 从 exe 路径向上查找项目根（CMakeLists.txt + res/），所有 res 读写以项目根/res/ 为基准；移除 CMakeLists.txt 的 copy_directory post-build 步骤；x64-debug 构建通过

- [x] TODO-009 【Content Browser】新建文件夹应在当前浏览的子文件夹下创建
  - 上下文：手动收集区整理；当前 Create 按钮固定在 content/ 根下创建目录，未拼接 currentFolder_。用户在子文件夹浏览时点 Create，新文件夹应创建在当前子文件夹内而非 content 根
  - 日期：2026-06-26
  - 来源：手动收集
  - 完成依据：IMGUIManager Create 按钮逻辑改为拼接 currentFolder_ + name 作为完整相对路径，create_directories 在 content/<currentFolder_>/<name> 下创建；currentFolder_ 为空时回退到 content/ 根；x64-debug 构建通过