# AGENTS.md
# Working agreements for Codex (and humans) — Prototype I-2..I-7

## Language
- Always respond in Simplified Chinese unless the user explicitly requests English.
- Keep commands, file paths, and code keywords unchanged (do not translate).

## Project log (mandatory)

### Location
- The project log file is: `docs/project_log.md`
- If it does not exist, create it.
- Always APPEND new content; never rewrite or delete existing log history.

### Language rule (strict)
- All log text written into `docs/project_log.md` MUST be in **Simplified Chinese**, including headings/section titles.
- Commands, file paths, identifiers, enum names, function names, and code snippets MUST remain exactly as-is in English (do not translate).

### When to write (two phases per issue)
For each Issue, you MUST write to the log in two phases:

#### Phase A: Plan Frozen
Trigger: when the user explicitly confirms the plan (e.g., “OK / confirmed / can start coding / start implementing”).
Action:
- Append a new section using the exact template below: “Issue <n>：<shortname>（方案确认）”.
- The section MUST include:
  - initial request summary (from user)
  - additional requirements added later
  - final plan before coding (files/modules, interfaces, invariants/semantics, test plan, acceptance commands)

#### Phase B: Implementation Recap
Trigger: after implementation is done AND tests are green (or user confirms it’s OK).
Action:
- Append “实现复盘” under the same Issue section using the exact template below.
- Include:
  - diff-style file change summary (if you cannot run git, list changed files + what changed)
  - per-file recap (what/why)
  - behavior changes and failure modes (trap, etc.)
  - commands proposed/ran + tests passed
  - known limitations + next steps

### Hard constraints
- Never run `git push` unless the user approves.
- If an issue is cancelled/deferred (no coding), still write a log entry with Status: 取消/延期.

### Status rules (push is NOT required to be "implemented")
- "cancelled/deferred" is ONLY for issues that did NOT proceed to coding, or were abandoned mid-way.
- If coding is done and tests are green but the user has NOT pushed yet:
  - still write "实现复盘"
  - set Status to: 已实现（未推送）
  - set Remote to: 未推送
- If the user later pushes the branch, append a short update under the same Issue section:
  - **状态：** 已推送
  - **远端：** origin/<branch>

### Log entry template (MUST follow exactly; content in Chinese)
Append the following template and fill it:

## Issue <n>：<shortname>（方案确认）
**日期：** <YYYY-MM-DD>  
**分支：** <issue-<n>-<shortname>>  
**状态：** 方案已确认

### 初始需求（用户提出）
- ...
- ...

### 额外补充/优化需求（对话新增）
- ...（如无则写“无”）

### Coding 前最终方案
#### 文件与模块清单
- 新增：
  - ...
- 修改：
  - ...

#### 关键接口/数据结构（签名级）
- ...
- ...

#### 语义/不变量（必须测死，后续不得漂移）
- ...
- ...

#### 测试计划（测试名 + 核心断言点）
- <TestName>：...
- ...

#### 验收命令（仅列出，将由用户批准后执行）
- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
- cmake --build build
- ctest --test-dir build
- <demo run command>

### 实现复盘
**状态：** <已实现｜已实现（未推送）｜已推送｜取消｜延期>  
**提交：** <commit hash｜TBD>  
**远端：** <未推送｜origin/<branch>>

Optional (when user pushes later, append this mini-update under the same Issue section):
#### 推送状态更新
**状态：** 已推送  
**远端：** origin/<branch>

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
- Any shell command (including build/test/git) must be PROPOSED first; user will approve before execution.
- It is allowed to edit files in the working tree and run tests *after* command approval.

## Implementation constraints
- Language: C++17 (stdlib only). Keep external deps = 0.
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
- Always run `ctest` after code changes (once user approves the commands).
- Do not merge incomplete work that breaks tests.

## Workflow: small issues only
We work in small, reviewable issues. For each issue:
1) Read `docs/spec_i2_i7.md` sections relevant to the issue.
2) Propose a plan (files to edit, APIs, tests, demo changes).
3) After user confirms, implement.
4) Propose commands to run (build/test/demo).
5) After tests pass, prepare git branch + commit + push (user must approve commands).

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
- If the spec is unclear: ask a targeted question and point to the exact section/file.
- If design conflicts: `docs/spec_i2_i7.md` wins for this prototype.
- If you want to add a feature not in scope: propose it explicitly as a follow-up issue (do not implement silently).
