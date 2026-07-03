## 2026-07-03

- [ ] Planned the remaining 7-day development roadmap for Phase C (Sequencer) and Phase D (Asset System), plus two new feature additions: Content Browser offscreen thumbnail fix and Sequencer Camera with PiP view
- [ ] Updated TDD.md: added §4.12.5 Sequencer system spec (C1-C6), §4.12.6 AnimationAssetRegistry spec, updated directory structure (animators/, sequences/, icons/), updated init order with thumbnail generation and asset scanning, extended §15 known-limitations table with new planned items
- [ ] Updated todolist.md with 7-day plan: Day 1 (C1+C5+thumbnail fix), Day 2 (C2+C4), Day 3 (C3+Camera PiP), Day 4 (C6 panel top), Day 5 (C6 panel bottom+D1), Day 6 (D2/D3+Camera model), Day 7 (integration test+build)
- [ ] Recorded VS 2022 CMake path for future builds: C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

## 2026-07-02

- [ ] Fixed `ensureAnimationAssetForMeshAst` fallback logic: when called with a non-mesh `.ast` path (e.g. `.material.ast`) that lacks an `"animations"` array, the function now checks the target entity's `astRelPath` and falls back to reading the mesh `.ast` to find all `.anim.ast` references
- [ ] Fixed `loadAndApplyMaterialAsset` to record `astRelPath` on the entity when swapping via a `.mesh.ast`, so subsequent `ensureAnimationAssetForMeshAst` calls can correctly locate animation assets
- [ ] Completed TODO-017: Remy model now correctly loads all 3 `.anim.ast` clips instead of only 1

## 2026-06-30

- [ ] Added `FbxImporter::loadAnimationOnly()` to parse without-skin (Mixamo-style) FBX as a virtual skeleton from scene node hierarchy.
- [ ] Added `AnimationRetargeter` module with `buildMapping()`, `retargetClip()` and `retargetPose()` for name-based bone remapping between source and target skeletons.
- [ ] Added `Application::importAnimationFbx()` to orchestrate the full animation retarget pipeline: load target mesh skeleton, parse source FBX animation, retarget clips, persist as `.anim.ast` + `.anim.bin`, and update the target mesh `.ast` animations field.
- [ ] Integrated animation retarget into the import model flow so FBX models with embedded animations also go through the retarget pipeline.
- [ ] Added `Import Anim...` button and target-mesh picker modal to the Content Browser UI for importing without-skin animation FBX files.
- [ ] Fixed `existingAnimAsts` variable scope bug in `ensureAnimationAssetForMeshAst` that caused a compile error (declared inside a block but used later outside it).
- [ ] Fixed `contentPrefix` double-prefix bug where the animation asset path was written as `content/content/...` instead of `content/...`.
- [ ] Removed unused `FbxAnimationOnlyResult::clipName` field and its related code.
- [ ] Renamed the selected animation clip from Scene to Idle during UI validation, synchronized TDD.md, and passed the final x64-debug build.

- [ ] Added a model-aware Animator Controller asset browser that recursively filters `res/animators/` by clip compatibility and provides Refresh, New AnimController, and Save Current actions.
- [ ] Reworked Animator clip rows with explicit Preview/Stop and inline Rename controls, and replaced unsupported Unicode state icons with ASCII Active/State labels.
- [ ] Implemented validated animation clip renaming that updates `.anim.ast` metadata without rewriting `.anim.bin` and synchronizes runtime clips plus current controller state/transition references.
- [ ] Added per-entity animation/controller asset path tracking for glTF and FBX-backed models so UI edits persist to the correct assets.
- [ ] Fixed Windows animation-asset replacement failures by releasing the input file before transactional temp/backup rename and using font-safe error codes.
- [ ] Renamed the selected animation clip from Scene to Idle during UI validation, synchronized TDD.md, and passed the final x64-debug build.

## 2026-06-29

- [ ] Implemented Phase B3 blend quality: added BlendCurve enum (Linear/SmoothStep/EaseIn/EaseOut) and applyBlendCurve() helper; AnimatorTransition.blendCurve field; update() applies the active curve to blendWeight output; rotation already uses glm::slerp from B2.
- [ ] Implemented Phase B4 state machine serialization: AnimatorController::saveToFile/loadFromFile using nlohmann/json; MaterialAssetDesc gains animControllerPath field; MaterialAssetLoader parses .ast "animController" field; Application::loadAndApplyMaterialAsset auto-loads the controller; created res/animators/example.animctrl.json; loadFromFile validates all JSON keys with contains()+is_array() to avoid exceptions.
- [ ] Implemented Phase B5 Animator Panel (animation control center): three-zone layout (left sidebar + right-top dual columns + bottom dual columns); left-top shows current controller with Load/Save buttons and DND_ANIMCTRL drag source; left-bottom lists animation clips with [▶] preview / [■] stop buttons and DND_ANIMCLIP drag source; right-top-left States list with ● current marker and drag-drop target to create states from clips; right-top-right Outgoing Transitions list with + Add Transition button; bottom-left Transition/State editor (toState combo, fade/exitTime/blendCurve/conditions with per-index ImGui IDs to avoid conflicts); bottom-right status info + Add Param/State buttons + Reset Controller.
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
- [ ] **Multi-skeleton animation entities (Bug #2)**: Moved animation state from `SceneManager` global singletons (`skeleton_` / `animationClips_`) down to per-`ModelEntity` fields (`ent.skeleton` / `ent.animationClips`). `loadModelFromObj`/`loadModelFromGltf` now populate the entity's own skeleton/clips. `drawFrame` rewritten to iterate entities and evaluate each one's animation independently. New `setEntityAnimationData`/`getEntitySkeleton`/`getEntityAnimationClips` APIs take an `entityId`. `ensureAnimationAssetForMeshAst` gained an `entityId` parameter. `CachedModelResource` now caches skeleton (shared_ptr) + clips so repeated drags of the same model share animation data. Removed the `ThumbnailRenderer` RAII skeleton save/restore guard (no longer needed: temp entity load no longer pollutes other entities' state). Different-skeleton animation models can now coexist in the same scene without overwriting each other.
- [ ] **Thumbnail skeleton pollution (Bug #1, superseded by Bug #2 fix)**: Initially added an RAII guard in `ThumbnailRenderer::renderAndSave` to save/restore the global `skeleton_`/`animationClips_` around temp-model loading. This guard was later removed once Bug #2 moved animation state per-entity, making the global pollution impossible.

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
- [ ] Updated `session-init` skill with DailyProgress reading step, updated `daily-progress` skill with verification marker spec, added `[ ]` markers to all DailyProgress entries
- [ ] Updated `session-init` skill with step 4: scan `.codex/skills/` directory, added Codex skill inventory to summary output, and documented `todo-sync-completed` / `todo-normalize-inbox` awareness
- [ ] Created `verify-fix` skill: processes external LLM verification feedback, analyzes root cause, writes lessons to `lessons-learned.md`, and marks verified items as `[✓]`
- [ ] Created `mark-verified` skill: simple manual toggle of `[ ]` → `[✓]` in DailyProgress for user-confirmed items
- [ ] Created `lessons-learned.md` and `lessons-learned_Chinese.md` templates for accumulating verification-driven coding wisdom
- [ ] Updated `session-init` skill to include `lessons-learned.md` reading in step 2 and inventory all 6 `.trae/skills/` in notes
