---
name: "session-init"
description: "初始化 tinyEngine 项目会话。读取 TDD.md 技术设计文档、animation-system-plan.md 动画系统计划，并扫描 src/ 代码目录结构。当用户说"初始化会话""开始工作""准备开发"或在新会话中需要了解项目上下文时调用。"
---

# tinyEngine 会话初始化

本 skill 用于在新对话会话开始时，快速建立对 tinyEngine 项目的整体认知。Agent 必须按照以下步骤执行：

## 会话行为约束

### Git 操作

- **禁止自动执行 git commit / push / merge**，除非用户明确要求
- 当需要记录变更时，必须先询问用户是否提交
- 允许的操作：`git status`、`git diff`、`git log`、`git branch`（只读类命令）
- 禁止的操作：`git commit`、`git push`、`git push --force`、`git merge`、`git rebase`、`git stash`、`git reset`（写类命令）

### 文件操作

- 修改代码前必须先用 Read 工具阅读相关文件原文
- 优先使用 SearchReplace 工具精确编辑，避免整文件重写
- 不要创建无关文件（如 *.md 临时笔记），除非用户明确要求

---

## 代码规范

### 文件组织

| 规则 | 说明 |
|---|---|
| 文件命名 | 类名即文件名：`ClassName.hpp` + `ClassName.cpp` |
| 头文件后缀 | 新代码统一用 `.hpp`（C++类）；仅第三方 C 代码用 `.h` |
| 一对一类文件 | 一个类对应一对 `.hpp`/`.cpp`，不将多个类混在一个文件 |
| 子目录组织 | 独立子系统放 `src/<Subsystem>/` 子目录（如 `src/Animation/`） |
| include 路径 | 使用相对路径 `#include "ClassName.hpp"`，不加 `src/` 前缀（CMake 已配置 include dirs） |
| include 顺序 | 先对应头文件，再项目内依赖，再第三方头文件，最后标准库 |

### 命名规范

| 类别 | 规范 | 示例 |
|---|---|---|
| 类名 | `PascalCase` | `SceneManager`, `PickSystem`, `VulkanContext` |
| 结构体 | `PascalCase` | `SubMesh`, `Vertex`, `UniformBufferObject` |
| 枚举类值 | `PascalCase` | `Mesh`, `Box`, `Translation`, `Rotation` |
| 公开方法 | `PascalCase` 或 `camelCase`（跟随现有风格） | `GetViewMatrix()`, `loadModel()`, `createMeshMaterial()` |
| 私有方法 | `camelCase` | `rebuildInstanceBuffer()`, `loadModelFromGltf()` |
| 公开成员变量 | `PascalCase`（Camera 风格）或 `camelCase` | `Position`, `Forward` / `clear_color` |
| 私有成员变量 | `camelCase_` 后缀下划线 | `vertexBuffer_`, `modelPosition_`, `pickCmdBuf_` |
| 静态常量 | `k` 前缀 + `PascalCase` | `kPickIdNone`, `kInvalidMaterialId`, `kMaxBones` |
| 宏 / define | `UPPER_SNAKE_CASE` | `WIDTH`, `HEIGHT`, `MAX_FRAMES_IN_FLIGHT` |
| 类型别名 | `PascalCase` 或 `camelCase`（using） | `RenderEntityId`, `MaterialId` |
| 局部变量 | `camelCase` | `imageIndex`, `fallbackMat`, `mappedData` |
| 参数名 | `camelCase`，不加前缀 m/p | `const glm::vec3& position` |
| 文件内匿名命名空间 | `camelCase` 函数 | `endsWithIgnoreCase()`, `resolveGltfImageRel()` |

### 类成员排列顺序

```cpp
class ClassName {
public:
    // 1. 嵌套类型（struct/enum/using）
    // 2. 静态常量
    // 3. 构造/析构
    // 4. 公开方法
    // 5. 公开成员变量

private:
    // 6. 私有成员变量
    // 7. 私有方法
};
```

### 注释规范

| 位置 | 语言 | 风格 |
|---|---|---|
| `.hpp` 头文件中的 Doxygen 注释 | 中文 | `/** @brief ... @param ... @return ... */` |
| `.cpp` 实现中的行内注释 | 中文 | `// 注释内容` |
| 分隔线注释 | ASCII | `// ─── Section Name ───` (仅当无中文编码问题时使用) |

---

## 执行步骤

### 步骤 1：阅读项目设计文档

使用 Read 工具读取以下文档：

1. **TDD.md**（`e:/Projects/tinyEngine/TDD.md`）—— 技术设计文档，包含：
   - 项目概述与技术栈
   - 完整目录结构
   - 渲染管线架构（主循环、初始化顺序）
   - 12 个 Manager 模块的职责与接口
   - 数据流（每帧数据流、材质资产加载流）
   - 顶点布局、UBO 布局、Push Constants
   - 相机系统、UI 系统、GPU 拾取系统
   - 第三方依赖清单
   - Shader 资源清单
   - CMake 编译定义
   - 已知限制与待扩展

2. **animation-system-plan.md**（`e:/Projects/tinyEngine/animation-system-plan.md`）—— 动画系统建设计划，包含：
   - Phase S0：多模型导入系统（Content Browser + 拖拽放置）
   - Phase A：基础骨骼动画（Skeleton / AnimationClip / GPU Skinning）
   - Phase B：状态机动画系统（AnimatorController / Blend）
   - Phase C：Sequencer 时序动画系统
   - Phase D：资产系统扩充
   - 每个 Phase 下有详细的子任务拆分（checkbox）、数据结构定义、伪代码、验收标准

### 步骤 2：扫描 src/ 源代码目录结构

使用 LS 工具扫描以下目录，了解源代码文件布局：

- `e:/Projects/tinyEngine/src/` —— 所有引擎源文件（.hpp / .cpp）

### 步骤 3：输出初始化摘要

完成阅读后，向用户汇报：

1. **项目概况**：一句话描述 tinyEngine 是什么
2. **当前功能状态**：已实现的核心模块列表
3. **开发计划**：当前 todo 和 Phase 概览
4. **就绪声明**：告知用户已准备好进行开发工作

## 注意事项

- 如果 TDD.md 或 animation-system-plan.md 的内容已在此次会话中读取过，可跳过对应文件，但需要扫描 src/ 目录验证文件结构是否一致。
- 如果某个文档不存在，应在摘要中说明缺失情况。
- src/ 目录扫描结果应与 TDD.md 第 2 节"目录结构"对照，如有差异应提醒用户。
- **禁止自动执行 git commit，代码变更的提交必须由用户手动触发。**
