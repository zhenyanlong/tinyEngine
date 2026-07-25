# tinyEngine

一个面向图形学学习与编辑器实践的 C++20 / Vulkan 实时 3D 引擎。

tinyEngine 采用模块化 Manager 架构，已经从基础 Vulkan 渲染器扩展为带 Content Browser、场景编辑、PBR 材质、骨骼动画、Animator 状态机、Sequencer、Camera/Shot、视频录制和 MCP Agent 控制能力的小型编辑器。

> 当前项目以 Windows + Visual Studio 2022 为主要开发环境。代码中的部分基础设施兼容其他平台，但原生文件对话框和 MP4 录制等功能仍依赖 Windows。

## 项目来源与主要扩展

本项目基于 [BoomingMercury/tinyEngine](https://github.com/BoomingMercury/tinyEngine) 的 Vulkan 渲染器继续开发。感谢原作者提供基础工程、Vulkan 渲染框架和编辑器原型。

在上游项目基础上，本仓库主要扩展了：

- 多模型导入、Content Browser、资产描述文件和场景持久化
- glTF/FBX 骨架动画、GPU Skinning、Animator 状态机和 Root Motion
- Sequencer 关键帧、Camera/Shot、多轨道预览和 H.264/MP4 录制
- 基础 MCP 控制层，包括场景查询、模型放置、Transform 操作和帧捕获

上游代码与后续扩展的许可说明见仓库中的 [LICENSE](LICENSE)。

## 功能概览

- Vulkan 实时渲染、交换链重建、深度测试和动态 Viewport/Scissor
- OBJ、glTF、GLB、FBX 模型导入
- PBR 材质、子材质槽、纹理热替换和材质资产缓存
- Content Browser、Scene Outliner、Properties 和 ImGuizmo 场景编辑
- GPU Entity ID 拾取，包含当前姿势下的蒙皮模型拾取
- glTF/FBX 骨架、动画 Clip、GPU Skinning 和多 SkinBinding
- Animator 状态机、条件过渡、Cross-fade、Reverse、BlendSpace1D 和 Root Motion
- Sequencer Transform/Animator 关键帧、Camera/Shot 硬切和稳定目标重绑定
- Camera Actor 与 Picture-in-Picture 预览
- 固定步长 Sequence 录制，输出 H.264/MP4，可选保留 PNG 帧
- 本地 MCP 服务：资产查询、模型放置、Transform 控制、场景快照和帧捕获

## 技术栈

| 领域 | 技术 |
|---|---|
| 语言 | C++20；可选 Python 3.10+ MCP 适配层 |
| 图形 | Vulkan 1.x、GLSL/SPIR-V |
| 窗口与输入 | GLFW |
| 数学 | GLM |
| 编辑器 UI | Dear ImGui、ImGuizmo |
| 模型导入 | tinyobjloader、cgltf、ufbx |
| 数据格式 | nlohmann/json |
| 视频编码 | Windows Media Foundation |
| 构建 | CMake、Visual Studio 2022 |

## 环境要求

推荐环境：

- Windows 10/11 x64
- Visual Studio 2022，安装“使用 C++ 的桌面开发”和 Windows SDK
- Vulkan SDK，以及支持 Vulkan 的显卡驱动
- CMake 3.21 或更高版本（项目本身最低声明为 3.8，使用 `CMakePresets.json` 时建议 3.21+）
- Git
- Python 3.10+（仅 MCP 服务和 Python 测试需要，推荐 3.11）

## 快速开始

### 1. 获取源码和子模块

```powershell
git clone --recursive https://github.com/zhenyanlong/tinyEngine.git
cd tinyEngine
git submodule update --init --recursive
```

如果仓库已经存在，只需执行最后一条命令，确保 GLFW、GLM、cgltf、ufbx 等依赖完整。

### 2. 配置并构建

Debug：

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Release：

```powershell
cmake --preset x64-release
cmake --build --preset x64-release
```

### 3. 运行

```powershell
.\out\build\x64-debug\Debug\tinyEngine.exe
```

Release 可执行文件位于：

```text
out/build/x64-release/Release/tinyEngine.exe
```

运行时资源不会复制到可执行文件旁。引擎会从可执行文件位置向上查找包含 `CMakeLists.txt` 和 `res/` 的项目根目录，并始终使用项目内的 `res/` 作为资源基准。

## 基本操作

| 输入 | 操作 |
|---|---|
| `W` / `A` / `S` / `D` | 前后左右移动主摄像机 |
| `Q` / `E` | 沿世界 Up 轴下降 / 上升 |
| 按住鼠标右键拖动 | 旋转主摄像机 |
| 鼠标滚轮 | 调整摄像机移动速度 |
| 鼠标左键 | GPU 拾取场景实体 |
| `F` | 聚焦到当前选中实体 |
| `1` / `2` / `3` | 切换 Gizmo 的平移 / 旋转 / 缩放 |
| `4` | 切换 Gizmo 世界 / 本地坐标系 |
| `Esc` | 退出 |

当 ImGui 正在捕获键盘或鼠标时，相机输入会暂停，避免编辑控件时误操作视口。

## 推荐工作流

1. 在主控制窗口打开 **Content Browser**。
2. 使用 **Import** 导入 `.obj`、`.gltf`、`.glb` 或 `.fbx`。
3. 将 Mesh 资产从 Content Browser 拖入场景。
4. 在 **Scene Outliner** 中选择实体，并在 **Properties** 中调整 Transform 和材质。
5. 对带动画的模型，在 **Animator** 中创建或绑定 Controller、配置 State 和 Transition。
6. 在 **Sequencer** 中将实体加入轨道，添加 Transform/Animator 关键帧和全局 Camera/Shot。
7. 开启 **Sequencer Control** 后预览或录制 Sequence。

FBX 动画文件也可以通过 **Import Anim** 导入，并按骨骼名称重定向到目标 Mesh。

## 资源布局

```text
res/
├── content/       # .mesh.ast / .material.ast / .anim.ast 描述文件
├── bin/
│   ├── mesh/      # 模型 payload
│   ├── anim/      # .anim.bin 动画数据
│   ├── texture/   # 导入或提取的纹理
│   └── sequence_captures/
├── animators/     # .animctrl.json
├── sequences/     # .seq.json
├── scenes/        # .scene.json
├── shaders/       # GLSL 与预编译 SPIR-V
└── thumbnails/    # Content Browser 缩略图
```

`res/content/` 和 `res/bin/` 是当前资产布局；`res/materials/`、`res/models/` 和 `res/textures/` 仅作为旧资产兼容路径保留。

## MCP Agent 控制

tinyEngine 可以开启本地 TCP/NDJSON 控制端点，再由 Python FastMCP 服务向 Codex 或其他 MCP Host 暴露工具。所有引擎修改命令都会排队到 Vulkan 主线程执行。

### 安装 Python 适配层

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -e .
```

### 启动引擎端点

```powershell
.\out\build\x64-debug\Debug\tinyEngine.exe --mcp --port 9527
```

支持的命令行参数：

| 参数 | 说明 |
|---|---|
| `--mcp` | 启用本地 MCP/IPC 端点 |
| `--port N` | 指定监听端口，默认 `9527` |
| `--exit-after N` | 渲染 N 帧后自动退出，便于冒烟测试 |

项目内的 `.codex/config.toml` 已包含本地 Codex MCP 配置。其他 MCP Host 可使用以下 stdio 命令：

```powershell
.\.venv\Scripts\python.exe -m mcp_server
```

当前提供 10 个工具：

- `tiny_ping`
- `tiny_engine_status`
- `tiny_scene_snapshot`
- `tiny_asset_list`
- `tiny_model_place`
- `tiny_model_get_transform`
- `tiny_model_set_transform`
- `tiny_model_delete`
- `tiny_capture_frame`
- `tiny_engine_shutdown`

对已启动的引擎执行 IPC 或完整 stdio 冒烟测试：

```powershell
.\.venv\Scripts\python.exe -m mcp_server.smoke --port 9527
.\.venv\Scripts\python.exe -m mcp_server.mcp_smoke --port 9527
```

## 测试与验证

构建是当前 C++ 代码的主要静态验证：

```powershell
cmake --build --preset x64-debug
```

运行 Python MCP 测试：

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s mcp_server/tests -v
```

涉及渲染、模型导入、动画、PiP 或录制的修改仍应使用真实资产进行运行时验证。Vulkan 相关问题可配合 Validation Layer 和 RenderDoc 排查。

## 架构概览

```text
Application
├── VulkanContext / SwapChain / RenderPass / Framebuffer
├── Pipeline / Descriptor / Buffer / Texture / Material
├── SceneManager / SceneSerializer / ModelRegistry
├── PickSystem / ThumbnailRenderer
├── Animation
│   ├── Skeleton / AnimationClip / AnimationAssetLoader
│   ├── AnimatorController / AnimationRetargeter
│   └── Sequence / SequencePlayer / SequenceAssetLoader
├── UIManager / Camera / Camera Actor / PiP
├── FrameCapture / MediaFoundationVideoEncoder
└── MCP CommandBridge / IpcServer / SceneSnapshot
```

`Application` 是顶层调度器，各 Manager 负责独立 Vulkan 或编辑器子系统。动画状态、材质绑定和 Transform 以实体为边界保存，渲染、PiP 与 GPU Pick 共用一致的实体和 SkinBinding 数据。

## 项目文档

- [TDD.md](TDD.md)：当前技术设计、模块职责、数据流和验证记录
- [animation-system-plan.md](animation-system-plan.md)：动画、Animator、Sequencer 与资产系统计划
- [multimodel-import-plan.md](multimodel-import-plan.md)：多模型导入设计
- [mcp-control-plan.md](mcp-control-plan.md)：MCP 控制与验证层设计
- [DailyProgress.md](DailyProgress.md)：英文开发进度
- [DailyProgress_Chinese.md](DailyProgress_Chinese.md)：中文开发进度
- [lessons-learned.md](lessons-learned.md)：外部验证中积累的缺陷模式与修复经验

## 当前限制与路线图

- Content Browser 离屏缩略图仍需完善材质显示、相机角度和蒙皮模型渲染。
- Phase D 的统一 Animation Asset Registry 和 Assets 面板尚未完成。
- 单个 Skin 的 GPU Bone Palette 上限为 256。
- FBX 暂不导入 Blend Shape、约束、灯光、相机和分层/程序化材质。
- Sequence MP4 录制目前没有音频轨道。
- 阴影和后处理尚未实现。
- Linux 仍属于实验性目标，Windows 原生文件对话框与 Media Foundation 功能需要替代实现。

## License

本项目基于 [BoomingMercury/tinyEngine](https://github.com/BoomingMercury/tinyEngine) 继续开发，并保留上游版权声明。项目使用 [MIT License](LICENSE)。
