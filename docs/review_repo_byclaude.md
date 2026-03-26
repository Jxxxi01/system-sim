# Prototype Code Review Report

> Reviewer: Claude Opus 4.6
> Date: 2026-03-26
> Scope: Full repository vs. Demo_Claim_Boundary_v3.1 / dev_plan / system_design_v4 / spec_i2_i7
> Policy: Read-only review. No file modifications.

---

## 0. Executive Summary

8/8 unit tests pass, 5/5 demos run correctly. dev_plan.md Issues 4-11 all implemented. Code architecture clean, module boundaries clear.

Key findings:

| Category | Count | Severity |
|----------|-------|----------|
| Claim evidence gap | 2 | High (affects paper claim strength) |
| Document out-of-date | 1 | High (Demo_Claim_Boundary_v3.1.md labels stale) |
| Missing baselines | 2 | Medium (B1-a, B1-b not implemented) |
| Audit event defect | 2 | Medium (user_id=0 in two event types) |
| Code quality | 5 | Low |

---

## 1. Claim-by-Claim Verification

### C1: Control-path existence

> "demo 证明：从 gateway_load 到 secure_context_switch 再到 Fetch → EWC → Decrypt → Decode → Execute 的最小控制路径是可执行的。"

**Verdict: PASS**

The complete path can be traced in code:

```
SecureIrBuilder::Build()             securir_builder.cpp:165
  -> Gateway::Load()                 gateway.cpp:406
       -> EwcTable::SetWindows()     gateway.cpp:448
       -> SpeTable::ConfigurePolicy  gateway.cpp:449
       -> StoreCodeRegion            gateway.cpp:451
  -> KernelProcessTable::LoadProcess process.cpp:24
       -> PvtTable::RegisterPage     process.cpp:34
  -> KernelProcessTable::ContextSwitch
       -> hardware.SetActiveHandle   process.cpp:98
       -> audit CTX_SWITCH           process.cpp:100

ExecuteProgram(entry_pc, options)    executor.cpp:232
  -> GetActiveHandle                 executor.cpp:242
  -> GetCodeRegion                   executor.cpp:250
  main loop:
    -> FetchStage                    executor.cpp:295
         -> EwcTable::Query          executor.cpp:128
         -> DeserializeCipherUnit    executor.cpp:171
    -> DecodeStage                   executor.cpp:307
         -> DecryptInstr             executor.cpp:180  (key_check + tag + XOR)
    -> instruction dispatch          executor.cpp:328-450
    -> SpeTable::CheckInstruction    executor.cpp:457
    -> commit PC                     executor.cpp:465
```

Evidence in demo output (`demo_normal` Case A):
```
AUDIT seq_no=1 type=GATEWAY_LOAD_OK ...
AUDIT seq_no=2 type=CTX_SWITCH ...
FINAL_REASON=HALT
```

This demonstrates the full pipeline: load -> context switch -> fetch/decode/execute -> HALT.

---

### C2: Illegal execution entry is structurally intercepted

> "非法 PC 进入不会退化为'随机崩溃'或'解密垃圾后失败'，而是在 Fetch 阶段先经过 EWC 的显式检查。"

**Verdict: PASS**

The enforcement order in `FetchStage` (`executor.cpp:124-173`) is:

```
line 128: ewc.Query(pc, context_handle)       // EWC check FIRST
line 129: if (!query_result.allow) -> trap     // deny BEFORE any memory access
line 135: if (pc < region_base_va) -> trap     // bounds checks come SECOND
line 142: alignment check                      // then alignment
line 160: code_memory bounds check             // then code memory access
```

This ordering ensures that an illegal PC is caught by EWC **before** the pipeline touches code memory. The trap reason is a distinct `TrapReason::EWC_ILLEGAL_PC` (not a generic crash), matching the claim's requirement for "明确 trap reason."

Evidence in demo output (`demo_cross_user` Case B):
```
FINAL_REASON=EWC_ILLEGAL_PC     // distinct trap, not random crash
FINAL_PC=4096                   // exact PC recorded
AUDIT ... type=EWC_ILLEGAL_PC   // event emitted
```

Contrast with what would happen without EWC: if the pipeline directly accessed code_memory using the illegal PC, the behavior would be **undefined** (could read garbage, decode random bytes, or crash at an unrelated point). The EWC check makes the failure **deterministic and named**.

---

### C3: Context switch changes effective execution semantics

> "context_handle 不是装饰性变量，而是会真实改变 active execution windows，进而影响后续 EWC 判定结果。"

**Verdict: PASS**

The mechanism chain is:

1. `KernelProcessTable::ContextSwitch` sets `hardware.SetActiveHandle(handle)` (`process.cpp:98`)
2. `ExecuteProgram` reads `GetActiveHandle()` (`executor.cpp:242`) and passes it to `FetchStage`
3. `FetchStage` calls `ewc.Query(pc, context_handle)` (`executor.cpp:128`)
4. `EwcTable::Query` looks up windows **by context_handle** (`ewc.cpp:44`):
   ```cpp
   auto it = windows_by_context_.find(context_handle);
   if (it == windows_by_context_.end()) {
       return result;  // allow=false, no windows for this context
   }
   ```

This means the same PC produces **different EWC results** depending on which context is active. `demo_cross_user` directly demonstrates this:

| Scenario | Active context | Entry PC | Result |
|----------|---------------|----------|--------|
| Case A | Alice (handle=1) | 0x1000 (Alice's code) | HALT (allowed) |
| Case C | Bob (handle=2) | 0x1000 (Alice's code) | EWC_ILLEGAL_PC (denied) |

Same PC, same code memory content, different context -> different outcome. This proves `context_handle` is load-bearing.

---

### C4: Key security events are observable

> "系统不会只在内部阻断 violation，而是会输出与 contract 对应的可观测事件。"

**Verdict: PARTIAL PASS -- one defect**

All claimed event types exist and are emitted:

| Event type | Emission point | user_id correct? |
|------------|---------------|-----------------|
| `GATEWAY_LOAD_OK` | `gateway.cpp:453` | Yes (`secure_ir.user_id`) |
| `GATEWAY_LOAD_FAIL` | `gateway.cpp:466` | No (hardcoded 0) |
| `GATEWAY_RELEASE` | `gateway.cpp:481` | Yes (from `handle_to_user_`) |
| `CTX_SWITCH` | `process.cpp:100` | Yes (`process->user_id`) |
| `EWC_ILLEGAL_PC` | `executor.cpp:298` | **No (hardcoded 0)** |
| `DECRYPT_DECODE_FAIL` | `executor.cpp:309` | Yes (`fetched.owner_user_id`) |
| `PVT_MISMATCH` | `pvt.cpp:54` | **No (from unmatched query = 0)** |
| `SPE_VIOLATION` | `spe.cpp:144` | Yes (`policy.user_id`) |

**Defect: EWC_ILLEGAL_PC has user_id=0**

```cpp
// executor.cpp:298
LogAudit(audit, "EWC_ILLEGAL_PC", 0, context_handle, fetch_trap.pc, ...);
//                                ^-- hardcoded zero
```

When EWC denies a PC, the pipeline hasn't matched any window, so there's no `owner_user_id` from the query result. But the **active context's user_id** is known (it was set during `ContextSwitch`). The code simply doesn't have access to this mapping.

Observable in demo output:
```
AUDIT seq_no=5 type=EWC_ILLEGAL_PC user_id=0 context_handle=2 pc=4096 ...
//                                 ^^^^^^^^^ should be 1002 (Bob's user_id)
```

The `context_handle=2` is present, so the user can be traced **indirectly**. But the claim says "用户上下文与 violation 能被对应起来" -- a user_id=0 in the event itself weakens this.

**Same issue in PVT_MISMATCH**: `pvt.cpp:54` uses `query_result.owner_user_id` which is 0 when no window matched.

**Root cause**: `SecurityHardware` does not maintain a `handle -> user_id` mapping. The `Gateway` has this (`handle_to_user_`), but the Executor and PVT don't have access to it.

**Suggested fix**: Add a `user_id` field to either `SecurityHardware`'s per-handle state, or pass user_id through the Executor's context so it's available at audit-emission time.

---

### C5: Minimal attack-driven narratives

> "当前 demo 支撑的攻击叙事均锚定到公认攻击原语。"

**Verdict: PASS (but document labels are stale)**

All five narratives now have runnable demos:

#### (a) Normal execution -- `demo_normal` Case A

```
LI x1, 10 -> LI x2, 32 -> ADD x3, x1, x2 -> SYSCALL 1 -> HALT
```
Result: `FINAL_REASON=HALT`. Complete happy-path.

#### (b) Cross-user illegal execution -- `demo_cross_user` Cases B/C

Case B: Bob's program jumps to Alice's VA (`J` with negative offset to 0x1000):
```cpp
// demo_cross_user.cpp:43-45
bob_source << "J "
           << (alice_base_va - (bob_base_va + 2 * kInstrBytes))  // PC-relative to Alice
           << '\n';
```
Result: `EWC_ILLEGAL_PC`. Bob's context has no window covering 0x1000.

Case C: Malicious OS sets Bob's entry_pc to Alice's base_va directly:
```cpp
// demo_cross_user.cpp:90
run_case("CASE_C_BOB_MALICIOUS_OS", bob_handle, alice_base_va);
```
Result: `EWC_ILLEGAL_PC`. Same mechanism -- EWC doesn't care how the PC got there.

#### (c) Code injection (ciphertext tampering) -- `demo_injection`

Case B -- full XOR tamper (`demo_injection.cpp:33-43`):
```cpp
void XorWholeCiphertext(..., std::uint8_t mask) {
    for (std::uint8_t& byte : region->code_memory) {
        byte ^= mask;  // flip every byte in code memory
    }
}
```
Result: `DECRYPT_DECODE_FAIL` with `reason=key_check_mismatch` (key_check field corrupted).

Case C -- partial payload tamper (`demo_injection.cpp:45-61`):
```cpp
void XorPayloadBytes(..., std::size_t instr_index, std::uint8_t mask) {
    // XOR only the payload bytes of instruction at instr_index
    for (std::size_t i = unit_offset; i < payload_end; ++i) {
        region->code_memory[i] ^= mask;
    }
}
```
Result: `DECRYPT_DECODE_FAIL` with `reason=tag_mismatch` (payload corrupted but key_check intact).

This distinction is important: the code correctly separates **key mismatch** (wrong decryption key) from **integrity violation** (content tampered). Both produce `DECRYPT_DECODE_FAIL` but with different `reason` detail, matching the two-stage verification in `DecryptInstr` (`code_codec.cpp:215-240`):
```
Step 1: key_check verification    (line 218) -> "key_check_mismatch"
Step 2: tag verification          (line 223) -> "tag_mismatch"
Step 3: XOR decrypt + magic check (line 228-234) -> "decode_mismatch"
```

#### (d) PVT mismatch -- `demo_cross_process` Case B

```cpp
// demo_cross_process.cpp:122-123
// Bob tries to register Alice's page in his context
hardware.GetPvtTable().RegisterPage(bob_handle, alice_base_va, PvtPageType::CODE);
```

PVT checks EWC: is there a window for `alice_base_va` under `bob_handle`? No -> `PVT_MISMATCH`.

Evidence:
```
REGISTER_PAGE_OK=false
REGISTER_PAGE_ERROR=reason=missing_window context_handle=2 va=4096 ...
AUDIT ... type=PVT_MISMATCH ...
```

**Note**: This only demonstrates `missing_window` (no EWC window for that VA in Bob's context). It does **not** demonstrate owner mismatch or VA alias checking, because the 1:1 VA=PA mapping makes those checks trivially pass. This limitation is acknowledged in Demo_Claim_Boundary_v3.1 Section 6 ("1:1 VA=PA identity mapping ... 无法演示 PVT 反 alias 检查").

#### (e) ROP / CFI violation -- `demo_rop`

Case A -- L3 CFI with valid CALL target:
```asm
main:
  CALL func     ; target = func (in call_targets whitelist)
  HALT
func:
  RET            ; returns to HALT (matches shadow stack)
```
Result: `HALT` (all CFI checks pass).

Case B -- L3 CFI with corrupted return address:
```cpp
// demo_rop.cpp:31-41 -- MakeRopSource
oss << "func:\n"
    << "  LI x1, " << bad_addr << '\n'  // corrupt link register
    << "  RET\n"                         // return to corrupted address
```
The CALL at `main` pushes `return_addr` onto SPE's shadow stack. Inside `func`, `LI x1, bad_addr` overwrites the link register. When `RET` executes, it reads x1 -> `committed_pc = bad_addr`, but the shadow stack expects `return_addr` -> mismatch.

SPE catches this at `spe.cpp:131-135`:
```cpp
const std::uint64_t expected_pc = policy.shadow_stack.back();
policy.shadow_stack.pop_back();
if (expected_pc != committed_pc) {  // shadow stack says one thing, x1 says another
    result = MakeViolationDetail("execute", op, "shadow_stack_mismatch", ...);
}
```

Result: `SPE_VIOLATION` with `reason=shadow_stack_mismatch`.

Cases C1/C2 show the multi-layer defense: with L1 CFI (no shadow stack), EWC is the last line of defense. If the ROP target is in-window (C1) -> succeeds; if out-of-window (C2) -> `EWC_ILLEGAL_PC`.

---

## 2. Demo_Claim_Boundary_v3.1 Staleness

The document was written when Issues 4/7/8 were not yet implemented. They are now complete. The following items need label updates:

### Section 5: Attack coverage table

| Row | Current label | Should be | Reason |
|-----|--------------|-----------|--------|
| "代码页篡改" | [Scaffolded] | **[Current]** | `demo_injection` runs both full and partial tamper |
| "PVT owner/VA inconsistency" | [Scaffolded] | **[Current] (partial)** | `demo_cross_process` Case B shows missing_window; anti-alias still not demonstrable |
| "ROP / CFI 违规" | [Scaffolded] | **[Current]** | `demo_rop` shows L3 shadow stack + L1 EWC fallback |

### Footnotes ¹²³

All three footnotes reference incomplete Issues:
- ¹ "依赖 Issue 4（pseudo decrypt）完成后可演示" -> **Issue 4 done, demo_injection exists**
- ² "依赖 Issue 7（PVT）完成后可演示" -> **Issue 7 done, demo_cross_process exists**
- ³ "依赖 Issue 8（SPE）完成后可演示" -> **Issue 8 done, demo_rop exists**

### Section 7: Attack coverage matrix (Tables A and B)

Evidence labels in the rightmost column should be updated from [Scaffolded] to [Current] for the three rows above.

### Section 9: Acceptance criteria

A3 and A4 are met (see Section 1 above). Their dependency notes should be updated.

### Section 11: Recommended experiment organization (G1 and G2)

Several [Scaffolded] items are now [Current]:
- "correct key / wrong key decryption behavior" -> [Current] (`demo_normal` Case B)
- "PVT consistency checks" -> [Current] (`demo_cross_process` Case B + `test_pvt.cpp`)
- "代码页篡改" -> [Current]
- "PVT owner/VA inconsistency" -> [Current] (partial)
- "ROP / CFI violation" -> [Current]

---

## 3. Evaluation Units (Section 8 of v3.1)

### E1: Path correctness -- PASS

| Path | Demo evidence |
|------|--------------|
| allow path | `demo_normal` Case A -> HALT |
| deny path (EWC) | `demo_cross_user` Case B -> EWC_ILLEGAL_PC |
| decode-fail path | `demo_normal` Case B / `demo_injection` Cases B,C -> DECRYPT_DECODE_FAIL |
| pvt-mismatch path | `demo_cross_process` Case B -> PVT_MISMATCH |
| spe-violation path | `demo_rop` Case B -> SPE_VIOLATION |

All five paths are exercised.

### E2: Violation-to-trap correctness -- PASS

Each violation maps to a unique `TrapReason` value (defined in `executor.hpp:14-25`):
- Unauthorized execution -> `EWC_ILLEGAL_PC`
- Integrity failure -> `DECRYPT_DECODE_FAIL`
- Page validation failure -> `PVT_MISMATCH`
- CFI violation -> `SPE_VIOLATION`

None collapse to a generic "error" or "crash."

### E3: Violation-to-event correctness -- PARTIAL

Each violation produces an audit event with `context_handle` and `pc`. But `user_id` is missing in two event types (see C4 analysis above).

The `detail` field in each event carries structured key=value data:
- EWC: `window_id=none`
- Decrypt: `key_id=11 reason=tag_mismatch`
- PVT: `reason=missing_window context_handle=2 va=4096 ...`
- SPE: `stage=execute op=RET reason=shadow_stack_mismatch cfi_level=3 ...`

### E4: Context-sensitive enforcement -- PASS

`demo_cross_user` Cases A vs C directly demonstrate: same PC (0x1000), different context (Alice vs Bob), different result (HALT vs EWC_ILLEGAL_PC).

---

## 4. Missing Baselines

### B1-a: No-enforcement baseline -- NOT IMPLEMENTED

Demo_Claim_Boundary_v3.1 Section 10 requires:

> "关闭全部安全检查，仅保留 toy CPU 执行。目的：说明安全模块确实改变了系统行为，而不是'可有可无'。"

No demo currently shows what happens when security modules are disabled. The closest approximation is `demo_rop` Case C1 (L1 CFI = no CFI checks, but EWC is still active).

**Why it matters**: Without this baseline, a reviewer could argue "the security modules are just checking conditions that would cause crashes anyway." The baseline would show that without EWC, an illegal PC access doesn't produce a clean `EWC_ILLEGAL_PC` -- it either reads garbage or crashes unpredictably.

**Suggestion**: Add a `demo_baseline` that:
1. Loads a program with SecurityHardware but without EWC windows configured
2. Attempts to fetch an instruction -> shows the undefined behavior (INVALID_PC from bounds check, not EWC_ILLEGAL_PC)
3. Contrasts with the same scenario with EWC enabled -> clean EWC_ILLEGAL_PC

### B1-b: Ablation baseline -- NOT IMPLEMENTED

Section 10 requires progressive module activation:

| Config | Modules |
|--------|---------|
| CPU only | No security checks |
| + execution authorization | CPU + Gateway + EWC |
| + code integrity | Above + Decrypt/MAC |
| + data ownership | Above + PVT |
| + behavior compliance | Above + SPE |
| + audit | Above + Audit |

**Suggestion**: This could be a single `demo_ablation` that runs the same attack scenario (e.g., cross-user) with increasing security layers enabled and shows how each layer changes the outcome.

---

## 5. Dev Plan Completion Status

All Issues from dev_plan.md are implemented. Specific mapping:

| Issue | Expected output files | Actual files | Status |
|-------|----------------------|-------------|--------|
| Issue 4 | `code_codec.hpp/cpp` | `include/security/code_codec.hpp`, `src/security/code_codec.cpp` | Done (location differs from plan: `security/` not `core/`) |
| Issue 5 | `gateway.hpp/cpp`, `test_gateway.cpp` | `include/security/gateway.hpp`, `src/security/gateway.cpp`, `tests/test_gateway.cpp` | Done |
| Issue 6A | `kernel_emu.hpp/cpp` | `include/kernel/process.hpp`, `src/kernel/process.cpp` | Done (renamed: `KernelProcessTable` not `KernelEmulator`) |
| Issue 6B | `demo_cross_user.cpp` | `demos/cross_user/demo_cross_user.cpp` | Done |
| Issue 7 | `pvt.hpp/cpp`, `test_pvt.cpp` | `include/security/pvt.hpp`, `src/security/pvt.cpp`, `tests/test_pvt.cpp` | Done |
| Issue 8 | `spe.hpp/cpp`, `test_spe.cpp` | `include/security/spe.hpp`, `src/security/spe.cpp`, `tests/test_spe.cpp` | Done |
| Issue 9 | 5 demo directories | `demos/{normal,cross_user,cross_process,injection,rop}` | Done |
| Issue 10 | `audit.hpp/cpp` | `include/security/audit.hpp`, `src/security/audit.cpp` | Done (location: `security/` not `core/`) |
| Issue 11 | SecureIR generator | `include/security/securir_builder.hpp`, `src/security/securir_builder.cpp` | Done (implemented as library, not CLI tool) |

**Naming deviations** from plan:
- `KernelEmulator` -> `KernelProcessTable` (reasonable: the class manages a process table, not an OS emulator)
- `code_codec` moved from `core/` to `security/` (reasonable: encryption is a security concern)
- `audit` moved from `core/` to `security/` (reasonable: audit is a security module)

---

## 6. Security Pipeline Correctness

### 6.1 Enforcement ordering

The pipeline in `executor.cpp` enforces security checks in this order:

```
[Fetch stage]
  1. EWC query             (line 128)    -- execution authorization
  2. PC bounds check       (line 135)    -- addressability
  3. Alignment check       (line 142)    -- structural validity
  4. Code memory fetch     (line 170)    -- data access

[Decode stage]
  5. key_check verify      (codec:218)   -- key authentication
  6. tag verify            (codec:223)   -- integrity (MAC equivalent)
  7. XOR decrypt           (codec:228)   -- confidentiality
  8. magic check           (codec:233)   -- structural validity

[Execute stage]
  9. Instruction dispatch  (line 328)    -- computation
  10. committed_pc calc    (per-instr)   -- control flow resolution

[Post-execute / pre-commit]
  11. SPE check            (line 457)    -- behavioral compliance (CFI)
  12. PC commit            (line 465)    -- state update
```

This matches the design document's requirement (system_design_v4): EWC at Fetch, decrypt at Decode, SPE at Decode/Execute. Check ordering is correct: authorization before data access, integrity before execution, CFI before commit.

### 6.2 Fail-safe defaults

- `EwcQueryResult::allow` defaults to `false` (`ewc.hpp:34`) -- fail-closed
- `SpeCheckResult::allow` defaults to `true` (`spe.hpp:17`) -- if no policy configured, allow
- `DecryptResult::ok` defaults to `false` (`code_codec.hpp:30`) -- fail-closed
- `PvtRegisterResult::ok` defaults to `false` (inferred from `pvt.hpp:40`)

The EWC and decrypt defaults are correct (deny by default). SPE defaults to allow because policies are optional (a program without CFI configured should still run). This is consistent with the design.

### 6.3 HALT bypasses SPE

```cpp
// executor.cpp:432-435
case sim::isa::Op::HALT: {
    result.state.regs[0] = 0;
    result.trap = Trap{TrapReason::HALT, fetched.pc, "halt"};
    return result;   // <-- returns before SPE check at line 457
}
```

HALT immediately returns without going through the SPE check. This is semantically correct (HALT is a terminal instruction; there's no "next PC" to validate). But it means a program can always terminate cleanly regardless of CFI state. This is fine -- a trapped program also terminates, just with a different reason code.

### 6.4 W-xor-X (code/data separation)

Code and data are stored in separate memory regions:
- Code: `CodeRegion.code_memory` (per-context, via `SecurityHardware`)
- Data: `CpuState.mem` (per-execution, allocated in `ExecuteProgram`)

There is no mechanism for code to write to `code_memory` or for the executor to fetch instructions from `CpuState.mem`. This provides W-xor-X by construction, matching Decision D2 from Issue6A_Decision_Summary.

---

## 7. Code Quality Findings

### 7.1 Duplicated `OpToString` [Low]

Two identical copies exist:

| Location | Lines |
|----------|-------|
| `executor.cpp:75-103` | 28 lines |
| `spe.cpp:10-38` | 28 lines |

Both convert `sim::isa::Op` to a C-string with identical switch statements. Suggest extracting to a shared utility (e.g., in `isa/opcode.hpp` or a new `isa/opcode.cpp`).

### 7.2 Dead code in `demo_normal.cpp` [Low]

```cpp
// demo_normal.cpp:16-18
std::uint64_t ProgramEndVa(const sim::isa::AsmProgram& program) {
    return program.base_va + program.code.size() * sim::isa::kInstrBytes;
}
```

This function is defined but never called. It should be removed.

### 7.3 EWC Query uses linear search despite sorted windows [Low]

`EwcTable::SetWindows` sorts windows by `start_va` (`ewc.cpp:10-11`):
```cpp
std::sort(windows.begin(), windows.end(),
          [](const ExecWindow& lhs, const ExecWindow& rhs) { return lhs.start_va < rhs.start_va; });
```

But `EwcTable::Query` (`ewc.cpp:49-59`) iterates linearly:
```cpp
for (const ExecWindow& window : it->second) {
    if (window.start_va <= pc && pc < window.end_va) { ... }
}
```

Since windows are sorted and non-overlapping, a binary search (or `std::lower_bound`) would be more appropriate. For the demo's 1-2 windows per context this is irrelevant, but the sorting implies a binary search was intended.

### 7.4 SPE `stage` label inconsistency [Low]

In `spe.cpp`, CALL/J/BEQ violations are labeled `stage="decode"`:
```cpp
// spe.cpp:101
result = MakeViolationDetail("decode", op, "call_target_not_allowed", ...);
```

But in the Executor, SPE is checked at line 457 -- **after** the Execute stage, not during Decode. The design document says SPE operates at "Decode/Execute" stage. The label `"execute"` would be more accurate, or a dedicated label like `"post-execute"`.

RET violations are correctly labeled `stage="execute"` (`spe.cpp:127,134`).

### 7.5 Gateway handle counter never resets on failure [Low]

```cpp
// gateway.cpp:407
const ContextHandle handle = next_handle_++;  // increments BEFORE capacity check
```

If `Load()` throws (e.g., parse error, capacity exceeded), the handle counter still advances. After many failed loads, the counter will be much larger than the number of active handles. This is harmless for the demo but could be surprising if someone inspects handle values.

---

## 8. Design Document vs. Implementation Deviations

### 8.1 SecureIR format (spec_i2_i7 vs. actual)

`spec_i2_i7.md` describes SecureIR as:
> "JSON 格式 {...} 包含 user_id, signature (stub), code sections (offset, size, key_id), entry_offset"

Actual implementation (`securir_builder.cpp:143-161`) generates JSON with:
```json
{
  "program_name": "...",
  "user_id": 1001,
  "signature": "stub-valid",
  "base_va": 4096,
  "windows": [{"window_id":1, "start_va":4096, "end_va":4112, "key_id":11, "type":"CODE", "code_policy_id":1}],
  "pages": [{"va":4096, "page_type":"CODE"}],
  "cfi_level": 3,
  "call_targets": [4104],
  "jmp_targets": []
}
```

The actual format is richer than spec_i2_i7's original description (adds `pages`, `cfi_level`, `call_targets`, `jmp_targets`, `windows` instead of flat `code_sections`). This is a natural evolution as Issues 7-8 were implemented. The spec document could be updated to reflect the final format.

### 8.2 Signature verification

`gateway.cpp:419-421`:
```cpp
if (secure_ir.signature.empty()) {
    throw std::runtime_error("gateway_invalid_signature reason=empty");
}
```

Signature verification is a stub that only checks non-empty. This matches spec_i2_i7's "signature (stub)" and Demo_Claim_Boundary's N1 ("不证明真实密码学可落地"). The stub behavior is correct and appropriately scoped.

### 8.3 system_design_v4 SecureIR plaintext/encrypted partition

system_design_v4 (DP-9) specifies:
> "SecureIR 明文区包含 user_pubkey, signature, code/data segment layout; K-加密区包含 code/data content + MAC, entry_offset, CFI level"

The current implementation does NOT partition SecureIR into plaintext/encrypted regions. All metadata (including `cfi_level`, `call_targets`, `entry_offset` equivalent `base_va`) is in plaintext JSON. Only the code bytes themselves are encrypted.

This is acceptable for the prototype scope (Demo_Claim_Boundary N1 applies), but if the paper discusses the plaintext/encrypted partition as a security property (OS can see layout but not content/policy), the demo doesn't currently demonstrate this.

---

## 9. Cross-Process Demo: Depth Analysis

`demo_cross_process` is the most complex demo with three cases showing defense-in-depth.

### Case A: Normal (PVT registration succeeds)

```
Alice loads with pages=[{va=0x1000, page_type=CODE}]
KernelProcessTable::LoadProcess registers this page via PVT
PVT checks: EWC has window for 0x1000 under Alice's handle? Yes -> register OK
Alice runs -> HALT
```

This shows the **happy path** for PVT: page registration succeeds when page VA matches EWC window.

### Case B: Malicious mapping (PVT catches)

```
Alice loads normally (handle=1, window at 0x1000)
Bob loads normally (handle=2, window at 0x2000)
OS calls: pvt.RegisterPage(bob_handle, alice_base_va=0x1000, CODE)
PVT checks: EWC has window for 0x1000 under Bob's handle=2? No -> PVT_MISMATCH
```

This shows PVT's **first line of defense**: if a malicious OS tries to map Alice's page into Bob's address space, PVT detects the inconsistency because Bob's EWC has no window for that VA.

**Limitation**: This only catches the `missing_window` case. A more sophisticated attack (where the OS creates a fake window) is prevented by the fact that only Gateway can call `EwcTable::SetWindows`, and Gateway only does so during `Load()` with signed SecureIR data. But this trust chain is not explicitly demonstrated.

### Case C: Defense-in-depth (EWC catches what PVT can't)

```
Bob loads normally (handle=1 here, window at 0x2000)
OS directly writes Alice's encrypted code to Bob's code_memory region at 0x1000
OS sets entry_pc = alice_base_va = 0x1000
Bob's context fetches at 0x1000 -> EWC denies (Bob's window is at 0x2000, not 0x1000)
```

This shows that even if OS bypasses PVT (writes directly to code_memory), EWC still denies the fetch because the PC is outside Bob's authorized window.

The demo correctly calls this out in output:
```
CASE_C_NOTE=PVT can be bypassed by a malicious OS write, but EWC still denies fetch at alice_base_va.
CASE_C_NOTE_2=PVT and EWC are complementary independent enforcement layers.
```

---

## 10. Test Coverage Assessment

| Module | Test file | Test count | Coverage notes |
|--------|-----------|-----------|---------------|
| ISA | `test_isa_assembler.cpp` | 15 | Labels, all opcodes, memory operands, error cases |
| Executor | `test_executor.cpp` | 16+ | Arithmetic, control flow, memory, EWC, decrypt, SPE |
| Gateway | `test_gateway.cpp` | 10+ | Load/release, validation, capacity, audit |
| Kernel | `test_kernel_process.cpp` | 13+ | Process lifecycle, context switch, rollback |
| PVT | `test_pvt.cpp` | 8+ | Registration, owner derivation, permission check |
| SPE | `test_spe.cpp` | 13+ | L1/L2/L3, shadow stack, target whitelist |
| SecureIrBuilder | `test_securir_builder.cpp` | 6+ | Build, round-trip, multi-window, CFI propagation |

**Gap**: No negative test for `AuditCollector::Clear()` behavior (seq_no continues incrementing after clear -- `audit.cpp:26` only clears the vector, `next_seq_no_` is not reset). This is likely intentional (monotonic sequence property), but there's no test asserting it.

**Gap**: No test for Gateway `GATEWAY_LOAD_FAIL` audit event (exception path at `gateway.cpp:466`). This path is exercised by error tests but the audit event isn't explicitly verified.

---

## 11. Improvement Recommendations (Prioritized)

### High Priority (Affects paper claim validity)

| # | Item | Effort | Impact |
|---|------|--------|--------|
| H1 | Fix `EWC_ILLEGAL_PC` audit `user_id=0` | Small (add handle->user_id lookup) | Strengthens C4 claim |
| H2 | Fix `PVT_MISMATCH` audit `user_id=0` (missing_window case) | Small | Same as above |
| H3 | Update Demo_Claim_Boundary_v3.1.md evidence labels | Small (doc change only) | Aligns document with reality |

### Medium Priority (Affects paper evaluation completeness)

| # | Item | Effort | Impact |
|---|------|--------|--------|
| M1 | Add no-enforcement baseline demo (B1-a) | Medium | Required by v3.1 Section 10 |
| M2 | Add ablation baseline demo (B1-b) | Medium-Large | Same |
| M3 | Add PVT owner mismatch case in demo_cross_process | Small | Completes A4 coverage |

### Low Priority (Code quality)

| # | Item | Effort | Impact |
|---|------|--------|--------|
| L1 | Extract shared `OpToString` utility | Trivial | Reduces duplication |
| L2 | Remove dead `ProgramEndVa` in demo_normal.cpp | Trivial | Clean code |
| L3 | Fix SPE `stage="decode"` label to `"execute"` for CALL/J/BEQ | Trivial | Accuracy |
| L4 | Use binary search in `EwcTable::Query` | Small | Consistency with sort |
| L5 | Update dev_plan.md Issue status markers | Small | Document hygiene |

---

## 12. Summary

The codebase successfully implements all planned modules (Issues 4-11) and demonstrates the five attack narratives specified in the development plan. The security enforcement pipeline is correctly ordered (EWC -> Decrypt -> Execute -> SPE), with fail-safe defaults and proper audit emission at each enforcement point.

The most actionable findings are:
1. **Two audit events have user_id=0** -- a small code fix that meaningfully strengthens the C4 claim about user-event correlation.
2. **Demo_Claim_Boundary_v3.1.md is stale** -- three evidence labels should be upgraded from [Scaffolded] to [Current] now that all demo modules are implemented.
3. **Baseline demos are missing** -- the document commits to a no-enforcement baseline and ablation baseline that don't yet exist. These are valuable for the paper's evaluation section.

The remaining findings (code duplication, label inconsistency, dead code) are minor cleanup items that don't affect functionality or claim validity.
