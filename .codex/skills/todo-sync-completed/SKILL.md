---
name: todo-sync-completed
description: Update tinyEngine todolist.md by comparing pending TODO items with the current conversation context, TDD.md, and relevant code evidence, then move completed TODO-prefixed items into the completed section. Use when the user asks to sync todolist, update completed todos, mark implemented todo items done, or invokes $todo-sync-completed in this repository.
---

# Todo Sync Completed

Use this skill to keep `todolist.md` aligned with implemented work.

## Workflow

1. Read `todolist.md` first.
2. Read `TDD.md` and use the current conversation context as implementation evidence.
3. For each item under `## 待办`, decide whether it is complete:
   - Prefer concrete code evidence, build/test output, or explicit current-session work.
   - Use `TDD.md` as supporting evidence, not as the only proof if code evidence contradicts it.
   - If evidence is weak or partial, leave the item in `## 待办`.
4. Preserve each item's stable prefix, such as `TODO-001`.
5. Move completed items from `## 待办` to `## 已完成`, changing `[ ]` to `[x]`.
6. Add or update a completion line in the moved item:
   - `- 完成日期：YYYY-MM-DD`
   - `- 完成依据：<short evidence summary>`
7. Keep `## 手动收集` unchanged.
8. If any pending item lacks a `TODO-NNN` prefix, assign the next available ID before syncing.

## Output

Report:

- Completed items moved.
- Pending items left in place and why.
- Any files or evidence used.

Do not run git commit, push, merge, rebase, reset, or stash.
