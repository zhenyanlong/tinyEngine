---
name: todo-normalize-inbox
description: Normalize hand-written raw requirements from the 手动收集 section of tinyEngine todolist.md into structured TODO-prefixed pending items. Use when the user says to organize manual todo notes, clean up todo inbox, turn raw requirements into todos, or invokes $todo-normalize-inbox in this repository.
---

# Todo Normalize Inbox

Use this skill to turn informal notes in `todolist.md` into structured pending TODO items.

## Expected Sections

`todolist.md` should contain:

- `## 手动收集` for raw user-written notes.
- `## 待办` for normalized pending items.
- `## 已完成` for completed items.

## Workflow

1. Read `todolist.md`.
2. Find raw notes under `## 手动收集`.
3. Convert each clear request into one pending item under `## 待办`.
4. Assign stable IDs using the next available `TODO-NNN` number across both pending and completed sections.
5. Format each new item as:

```markdown
- [ ] TODO-NNN 【模块/领域】一句话需求
  - 上下文：从手动收集区整理；补充必要背景
  - 日期：YYYY-MM-DD
  - 来源：手动收集
```

6. Keep the original wording in the context line when useful.
7. If a raw note is too ambiguous to normalize safely, leave it in `## 手动收集` and report what clarification is needed.
8. Remove only the raw notes that were successfully converted. If all raw notes are converted, leave `_（暂无手动条目）_`.

## Rules

- Do not mark new items complete.
- Do not duplicate an existing pending or completed TODO if the raw note clearly refers to the same work.
- Preserve existing TODO IDs and item text unless a small formatting fix is required.
- Do not run git commit, push, merge, rebase, reset, or stash.
