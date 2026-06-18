---
name: verify-implementation
description: Audit tinyEngine implementation completeness and potential bugs by comparing DailyProgress.md unchecked items, animation-system-plan.md, multimodel-import-plan.md, TDD.md, and current source evidence. Use when the user asks to verify plan/progress items, validate whether features are correctly and completely implemented, review unchecked DailyProgress work, audit animation or multi-model import functionality, or find bugs/regressions in claimed tinyEngine features.
---

# Verify Implementation

Use this skill to perform an evidence-based implementation audit for tinyEngine. Treat `DailyProgress.md` unchecked items as unverified claims, not facts, and compare them against code, build output, runtime behavior where feasible, and the project plans.

## Quick Start

1. Confirm the repository root contains `src/`, `TDD.md`, `DailyProgress.md`, `animation-system-plan.md`, and `multimodel-import-plan.md`.
2. Run the target extractor from the repository root:

```powershell
python .codex\skills\verify-implementation\scripts\extract_audit_targets.py
```

Use `--all` when checked plan items also need re-audit, and `--json` when a structured list is useful.

3. Read the relevant source files for each target before making claims.
4. Build or run the narrowest available verification command that can prove or disprove the claim.
5. Report findings first, with file/line evidence and residual test gaps.

## Audit Inputs

Always consider these files:

- `DailyProgress.md`: unchecked `- [ ]` items are the highest-priority audit queue. A checked item can still be re-audited if code evidence contradicts it.
- `animation-system-plan.md`: use task IDs such as `A3-4` or `A4-2` to define expected behavior, dependencies, and acceptance criteria.
- `multimodel-import-plan.md`: use `S0*` tasks and the known thumbnail issue as multi-model import acceptance criteria.
- `TDD.md`: use as architecture documentation and supporting context, not as proof by itself.
- `src/`, `res/`, `CMakeLists.txt`: use as implementation evidence.

## Workflow

1. Extract audit targets.
   - Prefer the helper script for checkbox discovery.
   - Prioritize `DailyProgress.md` unchecked items, then unchecked plan tasks in the requested subsystem.
   - If the user names a specific feature, restrict scope to matching progress and plan entries.

2. Build an evidence map.
   - Search by class, method, shader, task ID, and file names from the target.
   - Read all directly relevant `.hpp`, `.cpp`, shader, CMake, and asset files.
   - Map each claim to concrete evidence: declarations, implementations, call sites, resource lifetime handling, descriptor/pipeline layout compatibility, serialization paths, and UI entry points.

3. Verify behavior.
   - Run the narrowest build/test command available for the project.
   - For CMake projects, prefer existing presets before inventing commands.
   - If runtime verification is needed but impractical, state the gap and validate as much as possible through compile, static code inspection, asset checks, and acceptance-criteria tracing.
   - Do not mark `DailyProgress.md` or plan checkboxes complete unless the user explicitly asks.

4. Look for bugs, not just missing files.
   - Check initialization order, swapchain recreation, descriptor set layout compatibility, pipeline layout selection, resource destruction, stale caches, per-swapchain-image buffers, bounds checks, null/empty asset handling, and path normalization.
   - For skeletal animation, check vertex layout separation, JOINTS/WEIGHTS component types, weight normalization, bone-count limits, fallback bone matrices, and whether per-frame updates actually reach the skinned material.
   - For multi-model import, check duplicate material caching, sub-material slot handling, scene save/load round trips, drag/drop lifecycle, pick IDs, thumbnail rendering state, and Material-type assets that should not instantiate models.

5. Classify every audited target.
   - `Verified`: code and build/runtime evidence support the claim.
   - `Partial`: core pieces exist but acceptance criteria or integration is incomplete.
   - `Missing`: expected implementation is absent.
   - `Bug`: implementation exists but has a likely defect.
   - `Blocked`: verification requires unavailable runtime assets, tools, or user action.

## Reporting

Use a code-review style. Findings come first, ordered by severity.

For each finding include:

- Severity: `P0` crash/data loss, `P1` feature broken, `P2` correctness edge case, `P3` maintainability/test gap.
- Evidence: clickable file references with line numbers.
- Expected behavior from the plan/progress item.
- Actual behavior or risk.
- Suggested fix or next verification step.

Then include:

- Audited targets summary with status counts.
- Commands run and whether they passed.
- Items left unverified and why.

Do not commit, push, merge, rebase, reset, or stash. Do not rewrite progress or plan documents unless the user asks for synchronization after the audit.
