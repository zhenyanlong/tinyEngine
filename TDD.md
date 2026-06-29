# tinyEngine 技术设计文档

> 最后更新：2026-06-28

---

## 1. 项目概述

tinyEngine 是一个基于 **Vulkan API** 的 C++20 实时 3D 渲染引擎，定位为教学/实践项目。采用 **ECM（Engine Core Modules）模式** 将 Vulkan 各子系统拆分为独立 Manager 类，由 `Application` 作为顶层调度器统一驱动主循环。

- **语言：** C++20
- **图形 API：** Vulkan 1.x（通过 `Vulkan::Vulkan` CMake 包）
- **构建系统：** CMake 3.8+
- **窗口系统：** GLFW 3.x
- **宿主平台：** Windows（代码兼容 Linux，部分路径硬编码需调整）

---

## 2. 目录结构

```
tinyEngine/
├── src/                          # 引擎核心源码（.hpp / .cpp）
│   ├── Application.hpp/.cpp      # 顶层调度器，主循环入口
│   ├── mainWindows.h/.cpp        # main() 入口
│   ├── VulkanContext.hpp/.cpp     # Vulkan 实例/设备/队列创建与选择
│   ├── VulkanTypes.hpp           # 共享类型（UBO/PushConst/QueueFamily等）
│   ├── SwapChain.hpp/.cpp         # 交换链管理
│   ├── RenderPassManager.hpp/.cpp # 主渲染通道 + 拾取渲染通道
│   ├── FramebufferManager.hpp/.cpp# 帧缓冲 + 深度附件 + 拾取附件
│   ├── CommandManager.hpp/.cpp    # 命令池/命令缓冲/同步原语
│   ├── BufferManager.hpp/.cpp     # GPU 缓冲区创建/拷贝/上传
│   ├── PipelineManager.hpp/.cpp   # 图形管线创建/缓存（支持动态不同 Shader）
│   ├── DescriptorManager.hpp/.cpp # Box UBO 描述符集（材质集由 MaterialManager 管理）
│   ├── TextureManager.hpp/.cpp    # 纹理加载与采样器创建
│   ├── MaterialManager.hpp/.cpp   # 材质系统（PBR、纹理、UBO、DescriptorSet）
│   ├── MaterialAssetLoader.hpp/.cpp # .ast JSON 材质资产反序列化
│   ├── SceneManager.hpp/.cpp      # 场景图：模型加载（OBJ/glTF/FBX）、Box 实体管理、骨骼动画解析
│   ├── FbxImporter.hpp/.cpp       # ufbx 适配层：网格、材质、骨骼与动画转换
│   ├── SceneSerializer.hpp/.cpp   # 场景持久化（.scene.json，含旧 materials/ → content/ 路径迁移）
│   ├── PickSystem.hpp/.cpp        # GPU 拾取（射线检测 Entity ID）
│   ├── ModelRegistry.hpp/.cpp     # 模型资产注册表（扫描 + 缓存元数据）
│   ├── ThumbnailRenderer.hpp/.cpp # 模型缩略图离屏渲染
│   ├── IMGUIManager.hpp/.cpp      # Dear ImGui + ImGuizmo UI 层
│   ├── Transform.hpp              # ObjectTransform TRS 结构体
│   ├── camera.hpp/.cpp            # FPS 自由相机（四元数姿态、平滑聚焦）
│   ├── vectex.hpp                 # Vertex/InstanceData 结构体 + Vulkan 属性描述
│   ├── TinyEngineDebug.hpp/.cpp   # RenderDoc + VK_EXT_debug_utils 集成
│   └── Animation/                 # 动画子系统
│       ├── Skeleton.hpp/.cpp      # Bone / Skeleton 数据结构 + computeFinalMatrices
│       ├── AnimationClip.hpp/.cpp # AnimChannel / AnimationClip + 关键帧求值
│       ├── AnimationAssetLoader.hpp/.cpp # Anim .ast + .anim.bin 二进制资产序列化
│       └── AnimatorController.hpp/.cpp # 状态、参数、过渡条件与运行时状态机
│
├── res/                           # 运行时资源（唯一基准，不再复制到 exe 旁）
│   ├── content/                   # 新资产描述文件根（.mesh.ast / .material.ast / .anim.ast）
│   ├── bin/                       # 二进制 payload（mesh/ anim/ texture/ 分类）
│   ├── models/                    # 旧 3D 模型文件（兼容保留）
│   ├── materials/                 # 旧模型资产定义（兼容保留）
│   ├── scenes/                    # 场景持久化文件（.scene.json）
│   ├── shaders/                   # GLSL 源码 + 编译后的 .spv
│   ├── textures/                  # 旧纹理（兼容保留）
│   └── thumbnails/                # 模型缩略图 PNG
│
├── thirdParty/                    # 第三方库（Git Submodule / 直接包含）
│   ├── glfw/                      # GLFW3 窗口库
│   ├── glm/                       # OpenGL Mathematics（header-only）
│   ├── imgui/                     # Dear ImGui（即时模式 GUI）
│   ├── ImGuizmo/                  # 3D 变换 Gizmo
│   ├── stb/                       # stb_image（图片加载）
│   ├── stb_image/                 # 另一个 stb 副本
│   ├── tinyobjloader/             # OBJ 模型解析
│   ├── cgltf/                     # glTF 2.0 模型解析
│   ├── ufbx/                      # FBX 模型/材质/骨骼/动画解析（Git Submodule）
│   ├── renderdoc/                 # RenderDoc 调试 API
│   └── json/                      # nlohmann/json（JSON 解析）
│
├── CMakeLists.txt                 # 项目 CMake 配置
├── CMakePresets.json              # CMake 预设
├── animation-system-plan.md       # 动画系统建设计划
└── .gitignore / .gitmodules       # Git 配置
```

---

## 3. 核心架构：渲染管线

### 3.1 主循环流程

```
main() → Application::run()
  ├── initGLFW()           # 创建窗口，注册回调
  ├── initVulkan()         # 初始化所有 Manager + 加载默认模型
  └── gameLoop()
        ├── glfwPollEvents()
        ├── processInput()        # WASD / QE 相机移动
        ├── camera_.UpdataCameraPosition(dt)
        ├── ui_->prepareFrame()   # ImGui NewFrame + 面板 + ImGuizmo
        ├── drawFrame()
        │     ├── vkAcquireNextImageKHR
        │     ├── matMgr_.updateAllUBOs(imageIndex, view, proj)
        │     ├── recordCommandBuffer(cb, imageIndex)
        │     │     ├── vkBeginRenderPass (clear color + depth)
        │     │     ├── Main Model (submesh loop, bind desc set per material)
        │     │     ├── Box Instances (instanced draw)
        │     │     ├── Pick pass (GPU pick)
        │     │     └── ImGui_ImplVulkan_RenderDrawData
        │     │     └── vkEndRenderPass
        │     └── vkQueueSubmit + vkQueuePresentKHR
        └── currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT
```

### 3.2 初始化顺序（initVulkan 内部）

```
ctx_.init(window_)                          # 1. Vulkan 实例/设备/队列
cmdMgr_.create(ctx_)                        # 2. 命令池
swapChain_.create(ctx_, window_)            # 3. 交换链
rpMgr_.create(ctx_, swapChain_)             # 4. 渲染通道（主 + 拾取）
bufMgr_.init(ctx_, cmdMgr_)                 # 5. 缓冲管理器
pipeMgr_.create(ctx_, rpMgr_, ...)           # 6. 图形管线
fbMgr_.create(ctx_, swapChain_, rpMgr_)     # 7. 帧缓冲
sceneMgr_.createCubeTemplate(bufMgr_)       # 8. Box 模板几何体
sceneMgr_.loadModel(path, pos, bufMgr_)     # 9. 加载默认模型
matMgr_.init(ctx_, cmdMgr_, ...)             # 10. 材质管理器
descMgr_.create(ctx_, swapChain_, ...)      # 11. 描述符管理器
ui_->initIMGUI()                            # 12. ImGui 后端
pickSys_.create(ctx_, cmdMgr_)              # 13. 拾取系统
cmdMgr_.allocateCommandBuffers(...)         # 14. 分配绘制命令缓冲
cmdMgr_.createSyncObjects(...)              # 15. 同步原语
```

---

## 4. 模块详解

### 4.1 VulkanContext（VulkanContext.hpp）

Vulkan 基础设施的封装，负责实例、设备、队列家族的创建与选择。

| 成员 | 类型 | 说明 |
|---|---|---|
| `instance_` | VkInstance | Vulkan 实例 |
| `physicalDevice_` | VkPhysicalDevice | 选择的物理设备 |
| `device_` | VkDevice | 逻辑设备 |
| `graphicsQueue_` | VkQueue | 图形队列（也用于计算） |
| `presentQueue_` | VkQueue | 呈现队列 |
| `surface_` | VkSurfaceKHR | 窗口表面 |
| `graphicsQueueFamilyIndex_` | uint32_t | 图形队列家族索引 |

**关键方法：**
- `findMemoryType()` — 查找满足属性要求的内存类型索引
- `findQueueFamilies()` — 查询队列家族
- `querySwapChainSupport()` — 查询交换链支持详情
- `getMaxAnisotropy()` — 查询设备最大各向异性采样级别

### 4.2 SwapChain（SwapChain.hpp）

交换链管理，封装 VkSwapchainKHR + Image + ImageView。

**交换链格式选择策略：** 优先 SRGB 色彩空间（`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`）；呈现模式优先 `VK_PRESENT_MODE_MAILBOX_KHR`（三重缓冲），回退 `VK_PRESENT_MODE_FIFO_KHR`。

### 4.3 RenderPassManager（RenderPassManager.hpp）

管理两个渲染通道：
- **主渲染通道** — 单 subpass，color attachment（最终输出）+ depth attachment
- **拾取渲染通道** — 单 subpass，R32UINT color attachment + depth attachment（用于 Entity ID 写入）

### 4.4 FramebufferManager（FramebufferManager.hpp）

管理帧缓冲资源：
- **主帧缓冲** — 每个 swapchain image 对应一个 framebuffer（color 来自 swapchain image view + 共享 depth）
- **拾取帧缓冲** — 独立的 color image（R32UINT）+ depth image
- **深度附件** — 所有 framebuffer 共享

### 4.5 CommandManager（CommandManager.hpp）

- 命令池创建（graphics queue family）
- 命令缓冲分配（per swapchain image，MAX_FRAMES_IN_FLIGHT=2 个飞行帧）
- 同步原语：`imageAvailableSemaphores_` / `renderFinishedSemaphores_` / `inFlightFences_` / `imagesInFlight_`
- 单次提交辅助：`beginSingleTimeCommands()` / `endSingleTimeCommands()`

### 4.6 BufferManager（BufferManager.hpp）

GPU 缓冲创建/销毁/拷贝的统一入口。提供：
- `createBuffer()` — 通用缓冲分配
- `copyBuffer()` — 使用 staging buffer 拷贝
- `uploadToDeviceLocal()` — 数据 → staging → device local 一条龙
- `createVertexBuffer()` / `createIndexBuffer()` — 顶点/索引缓冲快捷方法
- `createInstanceBuffer()` — 实例数据缓冲

### 4.7 PipelineManager（PipelineManager.hpp）

图形管线管理器。支持：

| 默认管线 | Vertex Shader | Fragment Shader | 用途 |
|---|---|---|---|
| `mainPipeline_` | `vert.spv` | `frag.spv` | 标准 Mesh 渲染 |
| `boxPipeline_` | `box_vert.spv` | `box.spv` | 实例化 Box 渲染（含 instance data） |
| `pickPipeline_` | `pick_vert.spv` | `pick_frag.spv` | GPU 拾取（输出 Entity ID） |
| `skinnedMeshPipeline_` | `skinned_vert.spv` | `frag.spv` | GPU 骨骼蒙皮渲染 |
| `skinnedPickPipeline_` | `skinned_pick_vert.spv` | `pick_frag.spv` | 按当前骨骼姿势拾取蒙皮模型 |

- **动态管线缓存**（`dynamicPipelines_`）：`acquirePipeline()` 按 `"variant|vert|frag"` 键缓存，支持运行时切换材质 Shader 而不重编译默认管线
- **图形管线状态：** Cull back face、Counter-clockwise front face、Depth test/stencil、无 blend（Mesh）/ 无 blend（Box）

### 4.8 DescriptorManager（DescriptorManager.hpp）

管理 Box UBO 描述符集 + 全局 Uniform Buffer Objects。

- 每个 swapchain image 持有一个 UBO（`UniformBufferObject`），包含 `view/proj/materialTint/boxMaterialTint/emissive/cameraPos/lightDir/lightColor/pbrFactors/viewProj/invView/invProj`
- Layout：`set=0, binding=0` = UBO（vertex + fragment stages）
- **注意：** 网格材质的描述符集由 `MaterialManager` 自行管理，`DescriptorManager` 不再持有 mesh descriptor sets

### 4.9 TextureManager（TextureManager.hpp）

- `loadTexture()` — 加载漫反射纹理（SRGB）
- `loadNormalMap()` — 加载法线贴图（UNORM，线性采样）
- `createSampler()` — 创建带各向异性过滤的采样器
- 使用 `VK_FORMAT_R8G8B8A8_SRGB`（albedo）/ `VK_FORMAT_R8G8B8A8_UNORM`（normal）

### 4.10 MaterialManager（MaterialManager.hpp）

材质系统的核心管理器，每个材质是一个自包含单元：纹理 + UBO + DescriptorSet。

**材质类型：**

| MaterialType | 描述符集 Layout | 纹理绑定 | 用途 |
|---|---|---|---|
| `Mesh` | binding 0=UBO, 1=albedo+sampler, 2=normal+sampler, 3=metallicRoughness+sampler, 4=ao+sampler, 5=emissive+sampler | PBR 全纹理 | 标准模型（含 model 字段） |
| `Box` | binding 0=UBO | 无纹理 | 实例化 Box（颜色由 UBO 控制） |
| `Material` | 同 Mesh（共用 Mesh 管线） | PBR 全纹理 | 纯材质资产（无 model 字段，不可拖入场景） |

**材质创建流程：**
1. 调用 `loadMaterialFromAsset(astRelPath)` 解析 `.ast` JSON → `MaterialAssetDesc`
2. `MaterialAssetLoader::load()` 负责 JSON 反序列化、路径解析（相对于 `res/`）
3. 回调 `createMeshMaterial()` 或 `createBoxMaterial()` 创建材质
4. 创建时自动上传纹理 → 分配 UBO → 分配 DescriptorSet → 记录到内部 map

**UBO 更新：** `updateAllUBOs(imageIndex, view, proj)` 每帧将所有活跃材质的 UBO 写入对应 mapped buffer

**材质管道选择：** `getPipeline(matId)` 返回该材质绑定的自定义管线（若有无），否则回退到默认 Mesh/Box 管线

**资产缓存：** `loadMaterialFromAsset()` 内部维护 `assetCache_`（`unordered_map<string, MaterialId>`），同一 `.ast` 路径只创建一次材质，后续调用直接返回已缓存的 MaterialId。缓存随 `destroy()` 清空。

**无贴图材质：** Mesh/Material 材质始终绑定 1×1 fallback 纹理。Albedo、MR、AO、Emissive 的 fallback 使用乘法单位值，使 `baseColor`、`roughness`、`metallic`、`emissiveColor`、`emissiveIntensity` 等 UBO 参数在对应贴图缺失时仍直接参与着色；Normal 使用 `(0.5, 0.5, 1.0)` 中性法线。

**纹理热替换：** Properties 面板可修改 Mesh 与 Material 类型的 Albedo/Normal。相对路径基于 `MaterialAssetLoader` 配置的绝对 `res/` 根解析；新纹理成功创建后才销毁旧纹理并重写 DescriptorSet，失败时保留原材质并在 UI/控制台反馈。

### 4.11 SceneManager（SceneManager.hpp）

场景管理，当前支持：
- **多模型实体**（`ModelEntity`）：每个实体拥有独立 Transform、GPU 缓冲、材质、选中状态
- **Box 实体**（GPU 实例化渲染）：`addBox()` / `removeBox()` / `setBoxPosition()`
- **SubMesh 系统**：glTF 多 primitive 按 `SubMesh` 分片，每片可绑定独立材质
- **glTF 皮肤/动画解析**：加载带骨骼的 glTF 时自动解析 `cgltf_skin` → `Skeleton`，`cgltf_animation` → `AnimationClip`
- **FBX 导入适配**：`FbxImporter` 将 ufbx 场景转换为统一的 Vertex/SubMesh/Material/Skeleton/AnimationClip 数据
- **每实体 Animator**：`ModelEntity` 独立持有 `Skeleton`、`AnimationClip[]` 和 `AnimatorController`，不同动画实体互不覆盖运行时状态
- **glTF 材质预生成**：`dumpGltfMaterialAst()` 公开静态方法，将 glTF primitive 材质导出为 `.ast`。供 `Application::importModel` 在导入时调用；`loadModelFromGltf` 检测 `.ast` 已存在则跳过重复生成

**拖拽放置系统（dragPlace）：**
- `beginDragPlace(assetId)` — 根据 `ModelAsset` 创建模型实体并跟踪拖拽状态；`Material` 类型资产拒绝创建实体
- `updateDragPlace(mx, my)` — 将实体位置实时更新到鼠标指向的世界坐标（距离由 `placementDistance_` 控制）
- `endDragPlace()` — 结束拖拽，清理临时状态

**导入系统（importModel）：**
- `importModel(sourcePath, subFolder="")` — 将外部模型 payload 拷贝到 `res/bin/mesh/`，在 `res/content/<subFolder>/<模型名>/` 下生成入口 `.mesh.ast`。glTF/FBX 同时生成逐槽位 `.material.ast`；带骨骼动画时生成 `.anim.ast` + `res/bin/anim/*.anim.bin`。FBX 的外部或内嵌纹理会复制/提取到 `res/bin/texture/<模型名>/`。

**PickId 分配：**
- `kPickIdNone = 0` — 无命中
- `kPickIdMainModel = 1` — 主模型
- `kPickIdBoxBase = 2 + i` — 第 i 个 Box 实例

**格式支持：**
- `.obj` — 通过 tinyobjloader 解析
- `.glb` / `.gltf` — 通过 cgltf 解析（单文件 cgltf.h 实现）
- `.fbx` — 通过 ufbx 解析（支持二进制与 ASCII FBX）

**glTF 加载特性：**
- 解析多个 primitive → 生成 SubMesh 列表
- 提取基础色/金属/粗糙度/法线/自发光纹理
- 自动生成 `.ast` 材质文件到 `res/materials/`
- 保留 AABB（`modelLocalBoundsMin_` / `modelLocalBoundsMax_`）
- 解析 `skins` → 生成 `Skeleton`（骨骼名称、父子关系、IBM、绑定局部变换）
- 解析 `animations` → 生成 `AnimationClip` 列表（通道 + 关键帧时间/值）

**FBX 加载特性：**
- 统一转换为右手系、Y-up、米制；自动补法线并三角化 polygon
- 按 material part 生成 SubMesh，保留 ufbx material slot 索引
- 提取 PBR/FBX fallback 的 Base Color、Roughness、Metallic、Emission，以及 Albedo/Normal/Emissive 外部或内嵌纹理引用
- 在导入边界执行 `v = 1 - v`，适配 FBX/DCC 与 stb_image/Vulkan 当前纹理原点约定，不影响 OBJ/glTF 路径
- 合并 skin 骨骼层级，生成 per-skin bone remap；每顶点保留并归一化前 4 个权重
- 将 animation stack 以 30 Hz 烘焙为 Translation/Rotation/Scale 通道，并复用引擎 `AnimationClip` 与独立 Anim 资产格式

#### 4.11.1 FbxImporter（FbxImporter.hpp/.cpp）

`FbxImporter::load(path, result, error)` 是 ufbx 与引擎数据结构之间的 CPU 适配边界。返回的 `FbxImportResult` 包含去重顶点、索引、SubMesh/材质槽、PBR 材质、AABB，以及可选 Skeleton/AnimationClip；SceneManager 负责 GPU buffer 创建，Application 负责将解析结果持久化为 Mesh/Material/Anim 资产。

当前 FBX 路径不导入 blend shape、约束、灯光/相机、分层/程序化材质；一个 mesh 有多个 skin deformer 时只使用第一个。骨骼 palette 仍受 `kMaxBones = 256` 限制。

**公开访问器：**
- `getEntitySkeleton(entityId)` → 指定实体的 `shared_ptr<Skeleton>`
- `getEntityAnimationClips(entityId)` → 指定实体的 `const vector<AnimationClip>&`
- `setEntityAnimationData(entityId, skeleton, clips)` → 更新实体动画数据并按 clip 建立默认 Animator 状态

### 4.12 动画子系统（src/Animation/）

#### 4.12.1 Skeleton（Skeleton.hpp/.cpp）

骨骼层级表示，供蒙皮和动画求值使用。

```cpp
struct Bone {
    std::string name;
    int         parentIndex = -1;        // -1 = 根骨骼
    glm::mat4   inverseBindMatrix{};     // glTF skin.inverseBindMatrices[i]
    glm::mat4   localBindTransform{};    // 绑定姿势下的局部 TRS
};

struct Skeleton {
    std::vector<Bone>                  bones;   // 深度优先排列（parentIndex < i）
    std::unordered_map<std::string, int> boneNameToIndex;

    // global[i] = global[parent] * local[i]
    // final[i]  = global[i] * inverseBindMatrix[i]
    void computeFinalMatrices(
        const std::vector<glm::mat4>& localTransforms,
        std::vector<glm::mat4>&       finalBoneMatrices) const;
};
```

#### 4.12.2 AnimationClip（AnimationClip.hpp/.cpp）

骨骼动画关键帧数据与运行时求值。

```cpp
enum class AnimInterpolation { Step, Linear, CubicSpline };

struct AnimChannel {
    int               boneIndex = -1;
    enum class Target { Translation, Rotation, Scale } target;
    AnimInterpolation interp = AnimInterpolation::Linear;
    std::vector<float>     times;   // 关键帧时间戳（秒）
    std::vector<glm::vec4> values;  // T/S=vec3, R=quat(xyzw)
};

struct AnimationClip {
    std::string              name;
    float                    duration = 0.f;
    std::vector<AnimChannel> channels;

    // 对单根骨骼求值 T*R*S 局部矩阵
    glm::mat4 evaluateBoneLocalTransform(int boneIndex, float t) const;

    // 返回可独立混合的 Translation / Rotation / Scale
    BoneLocalTransform evaluateBoneLocalTransformParts(
        int boneIndex, float t, const glm::mat4& fallbackLocalTransform) const;
};
```

**插值支持：**
- `Step`: 取区间起点值
- `Linear`: `glm::mix`（T/S）/ `glm::slerp`（R）
- `CubicSpline`: 三次 Hermite 插值（glTF 标准 CUBICSPLINE）

#### 4.12.3 AnimatorController（AnimatorController.hpp/.cpp）

Phase B1-B5 的运行时状态机核心。状态机定义由 `AnimatorState`、`AnimatorTransition`、`TransitionCondition` 和 `AnimatorParam` 组成；参数支持 `Float`、`Int`、`Bool`、`Trigger` 四种类型。

**关键行为：**
- `configure()` 设置状态、过渡、参数及默认状态；`configureFromClips()` 为实体 clip 建立无过渡的默认状态
- `setFloat/setInt/setBool/setTrigger` 修改运行时参数；Trigger 在命中过渡后自动消费
- `update(dt, clips)` 推进当前/目标状态时间，检查 AnyState/当前状态过渡、条件和归一化 exit time，返回 `BlendCommand`
- `BlendCommand` 携带 clip A/B、各自采样时间和混合权重；零时长 fade 立即切换
- Controller 存放于 `ModelEntity`，不使用原计划中的全局 `AnimationPlayer`/`animCtrl_` 单例

**Phase B3 — 混合品质保障：**
- `BlendCurve` 枚举（Linear / SmoothStep / EaseIn / EaseOut）存于 `AnimatorTransition::blendCurve`，默认 SmoothStep
- `applyBlendCurve(t, curve)` 内联工具函数将归一化进度 t ∈ [0,1] 重映射
- `update()` 输出 `blendWeight` 时应用当前过渡的 `activeBlendCurve_`；过渡启动时从 `transition.blendCurve` 拷贝
- 旋转插值使用 `glm::slerp`（B2 已实现），Translation/Scale 使用 `glm::mix`

**Phase B4 — 状态机序列化：**
- `saveToFile(jsonPath)` / `loadFromFile(jsonPath)` 使用 nlohmann/json 实现 `.animctrl.json` 的读写
- 序列化字段：version、defaultState、states[]（name/clipName/speed/loop）、params[]（name/type/defaultValue）、transitions[]（from/to/fadeDuration/hasExitTime/exitTime/blendCurve/conditions[]）
- `loadFromFile` 对所有 JSON key 做 `contains()` + `is_array()` 检查，缺失字段使用默认值，避免异常
- `MaterialAssetDesc` 新增 `animControllerPath` 字段；`MaterialAssetLoader::load` 解析 `.ast` 中的 `animController` 字段
- `Application::loadAndApplyMaterialAsset` 在模型 swap 后，若 `.ast` 含 `animController` 路径，自动调用 `ents[0].animatorController.loadFromFile()`
- 示例文件：`res/animators/example.animctrl.json`（3 states / 3 transitions / 2 params）

**Phase B5 — ImGui 动画总控面板（Animator Panel）：**
- `UIManager::drawAnimatorPanel()` 在 `prepareFrame()` 中由 `showAnimatorPanel_` 开关控制
- 三区布局：左侧侧边栏（状态机资产 + 动画资产列表）| 右侧上半双栏（States | Outgoing Transitions）| 底部双栏（Transition/State 编辑器 | 状态信息 + 控制按钮）
- **左侧侧边栏上半**：显示当前 controller，Load/Save `.animctrl.json` 按钮，支持拖拽 payload `DND_ANIMCTRL`
- **左侧侧边栏下半**：列出 `ent->animationClips`，每项右侧 [▶] 预览按钮 / [■] 停止按钮；支持拖拽 payload `DND_ANIMCLIP` 到右侧 States 列表创建 state
- **右侧上半左栏 States**：`●` 标记当前状态，点选切换 `animatorSelectedStateIdx_`；接受 `DND_ANIMCLIP` 拖拽自动创建 state（clipName = name）
- **右侧上半右栏 Transitions**：列出当前选中 state 的 outgoing transitions（含 AnyState），`+ Add Transition` 按钮创建新过渡
- **底部左栏编辑器**：选中 transition 时编辑 toState/fadeDuration/hasExitTime/exitTime/blendCurve/conditions[]；选中 state 时编辑 speed/loop；condition 编辑使用临时 `char[]` 缓冲区（避免 `std::string` 直接绑定 `InputText`）
- **底部右栏**：状态机运行时状态（current state / transition progress bar / 计数）+ Add Param 按钮（Float/Bool/Trigger）+ Add State 快捷按钮 + Reset Controller
- **预览模式**：`ModelEntity::previewClipIndex >= 0` 时，`Application::drawFrame` 绕开状态机直接播放 `animationClips[previewClipIndex]`，`previewTime` 按 `previewSpeed` 推进并 mod duration
- **ImGui ID 安全**：condition 循环中所有控件 label 带 `_%d` 索引后缀，避免同帧多 condition 的 ID 冲突

**Phase B5-6 — 事件驱动系统：**
- `AnimatorEvent` 结构体（Type: SetFloat/SetInt/SetBool/SetTrigger + paramName + 值）
- `AnimatorController::dispatchEvent(event)` / `dispatchEvents(events)` 将事件转发到对应 `set*` 方法
- 为未来 Sequence 系统预留标准接口：Sequence 可在 clip 开始/结束或特定时间点触发 `dispatchEvent` 改变参数，从而驱动状态机过渡

**逐骨骼混合：** `Application::drawFrame(dt)` 分别采样两个 clip 的局部 TRS，Translation/Scale 使用 `glm::mix`，Rotation 使用 `glm::slerp`，之后再组合为局部矩阵并计算最终骨骼 palette。

### 4.13 ModelRegistry（ModelRegistry.hpp）

模型资产注册表：优先递归扫描 `res/content/**/*.ast`（目录不存在时兼容回退 `res/materials/**/*.ast`），解析 JSON 提取 `type`、`name`、`model`/`binary` 字段。

**数据结构：**

```cpp
struct ModelAsset {
    uint64_t    id;           // hash of astRelPath
    std::string name;         // display name from .ast's "name" field or stem
    std::string astRelPath;   // e.g. "materials/viking_room.ast"
    std::string modelRelPath; // e.g. "models/viking_room.obj" (from .ast's "model" field); empty for Material-type
    std::string astType;      // e.g. "Mesh" / "Box" / "Material" (from .ast's "type" field)
    std::string subFolder;    // subdirectory under materials/, e.g. "sci-fi"; empty for root-level
    ModelType   type;         // OBJ / GLTF / GLB / FBX / Unknown
    glm::vec3   boundsMin, boundsMax, displaySize;
    bool        hasThumbnail;
};
```

| 关键方法 | 说明 |
|---|---|
| `scan(resRoot)` | 优先扫描 `resRoot/content/**/*.ast`，递归遍历子目录，解析 JSON 填充 assets_ |
| `refresh()` | 重新扫描（运行时热刷新） |
| `search(keyword, subFolder)` | 按名称不区分大小写子串搜索，限定子文件夹 |
| `size(subFolder)` | 返回指定子文件夹下的资产数量 |
| `getFolders()` | 返回已发现的所有子文件夹名（去重排序） |
| `findById(id)` / `findByPath(astRelPath)` | 按 ID 或 .ast 路径查找 |

### 4.13.1 MaterialAssetLoader（MaterialAssetLoader.hpp）

负责 `.ast` JSON 文件的反序列化，输出 `MaterialAssetDesc` 结构体。

```cpp
struct MaterialAssetDesc {
    std::string    name;
    MaterialType   type = MaterialType::Mesh;
    std::string    vertSpv, fragSpv;
    MaterialParams params;
    std::string    albedoPath, normalPath, metallicRoughnessPath, aoPath, emissivePath;
    std::string    modelPath;            // resolved path, empty for Material-type
    std::string    animControllerPath;   // resolved path to .animctrl.json, empty if not specified
    std::vector<std::string> subMaterialPaths;  // from "subMaterials" array, e.g. ["materials/xxx_Body.ast", ...]
};
```

**type 字段解析：** `"Mesh"` / `"Box"` / `"Material"` 映射到 `MaterialType` 枚举。`"Material"` 类型不要求 `model` 字段。

**资源根解析：** `Application` 初始化时调用 `setResRoot()` 注入项目唯一的绝对 `res/` 根。`load()` 接受相对 `res/` 的路径并兼容去除重复的 `res/` 前缀，避免进程工作目录变化导致入口或子材质 `.ast` 打不开。

**subMaterials 解析：** 读取 `.ast` 中的 `"subMaterials"` 字符串数组，原样存入 `subMaterialPaths`（不作路径解析）。该数组在 `SceneSerializer` 的 save/load 和 `Application::beginDragPlace` 中用于逐槽位材质管理。

### 4.14 SceneSerializer（SceneSerializer.hpp）

场景持久化：将实体/材质/相机保存为 `.scene.json` 或从中恢复。

**.scene.json 格式**（v1）：

```json
{
  "version": 1,
  "entities": [
    {
      "astRelPath": "materials/viking_room.ast",
      "position": [0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1],
      "visible": true,
      "materialOverride": { "baseColor": [0.8, 0.3, 0.3, 1.0], "roughness": 0.2 },
      "subMaterialOverrides": [
        {
          "slot": 0,
          "astRelPath": "materials/viking_room_body.ast",
          "override": { "albedoPath": "res/models/.../custom.png", "roughness": 0.8 }
        }
      ]
    }
  ],
  "boxes": [
    { "entityId": 1, "position": [1, 2, 0] }
  ],
  "camera": {
    "position": [0, -4, 4],
    "orientation": [0, 0.15, 0, 0.988],
    "fov": 45
  }
}
```

**materialOverride 机制：** 保存时对比当前材质与 `.ast` 参考值（params + albedoPath/normalPath），仅当有差异时写入。加载时先通过 `loadMaterialFromAsset()` 从 `.ast` 重建材质，再叠加 `materialOverride`。

**subMaterialOverrides 机制：** 对于含 `subMaterials` 数组的入口 `.ast`，保存时对比每个槽位当前材质与该槽位 `.ast` 参考值的差异，写入 `subMaterialOverrides` 数组（含 `slot`、`astRelPath`、可选的 `override`）。加载时先逐槽位加载子材质，再叠加各槽位的 `override`。

**兼容性：** load 时优先读 `astRelPath`，回退 `displayName`（扫描 `res/models/`）；优先读 `orientation` 四元数，回退 `pitch/yaw`。

| 关键方法 | 说明 |
|---|---|
| `save(path, sceneMgr, matMgr, camera)` | 保存场景到 `.scene.json` |
| `load(path, ..., matMgr, ..., cmdMgr, fbMgr, pipeMgr, ...)` | 从 `.scene.json` 加载并重建场景 + 材质 |

---

## 5. 数据流

### 5.1 每帧数据流

```
camera (CPU)
  │
  ├── view matrix ──→ UniformBufferObject ◆ [all materials]
  ├── proj matrix ──→ UniformBufferObject ◆
  ├── position ────→ UniformBufferObject.cameraPos
  │
  └── viewProj ────→ Application (pass to PickSystem, ImGuizmo)

ModelEntity(s)
  │
  ├── ObjectTransform ──→ PushConstants.model / .normalMatrix
  ├── Vertex Buffer     ──→ vkCmdBindVertexBuffers
  ├── Index Buffer      ──→ vkCmdBindIndexBuffer
  └── MaterialId(s)     ──→ DescriptorSet lookup → vkCmdBindDescriptorSets
                            (per SubMesh)

AnimatorController (per ModelEntity)
  │
  ├── dt + parameters ──→ state/transition evaluation
  ├── BlendCommand ─────→ clipA/timeA + clipB/timeB + weight
  ├── per-bone TRS ─────→ mix(T/S) + slerp(R)
  └── final matrices ───→ per-skin palette → BoneMatricesUBO (binding=6)

Box Instances
  │
  ├── InstanceData[] ───→ Instance Buffer (binding=1, per-instance rate)
  ├── vertex/index     ──→ Shared cube template buffers
  └── Box MaterialId   ──→ DescriptorSet (shared across all boxes)

PickSystem
  │
  ├── view + proj  ──→ pick UBO；同步路径先等待 GPU，避免改写在用 buffer
  ├── static mesh  ──→ pickPipeline
  ├── skinned mesh ──→ skinnedPickPipeline + material descriptor + BoneMatricesUBO
  ├── submeshes    ──→ 按槽位材质分别绘制，保持 skin palette 一致
  └── R32UINT image ──→ vkCmdCopyImageToBuffer → readback on CPU
                           → pixel value = entity PickId
```

### 5.2 材质资产数据流

```
res/content/**/xxx.material.ast 或兼容 res/materials/xxx.ast (JSON)
    │
    ▼ MaterialAssetLoader::load()
    │   ├── 解析 JSON（type / params / textures / model / subMaterials）
    │   ├── 路径解析（textures / model 相对于 res/）
    │   └── 输出 MaterialAssetDesc（含 subMaterialPaths）
    │
    ▼ MaterialManager::loadMaterialFromAsset()
    │   ├── 材质类型路由：Mesh/Material → createMeshMaterial(), Box → createBoxMaterial()
    │   ├── 加载纹理 → TextureManager
    │   ├── 创建 UBO buffer (per swapchain image)
    │   ├── 分配 DescriptorSet
    │   └── 记录 MaterialEntry → materialMap_ + assetCache_
    │
    ▼ 返回 MaterialId

For subMaterials (SceneSerializer::load / beginDragPlace):
    │
    ▼ 逐槽位调用 loadMaterialFromAsset(subMaterialPaths[i])
    │   └── 填充 ModelEntity::subMeshMaterials[i]
```

### 5.3 FBX 导入与运行时加载数据流

```
外部 *.fbx
    │
    ▼ FbxImporter::load() / ufbx
    │   ├── Vertex + Index + SubMesh(materialSlot/skinIndex)
    │   ├── Material params + external/embedded textures
    │   └── Skeleton + baked AnimationClip[]
    │
    ▼ Application::importModel()
    │   ├── res/bin/mesh/*.fbx
    │   ├── res/bin/texture/<model>/*
    │   ├── res/content/**/<model>.mesh.ast + *.material.ast
    │   └── res/content/**/<model>.anim.ast + res/bin/anim/*.anim.bin
    │
    ▼ ModelRegistry → beginDragPlace() → SceneManager::loadModelFromFbx()
        ├── 创建/复用 GPU vertex/index buffer
        ├── 逐槽位加载 Material .ast
        └── 将 Skeleton/AnimationClip 绑定到 ModelEntity Animator
```

---

## 6. 顶点数据布局

### 6.1 Vertex（vectex.hpp）

| Location | Format | 字段 | 说明 |
|---|---|---|---|
| 0 | R32G32B32_SFLOAT | `pos` | 顶点位置 |
| 1 | R32G32B32_SFLOAT | `color` | 顶点颜色 |
| 2 | R32G32_SFLOAT | `texCoord` | 纹理坐标 |
| 3 | — |（跳过） | 保留给 per-instance 数据 |
| 4 | R32G32B32_SFLOAT | `normal` | 法线向量 |
| 5 | R32G32B32A32_SFLOAT | `tangent` | xyz=切线, w=bitangent 符号方向 |
| 6 | R32G32B32A32_SINT | `boneIndices` | 最多 4 个 skin-local 骨骼索引 |
| 7 | R32G32B32A32_SFLOAT | `boneWeights` | 归一化骨骼权重 |

### 6.2 InstanceData（Box 实例化）

| Binding | Input Rate | 字段 |
|---|---|---|
| 1 | VK_VERTEX_INPUT_RATE_INSTANCE | `modelMatrix` (mat4, 64 bytes) |

---

## 7. UBO 布局（UniformBufferObject）

```cpp
// std140 对齐，408 字节
struct UniformBufferObject {
    mat4   view;              // offset   0, size 64
    mat4   proj;             // offset  64, size 64
    vec4   materialTint;      // offset 128, size 16
    vec4   boxMaterialTint;   // offset 144, size 16
    vec4   emissive;          // offset 160, size 16
    vec4   cameraPos;         // offset 176, size 16
    vec4   lightDir;          // offset 192, size 16
    vec4   lightColor;        // offset 208, size 16
    vec4   pbrFactors;        // offset 224, size 16 (x=metallic, y=roughness, z=ao, w=normalScale)
    mat4   viewProj;          // offset 240, size 64
    mat4   invView;           // offset 304, size 64
    mat4   invProj;           // offset 368, size 64
};
```

### 7.1 骨骼矩阵 UBO（BoneMatricesUBO）

```cpp
constexpr int kMaxBones = 256;
struct BoneMatricesUBO {
    mat4 bones[kMaxBones]; // 单个 skin 的 local palette，未使用项填 identity
};
```

蒙皮材质在 `set=0, binding=6` 为每个 swapchain image 分配一份 BoneMatricesUBO；普通渲染与蒙皮拾取共用同一材质描述符集。

---

## 8. Push Constants

```cpp
// 128 字节，Vertex Stage，每个 Draw Call 推入
struct PushConstants {
    mat4 model;         // offset   0, size 64
    mat4 normalMatrix;  // offset  64, size 64
};
```

---

## 9. 相机系统（Camera）

- **姿态表示：** 四元数（`orientation_`），由独立累积的 `pitchAccum_` / `yawAccum_` 合成
- **视角移动：** 右键拖拽旋转，WASD 移动，Q/E 升降，鼠标滚轮调整速度
- **速度机制：** `SPEED` 基数 × deltaTime → 帧率无关
- **平滑聚焦（SmoothFocus）：** 选择物体后相机平滑移动到目标前方
- **View 矩阵：** 从 `worldTransform_`（Camera-to-World 矩阵） 取逆
- **场景持久化：** `GetOrientation()` 返回四元数，`SetOrientation(quat)` 直接设置四元数姿态并回解 pitch/yaw，确保 round-trip 无精度损失
- **ImGuizmo 操作：** 键盘 1/2/3 分别切换平移（TRANSLATE）、旋转（ROTATE）、缩放（SCALE）Gizmo 模式，由 `processInput()` 驱动 `UIManager::gizmoOperation_`

---

## 10. UI 系统（UIManager）

基于 Dear ImGui + ImGui_ImplVulkan_GLFW 后端。

**当前面板：**
- **主控制面板（tinyEngineOperationWindow）：** 清屏色、相机速度、下拉切换预设场景、材质列表（创建/删除）、Box 增删
- **Content Browser：** 左侧文件夹树 + 右侧缩略图网格。扫描 `res/content/**/*.ast`（兼容旧 `res/materials/`），按子文件夹分类浏览。
  - **文件夹导航：** 左侧面板列出 `content/` 下所有子文件夹，支持在当前浏览目录创建新文件夹。点击 `/ (root)` 浏览根目录，点击子文件夹切换浏览。
  - **类型筛选：** 提供 `All` / `Mesh` / `Box` / `Material` / `Anim` 下拉筛选器，根据 `.ast` 的 `type` 字段过滤显示。
  - **Import 导入：** Windows 原生文件对话框支持 `.obj/.gltf/.glb/.fbx`。导入结果写入 `res/content/<当前目录>/<模型名>/` 与对应 `res/bin/` 分类目录；glTF/FBX 会预生成材质，带动画时同时生成独立 Anim 资产。
  - **拖拽限制：** `Material` 类型资产（无 `model` 引用）不显示拖拽源，禁止拖入场景。
- **Scene Outliner：** 场景实体列表，单选/多选
- **Properties：** 选中实体的 Transform 编辑 + Material 材质参数/纹理内联编辑；纹理路径输入在切换材质时同步，Load 支持绝对路径和相对 `res/` 路径并显示成功/失败状态
- **Box 面板：** 添加/删除 Box、选中 Box 属性
- **ImGuizmo 集成：** 对选中实体施加 TRS 变换手柄，支持平移/旋转/缩放，通过键盘 1/2/3 切换模式

**关键流程：**
1. `initIMGUI()` → 创建 ImGui context + 初始化 Vulkan/GLFW 后端
2. 每帧 `prepareFrame()` → `ImGui_ImplVulkan_NewFrame()` → `ImGui_ImplGlfw_NewFrame()` → `ImGui::NewFrame()` → 绘制各面板 → `ImGuizmo::Manipulate()` → `ImGui::Render()`
3. `recordCommandBuffer()` 末尾调用 `ImGui_ImplVulkan_RenderDrawData()` 将 UI 叠加到 swapchain image

---

## 11. GPU 拾取系统（PickSystem）

**工作流程：**
1. 鼠标点击 → screenXY
2. 创建独立的 command buffer（`pickCmdBuf_`）
3. 普通模型使用 `pickPipeline_`；蒙皮模型按 submesh 使用 `skinnedPickPipeline_`，绑定对应材质的 bone UBO 后渲染当前动画姿势
4. `vkCmdCopyImageToBuffer` 复制到 host-visible readback buffer
5. 读取点击像素处的值 → 映射到 Entity PickId
6. 更新选中状态（mainModel / specific box）

拾取是同步路径：写入 slot 0 相机/材质 UBO 前调用 `vkDeviceWaitIdle`，避免覆盖仍被上一帧读取的 mapped buffer。该设计修复了动画 glTF 在屏幕姿势与绑定姿势不一致时无法点击选中的问题。

---

## 12. 第三方依赖一览

| 库 | 用途 | 集成方式 |
|---|---|---|
| GLFW | 窗口创建、输入处理 | CMake add_subdirectory |
| GLM | 线性代数（矩阵/向量/四元数） | CMake add_subdirectory |
| Dear ImGui | 即时模式 GUI | 手工源文件编译 |
| ImGuizmo | 3D 变换 Gizmo | 手工源文件编译 |
| stb_image | PNG/JPG 纹理加载 | header-only 包含 |
| tinyobjloader | OBJ 模型解析 | header-only 包含 |
| cgltf | glTF 2.0 模型解析 | header-only 包含 |
| ufbx | FBX 网格、材质、蒙皮与动画解析 | Git Submodule；编译 `ufbx.c` |
| nlohmann/json | JSON 解析 | header-only 包含 |
| RenderDoc | 图形调试捕获 | header + DLL 动态加载 |

---

## 13. Shader 资源

| Shader | 阶段 | 用途 |
|---|---|---|
| `shader.vert` / `.frag` | Mesh 渲染 | 标准 PBR 模型着色（含纹理） |
| `box_instanced.vert` / `box.frag` | Box 实例化 | 实例化 Cube 着色 |
| `modelVertex.shader` / `modelFrag.shader` | Mesh 变体 | 替代材质 Shader 路径 |
| `light.shader` | 未使用 | 预留 |
| `cameraVertex.shader` / `cameraFragment.shader` | 未使用 | 预留 |
| `pick.vert` / `pick.frag` | GPU 拾取 | 输出实体 ID 到 R32UINT |
| `skinned_vert.glsl` / `skinned_vert.spv` | 蒙皮 Mesh 渲染 | 读取 JOINTS_0/WEIGHTS_0 和 binding 6 骨骼 palette |
| `skinned_pick.vert` / `skinned_pick_vert.spv` | 蒙皮 GPU 拾取 | 使用同一 bone UBO 按当前动画姿势输出拾取几何 |

---

## 14. CMake 编译定义

```cmake
GLFW_INCLUDE_VULKAN       # GLFW 使用 Vulkan
GLM_FORCE_DEPTH_ZERO_TO_ONE  # GLM 深度范围适配 Vulkan [0,1]
GLM_FORCE_RADIANS         # GLM 角度单位为弧度
GLM_ENABLE_EXPERIMENTAL   # 启用 GLM 实验性头文件（gtx）
STB_IMAGE_IMPLEMENTATION  # stb_image 实现编译
TINYOBJLOADER_IMPLEMENTATION # tinyobjloader 实现编译
```

---

## 15. 已知限制与待扩展

| 项目 | 当前状态 | 计划 |
|---|---|---|
| 多模型导入 | 已完成 | Content Browser 扫描 res/content/*.ast + 拖拽放置 + 材质自动加载 |
| 新资产目录布局 | 已完成 | res/content/ (.ast) + res/bin/ (mesh/anim/texture payload) + 迁移脚本 + 旧目录兼容 |
| 资源路径架构 | 已完成 | applicationResourceRoot() 从 exe 向上查找项目根，res/ 为唯一基准，移除 CMake 复制 |
| 场景持久化 | 已完成 | SceneSerializer：astRelPath + materialOverride + 旧 materials/ → content/ 路径自动迁移 |
| materialOverride | 已完成 | 保存时对比 .ast 参考值仅写差异；加载时叠加到重建材质 |
| 骨骼数据结构 | 已完成 | Phase A1：Bone / Skeleton + computeFinalMatrices |
| 动画数据解析 | 已完成 | Phase A2：cgltf skin/anim → Skeleton / AnimationClip，含关键帧求值 |
| glTF 模型导入 | 已完成 | Content Browser Import... 按钮：原生文件对话框 + 自动拷贝 + 材质预生成 |
| FBX 模型/动画导入 | 核心支持已完成 | ufbx 子模块；静态/蒙皮网格、材质槽、外部/内嵌纹理、Skeleton、30 Hz 烘焙 AnimationClip 与独立 Anim 资产；高级 FBX 特性见 4.11.1 限制 |
| 无贴图材质参数着色 | 已完成 | 中性 fallback 纹理保证 BaseColor/Metallic/Roughness/Emission 参数不被默认采样值抵消 |
| Properties 纹理热替换 | 已完成 | 持久输入缓冲、绝对 res root 路径解析、事务式替换和 UI 状态反馈 |
| 材质资产缓存 | 已完成 | MaterialManager.assetCache_ 同一 .ast 仅创建一次材质，拖入同模型秒加载 |
| GPU 蒙皮渲染 | 已完成 | Phase A3：skinned_vert + BoneMatricesUBO + SkinnedPipeline + 多 skin per-palette |
| 多骨架动画实体 | 已完成 | Phase A4：animation state 下沉到 ModelEntity，drawFrame 逐实体求值，不再依赖全局单例 |
| 动画运行时播放 | 已完成 | drawFrame 每帧逐实体执行 AnimatorController → TRS 混合 → computeFinalMatrices → updateBoneMatrices |
| 动画资产序列化 | 已完成 | Phase A5：AnimationAssetLoader (.ast Header + .anim.bin 二进制) + 懒生成 + 启动恢复 |
| 共享 GPU buffer 缓存 | 已完成 | SceneManager.modelResourceCache_ 共享 vertex/index buffer + animation state，重复拖拽不暴涨 |
| 动画状态机 | 已完成 | Phase B1-B5：参数/状态/过渡/Trigger/exit time + 逐骨骼 TRS cross-fade（B1-B2）；BlendCurve ease 曲线（B3）；.animctrl.json 序列化 + .ast animController 字段（B4）；ImGui 动画总控面板 + 拖拽创建 state + 预览模式 + 事件驱动系统（B5） |
| 蒙皮模型 GPU 拾取 | 已完成 | skinned pick pipeline 复用材质 bone UBO，按 submesh/当前动画姿势写入 Entity ID |
| 重复蒙皮模型独立动画状态 | 受限 | Controller 已逐实体独立，但共享同一 skinned MaterialId 的实例仍共用 BoneMatricesUBO；后续需每实体描述符或 dynamic UBO |
| Sequencer | 待实现 | Phase C：时间轴编辑器 |
| PBR 管线 | 基础支持（metallic/roughness/ao） | 已有 |
| 阴影 | 不支持 | 未规划 |
| 后处理 | 不支持 | 未规划 |
