---
name: "verify-fix"
description: "Processes external LLM verification feedback for DailyProgress items, logs lessons to lessons-learned.md, and marks items as verified. Invoke when user says 'verify feedback', '处理验证反馈', or '记录教训'."
---

# 验证反馈处理与教训积累

此 skill 用于处理来自其他 LLM 或人工对 DailyProgress 中 `[ ]` 条目的功能完整性验证反馈。分析问题根因、记录教训、标记已验证。

## 适用场景

- 用户说 "verify feedback" / "处理验证反馈" / "记录教训"
- 用户提供另一 LLM 的验证反馈文字 + 已被修改的代码文件
- 需要将验证过程中发现的问题总结为可供未来参考的教训
- 需要将 DailyProgress 中已验证条目从 `[ ]` 标记为 `[✓]`

## 工作流程

### 步骤 1：收集验证上下文

用户会提供两部分内容：

1. **反馈文本** — 另一 LLM 或人工在验证过程中发现的问题描述、修改建议、修复方式
2. **修改后的代码** — 已经过修复的源代码文件内容

Agent 必须首先阅读当前未验证的 DailyProgress 条目作为对照基线：

- 读取 `DailyProgress.md`，找出所有 `- [ ]` 条目
- 读取 `DailyProgress_Chinese.md` 获取中文对照

### 步骤 2：匹配反馈到条目

将反馈文本中描述的问题与 DailyProgress 的具体条目进行匹配：

- 识别反馈中提到的功能模块、文件名、函数名
- 对应到 DailyProgress 中的具体条目
- 列出匹配到的条目编号和内容

若反馈内容无法明确匹配到任何条目，请向用户确认匹配关系。

### 步骤 3：分析问题与教训

对每个被验证的条目，总结：

1. **原始实现**：AI 最初做了什么（从 DailyProgress 条目 + 反馈推断）
2. **发现的问题**：另一 LLM 发现了什么功能完整性问题
3. **根因分析**：为什么会出错（API 理解偏差、边界条件遗漏、平台差异、设计缺陷等）
4. **修复方式**：如何修复的
5. **教训**：未来写类似代码时应注意什么 → 这是核心，必须具体、可操作

### 步骤 4：写入 lessons-learned.md

将教训追加到 `lessons-learned.md`（项目根目录）。

**格式规范：**

```markdown
## YYYY-MM-DD — #<编号>

### 原始条目
<引用 DailyProgress 中对应的英文条目>

### 发现的问题
<另一 LLM 发现的具体问题描述>

### 根因
<为什么会出现这个问题>

### 修复
<如何修复的，简要描述>

### 教训
<未来编码时应注意的关键点，可操作、具体>
```

**规则：**
- 编号从 `#1` 开始递增（读取已有最高编号 +1）
- 日期使用当前日期
- 每条教训一个独立 `##` 区块
- **日期倒序**：最新教训在最前
- 如果同一次验证涉及多个条目且问题根因相同，可合并为一个教训（列出所有关联条目）

### 步骤 5：翻译中文教训

将英文 `lessons-learned.md` 的新增条目翻译为中文，写入 `lessons-learned_Chinese.md`，格式相同。

### 步骤 6：标记已验证条目

在 `DailyProgress.md` 和 `DailyProgress_Chinese.md` 中，将已处理条目从 `- [ ]` 改为 `- [✓]`，同时追加验证依据：

```markdown
- [✓] 原始条目内容 — verified via external LLM review, see lessons-learned.md #<编号>
```

中文版：
```markdown
- [✓] 原始条目内容 — 经外部 LLM 验证，见 lessons-learned.md #<编号>
```

### 步骤 7：输出摘要

向用户汇报：

1. 匹配到的 DailyProgress 条目（编号 + 内容）
2. 新增的教训编号
3. 已验证条目数量和标记状态
4. 是否有未能匹配的反馈内容

## 注意事项

- 如果反馈内容与多条条目相关，逐条分析并在每条后面标注 `see lessons-learned.md #X`
- 同一根因的多条目可合并为一个教训章节
- 教训必须**具体**：避免 "注意边界条件" 这类空泛描述，应写 "`cgltf_accessor_read_float` 不适用于整数类型 attribute，需直接读取 raw buffer 并根据 component_type 选择 uint8_t/uint16_t/uint32_t"
- **禁止自动执行 git commit**
