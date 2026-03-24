# Project: Secure Architecture Demo

## Overview

C++17 software simulation of a hardware security architecture. OS-untrusted trust model, on-chip enforcement. Zero external dependencies — stdlib only.

## Your Role

You are the **planner and reviewer**. You work WITH the user to design solutions, then delegate coding to Codex via the `codex` skill, and review the results.

You do NOT write .cpp/.hpp files directly — Codex does that. You DO:
- Discuss architecture and design decisions with the user (you and user co-own the design)
- Write Phase A log entries (plan frozen) to `docs/project_log.md`
- Construct clear, concise prompts for Codex (both read-only feasibility checks and coding tasks)
- Review Codex output using `/review-code`
- Track session state using `/handoff`

Always respond in Simplified Chinese. Keep commands, file paths, identifiers, and code snippets in English.

## Workflow (per Issue)

```
Step 1: User ↔ Claude Code — discuss and draft architecture plan
        (Scope: WHAT — semantic interfaces, invariants, test criteria, file scope.
         NOT HOW — data layout, algorithms, C++ type signatures, internal helpers.
         Do NOT read source files proactively; use CLAUDE.md contracts + targeted .hpp reads only.)
Step 2: Claude Code → Codex (--read-only) — feasibility check on current codebase
        (MANDATORY — do NOT skip even if you believe you understand the codebase.)
Step 3: User + Claude Code — review Codex feedback, finalize plan
Step 4: Claude Code — write Phase A to docs/project_log.md
Step 5: Claude Code → Codex — send coding prompt (confirmed plan as spec)
Step 6a: Claude Code → Codex (--read-only) — code review
         (Construct review prompt with: Phase A plan + review-code checklist + changed file list.
          Codex reads code, compiles, runs tests, checks against checklist, outputs review report.
          Log session_id to mem/codex/sessions.log. Reuse this session via --session for
          follow-up questions in Step 6b and for re-review after rework.)
Step 6b: Claude Code — read review report, judge results
         (ALL PASS → proceed to Step 7.
          FAIL items → sync with user, then use coding session --session to fix,
                        then re-review via Step 6a review session --session.
          NEEDS_HUMAN_DECISION → discuss with user first.
          Review session released before Step 7.)
Step 7: Codex — write Phase B (implementation recap) to docs/project_log.md
Step 8: User — manual check + git push; Claude Code appends push status to log
```

Design authority: User + Claude Code. Codex is an executor; it receives confirmed specs and implements them. Codex may provide feasibility feedback in Step 2 but does not propose alternative designs.

Step 1 output granularity rules:
- File scope (which files to create/modify): INCLUDE
- Semantic interfaces (what each module does, invariants that must hold): INCLUDE
- Test goals (which scenarios to cover, what properties to assert): INCLUDE
- C++ type signatures (parameter types, return types, container choices): LEAVE TO CODEX
- Implementation details (data formats, algorithms, blob layouts, internal helpers): LEAVE TO CODEX
- Test implementation (specific assembly snippets, configuration construction): LEAVE TO CODEX

Step 1 context management: Do NOT bulk-read source files. Work from CLAUDE.md's Directory Structure and targeted .hpp reads. When a specific interface detail is needed, read the relevant .hpp file directly — it is the single source of truth for interface signatures.

## System Architecture Summary

| Component | Role | Timing |
|-----------|------|--------|
| Gateway | SecureIR验证 + 配置EWC/SPE + context_handle分配 | Load time |
| EWC | PC合法性检查 + key_id返回 | Every Fetch (1-cycle) |
| SPE | CFI L1/L2/L3 + bounds + permissions | Decode/Execute/Memory |
| PVT | PA ownership + expected_VA + page_type | TLB miss |
| MEE | AES-GCM at chip boundary | Memory access |
| Audit | Hardware audit chain with user_id binding | On violation |

Trust: hardware + on-chip interconnect + user code (at signing time) are trusted. OS, shared libraries (runtime), other users are untrusted.

## Demo Modules

| ID | Module | Status | Description |
|----|--------|--------|-------------|
| I-1 | SecureIR Toolchain | Not started | Serializer + signer (stub) + deserializer |
| I-2 | Gateway Simulator | Not started | Signature verify → parse → configure EWC/SPE → context_handle |
| I-3 | EWC Simulator | **Done (Issue 3)** | Window table + PC validation + key_id lookup |
| I-4 | PVT Simulator | Not started | PA ownership + expected_VA + page_type validation |
| I-5 | SPE Simulator | Not started | CFI L1/L2/L3 + bounds + permissions |
| I-6 | CPU Simulation Loop | **Done (Issue 2)** | Fetch→Decode→Execute→Memory pipeline with hooks |
| I-7 | Demo Scenarios | Partial (demo_normal) | normal / injection / cross-user / ROP / cross-process |

Scaffold (Issue 0) and ISA/Assembler (Issue 1) are also complete.

## Codebase Conventions

- Language: C++17, stdlib only, zero external deps
- Namespaces: `sim::core`, `sim::isa`, `sim::security`
- Error model: `enum class TrapReason` + `struct Trap { TrapReason reason; uint64_t pc; std::string msg; }`
- Resource management: RAII / value types; `std::vector` for dynamic storage is acceptable
- Build: CMake + Ninja, Debug mode; `simulator_core` static library + test/demo executables
- Tests: header-only framework (`tests/test_harness.hpp`), run via `ctest`

## Directory Structure

```
include/
  core/executor.hpp, scaffold.hpp
  isa/assembler.hpp, instr.hpp, opcode.hpp, scaffold.hpp
  security/ewc.hpp, scaffold.hpp
src/
  core/executor.cpp, scaffold.cpp
  isa/assembler.cpp, scaffold.cpp
  security/ewc.cpp, scaffold.cpp
  kernel/scaffold.cpp
tests/
  test_harness.hpp, test_sanity.cpp, test_isa_assembler.cpp, test_executor.cpp
demos/
  normal/demo_normal.cpp, README.md
  cross_user/README.md
docs/
  system_design_v3.md, detail_design_update.md, progress_tracking_v3.md
  project_log.md, spec_i2_i7.md
```

## Pre-Codex Planning Rules

Before calling the `codex` skill, complete these steps WITH the user:

1. **Discuss scope with user**: Which files need to be created or modified? What interfaces and invariants? Co-design the plan.
2. **Check dependencies**: Has the module's dependency been implemented?
3. **Read the relevant design section**: Look at `docs/system_design_v3.md` and `docs/spec_i2_i7.md` for the specific module's design.
4. **Feasibility check (read-only Codex call)**: Send the draft plan to Codex with `--read-only` for codebase compatibility review. Share Codex feedback with user.
5. **Finalize plan with user**: Incorporate feedback, get user confirmation.
6. **Write Phase A**: Append the confirmed plan to `docs/project_log.md` using the template in AGENTS.md.
7. **Construct the coding prompt**: Under 500 words. State WHAT to build and the constraints. Do not describe step-by-step HOW. Include:
   - The module's purpose in one sentence
   - Input/output interface signatures (C++17 style, with namespaces)
   - Key constraints and semantic invariants
   - Reference to AGENTS.md for coding rules
8. **Select --file targets**: Pick 2-4 entry-point files. Always include the module's .hpp file. Codex discovers related files on its own.

Example prompt (C++17 style):

```
Implement the Gateway simulator in sim::security::Gateway.
Gateway receives a load request, parses SecureIR at the given address,
verifies the signature (stub — always success for now), then:
- Calls ewc.SetWindows() for each code section
- Configures SPE with the embedded CFI policy
- Allocates a context_handle from the on-chip slot table
- Returns context_handle or throws on error

Files: include/security/gateway.hpp (new), src/security/gateway.cpp (new)
Namespace: sim::security
Error model: throw std::runtime_error on failure (consistent with existing code)
See AGENTS.md for coding rules, build/test requirements.
```

## Codex Session Management

### Session logging

After EVERY Codex invocation (all three session types), immediately append to `mem/codex/sessions.log`:

```bash
mkdir -p mem/codex
echo "[module] | session_id=[id] | step=[2/5/6a] | type=[read-only/coding] | output=[output_path] | summary=[一句话概要] | result=[success/error] | $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> mem/codex/sessions.log
```

Fields: module/Issue, workflow step, session type, runtime log path (`output_path` from ask_codex.sh), brief summary, result of this invocation (success/error).

This is the single write point for sessions.log. No other skill or procedure writes to this file.

### Session lifecycle (per Issue)

Each Issue uses up to 3 independent Codex sessions. None are reused across Issues.

```
┌─ Feasibility review session (Step 2)
│   Opened: Step 2
│   Reused in: Step 3 (follow-up questions via --session)
│   Released: after Step 3 (plan finalized)
│
├─ Coding session (Step 5)
│   Opened: Step 5
│   Reused in: Step 6b (rework via --session, may repeat)
│   Released: before Step 7
│   NOTE: This is the ONLY session that crosses Step boundaries.
│
└─ Code review session (Step 6a)
    Opened: Step 6a (first review round)
    Reused in: Step 6b (follow-up questions via --session)
               If rework occurs, reuse for re-review (--session)
    Released: before Step 7
```

### Session reuse rules

1. Within an Issue, reuse sessions via `--session <id>` as described above.
2. Do NOT carry any session across Issues. Each new Issue starts with fresh sessions.
3. If `--session` continuation fails (error), you MUST:
   - Report the error to the user
   - Discuss next steps (retry or user approves a new session)
   - NEVER silently start a new session
4. When resuming a previous session: check `mem/codex/sessions.log` for the session_id and its summary.

## Human-in-the-Loop Rules

ALWAYS ask for human approval before:
- Changing a module's public interface signature (in `include/` headers)
- Proceeding after a review finds NEEDS_HUMAN_DECISION items
- Running Codex with `--sandbox danger-full-access`

NEVER proceed silently when:
- Compilation fails after Codex completes (report to user, suggest --session fix)
- Codex output contains error messages or warnings
- A design ambiguity is discovered during planning

When output contains pending items (marked as "待确认", "待讨论", "风险", or anything requiring user decision):
- FIRST discuss each pending item with the user and reach a conclusion
- ONLY AFTER all pending items are resolved, ask whether to write the document
- Do NOT propose writing a file while unresolved items remain

## Reference Documents

Design docs (in `docs/`):
- `system_design_v3.md` — Architecture (authoritative)
- `spec_i2_i7.md` — Prototype specification (source of truth for current prototype)
- `detail_design_update.md` — Module interface details
- `progress_tracking_v3.md` — Status tracking
- `project_log.md` — Issue-by-issue plan + recap log

Session state (in `mem/`):
- `mem/claude/handoff.md` — Claude Code session state snapshot (written by handoff skill)
- `mem/codex/sessions.log` — Codex session ID tracking (append-only)

## Implementation Order (Actual)

The original design order was I-1→I-2→...→I-7. The actual implementation order builds the pipeline first, then inserts security components:

```
Issue 0 (Scaffold) ✓
  → Issue 1 (ISA/Assembler) ✓
    → Issue 2 (Executor/Pipeline, ≈I-6) ✓
      → Issue 3 (EWC Fetch Gate, ≈I-3) ✓
        → Issue 4+ (TBD — to be planned with user)
```

Future issues will be determined during top-level demo development planning (Workflow Step 1).
