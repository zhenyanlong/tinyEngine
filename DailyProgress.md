## 2026-07-25

- [ ] Replaced Swapchain-bound Sequence frame recording with a fixed-resolution `SequenceCaptureTarget`: each frame renders to an independent offscreen color/depth target, reads back sequential RGBA, and feeds the existing Media Foundation H.264/MP4 encoder without changing Sequence timing or asset formats
- [ ] Made Sequence recording survive lossless Swapchain rebuilds: resize/minimize/restore and shader-triggered rebuilds pause the current fixed-step frame, recreate only the capture GPU target, and retry the same frame index without finalizing or truncating the MP4
- [ ] Classified Camera Actor meshes as editor-only visualization: free editor and GPU Pick views retain them, while Shot-following main view, PiP, and final Sequence capture filter every Camera Actor; Camera/Shot output now also applies actor FOV/Near/Far lens settings
- [ ] Expanded recording diagnostics and UI documentation: `capture.json` now includes completion, captured duration, fixed-offscreen target, last Shot Camera, and Swapchain rebuild count; Include UI temporarily locks window resize and restores it afterward
- [ ] Validation passed: `example01_withCamera` completed 600/600 frames at 20.0 s and 30 FPS while surviving two deliberate window resizes; a retained offscreen PNG contained no Camera Actor geometry; x64-debug/x64-release builds, 10/10 MCP tests, and `git diff --check` passed; user confirmed the final program run succeeded

## 2026-07-23

- [ ] Replaced Animator Preview Mode with default-off Sequencer Control: only explicitly tracked Transform/Animator/Camera outputs are owned by the Sequencer, while untracked animators continue live; fixed Clip Preview and reset root-motion history across ownership/seek changes
- [ ] Consolidated Sequencer state-machine events into AnimatorKeyframe SetFloat/SetInt/SetBool/SetTrigger evaluation, and replaced Track Management with invariant-safe Track Actions for Group selection/creation and confirmed cascade deletion
- [ ] Added a sequence-scoped, unique global Camera/Shot track with hard-cut keyframes, stable Camera Actor rebinding, same-frame Transform-before-Shot evaluation, pinned timeline editing, and version-2 JSON round trips; retained CameraPath only for legacy sequences without a Shot track
- [ ] Implemented fixed-step PNG sequence recording with start/end/FPS/Take/include-UI settings, nonzero-start pre-roll, exact frame-time evaluation, sequential GPU readback, capture manifests, partial-result Stop/Cancel/resize handling, and restoration of playhead/camera/entity state
- [ ] Improved editor layout: the timeline ruler now ends exactly at Duration, the Sequencer parameter area uses a larger resizable horizontal split, the Animator resource/editor columns use a resizable vertical split, and Controller action buttons wrap to the resource-panel width
- [ ] Added explicit Forward/Reverse Animator state playback with non-negative Play Rate and normalized progress; version-1 negative speed assets migrate automatically to Reverse + absolute rate
- [ ] Added SingleClip/BlendSpace1D State Motion with Float-driven positioned samples, endpoint clamping, normalized-time synchronization, and two-layer BlendSpace/State-transition TRS pose blending
- [ ] Made Root Motion direction- and loop-aware by extracting per-clip deltas and blending them across BlendSpace samples and State transitions; discontinuous Sequencer seeks reset per-entity clip history
- [ ] Upgraded `.animctrl.json` to version 2 while retaining v1 loading; expanded Animator UI, compatible-controller filtering, save validation, Clip rename, parameter rename propagation, referenced-parameter deletion protection, and current-Sequence event updates
- [ ] Updated TDD.md and animation-system-plan.md for Phase B6 architecture, migration, compatibility, data flow, UI, Root Motion, and acceptance criteria
- [ ] Validation: x64-debug builds and Animator/CameraShot smoke tests passed; the final auto-wrap-only edit passed ClCompile with 0 errors, while full relink was blocked by the running tinyEngine.exe (LNK1168); git diff checks passed

## 2026-07-22

- [ ] Fixed lossless Swapchain recreation: resize now preserves all scene entities/model buffers, transforms, selections, material/texture IDs, and per-skin SkinBindings; only swapchain-dependent UBO/descriptors, framebuffers, pipelines, command buffers, image-fence ownership, Pick, PiP, and ImGui texture descriptors are rebuilt
- [ ] Added continuous timeline scrubbing: ruler drag pauses playback, captures the mouse outside the ruler, clamps to Sequence duration, and continuously seeks/previews Transform and Animator tracks
- [ ] Made AnimatorKeyframe evaluation deterministic from 0 to the requested time: persistent SetFloat/SetInt/SetBool values are sampled at fixed and exact keyframe boundaries, SetTrigger is a one-boundary pulse, and playback/direct/backward seek share the same pure state-machine evaluation
- [ ] Deprecated independent Sequencer Event and AnimationClip execution, removed their callbacks and global Trigger broadcast, added explicit Group tracks, and migrated empty legacy AnimationClip parent tracks to Group while retaining non-empty legacy data as disabled JSON-compatible tracks
- [ ] Validation passed: x64-debug build, Animator timeline/legacy migration C++ smoke test, hidden-window resize runtime test preserving 3/3 entity signatures, 10/10 Python MCP tests, and git diff checks
- [ ] Fixed Mixamo multi-skin eye drift by separating runtime bone UBO/descriptor ownership from shared MaterialId into per-entity/material/skinIndex SkinBinding resources; main view, PiP, GPU pick, cache reuse, scene reload, and cleanup now use the matching binding (MaterialManager.hpp/.cpp, SceneManager.hpp/.cpp, Application.cpp, PickSystem.cpp, SceneSerializer.cpp)
- [ ] Added a recursive Sequence asset picker with search, refresh, metadata/error display, double-click loading, and atomic load semantics that preserve the current edit on parse failure (IMGUIManager.hpp/.cpp, Application.hpp/.cpp, SequenceAssetLoader.cpp)
- [ ] Changed main-camera yaw to rotate around world WorldUp, added Q/E world-axis descent/ascent, and gated camera input while ImGui captures keyboard or mouse (camera.hpp/.cpp, Application.cpp)
- [ ] Fixed Sequence tracks losing their model after restart: scene files now persist entityId/displayName, model and Camera IDs share one allocator, and Transform/Animator tracks persist targetEntityId + targetAstRelPath + targetDisplayName (SceneManager.hpp/.cpp, SceneSerializer.cpp, Sequence.hpp, SequenceAssetLoader.cpp)
- [ ] Added backward-compatible target recovery in Sequence loading: existing ID, unique stable metadata, then legacy `[T]/[A]` track-name inference; unresolved or ambiguous tracks open a manual Rebind Sequence Tracks modal and show red Target Missing state (Application.hpp/.cpp, IMGUIManager.hpp/.cpp)
- [ ] Fixed single-shot preview with multiple Transform tracks by clearing sequencerPreviewPending_ after all track evaluation; fixed EventClip deduplication by making setSequenceRef() retain state when the referenced Sequence is unchanged (Application.cpp, SequencePlayer.cpp)
- [ ] Changed New Camera to spawn exactly at the current main-camera world position and orientation instead of five units in front (IMGUIManager.cpp)
- [ ] Updated TDD.md architecture, compatibility, known-status, migration, and validation sections for the session's skinning, Sequencer, camera-control, and Camera Actor changes
- [ ] Validation passed: x64-debug builds, 10/10 Python MCP/stdio tests, Sequence binding metadata C++ round-trip, camera/Sequence smoke tests, and git diff checks

## 2026-07-19

- [ ] Extended frame capture with per-request `include_ui` selection: `include_ui=false` skips ImGui composition only for the captured frame and restores UI automatically on the next frame (FrameCapture.hpp/.cpp, Application.cpp, mcp_server/server.py)
- [ ] Added registered-asset discovery through `tiny_asset_list`; results are restricted to placeable ModelRegistry entries with valid OBJ/glTF/GLB/FBX payloads (Application.cpp, mcp_server/server.py)
- [ ] Added Agent scene-building tools `tiny_model_place`, `tiny_model_get_transform`, `tiny_model_set_transform`, and `tiny_model_delete`, including partial TRS updates, Euler/quaternion rotation input, finite-number checks, scale bounds, and structured errors (Application.hpp/.cpp, mcp_server/server.py)
- [ ] Extracted `Application::placeRegisteredModel()` so MCP and Content Browser drag-place reuse the same model, material, sub-material, animation-asset, and skinned-material setup path without simulating mouse input (Application.hpp/.cpp)
- [ ] Fixed the first model entity Transform double-source bug: main and PiP rendering plus the legacy ImGuizmo path now use `ModelEntity::transform`; `mainModelTransform` remains only as a compatibility mirror (Application.cpp, IMGUIManager.cpp)
- [ ] Added per-entity local bounds and schema-v2 world AABB calculation to scene snapshots, including entity TRS and camera model rotation offset (SceneManager.hpp/.cpp, Mcp/SceneSnapshot.cpp)
- [ ] Expanded the FastMCP server to 10 tools and added scene-tool protocol tests; x64-debug build and all 10 Python/stdio tests passed (mcp_server/tests)
- [ ] Completed live-engine validation: listed 9 placeable assets, placed two models, updated/read back Transform, verified non-zero world bounds, and visually checked 1280x720 screenshots with and without UI; removed validation entities and gracefully shut down the engine
- [ ] Synchronized MCP implementation status, remaining manual reload step, architecture, protocol, and validation evidence across mcp-control-plan.md and TDD.md

## 2026-07-18

- [ ] Reviewed the existing MCP Agent plan and produced an achievable one-night MVP boundary, separating Codex implementation work from manual engine/Codex reload steps (mcp-control-plan.md)
- [ ] Assessed animation frame-rate compression with curve fitting/optimization, defined translation/rotation/scale error metrics and staged delivery expectations, and saved the proposal in animation-compression-plan.md
- [ ] Implemented the tinyEngine MCP foundation: main-thread CommandBridge queue, cross-platform localhost NDJSON IpcServer, structured error handling, reconnect-safe lifecycle, and `--mcp/--port/--exit-after` startup controls (src/Mcp, Application.cpp)
- [ ] Added `tiny_ping`, `tiny_engine_status`, `tiny_scene_snapshot`, and `tiny_engine_shutdown`, then verified them against a running engine with a six-entity scene (mcp_server/server.py, Mcp/SceneSnapshot.cpp)
- [ ] Implemented asynchronous Swapchain frame capture with Vulkan layout transitions, GPU-to-CPU readback, BGRA/RGBA conversion, PNG encoding, capture-root path validation, and direct MCP ImageContent output (Mcp/FrameCapture.cpp, mcp_server/server.py)
- [ ] Added project-local Codex MCP configuration and Python packaging/tests; initial x64-debug build and six MCP/IPC/capture tests passed (.codex/config.toml, pyproject.toml, mcp_server/tests)

## 2026-07-15

- [ ] Refactored Sequencer panel from single-column to dual-column layout: fixed left label column (##seqLabels, NoScrollbar) + scrollable right timeline column (##seqTimeline, HorizontalScrollbar), with vertical scroll sync via seqTimelineScrollY_ (IMGUIManager.hpp/.cpp, TDD.md)
- [ ] Replaced track label InvisibleButton+Dummy mixed layout with dedicated ImGui::Selectable in left column, fixing hover/click instability (IMGUIManager.cpp)
- [ ] Fixed isParentTrack false-positive for empty sub-tracks: added `!isSubTrack(ti)` guard so newly created [T]/[A] sub-tracks are not misidentified as parent tracks (IMGUIManager.cpp)
- [ ] Fixed per-row cursor Y drift in timeline column: replaced GetCursorScreenPos() with independent rowCursorY accumulator, decoupling row layout from clip/keyframe item cursor pollution (IMGUIManager.cpp)
- [ ] Fixed dual-column vertical alignment drift: unified both columns to use explicit SetCursorScreenPos + rowCursorY accumulation, bypassing ImGui ItemSpacing (IMGUIManager.cpp)
- [ ] Added duplicate entity detection: "Add Selected to Track" now skips creation if the entity already has a parent track in the current sequence, showing status message instead (IMGUIManager.cpp)
- [ ] Updated TDD.md Sequencer C6 section: replaced single-column layout docs with dual-column layout, removed the "Known Bugs" section (both bugs fixed)
- [ ] Added SceneSerializer persistence for animatorControllerPath: save/load controller bindings in .scene.json (SceneSerializer.cpp)
- [ ] Added Clear Controller button in Animator Panel to unbind and revert to runtime controller (IMGUIManager.cpp)
- [ ] Added Set as Default State button in State Properties editor (AnimatorController.hpp, IMGUIManager.cpp)
- [ ] Added defaultStateName() getter to AnimatorController (AnimatorController.hpp)
- [ ] Changed Animator keyframe Param editor from InputText to Combo dropdown with auto type inference from Controller params (IMGUIManager.cpp)
- [ ] Changed Animator keyframe Initial State from InputText to Combo dropdown from Controller states list (IMGUIManager.cpp)
- [ ] Fixed scene-load AnimatorController binding overwrite: added guard in SceneManager::setEntityAnimationData() to skip configureFromClips() when entity already has animatorControllerPath from .scene.json. Removed redundant Controller reload retry in loadScene(). (SceneManager.cpp, Application.cpp)
- [ ] Fixed AnimatorKeyframe sequencer preview not reflecting state transitions: root cause was onAnimatorKeyframeEval calling AnimatorController::update(t) with large dt causing exitTime detection to skip and trigger states coupling with internal params. Refactored by adding computeBlendAtTime() pure function to AnimatorController — simulates state evolution from 0 to t in 0.05s steps using local param copies, returns BlendCommand without mutating internal state. (AnimatorController.hpp/.cpp, Application.cpp)
- [ ] Added auto-preview on AnimatorKeyframe event changes: all event editor mutation points (param combo/input, value edit, interp mode, add/remove event, delete keyframe) now automatically trigger seek(editTime) + requestSequencerPreview(). (IMGUIManager.cpp)
- [ ] Updated TDD.md: documented computeBlendAtTime() in §4.12.3, moved two resolved bugs from "Known Bugs" to "Fixed Bugs" in §4.12.5, updated C7 runtime description

## 2026-07-14

- [ ] Implemented AnimatorKeyframeTrack system: new TrackType::AnimatorKeyframe, AnimatorParamEvent/AnimatorKeyframe/AnimatorKeyframeTrack structs, ParamInterp enum (Step/Linear/SmoothStep/EaseIn/EaseOut/EaseInOut/Cubic/Exponential) (Sequence.hpp/.cpp)
- [ ] Implemented AnimatorKeyframeTrack::evaluate() with cumulative event application from t=0, SetFloat/SetInt interpolation between keyframes, SetBool/SetTrigger step mode (Sequence.cpp)
- [ ] Extended EventClip from eventName string to full AnimatorEvent struct, supporting SetFloat/SetInt/SetBool/SetTrigger (Sequence.hpp)
- [ ] Added onAnimatorKeyframeEval callback to SequencePlayer::FrameCallbacks (SequencePlayer.hpp/.cpp)
- [ ] Implemented onAnimatorKeyframeEval in Application.cpp: reset state machine → apply accumulated params → update(t) to advance to current frame; BlendCommand uses state machine when AnimatorKeyframe track present (Application.cpp)
- [ ] Implemented full serialization for AnimatorKeyframeTrack: events/interp/initialState in .seq.json; EventClip serialization supports all event types; added paramInterpToStr/strToParamInterp helpers (SequenceAssetLoader.hpp/.cpp)
- [ ] Added UE-style hierarchical tracks: "Add Selected to Track" creates parent track + Transform sub-track + Animator sub-track (if entity has AnimatorController); tracks named by entity displayName (IMGUIManager.cpp)
- [ ] Unified "Add Keyframe" button: auto-detects selected track type and adds Transform or Animator keyframe accordingly (IMGUIManager.cpp)
- [ ] Added Animator keyframe editor UI: event type dropdown / param name input / value control / interp mode dropdown (IMGUIManager.cpp)
- [ ] Marked TransformTween track type as "(Deprecated)" in UI with gray color (IMGUIManager.cpp)
- [ ] Removed Mute/Solo buttons and trackMuted_/trackSoloed_ member variables (simplification, feature was never implemented) (IMGUIManager.hpp/.cpp)
- [ ] Removed redundant selectedKeyframeTrackIdx_ state variable, unified to selectedTrackIdx (IMGUIManager.hpp/.cpp)
- [ ] Refactored Sequencer panel to single-column layout: all content (ruler + track rows) in one ##timelineScroll child window, avoiding parallel child window mouse focus contention (IMGUIManager.cpp)
- [ ] **BUG (unresolved)**: Track label InvisibleButton hover/click is unreliable in single-column layout. Root cause likely related to Dummy placeholder interaction with InvisibleButton in same window. Needs new session to investigate ImGui item interaction logic
- [ ] **BUG (unresolved)**: Keyframe vertical alignment with timeline ruler may be inconsistent due to horizontal scroll offset

## 2026-07-13

- [ ] Fixed Animator Preview being cleared by Sequencer: changed condition from `!seqHasAnimTrack || !seqPlayer_.isPlaying()` to `seqPlayer_.isPlaying() && !seqHasAnimTrack` so Animator panel Preview button is not overwritten every frame (Application.cpp)
- [ ] Fixed `+ Add State` / drag-create state resetting current state to old one: pass new state name as defaultState to `configure()` instead of `ctrl.currentStateName()`, state now activates on creation (IMGUIManager.cpp)
- [ ] Added clipName display and editing in State Properties area, allowing users to see and modify which clip a state binds to (IMGUIManager.cpp)
- [ ] Fixed duplicate clip names across .anim.ast files: auto-dedup with `_1`, `_2` suffix on load (AnimationAssetLoader.cpp)
- [ ] Removed Apply State button: clipName/speed/loop edits now write directly to state machine, no longer trigger `configure()` → `reset()` (IMGUIManager.cpp)
- [ ] Added Set Active State button: directly switches current state without going through transitions (AnimatorController.hpp/.cpp, IMGUIManager.cpp)
- [ ] Removed Apply Transition button: all transition properties (toState, fadeDuration, hasExitTime, exitTime, blendCurve, conditions) now write directly to state machine (IMGUIManager.cpp)
- [ ] Added per-state Root Motion support: 3 modes (None / Locked / Follow), configurable root bone, Follow mode applies root delta to entity transform (AnimatorController.hpp/.cpp, SceneManager.hpp, Application.cpp, IMGUIManager.cpp)
- [ ] Added parameter name editing and delete button in Parameters section (IMGUIManager.cpp)
- [ ] Changed Condition param from manual InputText to Combo dropdown with dynamic Op/Threshold per param type (Float: Greater/Less/Equal/NotEqual + DragFloat, Bool: Equal/NotEqual/True/False, Trigger: True/False) (IMGUIManager.cpp)
- [ ] Added Animator Preview Mode toggle: ON (default) — state machine drives animation; OFF — Sequencer drives animation, Animator panel becomes read-only, `onAnimClipEval` skipped in preview mode (Application.hpp/.cpp, IMGUIManager.cpp)
- [ ] Shrunk Parameter and Condition element widths to improve layout compactness (IMGUIManager.cpp)

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

## 2026-06-30

- [ ] Added a model-aware Animator Controller asset browser that recursively filters `res/animators/` by clip compatibility and provides Refresh, New AnimController, and Save Current actions.
- [ ] Reworked Animator clip rows with explicit Preview/Stop and inline Rename controls, and replaced unsupported Unicode state icons with ASCII Active/State labels.
- [ ] Implemented validated animation clip renaming that updates `.anim.ast` metadata without rewriting `.anim.bin` and synchronizes runtime clips plus current controller state/transition references.
- [ ] Added per-entity animation/controller asset path tracking for glTF and FBX-backed models so UI edits persist to the correct assets.
- [ ] Fixed Windows animation-asset replacement failures by releasing the input file before transactional temp/backup rename and using font-safe error codes.
- [ ] Renamed the selected animation clip from Scene to Idle during UI validation, synchronized TDD.md, and passed the final x64-debug build.

## 2026-06-29

- [ ] Implemented Phase B3 blend quality: added BlendCurve enum (Linear/SmoothStep/EaseIn/EaseOut) and applyBlendCurve() helper; AnimatorTransition.blendCurve field; update() applies the active curve to blendWeight output; rotation already uses glm::slerp from B2.
- [ ] Implemented Phase B4 state machine serialization: AnimatorController::saveToFile/loadFromFile using nlohmann/json; MaterialAssetDesc gains animControllerPath field; MaterialAssetLoader parses .ast "animController" field; Application::loadAndApplyMaterialAsset auto-loads the controller; created res/animators/example.animctrl.json; loadFromFile validates all JSON keys with contains()+is_array() to avoid exceptions.
- [ ] Implemented Phase B5 Animator Panel (animation control center): three-zone layout (left sidebar + right-top dual columns + bottom dual columns); left-top shows current controller with Load/Save buttons and DND_ANIMCTRL drag source; left-bottom lists animation clips with Preview/Stop buttons and DND_ANIMCLIP drag source; right-top-left States list with current marker and drag-drop target to create states from clips; right-top-right Outgoing Transitions list with + Add Transition button; bottom-left Transition/State editor (toState combo, fade/exitTime/blendCurve/conditions with per-index ImGui IDs to avoid conflicts); bottom-right status info + Add Param/State buttons + Reset Controller.
- [ ] Implemented Phase B5 preview mode: ModelEntity gains previewClipIndex/previewTime/previewSpeed fields; Application::drawFrame bypasses the state machine and plays animationClips[previewClipIndex] directly when previewClipIndex >= 0.
- [ ] Implemented Phase B5-6 event-driven system: AnimatorEvent struct (SetFloat/SetInt/SetBool/SetTrigger + paramName + value) and AnimatorController::dispatchEvent/dispatchEvents methods, providing a standard interface for the future Sequence system to drive state machine transitions.
- [ ] Fixed Animator panel crash caused by unmatched ImGui BeginGroup/EndGroup (extra BeginGroup at function top with no matching EndGroup, triggering Missing EndGroup() assert in ErrorRecoveryTryToRecoverWindowState).
- [ ] Fixed entity fallback logic to only select entities with hasSkin_ && skeleton, preventing null dereference when the first model entity lacks skinning data.
- [ ] Fixed ImGui ID conflicts in the condition editor loop by appending _%d index suffixes to all control labels (Param/Op/Threshold/X).
- [ ] Hardened loadFromFile with is_object() check and per-key contains()+is_array() guards for states/params/transitions/conditions arrays.
- [ ] Updated TDD.md section 4.12.3 with B3-B5 details, MaterialAssetDesc.animControllerPath field, and "Known limitations" table; updated animation-system-plan.md B3/B4/B5 checkboxes to [x] and added B5-6; added TODO-016 to todolist.md and marked TODO-014 as completed.
- [ ] Passed x64-debug build with 0 errors.

## 2026-06-28

- [ ] Added ufbx as a Git submodule and integrated `ufbx.c` plus include paths into the CMake build.
- [ ] Implemented `FbxImporter` for binary/ASCII FBX mesh triangulation, material slots and PBR parameters, external/embedded textures, skeleton/skin weights, and 30 Hz baked animation clips; normalized axes/units and flipped FBX V coordinates at the import boundary.
- [ ] Integrated FBX into the Content Browser import dialog, categorized Mesh/Material/Anim asset generation, ModelRegistry format classification, SceneManager runtime loading/cache, and recursive thumbnail discovery.
- [ ] Fixed imported FBX entities falling back to Default Mesh by configuring `MaterialAssetLoader` with the project-absolute `res/` root and resolving entry/sub-material assets independently of the process working directory.
- [ ] Fixed texture-less material rendering with neutral fallback textures so Base Color, Metallic, Roughness, Emissive Color, and Emissive Intensity remain effective without texture maps.
- [ ] Fixed Properties texture editing with per-material persistent input buffers, absolute or `res/`-relative path resolution, transactional Albedo/Normal replacement, Material-type support, and visible load status.
- [ ] Passed the x64-debug build and FBX smoke tests covering binary and ASCII geometry, material/texture references, skinned meshes, and finite animation matrices; synchronized TDD.md and completed TODO-007/TODO-013.

## 2026-06-27

- [ ] Implemented Phase B1 AnimatorState, AnimatorTransition, typed parameters, condition evaluation, and one-shot Trigger consumption in the new AnimatorController module.
- [ ] Implemented Phase B2 per-entity state-machine updates with AnyState/current-state transitions, normalized exit time, zero-duration switching, and BlendCommand output.
- [ ] Added BoneLocalTransform TRS sampling and per-bone cross-fade blending using translation/scale mix and quaternion slerp before final matrix evaluation.
- [ ] Fixed animated glTF mouse selection by adding a skinned GPU pick shader/pipeline that reuses per-submesh material descriptors and current bone palettes.
- [ ] Passed the x64-debug build, AnimatorController condition/fade/Trigger/exit-time smoke test, and SPIR-V validation; synchronized TDD.md and Phase B1-B2 plan checkboxes.

## 2026-06-26

- [ ] **Resource path architecture**: Refactored `applicationResourceRoot()` to locate the project root (containing `CMakeLists.txt` + `res/`) by walking up from the exe path. All res read/write (mesh/anim/scene/texture/shader/thumbnail) now use `<project-root>/res/` as the single source of truth. Removed the CMake `copy_directory` post-build step that previously synced `res/` next to the exe, which was silently overwriting saved scenes on every rebuild. Rolled back the temporary `saveScene` double-write hack and the `ThumbnailRenderer` source-dir search, both no longer needed.
- [ ] **Scene path migration (Bug #3)**: Added `resolveLegacyAstPath()` in `SceneSerializer` to remap old `materials/<stem>.ast` references to new `content/<stem>.mesh.ast` by recursively searching `res/content/`. Applied at both `.ast` model-field lookup and `ent->astRelPath` assignment in `load()`, so saved scenes automatically migrate to the new layout on the next save.
- [ ] **Sub-material field compatibility**: `MaterialAssetLoader::load` now reads both `subMaterials` (legacy) and `materials` (new migration-script format) array fields. The migration script renamed `subMaterials` → `materials`, but the loader only read the old name, causing `loadScene` to skip sub-material loading (all slots fell back to default white textures) while `beginDragPlace` worked due to its `autoAstPaths` fallback. Fixed by reading `materials` as a fallback when `subMaterials` is empty.
- [ ] **Multi-skeleton animation entities (Bug #2)**: Moved animation state from `SceneManager` global singletons down to per-`ModelEntity` fields. `drawFrame` rewritten to iterate entities and evaluate each one's animation independently. `CachedModelResource` now caches skeleton (shared_ptr) + clips so repeated drags of the same model share animation data.
- [ ] **Thumbnail skeleton pollution (Bug #1, superseded by Bug #2 fix)**: Initially added an RAII guard in `ThumbnailRenderer::renderAndSave` to save/restore the global skeleton/clips around temp-model loading. This guard was later removed once Bug #2 moved animation state per-entity.

## 2026-06-18

- [✓] External LLM verification and runtime validation: fixed skeleton parse order, bone UBO identity init, recreate() vert→frag param, push constants layout, animation bind-pose fallback, multi-skin handling, per-slot material cloning, per-skin bone palettes, and the 128-bone limit. Build passed (MSVC 2022, C++20), and user confirmed the multi-part character/robot animation now renders correctly.
- [✓] Logged verification lessons covering data-dependency ordering, Vulkan UBO initialization, create/recreate parity, push-constant/descriptor-set layout consistency, partial animation channel handling, per-skin palettes for multi-skin glTFs, and bone palette capacity.
- [✓] Marked verified A3 DailyProgress entries as [✓] after source audit, build verification, asset inspection, and user runtime confirmation.
- [✓] Added `createSkinnedMaterialFrom(src)` to MaterialManager to clone an existing non-skinned material into a skinned version preserving all texture paths and MaterialParams.
- [✓] Added `getMetallicRoughnessPath()` / `getAoPath()` / `getEmissivePath()` / `hasSkinning()` accessors to MaterialManager.
- [✓] Added `Application::convertModelMaterialsToSkinned()` helper that iterates all sub-mesh slots and converts each PBR material individually.
- [✓] Replaced duplicated skinned-material-creation blocks in initVulkan/recreateSwapChain/beginDragPlace/loadAndApplyMaterialAsset with `convertModelMaterialsToSkinned()`.
- [✓] Implemented multi-skin skeleton merge in SceneManager::loadModelFromGltf: iterate all skins, build unified skeleton via globalNodeToBone, and construct `skinBoneIndices` per skin for palette generation.
- [✓] Added mesh-node skin binding: map each glTF mesh to the skin referenced by its node, store `SubMesh::skinIndex`, and keep vertex JOINTS_0 values skin-local for the shader.
- [✓] Updated animation parsing to use globalNodeToBone for channel boneIndex lookup instead of skin[0]-only.
- [✓] Joint clamp now uses the selected skin's local joint count; vertex weights are normalized per vertex.
- [✓] Raised GPU bone palette capacity to `kMaxBones = 256` and recompiled `skinned_vert.spv`, covering the verified asset's 191-joint character skin.
- [✓] Updated TDD.md §4.10/§4.11/§4.12.3/§7.1/§15 with final multi-skin, per-skin palette, material cloning, 256-bone palette, and verification lessons; marked GPU skinning as completed.
- [✓] **Resolved runtime issue**: skinned materials and animations now render correctly for the two-part character + robot model after per-skin palette upload and 256-bone shader/UBO capacity.

## 2026-06-17

- [✓] Extended Vertex struct with `boneIndices` (ivec4) and `boneWeights` (vec4) fields for skinning vertex data — verified, pipeline renders correctly
- [✓] Added `Vertex::getSkinnedAttributeDescriptions()` returning 7-attribute layout (locations 6/7 for bones) — verified, pipeline uses skinned attribute desc
- [✓] Updated `Vertex::operator==` and `VertexHash` to include skinning attributes in comparison and hashing — verified, mesh loading with skinning data works
- [✓] Added `kMaxBones = 256` constant and `BoneMatricesUBO` struct (256 × mat4, 16384 bytes) to `VulkanTypes.hpp` — verified, covers the 191-joint character skin; see lessons-learned.md #1
- [✓] Created `skinned_vert.glsl` vertex shader with 4-bone weighted blend GPU skinning and compiled to `skinned_vert.spv` — verified, pipeline compiles correctly; see lessons-learned.md #3
- [✓] Implemented `createSkinnedMeshMaterial()` in `MaterialManager` with per-swapchain-image `BoneMatricesUBO` allocation — verified, identity-init fix applied; see lessons-learned.md #2
- [✓] Added `updateBoneMatrices()` to `MaterialManager` for per-frame bone matrix upload via `memcpy` — verified, identity-fill fix + now called in drawFrame; see lessons-learned.md #2, #4
- [✓] Extended `MaterialEntry` with `hasSkinning_` flag and `boneUBOs/Memory/Mapped` vectors, updated `allocateDescSets` / `writeDescSets` / `destroyEntry` / `onSwapchainRecreate` for skinned material support — verified, recreate identity-init fix applied; see lessons-learned.md #2
- [✓] Added skinned descriptor set layout (binding 6 = BoneMatricesUBO, vertex stage) and skinned mesh pipeline (`skinnedMeshPipeline_`) to `PipelineManager` — verified, recreate() vert/frag parameter fix applied; see lessons-learned.md #3
- [✓] Updated `SceneManager::loadModelFromGltf` to read `JOINTS_0` (uint8/uint16/uint32 via raw buffer) and `WEIGHTS_0` vertex attributes, set `ModelEntity::hasSkin_` flag — verified, parse order + joint clamp + weight normalize fix applied; see lessons-learned.md #1
- [✓] Integrated skinned pipeline routing in `Application::recordCommandBuffer` with dynamic `VkPipelineLayout` selection — verified, push constants layout fix applied; see lessons-learned.md #4
- [✓] Auto-created skinned materials for skeletal entities in `initVulkan`, `recreateSwapChain`, `beginDragPlace`, and `loadAndApplyMaterialAsset` — verified, animation sampling + bone matrix upload wired in drawFrame; see lessons-learned.md #4
- [✓] Per-frame animation sampling: evaluate AnimationClip 0 with bind-pose fallback, computeFinalMatrices, updateBoneMatrices for all skinned entities — verified, bind-pose fallback overload added to AnimationClip; see lessons-learned.md #5
- [✓] All 7 A3 sub-tasks completed, project compiles with zero errors (MSVC 2022, C++20) — verified, cmake --build --preset x64-debug passed
- [✓] Updated TDD.md with Phase A3 GPU skinning documentation: Vertex layout (locations 6/7), BoneMatricesUBO, skinned descriptor set layout, skinned pipeline specs, skinned_vert.glsl shader details, material auto-creation flow, data flow extensions, and marked GPU skinning as completed
