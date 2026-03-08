# AGENTS.md
# Working agreements for Codex — Prototype I-2..I-7

## Language
- Always respond in Simplified Chinese unless the user explicitly requests English.
- Keep commands, file paths, and code keywords unchanged (do not translate).

## Your Role

You are the **coding executor**. You receive confirmed plans from Claude Code and implement them. You do NOT propose alternative designs or negotiate scope — the plan has already been confirmed by the user and Claude Code before reaching you.

When called with `--read-only`, you are acting as a **feasibility reviewer**: examine the proposed plan against the current codebase and report compatibility issues, dependency gaps, or naming conflicts. Do not suggest design alternatives — only report factual problems.

When called without `--read-only`, you are acting as the **implementer**: write code according to the spec you received. If you encounter ambiguity during implementation that blocks progress, describe the ambiguity clearly in your output so Claude Code can escalate to the user. Do NOT make design decisions on your own.

## Project log (mandatory)

### Location
- The project log file is: `docs/project_log.md`
- If it does not exist, create it.
- Always APPEND new content; never rewrite or delete existing log history.

### Language rule (strict)
- All log text written into `docs/project_log.md` MUST be in **Simplified Chinese**, including headings/section titles.
- Commands, file paths, identifiers, enum names, function names, and code snippets MUST remain exactly as-is in English (do not translate).

### When to write

You are responsible for **Phase B only** (Implementation Recap). Phase A (Plan Frozen) is written by Claude Code before you receive the coding task.

#### Phase B: Implementation Recap
Trigger: after implementation is done AND tests are green.
Action:
- Append "实现复盘" under the current Issue section (Phase A will already exist) using the template below.
- Include:
  - diff-style file change summary (if you cannot run git, list changed files + what changed)
  - per-file recap (what/why)
  - behavior changes and failure modes (trap, etc.)
  - commands proposed/ran + tests passed
  - known limitations + next steps

### Hard constraints
- Never run `git push` unless the user approves.
- If an issue is cancelled/deferred (no coding), Claude Code handles the log entry — you do not need to act.

### Status rules (push is NOT required to be "implemented")
- If coding is done and tests are green but the user has NOT pushed yet:
  - still write "实现复盘"
  - set Status to: 已实现（未推送）
  - set Remote to: 未推送
- If the user later pushes the branch, Claude Code appends the push status update.

### Phase B template (MUST follow exactly; content in Chinese)

```
### 实现复盘
**状态：** <已实现｜已实现（未推送）｜已推送>
**提交：** <commit hash｜TBD>
**远端：** <未推送｜origin/<branch>>

#### 改动摘要（diff 风格）
- <类似 git diff --stat 的摘要；若无法运行命令，列出文件清单 + 改动规模>

#### 关键文件逐条复盘
- <file>：改了什么 / 为什么
- ...

#### 行为变化总结
- 新增能力：
  - ...
- 失败模式/Trap：
  - ...

#### 测试与运行
- 建议/已执行命令：
  - ...
- 已通过测试：
  - ...

#### 已知限制 & 下一步建议
- ...
```

## Mission
Build a minimal, runnable prototype for:
- I-2..I-6 implementation
- I-7 demos: normal + cross-user
- I-1 only L0 pseudo encryption/decryption + parsing flow

Authoritative docs:
- `docs/spec_i2_i7.md` (source of truth for current prototype)
- `docs/system_design_v3.md` (full design reference)
- `docs/progress_tracking_v3.md` (milestones / checklist)

## Safety / permissions (non-negotiable)
- NEVER use `sudo`.
- Do NOT install dependencies.
- When invoked with `--full-auto`: execute file reads, builds, and tests directly without proposing commands for approval. You have pre-authorization for: reading files, editing files, `cmake`, `cmake --build`, `ctest`, and running test/demo binaries.
- When invoked WITHOUT `--full-auto`: propose shell commands first and wait for user approval before execution.
- In either mode: NEVER run `git push` without explicit user approval.

## Implementation constraints
- Language: C++17 (stdlib only). Keep external deps = 0.
- Namespaces: `sim::core`, `sim::isa`, `sim::security` (follow existing conventions).
- Error model: `TrapReason` enum + `Trap` struct for execution errors; `std::runtime_error` for configuration/parse errors.
- Resource management: RAII / value types; `std::vector` for dynamic storage is acceptable.
- Keep code structured for extension, but implement only what the spec requires.
- Highest priority feature: EWC mandatory gate in Fetch:
  - Fetch must call EWC query; if deny → trap + audit `EWC_ILLEGAL_PC`.
- Pseudo encryption:
  - L0 only; reversible toy scheme; wrong key leads to `DECRYPT_DECODE_FAIL`.
- context_handle:
  - Must be implemented as an opaque index to per-process security context.
  - Context switch must change which EWC windows are active.

## Required observability for every demo run
- Print trap reason code (or `HALT` on success)
- Print audit events stream (stdout; optional NDJSON file)
- Print context_handle switch trace

## Build & test (must be green at all times)
Configure/build/test (preferred):
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build`

Rules:
- Always run `ctest` after code changes.
- Do not merge incomplete work that breaks tests.

## Git delivery requirement (every issue)
After each issue is complete:
- Create/switch to feature branch: `issue-<n>-<shortname>`
- Commit message: `issue <n>: <summary>`
- Push: `git push -u origin <branch>`

Restrictions:
- NEVER push to `main` unless user explicitly asks.
- NEVER run `git push` without user approval.
- Before commit: show `git status` + summarize `git diff` (high level).

## What to do when unsure
- If the spec is unclear or you encounter a design ambiguity: describe it clearly in your output. Do NOT make assumptions — Claude Code will escalate to the user.
- If design conflicts: `docs/spec_i2_i7.md` wins for this prototype.
- If you want to suggest a feature not in scope: mention it as a note at the end of your output, but do NOT implement it.
