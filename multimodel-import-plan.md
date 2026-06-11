# tinyEngine 多模型导入系统建设计划

## Phase S0 — 多模型导入系统（Content Browser + 拖拽放置）

> **前置依赖：** 无。这是 `todo 0` 的详细拆分。
> **目标：** 在运行时提供一个类 UE/Unity 的内容浏览器面板，用户可以从 `res/materials/` 中浏览 `.ast` 资产文件（每个 `.ast` 包含材质参数和模型路径引用），通过拖拽或点击将模型实例放置到 3D 场景中。每个实例是独立的实体，拥有自己的 Transform、材质和选中状态。

> **实施状态（2026-06-13）：**
>
> | 阶段 | 状态 | 说明 |
> |---|---|---|
> | S0A ContentBrowser | ✅ 完成 | `drawContentBrowser()` 搜索+缩略图网格+拖拽源 |
> | S0B ModelRegistry | ✅ 完成 | `ModelRegistry::scan/search/findById/getResRoot` |
> | S0C SceneManager 重构 | ✅ 完成 | `ModelEntity` + 多实体 API + 兼容旧接口 |
> | S0D 拖拽放置 | ✅ 完成 | `DragPlaceState` + `screenToWorld` 实时跟随 + gameLoop payload 检测 |
> | S0E 选中与每实体操作 | ✅ 完成 | Scene Outliner + Properties 面板 + ImGuizmo 多实体 + PickSystem 适配 |
> | S0F 多模型渲染 | ✅ 完成 | recordCommandBuffer 统一遍历 `getModelEntities()` |
> | S0G 场景持久化 | ✅ 完成 | SceneSerializer save/load + astRelPath + materialOverride + 四元数相机 + auto-load default.scene.json |
| S0H Properties 材质编辑 | ✅ 完成 | `drawPropertiesPanel()` 内联 Material 参数/纹理编辑；保存时对比 .ast 参考值写 materialOverride |
> | 缩略图 v1（占位图标） | ✅ 完成 | `res/icons/model.png` 作为共享占位符 |
> | 缩略图 v2（自动生成） | 🟡 有 Bug | ThumbnailRenderer 离屏渲染输出全灰色，待排查 |

### 整体架构概览

```
多模型导入系统
├── S0A. 内容浏览器面板（ImGui Content Browser）
│   ├── S0A1. res/materials/*.ast 资产文件扫描
│   ├── S0A2. 缩略图网格 UI（ImGui::ImageButton）
│   ├── S0A3. 搜索 / 过滤
│   └── S0A4. 浏览器面板集成到 UIManager
│
├── S0B. 模型资产注册表（ModelRegistry）
│   ├── S0B1. ModelAsset 数据结构（路径、名称、类型、AABB、modelRelPath）
│   ├── S0B2. ModelRegistry 单例/管理器
│   └── S0B3. 启动时自动扫描 res/materials/*.ast + 运行时热刷新
│
├── S0C. 场景实体系统重构（ModelEntity 替代单一主模型）
│   ├── S0C1. ModelEntity 数据结构（Transform + ModelAsset引用 + MaterialId）
│   ├── S0C2. SceneManager 从单模型变为多实体管理
│   ├── S0C3. addModelEntity / removeModelEntity / getModelEntities
│   └── S0C4. 保持现有 Box 实例系统不变
│
├── S0D. 拖拽放置交互（Drag & Drop + Click-to-Place）
│   ├── S0D1. ImGui DragDropSource（Content Browser 侧）
│   ├── S0D2. ImGui DragDropTarget（3D 视口 / 场景面板侧）
│   └── S0D3. 点击放置模式（Click-to-Place cursor）
│
├── S0E. 选中与每实体操作
│   ├── S0E1. 场景实体列表面板（Scene Outliner）
│   ├── S0E2. 单选/多选/删除
│   ├── S0E3. 选中实体属性面板（Transform + Material 编辑）
│   └── S0E4. ImGuizmo 操作当前选中实体
│
├── S0F. 多模型渲染（Multi-Entity Rendering）
│   ├── S0F1. 渲染循环改为遍历 ModelEntity 列表
│   ├── S0F2. 每实体绑定不同的 DescriptorSet（材质）
│   └── S0F3. 保持 Instance Buffer 对 Box 可用
│
└── S0G. 场景持久化（Scene Save/Load）
    ├── S0G1. 场景 JSON 格式设计（.scene.json，astRelPath + materialOverride + 四元数相机）
    ├── S0G2. saveScene / loadScene 实现（materialOverride 对比 .ast 参考值差分写入）
    └── S0G3. ImGui 面板按钮绑定 + default.scene.json 自启动加载
```

---

### S0A. 内容浏览器面板（ImGui Content Browser）

**目标：** 创建一个独立可停靠的 ImGui 窗口，以缩略图网格形式展示 `res/materials/` 下所有 `.ast` 资产文件（每个 `.ast` 包含材质参数和模型路径引用），支持搜索过滤和拖拽。

**新建/修改文件：** `src/UIManager`（新增面板方法，不新建文件）

**面板布局设计：**

```
┌─────────────────────────────────────────┐
│ Content Browser              [x] [_]   │
├─────────────────────────────────────────┤
│  [🔍 Search...          ]  [Refresh]   │
├─────────────────────────────────────────┤
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐  │
│  │viking │ │cyber_ │ │fantasy│ │mainmod│  │
│  │_room  │ │room   │ │_inn   │ │el     │  │
│  │ .ast  │ │ .ast  │ │ .ast  │ │ .ast  │  │
│  └──────┘ └──────┘ └──────┘ └──────┘  │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐  │
│  │ ...   │ │ ...   │ │ ...   │ │ ...   │  │
│  └──────┘ └──────┘ └──────┘ └──────┘  │
├─────────────────────────────────────────┤
│  Status: 8 assets found                 │
└─────────────────────────────────────────┘
```

**具体任务：**

- [x] **S0A1-1** 在 `IMGUIManager.hpp` 新增 `bool showContentBrowser_ = true` 成员和声明 `void drawContentBrowser()` 方法
- [x] **S0A1-2** 在 `UIManager::prepareFrame()` 主面板顶部增加 `Checkbox("Content Browser", &showContentBrowser_)` 按钮
- [x] **S0A1-3** 实现 `drawContentBrowser()` 方法（位于 `IMGUIManager.cpp`）：
  - 用 `ImGui::Begin("Content Browser", &showContentBrowser_)` 创建窗口
  - 顶部搜索框：`ImGui::InputText("##search", ...)` + `ImGui::SameLine()` + `ImGui::Button("Refresh")`
  - 读取 `Application::getModelRegistry()` 获取模型列表
  - 每行 4 个缩略图，用 `ImGui::ImageButton` 或带文字的 `ImGui::Selectable` 展示（初期用纯色方块 + 文件名，缩略图在 S0A2 迭代）

**验收标准：** 启动引擎后，Content Browser 面板展示 `res/materials/` 中所有 `.ast` 文件，显示名称来自 `.ast` 的 `name` 字段，拖拽放置时解析 `.ast` 中的 `model` 字段加载对应模型。

---

### S0B. 模型资产注册表（ModelRegistry）

**目标：** 建立中心化的模型资产管理，扫描 `res/materials/*.ast` 文件，解析其中的 `name`、`model`、`shader`、`textures` 字段，缓存元数据。

**新建文件：** `src/ModelRegistry.hpp` / `src/ModelRegistry.cpp`

**数据结构：**

```cpp
enum class ModelType { OBJ, GLTF, GLB, Unknown };

struct ModelAsset {
    uint64_t    id;           // 唯一标识 == std::hash<astRelPath>
    std::string name;         // 显示名称（来自 .ast 的 name 字段，或文件名 stem）
    std::string astRelPath;   // .ast 文件相对 res/ 的路径，如 "materials/viking_room.ast"
    std::string modelRelPath; // .ast 中 model 字段指向的模型路径，如 "models/viking_room.obj"
    ModelType   type;         // 从 modelRelPath 扩展名推断
    glm::vec3   boundsMin;    // AABB 最小值（世界空间，单位模型）
    glm::vec3   boundsMax;    // AABB 最大值
    glm::vec3   displaySize;  // boundsMax - boundsMin
    bool        hasThumbnail = false;  // 缩略图可用
};

class ModelRegistry {
public:
    void scan(const std::string& resRoot);  // 扫描 res/materials/*.ast
    void refresh();                          // 热刷新

    const std::vector<ModelAsset>& getAll()                       const;
    const ModelAsset*              findByPath(const std::string& relPath) const;
    const ModelAsset*              findById(uint64_t id)          const;

    // 过滤：名称包含 keyword（不区分大小写）
    std::vector<const ModelAsset*> search(const std::string& keyword) const;

private:
    std::vector<ModelAsset>          assets_;
    std::unordered_map<std::string, uint64_t> pathToId_;
    std::string                      resRoot_;
};
```

**具体任务：**

- [x] **S0B1-1** 新建 `src/ModelRegistry.hpp` / `.cpp`，定义 `ModelAsset` 和 `ModelRegistry`
- [x] **S0B1-2** 实现 `ModelRegistry::scan()`：扫描 `resRoot + "/materials/"`，筛选 `.ast` 后缀，解析 JSON 提取 `name` 和 `model` 字段，创建 `ModelAsset` 条目
- [x] **S0B1-3** ModelAsset 的 `boundsMin/boundsMax` 在扫描阶段暂不解析（耗时长），延迟到首次加载时计算并缓存
- [ ] **S0B1-4** 在 `Application` 中新增成员 `ModelRegistry modelRegistry_`，在 `initVulkan()` 中调用 `modelRegistry_.scan(...)` 初始化
- [ ] **S0B1-5** 在 `Application` 中新增公开访问器 `ModelRegistry& getModelRegistry() { return modelRegistry_; }`

**验收标准：** 启动引擎后，在调试日志中打印扫描到的模型数量，且内容浏览器能展示所有模型名称。

---

### S0C. 场景实体系统重构

**目标：** 将 `SceneManager` 从"一个主模型 + 一组 Box"改为"多个 ModelEntity + 一组 Box"的泛化架构，支撑多模型场景。

**新建/修改文件：** `src/SceneManager.hpp` / `.cpp`（重结构，不重写）

**数据结构设计：**

```cpp
struct ModelEntity {
    uint64_t       entityId;       // 全局唯一，也用作 PickId
    uint64_t       modelAssetId;   // ModelAsset::id，关联到 ModelRegistry
    ObjectTransform transform;
    uint32_t       materialId   = 0;        // 0 表示不使用材质
    bool           visible      = true;
    bool           selected     = false;
    std::string    displayName;             // 在 Outliner 中显示
    std::string    astRelPath;              // 创建实体的 .ast 路径（供 SceneSerializer 保存）

    // GPU 资源引用（由 SceneManager 管理生命周期）
    VkBuffer       vertexBuffer{};  VkDeviceMemory vertexMemory{};
    VkBuffer       indexBuffer{};   VkDeviceMemory indexMemory{};
    uint32_t       indexCount  = 0;
    std::vector<SubMesh> subMeshes;
};
```

**当前状态分析（重构影响范围）：**

| 现有成员                                         | 重构策略                                                                |
| -------------------------------------------- | ------------------------------------------------------------------- |
| `modelPosition_` + 单模型顶点/索引缓冲                | 迁移到 `std::vector<ModelEntity>` 的第一个元素                               |
| `modelMaterialId_`                           | 迁移到 `ModelEntity::materialId`                                       |
| `modelSubMeshes_` / `modelSubMeshMaterials_` | 迁移到 `ModelEntity::subMeshes`                                        |
| `boxes_` / `boxMaterialIds_`                 | 保持不变                                                                |
| `cubeTemplateVertices_/Indices_`             | 保持不变                                                                |
| `loadModel(path, pos, bufMgr)`               | 改名 `createModelEntity(assetId, pos, bufMgr)` 返回 `uint64_t entityId` |

**具体任务：**

- [ ] **S0C2-1** 在 `SceneManager.hpp` 中定义 `ModelEntity` 结构体（放在 `SubMesh` 之后）

- [ ] **S0C2-2** 新增 `std::vector<ModelEntity> modelEntities_` 成员，保留旧有单模型成员作为 deprecated 过渡

- [ ] **S0C2-3** 实现 `SceneManager::createModelEntity(uint64_t assetId, const glm::vec3& pos, const BufferManager&)`：
  
  - 通过 `ModelRegistry` 查找 `ModelAsset`
  - 根据 `asset.type` 分发到 `loadEntityFromObj` / `loadEntityFromGltf`
  - 填充 `ModelEntity` 的 GPU 缓冲、SubMesh、indexCount
  - 返回 `entityId`

- [ ] **S0C2-4** 实现 `SceneManager::removeModelEntity(uint64_t entityId, const VulkanContext&)`：销毁对应 GPU 资源并从 vector 移除

- [ ] **S0C2-5** 实现以下公开访问器：
  
  ```cpp
  const std::vector<ModelEntity>& getModelEntities() const;
  ModelEntity* getModelEntity(uint64_t id);
  void         setEntityTransform(uint64_t id, const ObjectTransform& t);
  void         setEntityMaterial(uint64_t id, uint32_t matId);
  void         setEntityVisibility(uint64_t id, bool v);
  ```

- [ ] **S0C2-6** 兼容性：`SceneManager::loadModel(path, pos, bufMgr)` 内部改为调用 `createModelEntity`（对 `Application::initVulkan` 透明）

- [ ] **S0C2-7** 为 `ModelEntity` 新增 GPU 选择缓冲逻辑：`entityId` 映射回 PickId（用 `static_cast<uint32_t>(entityId & 0xFFFFFFFF)` 或维护 map）

**验收标准：** 重构后现有流程（加载 viking_room.obj → 渲染 → 选中 → ImGuizmo）完全正常运行，且能额外 `createModelEntity` 加载第二个模型正常显示。

---

### S0D. 拖拽放置交互（实时预览模式）

**目标：** 从 Content Browser 拖拽模型到 3D 视口区域，松开瞬间完成放置。拖拽期间模型实时跟随鼠标位置移动，形成"预览拖拽"效果。

**核心机制：**

1. 鼠标屏幕坐标 → 通过 Camera 的 `invViewProj` 矩阵反投影到世界空间射线
2. 沿射线方向、距离相机 `placementDistance`（可在 Content Browser 面板用 Slider 调整）处计算世界位置
3. 拖拽开始时立即创建 ModelEntity（预览实体），每个 Game Loop 帧更新其 `modelEntities_[].transform.position`
4. 鼠标左键松开时结束拖拽，预览实体转为正式实体（保留在最终位置）

**新建/修改文件：** `Application.hpp/.cpp`（新增 screenToWorld + 拖拽状态），`IMGUIManager.hpp/.cpp`（拖拽源 + 视口 Drop Target + 距离 Slider）

---

#### S0D.1 拖拽状态机

在 `Application.hpp` 中新增结构体和成员：

```cpp
struct DragPlaceState {
    bool     active     = false;      // 拖拽进行中
    uint64_t assetId    = 0;          // ModelAsset::id
    uint64_t entityId   = 0;          // 已创建的预览 ModelEntity::entityId
    float    distance   = 5.0f;       // 放置距离（距相机）
    glm::vec3 worldPos  = {0,0,0};    // 当前计算的世界位置
};
```

**具体任务：**

- [ ] **S0D1-1** 在 `Application.hpp` 中新增 `DragPlaceState dragPlace_` 成员，声明方法：
  - `glm::vec3 screenToWorld(float mx, float my, float distance)` — 屏幕坐标 → 世界坐标
  - `void beginDragPlace(uint64_t assetId)` — 开始拖拽（创建预览实体）
  - `void updateDragPlace(float mx, float my)` — 每帧更新预览实体位置
  - `void endDragPlace()` — 结束拖拽（预览实体转为正式实体）
  - `void cancelDragPlace()` — 取消拖拽（销毁预览实体）
- [ ] **S0D1-2** 实现 `screenToWorld(mx, my, distance)`：
  ```
  // 将屏幕坐标归一化到 [-1, 1] NDC
  ndcX = (2.0f * mx / viewportW) - 1.0f
  ndcY = 1.0f - (2.0f * my / viewportH)   // Vulkan Y 翻转
  ndcZ = 0.0f  // 近平面
  // 构造 NDC 点，乘 invViewProj 得到世界空间近平面点
  nearPoint = invViewProj * vec4(ndcX, ndcY, ndcZ, 1.0f)
  nearPoint /= nearPoint.w
  // 射线方向 = normalize(nearPoint - cameraPos)
  rayDir = normalize(nearPoint - camera_.Position)
  // 目标位置 = cameraPos + rayDir * distance
  return camera_.Position + rayDir * distance
  ```
- [ ] **S0D1-3** 实现 `beginDragPlace(assetId)`：
  - 从 `modelRegistry_.findById(assetId)` 获取 `ModelAsset`
  - 调用 `sceneMgr_.createModelEntity(asset.relPath, glm::vec3(0.f), bufMgr_)` 创建实体
  - 记录 `entityId`、设置 `active = true`
- [ ] **S0D1-4** 实现 `updateDragPlace(mx, my)`：
  - 调用 `screenToWorld(mx, my, dragPlace_.distance)` 计算世界位置
  - 调用 `sceneMgr_.setEntityTransform(dragPlace_.entityId, ObjectTransform{worldPos})`
- [ ] **S0D1-5** 实现 `endDragPlace()`：
  - 实体已在最终位置，无需额外操作
  - 重置 `dragPlace_.active = false`、`entityId = 0`
- [ ] **S0D1-6** 实现 `cancelDragPlace()`：
  - 调用 `sceneMgr_.removeModelEntity(dragPlace_.entityId, ctx_)` 销毁预览实体
  - 重置状态

---

#### S0D.2 Content Browser 拖拽源

在 `IMGUIManager::drawContentBrowser()` 的缩略图 item 上添加拖拽源：

**具体任务：**

- [ ] **S0D2-1** 在每个缩略图的 `BeginGroup()` 内、`InvisibleButton` 位置包裹 `ImGui::DragDropSource()`：
  ```cpp
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
      uint64_t id = asset->id;
      ImGui::SetDragDropPayload("MODEL_ASSET", &id, sizeof(uint64_t));
      ImGui::Text("%s", asset->name.c_str());
      ImGui::EndDragDropSource();
  }
  ```
- [ ] **S0D2-2** 在 Content Browser 面板底部新增 `placementDistance` 调节：
  ```cpp
  ImGui::SliderFloat("Place Distance", &placementDistance_, 1.0f, 50.0f);
  ```
  `placementDistance_` 的值同步到 `vulkanRender->getDragPlaceState().distance`

---

#### S0D.3 每帧拖拽更新（无 DropTarget 窗口）

**设计思路：** 不需要额外的 ImGui DropTarget 窗口。ImGui 的 `GetDragDropPayload()` 在整个拖拽过程中（鼠标按住期间）返回非空指针，松开后返回 `nullptr`。直接在 `Application::gameLoop()` 中检测 payload 状态驱动拖拽生命周期即可。现有的鼠标点击拾取逻辑已有 `WantCaptureMouse` 保护，拖拽期间不会误触发。

**具体任务：**

- [ ] **S0D3-1** 在 `IMGUIManager.hpp` 中新增 `float placementDistance_ = 5.0f` 成员
- [ ] **S0D3-2** 在 `Application::gameLoop()` 中，`processInput()` 之后、`camera_.UpdataCameraPosition()` 之前，增加拖拽更新逻辑：
  ```cpp
  // 拖拽放置更新
  if (ImGui::GetCurrentContext()) {
      if (auto* payload = ImGui::GetDragDropPayload();
          payload && strcmp(payload->DataType, "MODEL_ASSET") == 0) {
          if (!dragPlace_.active) {
              uint64_t assetId = *static_cast<const uint64_t*>(payload->Data);
              beginDragPlace(assetId);
          }
          // 每帧更新预览实体位置
          ImVec2 mp = ImGui::GetMousePos();
          updateDragPlace(mp.x, mp.y);
      } else if (dragPlace_.active) {
          // payload 消失 = 用户松开鼠标 → 实体停在最后位置
          endDragPlace();
      }
  } else if (dragPlace_.active) {
      endDragPlace();
  }
  ```
  > **说明：** 松开鼠标后 payload 变 null，实体直接停在最后更新的世界位置，不需要 `cancelDragPlace`。
- [ ] **S0D3-3** 同步 placement distance：`drawContentBrowser()` 中的 Slider 值在每帧 `updateDragPlace` 调用前通过 `vulkanRender->dragPlace_.distance = placementDistance_` 同步

**验收标准：** 打开 Content Browser，设置 Place Distance = 10.0，拖拽一个模型 → 模型实时出现在鼠标映射的世界位置（保持距相机 10 单位），拖动过程中模型跟随鼠标移动，松开后模型留在最终位置。调节 Place Distance Slider 后再次拖拽，放置距离随之变化。

---

### S0E. 选中与每实体操作

**目标：** 提供场景实体列表、点击选中、属性面板编辑、ImGuizmo 变换操作。

**新增 ImGui 面板：**

```
Scene Outliner                    Entity Properties
┌──────────────────┐            ┌──────────────────────┐
│ Scene Entities   │            │ Properties           │
│ ┌──────────────┐ │            │ Name: [viking_room ] │
│ │viking_room   │ │            │                      │
│ │cyber_room    │ │            │ Transform            │
│ │fantasy_inn   │ │            │  Pos X:[0.0] Y:[0.0]│
│ │Box_001       │ │            │  Rot X:[0]  Y:[0]   │
│ └──────────────┘ │            │  Scl X:[1]  Y:[1]   │
│ [Delete] [Focus] │            │                      │
│                  │            │ Material            │
│                  │            │  [v] PBR_Default    │
│                  │            │  [Load .ast...]     │
└──────────────────┘            └──────────────────────┘
```

**具体任务：**

- [ ] **S0E1-1** 在 `IMGUIManager.hpp` 新增变量和声明：
  
  ```cpp
  bool showOutliner_        = true;
  bool showPropertiesPanel_ = true;
  uint64_t selectedEntityId_ = 0;
  void drawSceneOutliner();
  void drawPropertiesPanel();
  ```

- [ ] **S0E1-2** 实现 `drawSceneOutliner()`：
  
  - `ImGui::Begin("Scene Outliner", &showOutliner_)`
  - 遍历 `app->getSceneManager().getModelEntities()` + Box 实体
  - 每行 `ImGui::Selectable(entity.displayName, entity.selected)`，点击时设置 `selectedEntityId_` 并同步 `entity.selected = true`
  - 支持 Ctrl+Click 多选（初期可只做单选）
  - 右键菜单：Duplicate / Delete / Focus Camera / Create Box
  - 底部手动 `Add Box` 按钮

- [ ] **S0E3-1** 实现 `drawPropertiesPanel()`：
  
  - `ImGui::Begin("Properties", &showPropertiesPanel_)`
  - 查找当前 `selectedEntityId_` 对应的 `ModelEntity*`
  - Transform 编辑：`ImGui::DragFloat3("Position", ...)`, `ImGui::DragFloat3("Rotation", ...)`, `ImGui::DragFloat3("Scale", ...)`
  - 每次修改后调用 `app->getSceneManager().setEntityTransform(...)` 更新
  - Material 下拉列表（从 `MaterialManager` 获取所有材质名）
  - Visibility 复选框

- [ ] **S0E4-1** 修改 `UIManager::prepareFrame()` 中的 ImGuizmo 逻辑：
  
  - 当前硬编码 `mainModelTransform` —— 改为读取选中实体
  - 若 `selectedEntityId_` 有效且不是 Box，用实体自己的 `ObjectTransform` 驱动 ImGuizmo `Manipulate`
  - Manipulate 结果写回该实体的 Transform

- [ ] **S0E4-2** 扩展 PickSystem 支持多实体：
  
  - 当前的 `kPickIdMainModel` 改为每个 `ModelEntity::entityId` 映射
  - `PickSystem::runPick` 返回 hit entityId（而不是简单的 mainModel/box 区分）
  - 点击选中后，更新 `selectedEntityId_` 和 Outliner 高亮

**验收标准：** 场景中有多个模型，点击 Outliner 中任一条目可选中，Properties Panel 显示其 Transform 并可编辑，ImGuizmo 正确操作被选中的实体。

---

### S0F. 多模型渲染

**目标：** 渲染循环从单一主模型 + Box 实例化，改为遍历所有 ModelEntity + Box 实例化。

**修改文件：** `src/Application.cpp`（`recordCommandBuffer`）

**具体任务：**

- [ ] **S0F1-1** 重构 `recordCommandBuffer` 中"Main model"渲染段：
  
  ```cpp
  // Before: 一段代码渲染 sceneMgr_ 的单一模型
  // After: 遍历 sceneMgr_.getModelEntities()
  for (const auto& ent : sceneMgr_.getModelEntities()) {
      if (!ent.visible) continue;
      // bind vertex/index buffer from ent
      // push constants from ent.transform
      // iterate ent.subMeshes → bind descriptor set → DrawIndexed
  }
  ```

- [ ] **S0F1-2** 每实体的顶点/索引缓冲绑定改为使用 `ModelEntity` 自身存储的 VkBuffer 句柄（不再统一用 `sceneMgr_.getVertexBuffer()`）

- [ ] **S0F2-1** 每实体的 DescriptorSet 切换逻辑保持与现有 SubMesh 方案一致：不同的材质对应不同的 DescriptorSet

- [ ] **S0F3-1** Box 实例化渲染路径完全不变 —— `sceneMgr_.getInstanceBuffer()` 和 `getCubeIndexBuffer()` 继续使用

- [ ] **S0F3-2** 验证：场景中有 3 个不同模型（viking_room, cyber_room, fantasy_inn）同时渲染，帧率 > 30fps

**验收标准：** 多个不同模型在同一场景中正确渲染，各自使用独立材质和 Transform，Box 实例渲染不受影响。

---

### S0G. 场景持久化

**目标：** 将当前场景的实体列表、Transform、材质绑定保存为 `.scene.json`，下次启动可加载恢复。

**新建/修改文件：** `src/SceneSerializer.hpp` / `.cpp`

**文件格式（`res/scenes/<name>.scene.json`）：**

```json
{
  "version": 1,
  "entities": [
    {
      "astRelPath": "materials/viking_room.ast",
      "position": [0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale":    [1, 1, 1],
      "visible":  true,
      "materialOverride": { "baseColor": [0.8, 0.3, 0.3, 1.0], "roughness": 0.2 }
    },
    {
      "astRelPath": "materials/cyber_room.ast",
      "position": [5, 0, 3],
      "rotation": [0, 0.707, 0, 0.707],
      "scale":    [1, 1, 1]
    }
  ],
  "boxes": [
    { "entityId": 1, "position": [1, 2, 0] }
  ],
  "camera": {
    "position":    [0, -4, 4],
    "orientation": [0, 0.15, 0, 0.988],
    "speed": 5.0,
    "fov": 45
  }
}
```

> **materialOverride 机制：** 有 `astRelPath` 的实体不存 `materialId`（会话内递增数值重启后失效）。保存时对比当前材质与 `.ast` 参考值（params + albedoPath/normalPath），仅当有差异时写入 `materialOverride`。加载时先通过 `loadMaterialFromAsset()` 重建材质，再叠加 override。未修改材质的实体 JSON 中不出现 `materialOverride`。
>
> **相机姿态：** 使用 `orientation` 四元数（xyzw）代替旧格式的 `yaw/pitch`，避免 round-trip 偏航偏移。加载时兼容旧 `yaw/pitch` 格式。

**具体任务：**

- [x] **S0G1-1** 新建 `src/SceneSerializer.hpp` / `.cpp`
- [x] **S0G2-1** 实现 `SceneSerializer::save(path, sceneMgr, matMgr, camera)`：遍历 ModelEntity + Box，有 astRelPath 时对比材质写 materialOverride，序列化到 JSON
- [x] **S0G2-2** 实现 `SceneSerializer::load(path, app, sceneMgr, bufMgr, matMgr, cmdMgr, fbMgr, pipeMgr, camera)`：
  - 清空当前场景
  - 对每个 entity 条目读取 `astRelPath` → 解析对应 .ast 获取 `model` → `createModelEntity` + `loadMaterialFromAsset` + 叠加 `materialOverride`
  - 回退兼容旧格式 `displayName` 和 `yaw/pitch`
- [x] **S0G3-1** 实现 UI 保存/加载场景按钮
- [x] **S0G3-2** 引擎启动时尝试加载 `res/scenes/default.scene.json`

**验收标准：** 放置 3 个模型和 2 个 Box，调整位置，保存场景。关闭引擎后重启 → 加载场景 → 所有实体位置和材质完全一致。

---

## Phase S0 实施文件清单

| 文件 | 操作 | 所属阶段 |
|---|---|---|
| `src/ModelRegistry.hpp/.cpp` | 新建 | S0B |
| `src/SceneManager.hpp/.cpp` | 重构 | S0C |
| `src/PickSystem.hpp/.cpp` | 修改 | S0E |
| `src/Application.hpp/.cpp` | 修改 | S0D, S0E, S0G, 缩略图 |
| `src/IMGUIManager.hpp/.cpp` | 修改 | S0A, S0D, S0E, S0G, 缩略图 |
| `src/camera.hpp/.cpp` | 修改 | S0G |
| `src/SceneSerializer.hpp/.cpp` | 新建 | S0G |
| `src/ThumbnailRenderer.hpp/.cpp` | 新建 | 缩略图 v2 |
| `thirdParty/stb_image/stb_image_write.h` | 新增第三方 | 缩略图 v2 |
| `CMakeLists.txt` | 修改 | 缩略图 /utf-8 |

---

## 已知问题

### 缩略图 v2（ThumbnailRenderer）输出全灰色

**现象：** 引擎启动时调用 `ThumbnailRenderer::generateAll()`，生成的 128x128 PNG 缩略图全部为纯灰色，看不到模型轮廓。

**已尝试的修复（均未生效）：**
1. 将 render pass `finalLayout` 从 `TRANSFER_SRC_OPTIMAL` 改为 `COLOR_ATTACHMENT_OPTIMAL`，避免多轮之间的 implicit layout transition 失败
2. 拷贝后 barrier 转回 `UNDEFINED`，让每轮 render pass 走 `UNDEFINED→COLOR_ATTACHMENT_OPTIMAL`
3. CPU 端 BGRA→RGBA 通道 swap（`B8G8R8A8_UNORM` → `stbi_write_png`）
4. 删除残留旧灰色 PNG 文件强制重新生成

**排查方向（待后续调查）：**
- 缩略图的 pipeline/descriptor set 是否与主渲染管线冲突（共用了 `matMgr_->updateAllUBOs(0)` 和 `getDescriptorSet(matId, 0)`，而主渲染在第 0 帧也在用）
- 视图投影矩阵是否正确（`glm::lookAt` + Vulkan Y-flip）
- 可能在 `createModelEntity` 之后 GPU 资源尚未就绪（需要用 fence 同步而非 `vkDeviceWaitIdle`）
- Vertex push constants layout 是否与 pipeline 期望匹配
- `findDepthFormat` 返回的 depth format 是否被设备支持

---
