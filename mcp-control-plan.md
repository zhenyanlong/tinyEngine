# tinyEngine MCP 控制与验证系统建设计划

> **目标：** 为 tinyEngine 建立一套 MCP（Model Context Protocol）控制与验证层，让 AI Agent（以及未来的 CI）能够**程序化地操控**引擎的全部现有功能，并**自动验证**功能正确性，无需人工盯屏幕。本计划与现有 `animation-system-plan.md` / `multimodel-import-plan.md` 对齐：每当引擎新增一个功能，都必须同时登记对应的 MCP 工具与验证钩子，使"操控 + 验证"成为每个功能的交付定义的一部分。

---

## 2026-07-18 一晚 MVP 状态

**本轮范围：** 实现可稳定闭环的只读/生命周期基础，以及“当前窗口最终帧 → PNG → MCP 图片内容块”的截图 MVP；不包含资产修改、动画控制、自动启动引擎和基线图像比对。

### Codex 已完成

- [x] 加固 `CommandBridge` 与跨平台 `IpcServer`，修复 Windows 64 位 socket 句柄和停止阻塞风险。
- [x] 打通 `TCP → CommandBridge → 主线程 handler → TCP response`。
- [x] 实现 `ping`、`engine.status`、`engine.shutdown`、`scene.snapshot`。
- [x] `scene.snapshot` 返回实体、Box、相机、动画 clip 和 Animator 运行状态。
- [x] 创建 Python FastMCP stdio Server，固定官方 SDK `mcp>=1.27,<2`。
- [x] 创建线程安全 `IpcClient`、结构化错误透传和 `mcp_server.smoke`。
- [x] Python 6 项测试通过：3 项 IPC 单元测试 + 2 项截图安全/内容测试 + 1 项官方 MCP stdio 工具发现测试。
- [x] `x64-debug` 构建通过。
- [x] 真实引擎闭环通过：读取 6 个实体的快照并通过 MCP 请求优雅关闭进程。
- [x] 实现 `FrameCapture`：读取含 ImGui 的 Swapchain 最终帧，完成 Vulkan layout transition、GPU→CPU 回读、BGRA→RGBA 与 PNG 编码。
- [x] 实现异步 `capture.frame` / `capture.get` 作业协议，以及直接返回 `ImageContent` 的 `tiny_capture_frame` MCP 工具。
- [x] 路径限制在 `res/bin/verify/captures/`，Python 侧再次校验真实路径、PNG 后缀与 16 MiB 上限。
- [x] 真实截图验收通过：1280×720、424,384 bytes，颜色/方向/ImGui 覆盖层均正确，MCP 返回 `image/png`。

### 用户需要手动完成

- [x] 项目级 `.codex/config.toml` 已写入 tinyEngine stdio MCP Server 配置。
- [ ] 重载 Codex，让当前任务的 MCP 工具清单发现新增的 `tiny_capture_frame`；若项目配置出现信任提示，确认信任当前 tinyEngine 仓库。
- [ ] 若 Windows 首次弹出防火墙提示，只允许本机/专用网络访问。
- [x] 已用可见窗口启动引擎，并通过实际截图确认编辑器最终画面正常。

### 明确延期

- 自动 launch/attach 和场景路径启动参数；
- M2 写操作、M3 动画控制、M4 基线图像比对与断言；
- 真正 headless Vulkan、批量验证套件与 CI；
- Pillow/NumPy 依赖仅在 M4 图像验证开始时加入。

---

## 整体架构概览

```
MCP 控制与验证系统
├── M0. 引擎侧命令桥接基础（CommandBridge + IPC）
│   ├── M0A. CommandBridge 主线程命令队列（线程安全 dispatch）
│   ├── M0B. IPC 传输层（localhost TCP + 换行分隔 JSON-RPC）
│   ├── M0C. 场景状态快照（SceneSnapshot 查询）
│   └── M0D. 帧捕获与 GPU 回读（FrameCapture → PNG/像素）
│
├── M1. MCP Server 骨架（Python + MCP SDK）
│   ├── M1A. Server 进程与工具注册（stdio JSON-RPC）
│   ├── M1B. 引擎生命周期管理（launch / attach / shutdown / verify-mode）
│   └── M1C. 工具调用 → IPC 命令映射（带超时与错误透传）
│
├── M2. 场景控制工具集（暴露现有功能）
│   ├── M2A. 资产与模型工具（scan / import / load / drag-place / delete）
│   ├── M2B. 实体变换工具（select / move / rotate / scale / imguizmo）
│   ├── M2C. 材质工具（load .ast / set params / set textures）
│   ├── M2D. 相机工具（position / orientation / fov / focus）
│   └── M2E. Box 实体工具（add / remove / move）
│
├── M3. 动画控制工具集
│   ├── M3A. 动画播放/预览（play / stop / seek / previewClip）
│   ├── M3B. 状态机参数控制（setFloat/Bool/Trigger / query state）
│   └── M3C. 动画资产导入与重定向（importAnimFbx / retarget）
│
├── M4. 验证工具集（Verification Suite）
│   ├── M4A. 截图比对验证（baseline diff，感知哈希 + 像素容差）
│   ├── M4B. 场景状态断言（assert transform / material / anim state）
│   ├── M4C. GPU 拾取验证（pick at screen coord → entity id）
│   ├── M4D. 构建与冒烟测试验证（cmake build + run + exit code）
│   └── M4E. 像素/区域采样验证（sample rect → avg/max color）
│
├── M5. 未来功能扩展预留
│   ├── M5A. Sequencer 时序控制工具（对齐 Phase C）
│   ├── M5B. 事件驱动验证钩子（对齐 B5-6 AnimatorEvent）
│   └── M5C. 资产注册表查询工具（对齐 Phase D）
│
├── M6. 自动化验证流水线
│   ├── M6A. 回归测试套件定义（.verify.json 场景脚本）
│   ├── M6B. CI 集成与报告生成（HTML/JSON 报告）
│   └── M6C. DailyProgress 自动打勾联动（与 verify-fix / mark-verified 联动）
│
└── M7. 扩展协议（新功能必须登记的工具与验证规则）
```

### 数据流总览

```
AI Agent (TRAE / Claude / ...)
    │  MCP tool call (stdio JSON-RPC)
    ▼
Python MCP Server (mcp_server/)
    │  localhost TCP:9527  (换行分隔 JSON: {id, method, params})
    ▼
CommandBridge (C++, 引擎内)
    │  TCP 接收线程 → 入队 Command{id, fn, promise<Json>}
    │  主线程 gameLoop 每帧 drain 队列 → 在主线程执行（Vulkan/GLFW 线程安全）
    │  结果经 promise 回传 → TCP 线程写回 {id, result, error}
    ▼
Application / SceneManager / MaterialManager / PickSystem / FrameCapture
```

---

## Phase M0 — 引擎侧命令桥接基础

### M0A. CommandBridge 主线程命令队列

**目标：** 在引擎内建立一个线程安全的命令调度器，把来自 IPC 线程的命令安全地路由到主线程执行。Vulkan 与 GLFW 的绝大多数操作必须在主线程进行，因此所有命令一律在 `gameLoop` 的帧边界执行。

**新建文件：** `src/Mcp/CommandBridge.hpp` / `src/Mcp/CommandBridge.cpp`

**数据结构：**

```cpp
// 一个待执行的命令：携带参数 + 返回结果的 promise
struct McpCommand {
    uint64_t                          id;       // 请求 id，用于配对响应
    std::string                       method;   // 如 "scene.getEntityList"
    nlohmann::json                    params;   // 入参
    std::shared_ptr<std::promise<nlohmann::json>> result;  // 结果回传
};

class CommandBridge {
public:
    static CommandBridge& instance();   // 单例，主线程持有

    // IPC 线程调用：投递命令并阻塞等待结果（带超时）
    nlohmann::json dispatch(uint64_t id, const std::string& method,
                            const nlohmann::json& params,
                            std::chrono::milliseconds timeout = kDefaultTimeout);

    // 主线程 gameLoop 每帧调用：取出并执行所有排队命令
    void drainQueue();

    // 注册命令处理函数（method → handler），handler 在主线程执行
    using Handler = std::function<nlohmann::json(const nlohmann::json&)>;
    void registerHandler(const std::string& method, Handler h);

    bool enabled() const { return enabled_; }
    void setEnabled(bool e) { enabled_ = e; }

private:
    bool                                       enabled_ = false;
    std::mutex                                 mtx_;
    std::condition_variable                    cv_;
    std::deque<McpCommand>                     queue_;
    std::unordered_map<std::string, Handler>   handlers_;
    static constexpr int kDefaultTimeoutMs = 5000;
};
```

**具体任务：**

- [x] **M0A-1** 新建 `src/Mcp/` 目录与 `CommandBridge.hpp` / `.cpp`，定义上述结构
- [x] **M0A-2** 实现 `dispatch`：构造 `McpCommand` + `promise`，加锁入队，`cv_.notify_one()`，`future.wait(timeout)`；超时返回 `{"error":"timeout","method":...}`
- [x] **M0A-3** 实现 `drainQueue`：主线程加锁取出全部命令（交换 `queue_` 到局部），**无锁**逐个调用 `handlers_[method](params)`，`promise->set_value(result)`；handler 抛异常时 `set_value({"error":...})`
- [x] **M0A-4** 在 `Application::gameLoop()` 中调用 `CommandBridge::instance().drainQueue()`（仅 `enabled_` 时）
- [ ] **M0A-5** 在 `Application::initVulkan()` 末尾，若命令行含 `--mcp` 或 `--verify`，调用 `setEnabled(true)` 并注册 M2-M4 的全部 handler
- [ ] **M0A-6** 命令行解析：`mainWindows.cpp` 支持 `--mcp [--port N] [--headless-frames N] [--scene path] [--exit-after N]`

**验收标准：** 引擎以 `--mcp` 启动后，外部 TCP 客户端发送 `{"id":1,"method":"ping","params":{}}`，1 帧内收到 `{"id":1,"result":{"pong":true},"error":null}`。

---

### M0B. IPC 传输层（localhost TCP + 换行分隔 JSON）

**目标：** 提供跨平台（Windows/Linux）的 IPC 通道。选用 localhost TCP 而非 Windows 命名管道，保证引擎代码的 Linux 兼容性不变。

**新建文件：** `src/Mcp/IpcServer.hpp` / `src/Mcp/IpcServer.cpp`

**协议规范：**

- 监听 `127.0.0.1:9527`（端口可由 `--port` 覆盖）
- 每条消息为单行 JSON，以 `\n` 分隔
- 请求：`{"id":<uint64>,"method":"<dot.path>","params":{...}}`
- 响应：`{"id":<uint64>,"result":<any>,"error":null}` 或 `{"id":<uint64>,"result":null,"error":{"code":"<str>","message":"<str>"}}`
- 通知（单向，无 id）：`{"method":"<event>","params":{...}}`（用于 M5B 事件推送）

**具体任务：**

- [x] **M0B-1** 新建 `IpcServer.hpp` / `.cpp`，封装一个监听 socket + 接收线程（单连接即可，MCP 场景下同时只有一个 agent）
- [x] **M0B-2** 接收线程：按 `\n` 累积解析一行 JSON → `CommandBridge::dispatch(...)` 阻塞等结果 → 写回一行 JSON 响应
- [x] **M0B-3** 大响应旁路：引擎只通过 IPC 返回限定目录内的 PNG 文件路径和元数据；MCP Server 校验路径/大小后读取并编码为 `ImageContent`，不把 base64 塞进 C++ TCP 响应
- [x] **M0B-4** 连接生命周期：客户端断开时不关闭引擎；支持重连。引擎退出时 `IpcServer::stop()` 优雅关闭 socket
- [x] **M0B-5** 在 `Application` 中新增 `std::unique_ptr<IpcServer> ipc_`，`enabled_` 时于 `initVulkan` 末尾 `start()`，`cleanUp()` 中 `stop()`

**验收标准：** 用 `nc` 或 Python `socket` 连上 9527，发送 `ping` 请求得到正确响应；强制断开后再连，引擎仍可响应。

---

### M0C. 场景状态快照（SceneSnapshot）

**目标：** 提供只读的引擎状态查询，作为验证工具的数据来源。Agent 通过快照断言场景是否符合预期，而不依赖截图。

**新建文件：** `src/Mcp/SceneSnapshot.hpp` / `src/Mcp/SceneSnapshot.cpp`

**数据结构：**

```cpp
struct EntitySnapshot {
    uint64_t      entityId;
    std::string   displayName;
    std::string   astRelPath;
    glm::vec3     position;
    glm::quat     rotation;     // xyzw
    glm::vec3     scale;
    bool          visible;
    bool          selected;
    bool          hasSkin;
    uint32_t      materialId;           // 主材质（slot 0）
    std::vector<uint32_t> subMeshMaterialIds;
    // 动画状态
    std::string   currentAnimState;     // AnimatorController 当前状态名
    float         animTime = 0.f;       // 当前状态播放时间
    int           previewClipIndex = -1;
    int           boneCount = 0;
    std::vector<std::string> clipNames;
};

struct SceneSnapshot {
    std::vector<EntitySnapshot> entities;
    struct BoxSnap { uint64_t id; glm::vec3 position; };
    std::vector<BoxSnap> boxes;
    // 相机
    glm::vec3 camPosition; glm::quat camOrientation; float camFov;
    std::string loadedScenePath;
    nlohmann::json toJson() const;
};
```

**具体任务：**

- [x] **M0C-1** 新建 `SceneSnapshot.hpp` / `.cpp`，实现 JSON 捕获（glm 类型序列化为数组）
- [x] **M0C-2** 实现 `SceneSnapshot::capture(...)`：遍历模型实体、Box 和相机，读取 Transform、材质、clip 与 AnimatorController 状态
- [x] **M0C-3** 注册 handler `scene.snapshot` → 返回完整快照 JSON
- [ ] **M0C-4** 注册 handler `scene.getEntity`（params: `entityId`）→ 返回单个实体快照
- [ ] **M0C-5** 注册 handler `scene.listAssets`（params: `subFolder?`, `typeFilter?`）→ 返回 `ModelRegistry` 扫描结果

**验收标准：** 场景中放置 2 个模型 + 1 个 Box，`scene.snapshot` 返回的 JSON 中实体数=2、Box 数=1，transform 与屏幕上 ImGuizmo 显示一致。

---

### M0D. 帧捕获与 GPU 回读（FrameCapture）

**MVP 目标：** 捕获当前窗口分辨率下已经完成主场景和 ImGui 绘制的 Swapchain 最终帧，并以 PNG / MCP 图片内容块返回。自定义确定性分辨率与离屏基线比对保留到 M4。

**新建文件：** `src/Mcp/FrameCapture.hpp` / `src/Mcp/FrameCapture.cpp`

**具体任务：**

- [x] **M0D-1** 新建 `FrameCapture.hpp` / `.cpp`，维护可随 Swapchain 重建的 host-visible staging buffer 与单任务状态机（pending/submitted/ready/failed）
- [x] **M0D-2** 在主 RenderPass（含 ImGui）结束后执行 `PRESENT_SRC → TRANSFER_SRC → PRESENT_SRC`，用 `vkCmdCopyImageToBuffer` 回读，并处理 BGRA/RGBA 格式与 PNG 编码
- [x] **M0D-3** 注册 `capture.frame` / `capture.get` 异步 handler；输出到 `res/bin/verify/captures/`，并由 `tiny_capture_frame(timeout_seconds)` 返回 MCP `ImageContent` + 元数据
- [ ] **M0D-4** 注册 handler `capture.pick`（params: `x`, `y`）→ 调用 `PickSystem::runPick` 在屏幕坐标处拾取，返回 `{entityId, pickedBoxId}`
- [ ] **M0D-5** 注册 handler `capture.sampleRect`（params: `x,y,w,h`）→ 回读指定矩形像素，返回 `{avgColor:[r,g,b,a], maxColor, pixelCount}`（用于验证清屏色/材质基色）
- [ ] **M0D-6** 关键重构：把 `Application::recordCommandBuffer` 中"主模型 + Box + 拾取"的录制逻辑提取为 `recordSceneInto(...)`，使其可面向任意 framebuffer/extent，避免与 swapchain 耦合

**MVP 验收结果：** 已由真实引擎生成 1280×720 PNG，直接返回 `image/png` 内容块；画面方向、颜色、主场景和 ImGui 均正确。后续验收仍包括自定义 512×512 离屏捕获、`capture.pick` 与 `capture.sampleRect`。

---

## Phase M1 — MCP Server 骨架（Python + MCP SDK）

### M1A. Server 进程与工具注册

**目标：** 建立一个 Python MCP Server，以 stdio 与 Agent 通信，把引擎能力暴露为 MCP 工具。

**新建目录：** `mcp_server/`（项目根下，独立于 `src/`）

**目录结构：**

```
mcp_server/
├── __init__.py
├── server.py            # MCP Server 入口，注册所有工具
├── ipc_client.py        # TCP 连接 CommandBridge，封装 dispatch(id, method, params)
├── tools/
│   ├── __init__.py
│   ├── scene.py         # M2 场景控制工具
│   ├── animation.py     # M3 动画工具
│   ├── verify.py        # M4 验证工具
│   └── future.py        # M5 未来功能工具
├── verify/
│   ├── image_diff.py    # 感知哈希 + 像素容差比对
│   ├── assertions.py    # 场景状态断言
│   └── suites/          # .verify.json 回归脚本
└── pyproject.toml       # 依赖：mcp, pillow, numpy
```

**具体任务：**

- [x] **M1A-1** 新建 `mcp_server/` 目录与 `pyproject.toml`（MVP 仅依赖 `mcp>=1.27,<2`；`pillow`、`numpy` 延至 M4）
- [x] **M1A-2** 实现 `ipc_client.py`：`IpcClient` 类，`connect()`、`call(method, params, timeout)` 同步返回 result 或抛 `IpcError`；自动管理递增 id
- [x] **M1A-3** 实现 `server.py`：用官方 MCP SDK 创建 FastMCP stdio server，MVP 注册 5 个工具并映射到 IPC（含 `tiny_capture_frame`）
- [x] **M1A-4** 工具错误模型：IPC `error` → 抛 `ToolError`；超时保留 `ipc_timeout` 错误码
- [ ] **M1A-5** 提供 `mcp_server/README.md`（仅在用户要求文档时创建；本计划默认不生成）说明启动方式：`python -m mcp_server.server`

**验收标准：** 在 TRAE/Claude 配置 MCP server 后，agent 能列出全部工具并调用 `ping` 得到 `pong`。

---

### M1B. 引擎生命周期管理

**目标：** MCP Server 能自动启动/停止引擎进程，或连接到已运行的引擎。

**具体任务：**

- [ ] **M1B-1** 实现 `engine.py`（mcp_server 内）：`EngineProcess` 类，`launch(scene?, headless_frames?)` 以 `--mcp --port N` 启动编译好的 exe；`attach(port)` 连接已运行实例；`shutdown()` 发送 `engine.shutdown` 命令优雅退出，超时则 kill
- [ ] **M1B-2** 注册工具 `engine.launch`（params: `scenePath?`, `frames?`）、`engine.attach`（params: `port`）、`engine.shutdown`、`engine.status`
- [x] **M1B-3** `engine.shutdown` handler：设置延迟关闭请求，当前帧结束后退出并返回 `{accepted:true}`
- [x] **M1B-4** `--exit-after N` 模式：引擎跑满 N 帧后自动退出，用于窗口模式冒烟测试
- [ ] **M1B-5** 端口探测：server 启动时尝试连接默认端口，失败则 `launch` 新引擎；提供 `engine.port` 配置项

**验收标准：** agent 调用 `engine.launch` → 引擎启动并加载指定场景 → `engine.status` 返回 `running` → `engine.shutdown` 后进程消失。

---

### M1C. 工具调用 → IPC 命令映射

**目标：** 定义统一的映射约定，确保每个 MCP 工具与引擎 handler 一一对应、可发现、可扩展。

**具体任务：**

- [ ] **M1C-1** 命名约定：MCP 工具名 `tiny.<domain>.<action>`（如 `tiny.scene.moveEntity`）↔ IPC method `scene.moveEntity`（去掉 `tiny.` 前缀）
- [ ] **M1C-2** 每个 handler 在引擎侧用 `MCP_HANDLER(method, fn)` 宏登记到一张表；M1A-3 的工具列表由该表自动校验一致性（构建期脚本 `mcp_server/tools/_registry_check.py` 对比 C++ 源码 grep）
- [ ] **M1C-3** 参数校验：handler 入口对必填字段做 `contains()` 检查，缺失返回 `{"error":{"code":"bad_params","message":"..."}}`
- [ ] **M1C-4** 日志：所有 IPC 请求/响应写入 `res/bin/verify/logs/mcp_<ts>.ndjson`，便于事后排错

**验收标准：** `_registry_check.py` 在 CI 中能检测出"工具未注册 handler"或"handler 无对应工具"的不一致并报错。

---

## Phase M2 — 场景控制工具集（暴露现有功能）

> 本 Phase 把 [Application.hpp](file:///e:/Projects/tinyEngine/src/Application.hpp) 已有的公开能力封装为 MCP 工具。每个工具对应一个引擎 handler，均通过 `CommandBridge::dispatch` 在主线程执行。

### M2A. 资产与模型工具

**具体任务：**

- [ ] **M2A-1** `tiny.asset.scan`（params: `subFolder?`）→ `ModelRegistry::search` 结果
- [ ] **M2A-2** `tiny.asset.importModel`（params: `sourcePath`, `subFolder?`）→ `Application::importModel`
- [ ] **M2A-3** `tiny.asset.importAnimationFbx`（params: `fbxPath`, `targetMeshAstRelPath`）→ `Application::importAnimationFbx`
- [ ] **M2A-4** `tiny.asset.applyMaterial`（params: `entityId?`, `astRelPath`）→ `loadAndApplyMaterialAsset`（对指定实体或主模型）
- [ ] **M2A-5** `tiny.entity.place`（params: `astRelPath`, `position=[x,y,z]`, `distance?`）→ 模拟拖拽放置：`beginDragPlace` + `updateDragPlace` + `endDragPlace`，返回 `entityId`
- [ ] **M2A-6** `tiny.entity.delete`（params: `entityId`）→ `deleteModelEntity`
- [ ] **M2A-7** `tiny.scene.save` / `tiny.scene.load`（params: `path`）→ `saveScene` / `loadScene`

**验收标准：** agent 通过 `scan` 找到 `viking_room` → `place` 到 (0,0,0) → `scene.snapshot` 确认实体存在且位置正确 → `save` → 重启 `load` 后实体一致。

---

### M2B. 实体变换工具

**具体任务：**

- [ ] **M2B-1** `tiny.entity.select`（params: `entityId`）→ 设置 `selectedEntityId_` 并同步 `entity.selected`
- [ ] **M2B-2** `tiny.entity.setTransform`（params: `entityId`, `position?`, `rotation?`, `scale?`）→ `SceneManager::setEntityTransform`
- [ ] **M2B-3** `tiny.entity.getTransform`（params: `entityId`）→ 读取实体 ObjectTransform
- [ ] **M2B-4** `tiny.entity.setVisible`（params: `entityId`, `visible`）
- [ ] **M2B-5** `tiny.entity.focusCamera`（params: `entityId`）→ 复用相机 SmoothFocus 逻辑

**验收标准：** `setTransform` 后 `getTransform` 回读值一致；`focusCamera` 后相机快照位置在目标实体前方。

---

### M2C. 材质工具

**具体任务：**

- [ ] **M2C-1** `tiny.material.create`（params: `name`, `albedoPath?`, `normalPath?`, `params?`）→ `createMeshMaterial`
- [ ] **M2C-2** `tiny.material.createBox`（params: `name`, `params`）→ `createBoxMaterial`
- [ ] **M2C-3** `tiny.material.setAlbedo` / `setNormal`（params: `materialId`, `path`）→ `setMaterialAlbedo/Normal`
- [ ] **M2C-4** `tiny.material.setParams`（params: `materialId`, `params`）→ 直接写 `MaterialEntry` 的 `MaterialParams`（需在 MaterialManager 新增 `setParams(id, params)`）
- [ ] **M2C-5** `tiny.material.assign`（params: `entityId`, `materialId`, `slot?`）→ `SceneManager::setEntityMaterial`（按 submesh slot）
- [ ] **M2C-6** `tiny.material.list` → 返回所有材质 id/name/hasSkinning

**验收标准：** 创建材质 → 设 albedo → assign 到实体 → `scene.snapshot` 显示该实体 materialId 已更新 → 截图基色变化。

---

### M2D. 相机工具

**具体任务：**

- [ ] **M2D-1** `tiny.camera.set`（params: `position?`, `orientation?`, `fov?`）→ `Camera::SetPosition/SetOrientation/SetFov`
- [ ] **M2D-2** `tiny.camera.get` → 返回 position/orientation/fov
- [ ] **M2D-3** `tiny.camera.lookAt`（params: `eye`, `target`, `up?`）→ 用 `glm::lookAt` 反解 orientation 并设置
- [ ] **M2D-4** `tiny.camera.setSpeed`（params: `speed`）

**验收标准：** `camera.lookAt` 后 `capture.frame` 的视角与预期一致（与手放相机截图比对）。

---

### M2E. Box 实体工具

**具体任务：**

- [ ] **M2E-1** `tiny.box.add`（params: `position`）→ `addBox`，返回 entityId
- [ ] **M2E-2** `tiny.box.remove`（params: `entityId`）→ `removeBox`
- [ ] **M2E-3** `tiny.box.move`（params: `entityId`, `position`）→ `setBoxPosition`

**验收标准：** `box.add` → `scene.snapshot` 中 boxes 数 +1 → `box.move` → 位置更新 → `box.remove` → 数 -1。

---

## Phase M3 — 动画控制工具集

### M3A. 动画播放/预览

**具体任务：**

- [ ] **M3A-1** `tiny.anim.listClips`（params: `entityId`）→ `SceneManager::getEntityAnimationClips` 的 clip 名/时长
- [ ] **M3A-2** `tiny.anim.preview`（params: `entityId`, `clipIndex|clipName`, `speed?`）→ 设置 `ModelEntity::previewClipIndex/previewSpeed`，绕开状态机直接播放
- [ ] **M3A-3** `tiny.anim.stopPreview`（params: `entityId`）→ `previewClipIndex = -1`
- [ ] **M3A-4** `tiny.anim.step`（params: `entityId`, `clipIndex`, `time`）→ 设置预览 + `previewTime = time`，**单帧渲染**（不推进时间），用于精确关键帧验证
- [ ] **M3A-5** `tiny.anim.advance`（params: `entityId`, `frames`, `dtPerFrame?`）→ 强制推进 N 帧（每帧 drain + drawFrame），返回推进后的 `animTime`

**验收标准：** `anim.preview` clip 0 → `capture.frame` 显示非绑定姿势；`anim.step` t=0 与 t=mid 两张截图骨骼姿势不同。

---

### M3B. 状态机参数控制

**具体任务：**

- [ ] **M3B-1** `tiny.animator.getState`（params: `entityId`）→ 当前状态名、过渡进度、参数表
- [ ] **M3B-2** `tiny.animator.setParam`（params: `entityId`, `name`, `type`, `value`）→ `AnimatorController::setFloat/setInt/setBool/setTrigger`
- [ ] **M3B-3** `tiny.animator.dispatchEvent`（params: `entityId`, `events[]`）→ `dispatchEvents`，对齐 B5-6
- [ ] **M3B-4** `tiny.animator.load` / `save`（params: `entityId`, `path`）→ 加载/保存 `.animctrl.json`
- [ ] **M3B-5** `tiny.animator.listCompatible`（params: `entityId`）→ 扫描 `res/animators/` 返回与该实体 clips 兼容的控制器列表

**验收标准：** 设 `Speed>0.8` → `getState` 显示从 Idle 过渡到 Run；过渡完成后 current=Run。

---

### M3C. 动画资产导入与重定向

**具体任务：**

- [ ] **M3C-1** `tiny.anim.importFbx`（params: `fbxPath`, `targetMeshAstRelPath`）→ `Application::importAnimationFbx`
- [ ] **M3C-2** `tiny.anim.listAnimAssets`（params: `meshAstRelPath?`）→ 扫描 `.anim.ast` 列表
- [ ] **M3C-3** `tiny.anim.retargetPreview`（params: `sourceClipPath`, `targetEntityId`）→ 仅在内存重定向不落盘，返回映射骨骼数/丢弃通道数，供验证

**验收标准：** 导入 Mixamo FBX 到目标 mesh → `listClips` 出现新 clip → `preview` 播放无扭曲。

---

## Phase M4 — 验证工具集（Verification Suite）

> 验证工具让 agent 在**无人观察屏幕**的前提下确认功能正确性。这是本计划的核心价值：把 DailyProgress 中 `[ ]` 未验证条目转化为可自动执行的断言。

### M4A. 截图比对验证

**新建文件：** `mcp_server/verify/image_diff.py`

**具体任务：**

- [ ] **M4A-1** 实现 `image_diff.compare(actualPath, baselinePath, opts)`：
  - 感知哈希（pHash）相似度 → `similarity ∈ [0,1]`
  - 像素级容差比对：`maxPixelDiff`、`meanPixelDiff`、差异像素占比 `diffRatio`
  - `opts`: `{tolerance, ignoreRegions:[{x,y,w,h}], resize?}`
- [ ] **M4A-2** 注册工具 `tiny.verify.screenshot`（params: `baselinePath`, `captureWidth?`, `captureHeight?`, `tolerance?`, `ignoreRegions?`）→ `capture.frame` → 比对 → 返回 `{passed, similarity, diffRatio, diffImagePath}`
- [ ] **M4A-3** baseline 管理：`tiny.verify.saveBaseline`（params: `name`）→ 当前帧存为 `res/bin/verify/baselines/<name>.png`；首次运行自动建立 baseline
- [ ] **M4A-4** 失败时输出 diff 图（差异像素高亮红色叠层）到 `res/bin/verify/diffs/`

**验收标准：** 同一场景两次 `verify.screenshot` 通过；故意移动相机后 `similarity<阈值` 报失败并产出 diff 图。

---

### M4B. 场景状态断言

**新建文件：** `mcp_server/verify/assertions.py`

**具体任务：**

- [ ] **M4B-1** `tiny.assert.scene`（params: `expect`）：
  - `expect.entitiesCount`、`expect.boxesCount`
  - `expect.entity`（按 entityId 或 displayName 匹配）→ 断言 position/rotation/scale/materialId/currentAnimState
  - 浮点比较用 `epsilon`（默认 1e-4）
- [ ] **M4B-2** `tiny.assert.camera`（params: `expect`）→ 断言相机 position/orientation/fov
- [ ] **M4B-3** 返回 `{passed, failures:[{path, expected, actual}]}`，不抛异常（便于 agent 一次跑多条断言）

**验收标准：** 放置实体后 `assert.scene` 通过；改 transform 后断言失败并报告期望/实际值。

---

### M4C. GPU 拾取验证

**具体任务：**

- [ ] **M4C-1** `tiny.verify.pick`（params: `x`, `y`, `expectEntityId?`）→ `capture.pick` → 比对预期 → 返回 `{passed, actual, expected}`
- [ ] **M4C-2** `tiny.verify.pickEntityCenter`（params: `entityId`）→ 先 `scene.snapshot` 取实体 AABB 中心投影到屏幕 → pick → 断言命中自身（验证蒙皮拾取对齐当前姿势）

**验收标准：** 对已知模型中心点 pick 返回该模型 entityId；对空白区域 pick 返回 0。

---

### M4D. 构建与冒烟测试验证

**新建文件：** `mcp_server/verify/build.py`

**具体任务：**

- [ ] **M4D-1** `tiny.verify.build`（params: `preset?`）→ 子进程 `cmake --build --preset x64-debug` → 解析输出 → 返回 `{ok, errors:[{file,line,message}], warnings}`
- [ ] **M4D-2** `tiny.verify.smoke`（params: `scenePath?`, `frames?`）→ `engine.launch(frames=N)` → 跑满 N 帧无崩溃 → `engine.status` → 返回 `{ok, framesRun, exitCode, crashLog?}`
- [ ] **M4D-3** 崩溃捕获：引擎异常退出时收集 stderr + Windows minidump（若启用）路径

**验收标准：** 当前代码 `verify.build` 返回 `ok:true`；`verify.smoke` 跑 60 帧后 `ok:true`。

---

### M4E. 像素/区域采样验证

**具体任务：**

- [ ] **M4E-1** `tiny.verify.pixelColor`（params: `x`, `y`, `expect`, `tolerance?`）→ `capture.sampleRect(1x1)` → 比对
- [ ] **M4E-2** `tiny.verify.rectColor`（params: `x,y,w,h`, `expectAvg`, `tolerance?`）→ 区域平均色比对
- [ ] **M4E-3** `tiny.verify.clearColor`（params: `expect`）→ 全屏采样验证清屏色

**验收标准：** 设清屏色 (0.1,0.2,0.3) → `verify.clearColor` 通过；改清屏色后失败。

---

## Phase M5 — 未来功能扩展预留

> 本 Phase 不立即实现，而是定义**当对应引擎功能落地时，必须同步交付的 MCP 工具与验证钩子**，作为 M7 扩展协议的具体实例。

### M5A. Sequencer 时序控制工具（对齐 Phase C）

- [ ] **M5A-1**（当 C2 完成时）`tiny.seq.load` / `play` / `pause` / `seek`
- [ ] **M5A-2**（当 C2 完成时）`tiny.verify.seqState`（params: `expectTime`, `expectTrackMuted[]`）
- [ ] **M5A-3**（当 C3 完成时）`tiny.seq.recordCamera` / `tiny.verify.cameraPath`（采样路径关键帧）

### M5B. 事件驱动验证钩子（对齐 B5-6）

- [ ] **M5B-1** `tiny.animator.onEvent`（params: `eventName`, `expectWithinMs`）→ 引擎在 dispatchEvent 时通过 IPC **通知**（无 id 消息）server，server 等待匹配事件或超时失败
- [ ] **M5B-2** 事件订阅模型：server 维护 `pending_event_waiters`，收到通知后唤醒对应等待

### M5C. 资产注册表查询工具（对齐 Phase D）

- [ ] **M5C-1**（当 D1 完成时）`tiny.asset.scanAnim` / `scanAnimControllers` / `scanSequences` / `scanCameraPaths`
- [ ] **M5C-2**（当 D3 完成时）`tiny.asset.dragToTrack`（模拟 ImGui 拖拽创建 clip）

---

## Phase M6 — 自动化验证流水线

### M6A. 回归测试套件定义

**新建文件：** `mcp_server/verify/suites/*.verify.json`

**格式（`.verify.json`）：**

```json
{
  "name": "multi-model-render",
  "description": "放置三模型并验证渲染",
  "setup": [
    { "tool": "tiny.scene.load", "params": { "path": "scenes/default.scene.json" } },
    { "tool": "tiny.entity.place", "params": { "astRelPath": "materials/viking_room.ast", "position": [0,0,0] }, "saveAs": "vikingId" }
  ],
  "steps": [
    { "tool": "tiny.verify.screenshot", "params": { "baselinePath": "baselines/multi-model.png", "tolerance": 0.02 } },
    { "tool": "tiny.assert.scene", "params": { "expect": { "entitiesCount": ">=1" } } },
    { "tool": "tiny.verify.pickEntityCenter", "params": { "entityId": "${vikingId}" } }
  ],
  "teardown": [ { "tool": "tiny.scene.save", "params": { "path": "scenes/default.scene.json" } } ]
}
```

**具体任务：**

- [ ] **M6A-1** 实现 `suite_runner.py`：按顺序执行 setup/steps/teardown，支持变量 `${var}` 传递上一步 `saveAs` 的返回值
- [ ] **M6A-2** 提供 `tiny.verify.runSuite`（params: `suitePath`）工具，让 agent 可触发整条套件
- [ ] **M6A-3** 内置套件：`multi-model-render`、`animation-playback`、`animator-transition`、`fbx-import`、`material-hotswap`（每个对应 DailyProgress 中一组 `[ ]` 条目）

**验收标准：** `runSuite multi-model-render` 顺序执行全部步骤并返回聚合 `{passed, failed:[], duration}`。

---

### M6B. CI 集成与报告生成

**具体任务：**

- [ ] **M6B-1** 提供 CLI `python -m mcp_server.verify.run_all --suites suites/ --report res/bin/verify/report.html`
- [ ] **M6B-2** 报告含每套件的步骤明细、失败截图/diff、场景快照 diff
- [ ] **M6B-3** GitHub Actions（或本地任务）workflow：`build → launch --verify → run_all → 上传 report artifact`
- [ ] **M6B-4** 退出码：全部通过 0，任一失败 1，供 CI 门禁

**验收标准：** CI 中 `run_all` 产出 HTML 报告，失败时附 diff 图。

---

### M6C. DailyProgress 自动打勾联动

**目标：** 把验证结果与现有 `verify-fix` / `mark-verified` skill 打通：某条 DailyProgress `[ ]` 条目对应的 suite 通过后，自动建议标记 `[✓]`。

**具体任务：**

- [ ] **M6C-1** 在 `.verify.json` suite 中支持 `dailyProgressTag` 字段，关联 DailyProgress 条目关键词
- [ ] **M6C-2** suite 通过后，runner 输出提示："建议对 DailyProgress 含 '<keyword>' 的条目执行 mark-verified"
- [ ] **M6C-3** 不自动改文件（遵守 session-init 的"禁止自动 git/写文件除非明确要求"约束），仅提示 agent/用户

**验收标准：** `animation-playback` suite 通过后，runner 提示可标记 06-27 的 Animator 条目。

---

## Phase M7 — 扩展协议（新功能必须登记的工具与验证规则）

**目标：** 建立强制性约定，确保**未来每个引擎功能交付时，操控与验证一并到位**，避免功能堆积而无法自动验证。

### 协议规则

- [ ] **M7-1** **登记义务**：任何新增 `Application` 公开方法或 Manager 重要接口，必须同时：
  1. 在 `CommandBridge` 注册一个 IPC handler（`<domain>.<action>`）
  2. 在 `mcp_server/tools/` 注册对应 MCP 工具（`tiny.<domain>.<action>`）
  3. 在 `mcp_server/verify/suites/` 新增或扩展至少一个 `.verify.json` 覆盖该功能
- [ ] **M7-2** **`sync-plan` skill 扩展**：更新 `sync-plan` skill，在同步 TDD/计划时检查"新增公开 API 是否有对应 MCP 工具登记"，缺失则警告
- [ ] **M7-3** **`session-init` skill 扩展**：初始化摘要中新增"MCP 工具覆盖度"小节，统计 `Application` 公开方法中已暴露为 MCP 工具的比例
- [ ] **M7-4** **`verify-fix` skill 扩展**：外部验证反馈处理时，若反馈指向的功能已有 `.verify.json`，自动把失败用例补入套件（回归保护）
- [ ] **M7-5** **一致性检查脚本**：`mcp_server/tools/_registry_check.py` 对比 C++ `MCP_HANDLER` 宏登记表与 Python 工具注册表，CI 中不一致即失败

### 覆盖度矩阵（功能 → 工具 → 验证）

| 引擎功能 | 控制工具 | 验证手段 | 状态 |
|---|---|---|---|
| 多模型导入 | M2A | M4A 截图 + M4B 断言 | 待实现 |
| 拖拽放置 | M2A-5 | M4B position 断言 | 待实现 |
| 实体变换 | M2B | M4B transform 断言 | 待实现 |
| 材质系统 | M2C | M4E 基色采样 | 待实现 |
| 场景持久化 | M2A-7 | load 后 M4B 全场景断言 | 待实现 |
| 相机 | M2D | M4A 视角截图 | 待实现 |
| Box 实体 | M2E | M4B boxesCount | 待实现 |
| GPU 蒙皮 | M3A | M4A 姿势截图 + M4C 蒙皮拾取 | 待实现 |
| 动画状态机 | M3B | M4B currentAnimState 断言 | 待实现 |
| 动画重定向 | M3C | M4A 重定向后截图 | 待实现 |
| FBX 导入 | M2A-2/M3C-1 | M4D smoke + M4A | 待实现 |
| Sequencer（Phase C） | M5A | M5A-2 | 随 C 落地 |
| 事件驱动（B5-6） | M5B | M5B 事件等待 | 随 C 落地 |
| 资产浏览器（Phase D） | M5C | M4B 断言 | 随 D 落地 |

---

## 各 Phase 依赖关系与推荐开发顺序

```
M0A → M0B → M0C → M0D
              ↓
              M1A → M1B → M1C
                            ↓
        ┌───────────────────┼───────────────────┐
        M2(A-E)             M3(A-C)             M4(A-E)
        (并行)              (依赖 M2 已有实体)   (依赖 M0C/M0D + M2/M3)
                                                    ↓
                                                    M6(A-C)
                                                    ↓
                                                    M7（贯穿始终）
M5（按引擎功能落地节奏触发，不阻塞主线）
```

**推荐分阶段里程碑：**

| 里程碑 | 完成条件 | 预计子任务数 |
|--------|----------|-------------|
| MM1 桥接打通 | TCP ping 闭环 + scene.snapshot 返回正确 | M0（约 20 个任务） |
| MM2 Agent 可控 | 全部 M2 工具可用，agent 能建场景 | M1+M2（约 25 个任务） |
| MM3 动画可控 | M3 工具可播放/断言动画 | M3（约 13 个任务） |
| MM4 可自动验证 | M4 全套可用，至少 3 个 baseline 通过 | M4（约 14 个任务） |
| MM5 回归流水线 | run_all 在 CI 跑通并出报告 | M6（约 10 个任务） |
| MM6 扩展闭环 | M7 协议落地，sync-plan/session-init 扩展生效 | M7（约 5 个任务） |

---

## 关键实现风险与注意事项

### Vulkan/GLFW 线程安全（⚠️ 高风险）
绝大多数 Vulkan 调用与全部 GLFW 调用必须在主线程。若 IPC 线程直接调用引擎 API，会触发校验层错误或崩溃。
**解决方案：** `CommandBridge` 只把命令入队，所有 handler 在 `gameLoop` 主线程 `drainQueue` 中执行；查询类 handler 也走主线程以保证读到一致状态。

### 确定性截图（⚠️ 高风险）
swapchain 分辨率随窗口变化、呈现时机不确定，导致截图比对不稳定。
**解决方案：** `FrameCapture` 用**固定分辨率离屏渲染**（默认 512×512），独立于 swapchain；相机矩阵由 `camera.get` 显式设置后再捕获，避免帧间动画推进导致差异。`anim.step` 提供"冻结时间"捕获，排除时间推进噪声。

### 无头/窗口依赖（⚠️ 中风险）
引擎依赖 GLFW 窗口与 Vulkan surface，真正无头（无窗口）需 headless Vulkan instance，改造量大。
**解决方案：** 短期接受"有窗口"运行：`--exit-after N` 跑满 N 帧退出，窗口可最小化但不销毁；CI 用虚拟显示（Linux Xvfb）或 Windows 隐藏窗口。长期再评估 headless context。

### 主线程阻塞与超时（⚠️ 中风险）
若 handler 执行时间长（如导入大 FBX），`dispatch` 超时返回但命令仍在队列，可能错乱。
**解决方案：** 长任务（import/smoke）用异步通知：handler 立即返回 `{jobId}`，完成后引擎主动推 IPC 通知；server 用 `tiny.job.wait(jobId)` 等待。短任务保持同步。

### 截图大响应传输（⚠️ 低风险）
PNG base64 可能超过单行 JSON 合理性上限。
**解决方案：** M0B-3 的文件中转：响应写临时文件返回路径，server 读取后删除；baseline/diff 图统一存 `res/bin/verify/`。

### 与现有渲染路径耦合（⚠️ 中风险）
M0D-6 需重构 `recordCommandBuffer` 抽取 `recordSceneInto`，可能影响主渲染。
**解决方案：** 重构保持主渲染调用路径行为不变（先抽函数、后复用），新增 `FrameCapture` 单独测试；优先用 `ThumbnailRenderer` 已验证的离屏模式作为蓝本，规避其已知 bug（输出全灰，见 multimodel-import-plan 已知问题）。

---

## 新增文件清单

```
src/Mcp/
├── CommandBridge.hpp / .cpp      # 主线程命令队列 + handler 注册
├── IpcServer.hpp / .cpp          # localhost TCP 换行分隔 JSON
├── SceneSnapshot.hpp / .cpp      # 只读场景状态快照
└── FrameCapture.hpp / .cpp       # 离屏帧捕获 + GPU 回读

mcp_server/
├── __init__.py
├── server.py                     # MCP Server 入口 + 工具注册
├── ipc_client.py                 # TCP 客户端
├── engine.py                     # 引擎进程生命周期
├── tools/
│   ├── __init__.py
│   ├── scene.py                  # M2 工具
│   ├── animation.py              # M3 工具
│   ├── verify.py                 # M4 工具
│   ├── future.py                 # M5 工具
│   └── _registry_check.py        # C++/Python 工具一致性检查
├── verify/
│   ├── image_diff.py             # pHash + 像素容差
│   ├── assertions.py             # 场景/相机断言
│   ├── build.py                  # cmake build + smoke
│   ├── suite_runner.py           # .verify.json 执行器
│   └── suites/
│       ├── multi-model-render.verify.json
│       ├── animation-playback.verify.json
│       ├── animator-transition.verify.json
│       ├── fbx-import.verify.json
│       └── material-hotswap.verify.json
└── pyproject.toml

res/bin/verify/                   # 运行时产物（gitignore）
├── captures/
├── baselines/
├── diffs/
└── logs/
```

**修改现有文件：**

| 文件 | 修改内容摘要 |
|------|-------------|
| `src/Application.hpp/.cpp` | 新增 `ipc_`/`frameCapture_` 成员；`gameLoop` 调 `drainQueue`；`initVulkan`/`cleanUp` 接入 IPC；抽取 `recordSceneInto` 公共方法；新增 `shouldExit_` |
| `src/mainWindows.cpp` | 命令行解析 `--mcp/--port/--headless-frames/--scene/--exit-after` |
| `src/MaterialManager.hpp/.cpp` | 新增 `setParams(id, params)` 供 M2C-4 |
| `src/SceneManager.hpp/.cpp` | 新增按 slot 设置材质的访问器（若现有 `setEntityMaterial` 不支持 slot） |
| `.trae/skills/sync-plan/SKILL.md` | M7-2：检查新增公开 API 的 MCP 工具登记 |
| `.trae/skills/session-init/SKILL.md` | M7-3：摘要新增 MCP 工具覆盖度小节 |
| `.trae/skills/verify-fix/SKILL.md` | M7-4：失败用例补入对应 `.verify.json` |
| `CMakeLists.txt` | 新增 `src/Mcp/` 源文件；链接 nlohmann/json（已有） |
| `.gitignore` | 忽略 `res/bin/verify/` 运行时产物 |
