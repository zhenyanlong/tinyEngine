---
name: "mark-verified"
description: "将 DailyProgress.md 和 DailyProgress_Chinese.md 中指定条目的验证标记从 [ ] 切换为 [✓]，表示该功能已通过人工验证。当用户说「标记已验证」「mark as verified」「打勾」「verify item X」时调用。"
---

# 标记进度条目为已验证

此 skill 用于手动将 DailyProgress 中的条目从 `[ ]`（未验证）切换为 `[✓]`（已验证）。适用于用户本人已确认某条功能完整性，无需走 verify-fix 教训记录流程的简单场景。

## 适用场景

- 用户说 "mark item 3 as verified" / "标记第5条" / "verify item 6" / "打勾"
- 用户本人或内部团队已确认某功能完整可用
- 无需记录详细教训的简单验证

## 工作流程

### 步骤 1：确认目标条目

用户可通过以下方式指定要标记的条目：

- **按编号**："mark item 3" / "标记第5条" — 当天日期下从上到下第 N 条
- **按内容关键词**："verify the PipelineManager entry" — 匹配描述中包含关键词的条目
- **按日期**："mark all items from 2026-06-17" — 某日期下的全部条目
- **按范围**："verify items 3-7" — 某日期下第 3 到第 7 条

Agent 必须先读取 `DailyProgress.md`，列出当前所有 `- [ ]` 条目供用户选择确认。

### 步骤 2：标记 DailyProgress.md

将指定条目的 `- [ ] ` 替换为 `- [✓] `，在条目末尾追加验证来源：

```markdown
- [✓] Extended Vertex struct with boneIndices and boneWeights — verified manually
```

如果同一条目已在之前被部分标记（如已有关联的 verify-fix 引用），保留已有引用，只修改标记。

### 步骤 3：同步 DailyProgress_Chinese.md

在中文版中找到对应条目，执行同样的标记操作：

```markdown
- [✓] 扩展 Vertex 结构体，新增 boneIndices 和 boneWeights — 已手动验证
```

仅标记英文版中已标记的条目，不标记额外条目。

### 步骤 4：确认完成

汇报：

1. 已标记条目数
2. 剩余未验证条目数
3. 标记了哪些具体条目（日期 + 编号 + 摘要）

## 注意事项

- **禁止自动执行 git commit**
- 用户每次可以标记多条
- 如果某个条目之前已有 `— verified via external LLM review` 后缀，追加 `; manually confirmed` 而非覆盖
- 如果指定的条目已经是 `[✓]`，提示用户无需重复操作
