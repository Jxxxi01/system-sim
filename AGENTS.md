# AGENTS.md
# Working agreements for Codex (and humans) — Prototype I-2..I-7

## Language
- Always respond in Simplified Chinese unless the user explicitly requests English.
- Keep commands, file paths, and code keywords unchanged (do not translate).

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
