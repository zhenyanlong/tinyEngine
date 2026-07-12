## 2026-07-12

- [ ] Implemented Sequencer ImGui panel (TODO-026/027/028): timeline with adaptive second-based ruler, track list with Mute/Solo, clip rectangles + keyframe diamond nodes, clip property editor, track management (Add/Delete), Load/Save .seq.json, camera path recording controls, sequence duration editing (IMGUIManager.cpp)
- [ ] Implemented TransformKeyframe system (Phase B alternative): TransformKeyframe struct (time/position/rotation/scale/easeToNext) + TransformKeyframeTrack (targetEntityId binding) + evaluate(t) with prev/next keyframe interpolation (Sequence.hpp/.cpp)
- [ ] Added 7 blend modes: Linear, SmoothStep, EaseIn, EaseOut, EaseInOut, Cubic, Exponential via applyEaseCurve() utility function (Sequence.hpp)
- [ ] Extended SequencePlayer with onTransformKeyframeEval callback + setSequenceRef() for live UI sync + totalDur<=0 keyframe preview fallback (SequencePlayer.hpp/.cpp)
- [ ] Extended SequenceAssetLoader to serialize TransformKeyframeTrack (save/load with time-sorted keyframes) (SequenceAssetLoader.cpp)
- [ ] Added Sequencer preview mechanism: sequencerPreviewPending_ flag in Application, requestSequencerPreview() API, onTransformKeyframeEval only fires when playing or preview requested — prevents continuous Transform override (Application.hpp/.cpp)
- [ ] Fixed keyframe-timeline alignment: unified contentOriginX coordinate basis across timeline ruler and track clip rows, removed child window borders for consistent x positioning (IMGUIManager.cpp)
- [ ] Added UI workflow buttons: Add Selected to Track (bind camera to keyframe track), Add Keyframe (auto-record entity Transform), Update from Entity (write back Transform), Preview / Preview Here (single-shot preview) (IMGUIManager.cpp)
- [ ] Fixed seqPlayer_ vs currentSequence_ desync: setSequenceRef() called every frame in drawFrame to reference external Sequence instead of stale owned copy (Application.cpp, SequencePlayer.cpp)
- [ ] Fixed timeline click not triggering live preview: click now calls seek() + requestSequencerPreview() to update entity Transform immediately (IMGUIManager.cpp)
- [ ] Made timeline ruler adaptive: range = max(duration + 2, 10) instead of fixed 0-6 (IMGUIManager.cpp)
- [ ] Updated TDD.md §4.12.5: documented Sequencer Phase C as completed with keyframe system, 7 blend modes, preview mechanism, UI workflow

## 2026-07-09

- [ ] Fixed PiP color/depth output empty + PiP rendering same as main view (TODO-034): root cause was (1) all pipelines used static viewport/scissor without VK_DYNAMIC_STATE_VIEWPORT/SCISSOR, so PiP's vkCmdSetViewport(320×240) was silently ignored and geometry fell outside the framebuffer; (2) PiP renderpass used R8G8B8A8_UNORM while main pipelines were created on main renderpass (B8G8R8A8_SRGB), format-incompatible. Fix: enabled dynamic viewport/scissor on all 4 pipeline builders (PipelineManager.cpp); added vkCmdSetViewport/Scissor after main pass (Application.cpp) and pick pass (PickSystem.cpp) begin; changed PiP renderpass + color image to use swapchain format (RenderPassManager, FramebufferManager)
- [ ] Fixed skinned models still rendering with main view in PiP: createSkinnedMaterialFrom (MaterialManager.cpp) missed calling createPipResources, so skinned material pipDescSets was empty and getPipDescriptorSet fell back to the main descriptor set (bound to main view UBO). Fix: added createPipResources call after writeDescSets
- [ ] Fixed Camera model orientation off by Y -90° vs preview direction: added modelRotationOffset field to ModelEntity (SceneManager.hpp), set to Y -90° in createCameraEntity (SceneManager.cpp), applied as model matrix post-multiply in main pass, PiP pass, and pick pass (Application.cpp, PickSystem.cpp) — keeps cameraData.orientation (preview) unaffected while aligning displayed mesh orientation
- [ ] Added Gizmo world/local coordinate system toggle: pressing 4 key toggles gizmoLocal_ between ImGuizmo::WORLD and ImGuizmo::LOCAL (IMGUIManager.hpp/.cpp, Application.cpp processInput)
- [ ] Fixed createCameraEntity storing quaternion as Euler angles — `glm::degrees(glm::eulerAngles(orientation))` produced a vec3 in degrees but was assigned to a glm::quat field, corrupting rotation (SceneManager.cpp)
- [ ] Fixed SequencerCamera coordinate convention: changed forward() from -X back to -Z to match main Camera's convention (SequencerCamera.cpp)
- [ ] Fixed PiP shared mapped UBO timing conflict: added per-material PiP UBO + PiP descriptor set to MaterialManager (MaterialManager.hpp/.cpp), PiP render now uses updateAllPipUBOs + getPipDescriptorSet instead of overwriting/restoring main UBO (Application.cpp)
- [ ] Fixed Camera model loading: loadCameraModelOnce now uses FbxImporter::load() to load res/bin/mesh/Camera.fbx instead of incorrectly reading Camera.mesh.ast (JSON descriptor) as raw binary (SceneManager.cpp)
- [ ] Added PiP render skip for selected Camera entity itself to avoid self-occlusion (Application.cpp)
- [ ] Fixed recreateSwapChain not resetting pipTextureCreated_ after ImGui_ImplVulkan_Shutdown destroys all textures (Application.cpp)
- [ ] Cleaned up diagnostic std::cout statements from PiP render path
- [ ] **BUG (unresolved)**: PiP RenderPass draw calls execute correctly (input textures visible in RenderDoc), but color attachment and depth attachment outputs are empty — no fragments written. CPU-side verified: pipView != mainView, pipDescriptorSet != mainDescriptorSet, PiP UBO written correctly, pipeline colorWriteMask = RGBA, viewport/scissor set after beginRenderPass. Suspect: pipeline created on main renderpass (B8G8R8A8_SRGB) but used on PiP renderpass (R8G8B8A8_UNORM) — format mismatch may cause silent rendering failure. See TDD.md §16 for full investigation details.

## 2026-07-08

- [ ] Added ModelEntity::Type enum (Mesh/Camera) and Camera-specific fields (cameraData, cameraPreviewEnabled, cameraPreviewScale)
- [ ] Added syncCameraFromTransform() to sync position/orientation from ModelEntity transform to SequencerCamera
- [ ] Added SceneManager::createCameraEntity() to create Camera Actor entities with shared model cache
- [ ] Added Application::createCameraActor() and selectedCameraEntityId_ to track selected Camera
- [ ] Extended tryPickMainModel() to set selectedCameraEntityId_ when Camera Actor is clicked
- [ ] Added "New Camera" button in OperationWindow to create Camera Actor in front of main camera
- [ ] Added Camera Actor display in SceneOutliner with cyan color highlighting
- [ ] Modified PiP rendering logic to use selected Camera Actor's cameraData for view/proj matrices
- [ ] Added PiP overlay rendering in main viewport right-bottom corner (via ImGui foreground draw list)
- [ ] Added pipTextureCreated_ flag to avoid recreating PiP texture every frame
- [ ] Updated SequencerCamera::forward() to use -X as camera forward direction (per Camera Actor convention)
- [ ] **BUG (unresolved)**: PiP preview shows main viewport instead of selected Camera Actor's view — view matrix not correctly computed from Camera Actor transform
- [ ] Updated TDD.md §15 known-limitations table with Camera PiP bug status

## 2026-07-03