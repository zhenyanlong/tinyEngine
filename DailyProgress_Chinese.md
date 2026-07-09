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

## 2026-07-03