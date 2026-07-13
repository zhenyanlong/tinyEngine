---
name: "daily-progress"
description: "基于聊天上下文和工作区代码/资产变更，总结当日工作进度并记录到 DailyProgress.md（英文），同时翻译为中文同步到 DailyProgress_Chinese.md。每条记录带 [ ]/[✓] 验证标记，标记功能是否被人工或另一 AI 验证过。当用户说「记录工作进度」「更新进度」「daily progress」或完成一天的工作需要记录时调用。"
---

# 每日工作进度记录与同步

此 skill 用于在完成一天或一个阶段的工作后，根据聊天上下文和实际代码/资产变更，总结当日工作进度，并保持中英文版本同步。

## 适用场景

- 用户说 "记录工作进度" 或 "更新进度" 或 "daily progress"
- 完成了一天的工作，需要将今天的实现情况记录到 DailyProgress.md
- 需要同时维护中英文两份进度文档

## 工作流程

### 1. 收集信息

在记录进度之前，需要收集以下信息：

- **聊天上下文**：回顾本次会话中用户实现了哪些功能、讨论了哪些设计、做出了哪些决策
- **代码变更**：使用 `grep` / `SearchCodebase` / `LS` 等工具了解本次涉及的核心文件，了解：
  - 新增/修改了哪些类、函数、接口
  - 实现了哪些功能模块
  - 修复了哪些问题
- **资产变更**：查看是否新增或修改了 Blueprint、材质、网格等资产文件

### 2. 整理进度内容（英文）

根据收集到的信息，以简洁的要点形式总结今日工作：

- 每条要点一行，使用 `- [ ] ` 开头（`[ ]` 表示功能完整性尚未被手动或 AI 验证）
- 每条描述一个独立的工作项（新增功能、修复问题、重构等）
- 内容简洁，每条不超过一句话
- 不包含与工作无关的聊天内容

### 3. 写入 DailyProgress.md

**格式规范：**

```markdown
## YYYY-MM-DD

- [ ] Work item 1
- [ ] Work item 2
- [✓] Work item 3  (already verified)
```

**规则：**
- 使用 `## YYYY-MM-DD` 作为日期标题（使用今天的日期）
- 每条以 `- [ ] ` (未验证) 或 `- [✓] ` (已验证) 开头
- 新记录的条目**始终**以 `- [ ] ` 开头（默认未验证）
- **日期倒序排列（最新在上）**：最新的日期放在文件最开头，旧日期依次在后。这是为了方便 agent 从文件头部直接读到最新进度
- 如果是同一天再次更新，追加到同一天标题下的末尾，不重复创建标题
- 如果当天尚无记录，**先 Read 整个文件内容，将新日期标题和内容拼接到文件内容最前面，再用 Write 写入完整内容**。严禁直接覆写文件。
- 保持文件现有的所有历史记录不变

**验证标记说明：**
- `[ ]` — 未验证：该功能由 AI 实现或描述，尚未被人工或其他 AI 实际运行/编译/测试确认功能完整性
- `[✓]` — 已验证：功能已被人工或另一 AI 会话实际运行、编译通过并确认行为符合预期
- 用户可以说 "verify item X" / "mark item X as verified" / "验证第X条" 来更新标记状态
- 更新标记时，必须在 DailyProgress.md 和 DailyProgress_Chinese.md 中同步修改

### 4. 翻译为中文

- 将整理好的英文进度逐条翻译为中文
- 保持同样的要点格式
- 翻译应准确、自然，不添加额外信息

### 5. 写入 DailyProgress_Chinese.md

- 使用与 `DailyProgress.md` 完全相同的格式和规则
- 写入中文版本的内容
- 确保两份文件的日期和条目一一对应
- **注意**：同样需要先 Read 整个文件再拼接内容，严禁直接覆写

## 示例

假设今天实现了 GridVisualizer 模块并修复了一个渲染 Bug：

**DailyProgress.md：**

```markdown
## 2026-05-27

- [ ] Implemented GridVisualizer module for rendering grid lines in the editor
- [ ] Added configurable grid line thickness and color parameters
- [ ] Fixed rendering bug where grid lines disappeared at certain camera angles
```

**DailyProgress_Chinese.md：**

```markdown
## 2026-05-27

- [ ] 实现了 GridVisualizer 模块，用于在编辑器中渲染网格线
- [ ] 添加了可配置的网格线粗细和颜色参数
- [ ] 修复了特定摄像机角度下网格线消失的渲染问题
```

### 6. 生成 Commit Summary

进度记录完成后，必须生成一份英文的 commit summary 供用户手动提交时使用。格式规范：

- **标题行**：`<type>: <简短描述>`，如 `feat: Add ARoadTile with automatic mesh/rotation/scale switching`
- **正文**：用 `- ` 开头的要点列表，每条描述一个核心变更
- type 规范：`feat`（新功能）、`fix`（修复）、`refactor`（重构）、`docs`（文档）
- 内容应涵盖代码变更和文档变更

**示例：**

```
feat: Add ARoadTile with automatic mesh/rotation/scale switching

- Created ARoadTile inheriting AMeshGridPlaceableActor with FRoadMeshConfig struct
- Implemented FindMeshConfig() with 90° mask rotation for orientation auto-detection
- Added UpdateAppearance() for automatic Mesh/Rotation/Scale switching based on ConnectedMask
- Subscribed to OnCellChanged delegate for automatic neighbour refresh on placement/removal
```

## 重要规则

### 禁止自动提交（Commit）

**NEVER** 执行 `git commit` 操作。Commit summary 仅作为输出供用户手动使用，绝不能直接执行 `git commit` 命令。
