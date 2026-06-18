---
name: session-init
description: Initialize a tinyEngine Codex session from the project-local Trae skills. Use when the user asks to initialize the session, start work, prepare development context, load project context, or explicitly invokes $session-init in this repository.
---

# Session Init

Use this skill to bootstrap a new tinyEngine working session from the existing `.trae` skill set.

## Workflow

1. Confirm the repository root is the tinyEngine project root. Prefer the current working directory when it contains `.trae/skills`, `TDD.md`, and `src/`.
2. Enumerate every Trae skill file under `.trae/skills/*/SKILL.md`. Read all of them before doing project initialization, including any future skills added to that folder.
3. Treat the Trae skill contents as project-local operating instructions for this repository. Preserve constraints from those skills unless they conflict with higher-priority system, developer, or user instructions.
4. Execute the `.trae/skills/session-init/SKILL.md` workflow specifically:
   - Read `TDD.md`.
   - Read `animation-system-plan.md`.
   - Scan the `src/` directory structure.
   - Report the initialization summary requested by the Trae `session-init` skill.
5. If any expected file or directory is missing, mention it in the summary and continue with the available context.

## Notes

- Do not modify project files during session initialization.
- Do not run git write operations during initialization.
- If `.trae/skills/session-init/SKILL.md` is unreadable or absent, report that blocker and summarize the Trae skills that were available.
