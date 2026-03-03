# docs/spec_i2_i7.md
# Prototype Spec (I-2..I-6 + I-7 demos) — minimal, executable, Codex-friendly
# Scope: implement system skeleton to demonstrate EWC-gated execution + pseudo encryption + gateway/context switch.

## 0. Scope and non-goals

### In scope
- I-2 Gateway simulator
- I-3 EWC enforcement (highest priority): mandatory gate in Fetch stage
- I-4 PVT (minimal page registration + consistency check)
- I-5 SPE (minimal scaffolding, placeholder checks/events)
- I-6 CPU emulator loop: Fetch → Decrypt (if code) → Decode → Execute → Memory → Commit
- I-7 demos: (1) normal execution, (2) cross-user (context switch + illegal execution trap)
- I-1 only L0: SecureIR parsing + pseudo encryption/decryption flow (NOT real crypto)

### Out of scope (for now)
- Real signatures / real encryption (Ed25519/AES-GCM, etc.)
- Full RISC-V compatibility
- Performance tuning / counters

## 1. Repo-level authority
- Full design: `docs/system_design_v3.md`
- Milestone checklist: `docs/progress_tracking_v3.md`
- This spec (`docs/spec_i2_i7.md`) is the source of truth for the current prototype. If conflict exists, this spec wins.

## 2. Terminology and required observability

### Must-have outputs (for each demo run)
- (a) Trap reason code (if any)
- (b) Audit log events stream
- (c) context_handle switch trace

### Core identifiers
- `user_id`: small integer; prototype uses manual assignment (e.g., Alice=1, Bob=2). Gateway must maintain a mapping table.
- `context_handle`: opaque index to per-process security context slot.
- `window_id`: execution window identifier; unique within a context.
- `key_id`: pseudo key identifier used for pseudo-decryption.

## 3. Toy ISA (RISC-V-ish, minimal)

### 3.1 CPU state
- 32 general registers: `x0..x31` (x0 is hardwired to 0)
- Program counter `pc` (u64)
- Memory model: byte-addressable flat memory with page metadata (see section 4)

### 3.2 Instruction representation (implementation-defined)
Implement either:
- A textual assembler that compiles to an in-memory vector of `Instr{op, rd, rs1, rs2, imm}`; OR
- A simple binary encoding (optional).
Keep it simple and testable.

### 3.3 Required opcodes
- `NOP`
- `ADD rd, rs1, rs2`
- `XOR rd, rs1, rs2` (useful for toy programs)
- `LI  rd, imm`
- `LD  rd, [rs1 + imm]`
- `ST  rs2, [rs1 + imm]`
- `J   imm` (pc-relative jump)
- `BEQ rs1, rs2, imm` (pc-relative branch)
- `CALL imm` (pc-relative call; save return address in x1)
- `RET` (jump to x1)
- `HALT`
- `SYSCALL imm` (used to invoke kernel / gateway operations in demos)

### 3.4 Basic semantics
- `pc` increments by 4 (or 1 instruction slot) per step unless control flow changes.
- Memory ops trap on invalid/out-of-range address.

## 4. Memory model and pseudo encryption (I-1 L0)

### 4.1 Pages and types
Represent memory as pages of fixed size (e.g., 4KB).
Each page has metadata:
- `page_type`: `CODE` or `DATA`
- `owner_user_id`
- `state`: `UNMAPPED | MAPPED_PLAIN | MAPPED_CIPHERTEXT`
- `key_id` (only meaningful for ciphertext CODE pages)

### 4.2 Pseudo encryption/decryption scheme (deterministic, reversible)
- Store CODE pages as ciphertext bytes when `state == MAPPED_CIPHERTEXT`.
- In Fetch stage:
  1) EWC query decides allow/deny and returns `key_id` when allowed.
  2) If current `pc` is in a ciphertext CODE page:
     - Decrypt bytes using `key_id` with a deterministic reversible toy scheme.
     - Example (allowed to change, but must be deterministic & reversible):
       - `plain = cipher XOR (key_id & 0xFF) XOR (pc & 0xFF) XOR (offset & 0xFF)`
  3) Decode plaintext bytes / instruction object.
- Wrong `key_id` must reliably cause decode failure or illegal opcode and be treated as trap:
  - Trap reason: `DECRYPT_DECODE_FAIL`

## 5. Security modules and interfaces

### 5.1 Audit
Provide:
- `AuditEvent` fields:
  - `seq_no` (monotonic)
  - `type` (enum string)
  - `user_id`
  - `context_handle`
  - `pc`
  - `detail` (short string or key-value map)
- Output requirements:
  - Always print to stdout
  - Optional: also append to `logs/audit.ndjson`

Required event types (minimal set):
- `GATEWAY_LOAD_OK`
- `GATEWAY_LOAD_FAIL`
- `CTX_SWITCH`
- `EWC_ILLEGAL_PC`
- `DECRYPT_DECODE_FAIL`
- `PVT_MISMATCH`
- `SPE_VIOLATION` (placeholder)

### 5.2 Trap reasons
Define `TrapReason` enum:
- `NONE`
- `EWC_ILLEGAL_PC`
- `DECRYPT_DECODE_FAIL`
- `INVALID_MEMORY`
- `SYSCALL_FAIL`
- `PVT_MISMATCH`
- `SPE_VIOLATION`
- `HALT` (successful termination)

### 5.3 EWC (Execution Window Checker) — highest priority
#### Data model
`ExecWindow` fields:
- `window_id`
- `start_va` (inclusive)
- `end_va` (exclusive)
- `owner_user_id`
- `key_id`
- `type` (e.g., `CODE`)
- `code_policy_id` (u32; reserved for SPE binding / placeholder)

#### API
`EwcQueryResult query(pc, active_context_handle) -> {allow, key_id, window_id, owner_user_id, code_policy_id}`

Rules:
- If `pc` not in any window => deny.
- EWC is mandatory gate in Fetch:
  - deny => trap `EWC_ILLEGAL_PC` + emit audit `EWC_ILLEGAL_PC`.

### 5.4 Gateway (I-2)
Gateway is the only writer of EWC config.

#### SecureIR (L0) input format
Use JSON (simple and testable). Example:

    {
      "program_name": "demo_normal",
      "user": "alice",
      "user_id": 1,
      "base_va": 4096,
      "windows": [
        {"window_id": 1, "start": 4096, "end": 8192, "key_id": 11, "type":"CODE", "code_policy_id": 1}
      ],
      "pages": [
        {"va": 4096, "size": 4096, "page_type":"CODE", "ciphertext": true, "key_id": 11}
      ]
    }

#### Kernel-facing APIs
- `context_handle gateway_load(SecureIR) -> handle`
  - allocate new security context slot
  - register EWC windows for this handle
  - initialize user_id mapping table entry if needed
  - emit `GATEWAY_LOAD_OK` (or `GATEWAY_LOAD_FAIL`)
- `void gateway_release(handle)` (optional for prototype)
  - free slot, clear windows

### 5.5 PVT (Page Validation Table) (I-4)
#### Data model (minimal)
`PvtEntry`:
- `pa_page_id` (prototype can model as a unique integer)
- `owner_user_id`
- `expected_va`
- `permissions` (bitmask; minimal)
- `page_type` (CODE/DATA)
- `state` (expected mapping state)

#### SECURE_PAGE_LOAD path
Kernel emulator calls:
- `secure_page_load(handle, va, page_type, owner_user_id, key_id, ciphertext_flag)`

PVT checks (minimal and sufficient for demo):
- If `page_type == CODE`:
  - `va` must lie within at least one EWC window of `handle`
  - window owner must match `owner_user_id`
- If mismatch => trap `PVT_MISMATCH` + audit `PVT_MISMATCH`.

### 5.6 SPE (placeholder scaffolding) (I-5)
Provide data structures:
- `CodePolicy{code_policy_id, owner_user_id, cfi_level}`

Minimal enforcement (choose one):
- A) basic permission check on memory access (simple)
- B) no enforcement, but keep hooks + audit placeholder when invoked

If violation detected => trap `SPE_VIOLATION` + audit `SPE_VIOLATION`.

### 5.7 context_handle and context switching
Kernel maintains:
- `handle -> SecurityContext{user_id, ewc_windows, spe_state, pvt_state,...}`

API:
- `secure_context_switch(handle)`

Semantics:
- Update `active_context = handle`
- Emit audit `CTX_SWITCH` with handle + user_id
- From now on, EWC queries reference the active handle’s windows.

## 6. CPU pipeline (I-6) — minimal but structured
Per step:
1) Fetch:
   - query EWC with `pc` and `active_context`
   - deny => trap (`EWC_ILLEGAL_PC`)
2) Decrypt (if needed):
   - if PC maps to ciphertext CODE page => pseudo-decrypt using returned `key_id`
3) Decode:
   - decode plaintext into Instr
   - failure => trap (`DECRYPT_DECODE_FAIL`)
4) Execute + Memory
5) Commit (pc update)

Stop conditions:
- `HALT` => stop with trap reason `HALT` (successful)
- Any other trap => stop and print required outputs

## 7. Demos (I-7 minimal)

### 7.1 demo_normal
- Load Alice SecureIR via `gateway_load` => returns `handle_A`
- `secure_context_switch(handle_A)`
- Run until `HALT`

Expected:
- No EWC traps
- Prints audit stream including load + ctx_switch
- Prints final trap reason `HALT`

### 7.2 demo_cross_user
- Load Alice => `handle_A`; Load Bob => `handle_B`
- Switch to Alice, run a few steps
- Switch to Bob, attempt to execute at an address that is only inside Alice’s EWC window

Expected:
- Trap `EWC_ILLEGAL_PC`
- Audit includes `CTX_SWITCH` + `EWC_ILLEGAL_PC`
- context_handle trace clearly shows active handle when trap occurred

## 8. Tests (must keep green)
Required unit tests:
- ISA decode/execute basics
- EWC query allow/deny
- Fetch-stage enforcement (deny => trap)
- Pseudo-decrypt correctness for correct key and failure for wrong key
- Context switching changes active windows
- PVT mismatch detection

`ctest` must pass after each issue.

## 9. Git delivery rules
- After each issue: tests pass → commit → push to a feature branch:
  - branch name: `issue-<n>-<shortname>`
  - commit message: `issue <n>: <summary>`
- Never push to `main` unless explicitly requested.
- Never run `git push` without user approval.
