# 原型代码审查报告

> 审查者：Claude Opus 4.6
> 日期：2026-03-26
> 审查范围：完整代码仓库 vs. Demo_Claim_Boundary_v3.1 / dev_plan / system_design_v4 / spec_i2_i7
> 原则：只读审查，不修改任何文件。

---

## 0. 总览

8/8 单元测试通过，5/5 Demo 正确运行。dev_plan.md 中 Issue 4-11 全部完成实现。代码架构清晰，模块边界分明。

核心发现：

| 类别 | 数量 | 严重度 |
|------|------|--------|
| Claim 证据缺口 | 2 | 高（影响论文 claim 强度） |
| 文档过期 | 1 | 高（Demo_Claim_Boundary_v3.1.md 标签陈旧） |
| 缺失 Baseline | 2 | 中（B1-a、B1-b 未实现） |
| 审计事件缺陷 | 2 | 中（两类事件 user_id=0） |
| 代码质量 | 5 | 低 |

---

## 1. Claim 逐项验证

### C1：控制路径存在性（control-path existence）

> "demo 证明：从 gateway_load 到 secure_context_switch 再到 Fetch → EWC → Decrypt → Decode → Execute 的最小控制路径是可执行的。"

**结论：PASS**

完整路径在代码中可端到端追踪：

```
SecureIrBuilder::Build()                    securir_builder.cpp:165
  -> Gateway::Load()                        gateway.cpp:406
       -> EwcTable::SetWindows()            gateway.cpp:448    配置执行窗口
       -> SpeTable::ConfigurePolicy()       gateway.cpp:449    配置 CFI 策略
       -> StoreCodeRegion()                 gateway.cpp:451    存储加密代码
  -> KernelProcessTable::LoadProcess()      process.cpp:24
       -> PvtTable::RegisterPage()          process.cpp:34     注册页面到 PVT
  -> KernelProcessTable::ContextSwitch()
       -> hardware.SetActiveHandle()        process.cpp:98     激活上下文
       -> audit CTX_SWITCH                  process.cpp:100    审计事件

ExecuteProgram(entry_pc, options)           executor.cpp:232
  -> GetActiveHandle()                      executor.cpp:242   读取活跃上下文
  -> GetCodeRegion()                        executor.cpp:250   获取代码区域
  主循环:
    -> FetchStage()                         executor.cpp:295
         -> EwcTable::Query()               executor.cpp:128   执行授权检查
         -> DeserializeCipherUnit()         executor.cpp:171   读取密文
    -> DecodeStage()                        executor.cpp:307
         -> DecryptInstr()                  executor.cpp:180   key_check + tag + XOR 解密
    -> 指令分派                              executor.cpp:328-450
    -> SpeTable::CheckInstruction()         executor.cpp:457   CFI 检查
    -> 提交 PC                              executor.cpp:465   状态更新
```

`demo_normal` Case A 的输出直接证明了该路径的完整执行：

```
AUDIT seq_no=1 type=GATEWAY_LOAD_OK ...     加载成功
AUDIT seq_no=2 type=CTX_SWITCH ...          上下文切换
FINAL_REASON=HALT                           正常执行到终止
```

从 Gateway 加载 -> 上下文切换 -> Fetch/Decode/Execute -> HALT，管线完整打通。

---

### C2：非法执行入口可被结构化拦截

> "非法 PC 进入不会退化为'随机崩溃'或'解密垃圾后失败'，而是在 Fetch 阶段先经过 EWC 的显式检查，并触发明确 trap reason。"

**结论：PASS**

关键在于 `FetchStage`（`executor.cpp:124-173`）中各检查的**执行顺序**：

```cpp
// executor.cpp:128 — 第一步：EWC 授权检查
const sim::security::EwcQueryResult query_result = ewc.Query(pc, context_handle);
if (!query_result.allow) {                          // 拒绝后立即 trap
    *trap = Trap{TrapReason::EWC_ILLEGAL_PC, pc, ...};
    return false;                                    // 不会执行后续任何代码内存访问
}

// executor.cpp:135 — 第二步：PC 下界检查（仅在 EWC 通过后）
if (pc < region_base_va) { ... }

// executor.cpp:142 — 第三步：对齐检查
if ((delta % sim::isa::kInstrBytes) != 0) { ... }

// executor.cpp:160 — 第四步：代码内存越界检查
if (byte_end > code_memory_size) { ... }

// executor.cpp:170 — 第五步：实际读取代码内存（仅所有检查通过后）
packet->cipher = DeserializeCipherUnit(code_memory + byte_offset_size_t, ...);
```

**为什么这个顺序很重要**：EWC 检查在代码内存访问**之前**发生。如果没有 EWC，一个非法 PC（比如跨用户跳转到 Alice 的代码区域）会直接触碰代码内存——行为取决于那个地址碰巧有什么数据：可能是合法 opcode（静默错误执行，最危险）、可能是垃圾（随机崩溃）、可能是越界（段错误）。EWC 把这个不确定的失败变成了**确定的、命名的** `EWC_ILLEGAL_PC` trap。

`demo_cross_user` Case B 的输出直接展示了这一点：

```
FINAL_REASON=EWC_ILLEGAL_PC     不是随机崩溃，而是明确的 trap 类型
FINAL_PC=4096                   精确记录出错 PC
AUDIT ... type=EWC_ILLEGAL_PC   审计事件已发射
```

---

### C3：用户上下文切换会改变有效执行语义

> "context_handle 不是装饰性变量，而是会真实改变 active execution windows，进而影响后续 EWC 判定结果。"

**结论：PASS**

机制链条如下：

1. `KernelProcessTable::ContextSwitch` 调用 `hardware.SetActiveHandle(handle)`（`process.cpp:98`）
2. `ExecuteProgram` 通过 `GetActiveHandle()`（`executor.cpp:242`）获取当前活跃 handle
3. `FetchStage` 将 `context_handle` 传给 `ewc.Query(pc, context_handle)`（`executor.cpp:128`）
4. `EwcTable::Query` 按 `context_handle` 查找窗口表（`ewc.cpp:44`）：

```cpp
// ewc.cpp:44-47
auto it = windows_by_context_.find(context_handle);
if (it == windows_by_context_.end()) {
    return result;  // allow=false —— 这个 context 没有任何窗口，拒绝一切
}
```

这意味着**同一个 PC 在不同 context 下会产生不同的 EWC 结果**。`demo_cross_user` 直接证明了这一点：

| 场景 | 活跃 context | 入口 PC | 结果 |
|------|-------------|---------|------|
| Case A | Alice（handle=1） | 0x1000（Alice 的代码） | HALT（允许） |
| Case C | Bob（handle=2） | 0x1000（Alice 的代码） | EWC_ILLEGAL_PC（拒绝） |

相同的 PC、相同的代码内存内容、不同的 context → 不同的结果。这证明 `context_handle` 是承载安全语义的关键变量，而非摆设。

---

### C4：关键安全事件可观测

> "系统不会只在内部阻断 violation，而是会输出与 contract 对应的可观测事件。"

**结论：部分通过 —— 存在一个缺陷**

文档要求的所有事件类型均已实现并在 demo 中出现：

| 事件类型 | 发射位置 | user_id 是否正确？ |
|---------|---------|------------------|
| `GATEWAY_LOAD_OK` | `gateway.cpp:453` | 正确（`secure_ir.user_id`） |
| `GATEWAY_LOAD_FAIL` | `gateway.cpp:466` | 不正确（硬编码 0） |
| `GATEWAY_RELEASE` | `gateway.cpp:481` | 正确（从 `handle_to_user_` 查询） |
| `CTX_SWITCH` | `process.cpp:100` | 正确（`process->user_id`） |
| `EWC_ILLEGAL_PC` | `executor.cpp:298` | **不正确（硬编码 0）** |
| `DECRYPT_DECODE_FAIL` | `executor.cpp:309` | 正确（`fetched.owner_user_id`） |
| `PVT_MISMATCH` | `pvt.cpp:54` | **不正确（未匹配窗口时为 0）** |
| `SPE_VIOLATION` | `spe.cpp:144` | 正确（`policy.user_id`） |

#### 缺陷详解：EWC_ILLEGAL_PC 审计事件 user_id=0

```cpp
// executor.cpp:298
LogAudit(audit, "EWC_ILLEGAL_PC", 0, context_handle, fetch_trap.pc, std::move(deny_detail));
//                                ^-- 硬编码为 0
```

**为什么是 0**：当 EWC 拒绝一个 PC 时，`EwcQueryResult` 没有匹配到任何窗口，因此 `owner_user_id` 字段保持默认值 0。而 Executor 没有 handle → user_id 的映射，所以无法填入正确的 user_id。

**在 demo 输出中可以直接观察到这个问题**：

```
AUDIT seq_no=5 type=EWC_ILLEGAL_PC user_id=0 context_handle=2 pc=4096 ...
//                                 ^^^^^^^^^
// 这里应该是 1002（Bob 的 user_id），因为是在 Bob 的 context 下触发的 violation
```

虽然 `context_handle=2` 存在，可以**间接**追溯到用户，但 Claim 原文要求"用户上下文与 violation 能被对应起来" —— 审计事件本身无法直接识别是哪个用户的 violation，削弱了 C4 的说服力。

#### 同样的问题出现在 PVT_MISMATCH

```cpp
// pvt.cpp:54 — missing_window 分支
audit_.LogEvent("PVT_MISMATCH", query_result.owner_user_id, handle, va, detail);
//                               ^^^^^^^^^^^^^^^^^^^^^^^^
// query_result 未匹配窗口时 owner_user_id = 0（默认值）
```

`demo_cross_process` Case B 的输出中可以看到：

```
AUDIT seq_no=3 type=PVT_MISMATCH user_id=0 context_handle=2 ...
//                                ^^^^^^^^^  应该是 1002（Bob）
```

#### 根因

`SecurityHardware` 类没有维护 `handle → user_id` 的映射。这个映射存在于 `Gateway` 的 `handle_to_user_` 中（`gateway.cpp:452`），但 Executor 和 PVT 无法访问 Gateway。

#### 修复建议

在 `SecurityHardware` 中增加 `handle → user_id` 映射（可以在 `StoreCodeRegion` 时同步写入），让 Executor 在 EWC deny 时从中查询正确的 user_id。或者让 `Gateway::Load` 在配置完 EWC 窗口后，同时把 user_id 注入 `SecurityHardware` 的 per-handle 元数据。

---

### C5：最小 attack-driven 叙事可成立

> "当前 demo 支撑的攻击叙事均锚定到公认攻击原语。"

**结论：PASS（但文档标签已过期）**

五类攻击叙事均有可运行的 demo。以下逐个说明代码实现和对应的安全语义。

#### (a) 正常执行 — `demo_normal` Case A

```asm
LI x1, 10        ; 加载立即数
LI x2, 32
ADD x3, x1, x2   ; 计算
SYSCALL 1         ; 系统调用
HALT              ; 正常终止
```

完整 happy-path：`GATEWAY_LOAD_OK` → `CTX_SWITCH` → 正常执行 → `HALT`。没有安全事件被触发，证明合法程序不会被误拦截。

#### (b) 跨用户非法执行 — `demo_cross_user` Cases B/C

**Case B**：Bob 的程序通过 `J` 指令跳转到 Alice 的代码区域。跳转目标的计算方式：

```cpp
// demo_cross_user.cpp:43-45
bob_source << "J "
           << (static_cast<std::int64_t>(alice_base_va) -
               static_cast<std::int64_t>(bob_base_va + 2 * sim::isa::kInstrBytes))
           << '\n';
// 生成 PC-relative 偏移，使 Bob 的第 2 条指令跳到 alice_base_va=0x1000
```

Executor 在 Fetch 阶段查询 EWC：Bob 的 context（handle=2）没有覆盖 0x1000 的窗口 → `EWC_ILLEGAL_PC`。

**Case C**：恶意 OS 直接把 Bob 的入口 PC 设为 Alice 的 base_va：

```cpp
// demo_cross_user.cpp:90
run_case("CASE_C_BOB_MALICIOUS_OS", bob_handle, alice_base_va);
// 不通过 J 指令跳转，而是直接从 alice_base_va 开始执行
```

结果同样是 `EWC_ILLEGAL_PC` —— EWC 不关心 PC 是怎么到达这个地址的（跳转还是 OS 注入），只关心当前 context 是否有权执行这个地址。

#### (c) 代码注入（密文篡改） — `demo_injection`

这个 demo 演示了两种不同的篡改方式，它们触发 `DecryptInstr`（`code_codec.cpp:215-240`）中**不同阶段**的检测：

```
DecryptInstr 三阶段验证流程：
  Step 1: key_check 验证   (line 218) → 失败原因: "key_check_mismatch"
  Step 2: tag 验证          (line 223) → 失败原因: "tag_mismatch"
  Step 3: XOR 解密 + magic  (line 228-234) → 失败原因: "decode_mismatch"
```

**Case B — 全量 XOR 篡改**（`demo_injection.cpp:33-43`）：

```cpp
void XorWholeCiphertext(..., std::uint8_t mask) {
    for (std::uint8_t& byte : region->code_memory) {
        byte ^= mask;  // 翻转代码内存中的每一个字节
    }
}
```

所有字节被翻转，包括 `key_check` 字段 → 在 Step 1 就被检测到 → `reason=key_check_mismatch`。

**Case C — 部分 payload 篡改**（`demo_injection.cpp:45-61`）：

```cpp
void XorPayloadBytes(..., std::size_t instr_index, std::uint8_t mask) {
    const std::size_t unit_offset = instr_index * sim::security::kCipherUnitBytes;
    const std::size_t payload_end = unit_offset + 24;  // 只篡改 payload（前 24 字节）
    for (std::size_t i = unit_offset; i < payload_end; ++i) {
        region->code_memory[i] ^= mask;  // 不动 key_check 和 tag 字段
    }
}
```

只修改了第 4 条指令的 payload 字节，`key_check`（全局字段）未被改动 → Step 1 通过。但 payload 内容变了而 tag 不变 → Step 2 检测到 → `reason=tag_mismatch`。

**这个区分很重要**：它证明加密方案有两层独立保护——密钥认证和完整性校验。不同的篡改方式在不同的检查阶段被捕获，而非笼统地"解密失败"。

#### (d) PVT 不一致 — `demo_cross_process` Case B

```cpp
// demo_cross_process.cpp:122-123
// Bob（handle=2）尝试在自己的 context 下注册 Alice 的页面地址
hardware.GetPvtTable().RegisterPage(bob_handle, alice_base_va, PvtPageType::CODE);
```

PVT 的验证逻辑（`pvt.cpp:48-56`）：

```cpp
// pvt.cpp:50-56
const EwcQueryResult query_result = ewc_.Query(va, handle);  // 查 EWC：Bob 的 context 有没有覆盖 alice_base_va 的窗口？
if (!query_result.matched_window) {                           // 没有 → 不一致
    audit_.LogEvent("PVT_MISMATCH", ...);                     // 记录审计事件
    return PvtRegisterResult{false, pa_page_id, detail};      // 注册失败
}
```

结果：`PVT_MISMATCH`，`reason=missing_window`。恶意 OS 试图把 Alice 的页面映射到 Bob 的地址空间，但 PVT 发现 Bob 的 EWC 窗口不包含这个 VA → 拒绝注册。

**局限性**：当前仅演示了 `missing_window` 这一种检查。PVT 的 owner mismatch 和 VA alias 检查由于 1:1 VA=PA 映射限制（Demo_Claim_Boundary Section 6 已声明）无法演示。后者需要真实的页表翻译层才能有意义。

#### (e) ROP / CFI 违规 — `demo_rop`

**Case B — L3 CFI 下的 ROP 攻击**：

攻击构造（`demo_rop.cpp:31-41`）：

```cpp
std::string MakeRopSource(std::uint64_t bad_addr) {
    oss << "main:\n"
        << "  CALL func\n"          // 正常调用 func（target 在白名单中）
        << "  HALT\n"               // 正常返回地址
        << "func:\n"
        << "  LI x1, " << bad_addr  // 攻击者篡改了链接寄存器 x1
        << '\n'
        << "  RET\n"                // RET 读 x1 → 跳到 bad_addr
        << "landing:\n"
        << "  HALT\n";
}
```

SPE 的 shadow stack 机制检测这个攻击（`spe.cpp:99-137`）：

```
执行 CALL func:
  1. L3 检查: committed_pc（func 的 VA）在 call_targets 白名单中？是 → 通过
  2. push next_pc（HALT 的 VA = 0x4004）到 shadow_stack

执行 LI x1, bad_addr:
  普通指令，SPE 不检查

执行 RET:
  committed_pc = x1 = bad_addr = 0x4010
  shadow_stack.back() = 0x4004（CALL 压入的返回地址）
  0x4010 != 0x4004 → shadow_stack_mismatch → SPE_VIOLATION
```

Demo 输出确认：

```
AUDIT ... type=SPE_VIOLATION ... reason=shadow_stack_mismatch
         cfi_level=3 committed_pc=16400 expected_pc=16388
```

**Case C1/C2 — L1 CFI（无检查）下的多层防御**：

当 CFI 降级到 L1 时，SPE 不做任何控制流检查（`spe.cpp:93-95`）：

```cpp
if (policy.cfi_level <= 1) {
    return SpeCheckResult{};  // allow=true，跳过所有检查
}
```

此时 EWC 成为最后一道防线：
- Case C1：ROP target 在 EWC 窗口内 → 攻击成功（HALT）。说明仅靠 EWC 不足以防御 ROP。
- Case C2：ROP target 在 EWC 窗口外 → `EWC_ILLEGAL_PC`。说明 EWC 仍能拦截窗口外的跳转。

这组对照完整展示了**多层防御**的意义：SPE 拦截语义级攻击（合法地址但非法控制流），EWC 拦截地址级攻击（非法地址）。

---

## 2. Demo_Claim_Boundary_v3.1 过期分析

该文档写于 Issue 4/7/8 尚未实现时。现在这些 Issue 均已完成，以下标签需要更新：

### Section 5 攻击覆盖表

| 行 | 当前标签 | 应更新为 | 理由 |
|----|---------|---------|------|
| 代码页篡改（密文-key 不匹配） | [Scaffolded] | **[Current]** | `demo_injection` 实现了全量和部分篡改两种案例 |
| PVT owner/VA inconsistency | [Scaffolded] | **[Current]（部分）** | `demo_cross_process` Case B 演示了 missing_window 检查；但 anti-alias 因 1:1 VA=PA 仍无法演示 |
| ROP / CFI 违规 | [Scaffolded] | **[Current]** | `demo_rop` 演示了 L3 shadow stack + L1 EWC 后备 |

### 脚注 ¹²³

三个脚注引用的都是"尚未完成的 Issue"：

- ¹ "依赖 Issue 4（pseudo decrypt）完成后可演示" → **Issue 4 已完成，`demo_injection` 已存在**
- ² "依赖 Issue 7（PVT）完成后可演示" → **Issue 7 已完成，`demo_cross_process` 已存在**
- ³ "依赖 Issue 8（SPE）完成后可演示" → **Issue 8 已完成，`demo_rop` 已存在**

### Section 7 攻击覆盖矩阵（表 A 和表 B）

右侧"证据标签"列中，上述三行的 [Scaffolded] 应更新为 [Current]。

### Section 9 验收标准

A3（代码篡改）和 A4（PVT 不一致）的依赖说明应删除或标记为已满足。

### Section 11 实验组织建议（G1 和 G2）

以下 [Scaffolded] 条目已变为 [Current]：
- "correct key / wrong key decryption behavior" → [Current]（`demo_normal` Case B）
- "PVT consistency checks" → [Current]（`demo_cross_process` Case B + `test_pvt.cpp`）
- "代码页篡改" → [Current]
- "PVT owner/VA inconsistency" → [Current]（部分）
- "ROP / CFI violation" → [Current]

---

## 3. 评估单位验证（Section 8 of v3.1）

### E1：路径正确性（Path correctness） — PASS

| 路径 | Demo 证据 |
|------|----------|
| allow path | `demo_normal` Case A → HALT |
| deny path (EWC) | `demo_cross_user` Case B → EWC_ILLEGAL_PC |
| decode-fail path | `demo_normal` Case B / `demo_injection` Cases B,C → DECRYPT_DECODE_FAIL |
| pvt-mismatch path | `demo_cross_process` Case B → PVT_MISMATCH |
| spe-violation path | `demo_rop` Case B → SPE_VIOLATION |

五条路径全部被 demo 覆盖。

### E2：违规到 trap 的正确性（Violation-to-trap） — PASS

每种违规映射到独立的 `TrapReason` 枚举值（定义于 `executor.hpp:14-25`）：

```cpp
enum class TrapReason {
    HALT,               // 正常终止
    INVALID_PC,         // 通用 PC 错误
    INVALID_MEMORY,     // 内存越界
    SYSCALL_FAIL,       // 系统调用失败（保留）
    UNKNOWN_OPCODE,     // 未知指令
    STEP_LIMIT,         // 执行步数超限
    EWC_ILLEGAL_PC,     // 执行授权失败
    DECRYPT_DECODE_FAIL,// 解密/完整性失败
    PVT_MISMATCH,       // 页面验证不一致
    SPE_VIOLATION       // CFI 违规
};
```

没有任何安全违规退化为通用的 "error" 或 "crash"。每种违规都有明确的名称。

### E3：违规到事件的正确性（Violation-to-event） — 部分通过

每种违规都产生审计事件，事件中包含 `context_handle` 和 `pc`。`detail` 字段携带结构化的 key=value 数据：

- EWC：`window_id=none`
- Decrypt：`key_id=11 reason=tag_mismatch`
- PVT：`reason=missing_window context_handle=2 va=4096 ...`
- SPE：`stage=execute op=RET reason=shadow_stack_mismatch cfi_level=3 ...`

**但 user_id 在两类事件中缺失**（见 C4 分析），使得事件无法直接关联到用户。

### E4：上下文敏感的执行语义（Context-sensitive enforcement） — PASS

`demo_cross_user` Case A vs Case C 直接证明：相同 PC（0x1000），不同 context（Alice vs Bob），不同结果（HALT vs EWC_ILLEGAL_PC）。

---

## 4. 缺失的 Baseline

### B1-a：无安全检查 Baseline — 未实现

Demo_Claim_Boundary_v3.1 Section 10 要求：

> "关闭全部安全检查，仅保留 toy CPU 执行。目的：说明安全模块确实改变了系统行为，而不是'可有可无'。"

当前没有任何 demo 展示"关闭安全模块时系统如何表现"。

**为什么重要**：没有这个 baseline，reviewer 可能质疑"安全模块只是在检查本来就会崩溃的条件"。Baseline 应展示：没有 EWC 时，一个非法 PC 不会产生干净的 `EWC_ILLEGAL_PC`，而是产生不确定的行为（可能读到垃圾数据、可能越界崩溃、可能碰巧执行到合法 opcode 而静默错误运行）。

**建议**：新增 `demo_baseline`，对比有/无 EWC 时同一个非法 PC 的行为差异。

### B1-b：消融 Baseline — 未实现

Section 10 要求逐步启用安全模块：

| 配置 | 当前 prototype 对应 |
|------|-------------------|
| CPU only | 无安全检查 |
| + 执行授权 | CPU + Gateway + EWC |
| + 代码完整性 | 上述 + Decrypt/MAC |
| + 数据归属 | 上述 + PVT |
| + 行为合规 | 上述 + SPE |
| + 审计 | 上述 + Audit |

**建议**：新增 `demo_ablation`，用同一个攻击场景（如跨用户执行），逐层启用安全模块，展示每层如何改变系统行为。

---

## 5. Dev Plan 完成度追踪

所有 Issue 已实现。具体对照：

| Issue | 计划产出 | 实际文件 | 状态 | 偏差说明 |
|-------|---------|---------|------|---------|
| Issue 4（伪解密） | `code_codec.hpp/cpp` | `include/security/code_codec.hpp`, `src/security/code_codec.cpp` | 完成 | 位置从 `core/` 移到了 `security/`（合理） |
| Issue 5（Gateway） | `gateway.hpp/cpp`, `test_gateway.cpp` | `include/security/gateway.hpp`, `src/security/gateway.cpp`, `tests/test_gateway.cpp` | 完成 | 无偏差 |
| Issue 6A（内核基础） | `kernel_emu.hpp/cpp` | `include/kernel/process.hpp`, `src/kernel/process.cpp` | 完成 | 命名从 `KernelEmulator` 改为 `KernelProcessTable`（更准确） |
| Issue 6B（上下文切换） | `demo_cross_user.cpp` | `demos/cross_user/demo_cross_user.cpp` | 完成 | 无偏差 |
| Issue 7（PVT） | `pvt.hpp/cpp`, `test_pvt.cpp` | `include/security/pvt.hpp`, `src/security/pvt.cpp`, `tests/test_pvt.cpp` | 完成 | 无偏差 |
| Issue 8（SPE） | `spe.hpp/cpp`, `test_spe.cpp` | `include/security/spe.hpp`, `src/security/spe.cpp`, `tests/test_spe.cpp` | 完成 | 无偏差 |
| Issue 9（5 类 demo） | 5 个 demo 目录 | `demos/{normal,cross_user,cross_process,injection,rop}` | 完成 | 无偏差 |
| Issue 10（审计正规化） | `audit.hpp/cpp` | `include/security/audit.hpp`, `src/security/audit.cpp` | 完成 | 位置从 `core/` 移到了 `security/`（合理） |
| Issue 11（SecureIR 生成） | `tools/secureir_gen.cpp` 或库函数 | `include/security/securir_builder.hpp`, `src/security/securir_builder.cpp` | 完成 | 实现为库而非 CLI 工具（更灵活） |

---

## 6. 安全管线正确性分析

### 6.1 执行顺序

`executor.cpp` 中安全检查的完整执行顺序：

```
[Fetch 阶段]
  1. EWC 查询               (executor.cpp:128)    执行授权
  2. PC 下界检查             (executor.cpp:135)    地址可达性
  3. 对齐检查               (executor.cpp:142)    结构合法性
  4. 代码内存读取            (executor.cpp:170)    数据访问

[Decode 阶段]
  5. key_check 验证          (code_codec.cpp:218)  密钥认证
  6. tag 验证                (code_codec.cpp:223)  完整性（MAC 等价物）
  7. XOR 解密                (code_codec.cpp:228)  机密性
  8. magic 检查              (code_codec.cpp:233)  结构合法性

[Execute 阶段]
  9. 指令分派                (executor.cpp:328)    计算
  10. committed_pc 计算      (各指令内部)          控制流解析

[提交前]
  11. SPE 检查               (executor.cpp:457)    行为合规（CFI）
  12. PC 提交                (executor.cpp:465)    状态更新
```

该顺序与设计文档的要求一致：EWC 在 Fetch、解密在 Decode、SPE 在 Decode/Execute。授权检查在数据访问之前，完整性检查在执行之前，CFI 检查在 PC 提交之前。

### 6.2 Fail-safe 默认值

| 组件 | 默认值 | 安全含义 |
|------|--------|---------|
| `EwcQueryResult::allow` | `false`（`ewc.hpp:34`） | **默认拒绝** —— 正确 |
| `DecryptResult::ok` | `false`（`code_codec.hpp:30`） | **默认失败** —— 正确 |
| `PvtRegisterResult::ok` | `false`（推断自 `pvt.hpp`） | **默认拒绝** —— 正确 |
| `SpeCheckResult::allow` | `true`（`spe.hpp:17`） | **默认允许** —— 合理 |

前三个是 fail-closed（未配置时拒绝），符合安全原则。SPE 默认允许是因为 CFI 策略是可选的（没有配置 CFI 的程序应该能正常运行），这与设计一致。

### 6.3 HALT 绕过 SPE 检查

```cpp
// executor.cpp:432-435
case sim::isa::Op::HALT: {
    result.state.regs[0] = 0;
    result.trap = Trap{TrapReason::HALT, fetched.pc, "halt"};
    return result;   // 直接返回，不经过 line 457 的 SPE 检查
}
```

HALT 在 SPE 检查**之前**就 return 了。这在语义上是正确的：HALT 是终止指令，没有"下一个 PC"需要 CFI 验证。程序无论 CFI 状态如何都能正常终止。Trap 也是终止——只是终止原因不同。

### 6.4 代码/数据分离（W-xor-X）

代码和数据存储在完全分离的内存区域中：

- 代码：`CodeRegion.code_memory`（per-context，通过 `SecurityHardware` 管理）
- 数据：`CpuState.mem`（per-execution，在 `ExecuteProgram` 中分配）

Executor 中没有任何路径可以：
- 从 `CpuState.mem` 获取指令（Fetch 只读 `code_memory`）
- 向 `code_memory` 写入数据（`ST` 指令只写 `CpuState.mem`）

这通过**构造**实现了 W-xor-X，与 Issue6A_Decision_Summary 中的 Decision D2 一致。

---

## 7. Cross-process Demo 深度分析

`demo_cross_process` 是最复杂的 demo，展示了三个渐进的安全场景，构成防御纵深叙事。

### Case A：正常流程（PVT 注册成功）

```
Alice 加载 SecureIR，pages=[{va=0x1000, page_type=CODE}]
KernelProcessTable::LoadProcess 调用 PVT::RegisterPage
PVT 查询 EWC：Alice 的 context 有覆盖 0x1000 的窗口？→ 有 → 注册成功
Alice 运行 → HALT
```

展示 PVT 的 happy-path：当页面 VA 与 EWC 窗口一致时，注册成功。

### Case B：恶意映射（PVT 拦截）

```
Alice 正常加载（handle=1，窗口在 0x1000）
Bob 正常加载（handle=2，窗口在 0x2000）
恶意 OS 调用：pvt.RegisterPage(bob_handle=2, va=0x1000, CODE)
PVT 查询 EWC：Bob 的 context 有覆盖 0x1000 的窗口？→ 没有 → PVT_MISMATCH
```

展示 PVT 的**第一层防御**：恶意 OS 试图把 Alice 的页面映射到 Bob 的地址空间，PVT 发现 Bob 的 EWC 窗口不包含这个 VA。

### Case C：纵深防御（EWC 兜底）

```
Bob 正常加载（handle=1，窗口在 0x2000）
恶意 OS 直接把 Alice 的加密代码写入 Bob 的 code_memory 区域
  （绕过 PVT，直接修改 SecurityHardware 中的 CodeRegion）
OS 设置 entry_pc = alice_base_va = 0x1000
Bob 的 context 执行 → Fetch 阶段查 EWC → Bob 的窗口只在 0x2000，0x1000 无匹配
→ EWC_ILLEGAL_PC
```

代码中实现为（`demo_cross_process.cpp:167`）：

```cpp
hardware.StoreCodeRegion(bob_handle, alice_base_va, alice_code_memory);
// 直接往 Bob 的 code region 写入 Alice 的代码（模拟恶意 OS 绕过 PVT）
```

即使 OS 绕过了 PVT（直接操作 code_memory），EWC 仍然拒绝执行——因为 Bob 的 EWC 窗口只覆盖 0x2000，不覆盖 0x1000。

Demo 输出中的注释准确总结了这一点：

```
CASE_C_NOTE=PVT can be bypassed by a malicious OS write, but EWC still denies fetch at alice_base_va.
CASE_C_NOTE_2=PVT and EWC are complementary independent enforcement layers.
```

**叙事价值**：Case B 和 Case C 共同说明了纵深防御的意义 —— PVT 是第一层（在页面注册时检查），EWC 是第二层（在每次 Fetch 时检查）。攻击者需要同时绕过两者才能成功。

---

## 8. 测试覆盖评估

| 模块 | 测试文件 | 测试数量 | 覆盖要点 |
|------|---------|---------|---------|
| ISA 汇编器 | `test_isa_assembler.cpp` | 15 | label 解析、所有 opcode、内存操作数、错误用例 |
| Executor | `test_executor.cpp` | 16+ | 算术、控制流、内存、EWC、解密、SPE |
| Gateway | `test_gateway.cpp` | 10+ | 加载/释放、校验、容量限制、审计 |
| Kernel | `test_kernel_process.cpp` | 13+ | 进程生命周期、上下文切换、失败回滚 |
| PVT | `test_pvt.cpp` | 8+ | 注册、owner 推导、权限检查 |
| SPE | `test_spe.cpp` | 13+ | L1/L2/L3、shadow stack、目标白名单 |
| SecureIrBuilder | `test_securir_builder.cpp` | 6+ | 构建、round-trip、多窗口、CFI 传播 |

**覆盖缺口**：

1. `AuditCollector::Clear()` 后 `seq_no` 继续递增的行为没有专门测试。`audit.cpp:26` 只清空事件向量但不重置 `next_seq_no_`——这很可能是有意设计（保证全局单调递增），但没有测试断言。

2. Gateway `GATEWAY_LOAD_FAIL` 审计事件（`gateway.cpp:466` 的异常路径）虽然被错误测试间接触发，但没有显式验证该审计事件的内容。

---

## 9. 代码质量发现

### 9.1 `OpToString` 重复定义 [低]

两处完全相同的实现：

| 位置 | 行数 |
|------|------|
| `executor.cpp:75-103` | 28 行 |
| `spe.cpp:10-38` | 28 行 |

两者都是 `sim::isa::Op` 到 C-string 的 switch 语句，逻辑完全一致。建议提取到共享位置（如 `isa/opcode.hpp` 中增加 utility 函数）。

### 9.2 `demo_normal.cpp` 中的死代码 [低]

```cpp
// demo_normal.cpp:16-18
std::uint64_t ProgramEndVa(const sim::isa::AsmProgram& program) {
    return program.base_va + program.code.size() * sim::isa::kInstrBytes;
}
```

该函数已定义但从未被调用。应删除。

### 9.3 EWC Query 使用线性搜索而非二分搜索 [低]

`EwcTable::SetWindows` 对窗口按 `start_va` 排序（`ewc.cpp:10-11`）：

```cpp
std::sort(windows.begin(), windows.end(),
          [](const ExecWindow& lhs, const ExecWindow& rhs) { return lhs.start_va < rhs.start_va; });
```

但 `EwcTable::Query`（`ewc.cpp:49-59`）使用线性遍历：

```cpp
for (const ExecWindow& window : it->second) {
    if (window.start_va <= pc && pc < window.end_va) { ... }
}
```

窗口已排序且不重叠，适合使用 `std::lower_bound` 做二分搜索。在 demo 的 1-2 个窗口规模下无性能影响，但排序暗示了二分搜索的意图，两者不一致。

### 9.4 SPE `stage` 标签不一致 [低]

`spe.cpp` 中 CALL/J/BEQ 的违规被标记为 `stage="decode"`：

```cpp
// spe.cpp:101
result = MakeViolationDetail("decode", op, "call_target_not_allowed", ...);
```

但在 Executor 中，SPE 实际在 line 457 执行检查——这是在 Execute 阶段**之后**，不是 Decode 阶段。RET 违规正确地标记了 `stage="execute"`（`spe.cpp:127,134`），但 CALL/J/BEQ 应该也是 `"execute"` 而非 `"decode"`。

### 9.5 Gateway handle 计数器在失败时也递增 [低]

```cpp
// gateway.cpp:407
const ContextHandle handle = next_handle_++;  // 在容量检查之前就递增了
```

如果 `Load()` 抛出异常（解析错误、容量超限等），handle 计数器仍然前进。多次失败后计数器会远大于实际活跃 handle 数。对 demo 无实际影响，但如果有人检查 handle 值会觉得奇怪。

---

## 10. 设计文档与实现的偏差

### 10.1 SecureIR 格式演进

`spec_i2_i7.md` 描述 SecureIR 为简单的 JSON（包含 `user_id`, `signature`, `code_sections`, `entry_offset`）。

实际实现（`securir_builder.cpp:143-161`）生成的 JSON 格式更丰富：

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

新增了 `pages`、`cfi_level`、`call_targets`、`jmp_targets`、`windows`（替代原来的 flat `code_sections`）。这是 Issue 7/8 实现过程中的自然演进。建议更新 spec 文档以反映最终格式。

### 10.2 签名验证

```cpp
// gateway.cpp:419-421
if (secure_ir.signature.empty()) {
    throw std::runtime_error("gateway_invalid_signature reason=empty");
}
```

签名验证是 stub，仅检查非空。这与 spec_i2_i7 的 "signature (stub)" 和 Demo_Claim_Boundary 的 N1（"不证明真实密码学可落地"）一致。Stub 行为正确且范围恰当。

### 10.3 SecureIR 明文/密文分区

system_design_v4（DP-9）规定：

> "SecureIR 明文区包含 user_pubkey, signature, code/data segment layout；K-加密区包含 code/data content + MAC, entry_offset, CFI level"

当前实现**没有**将 SecureIR 分为明文/密文两个区域。所有元数据（包括 `cfi_level`、`call_targets`、`base_va`）都在明文 JSON 中，只有代码字节本身是加密的。

这在 prototype 范围内可接受（N1 适用），但如果论文讨论明文/密文分区作为安全属性（OS 能看到布局但看不到内容/策略），当前 demo 不能演示这一点。

---

## 11. 改进建议（按优先级）

### 高优先级（影响论文 claim 有效性）

| # | 条目 | 工作量 | 影响 |
|---|------|--------|------|
| H1 | 修复 `EWC_ILLEGAL_PC` 审计事件 `user_id=0` | 小（增加 handle→user_id 查询） | 强化 C4 claim |
| H2 | 修复 `PVT_MISMATCH` 审计事件 `user_id=0`（missing_window 分支） | 小 | 同上 |
| H3 | 更新 Demo_Claim_Boundary_v3.1.md 证据标签 | 小（纯文档修改） | 使文档与现实对齐 |

### 中优先级（影响论文 evaluation 完整性）

| # | 条目 | 工作量 | 影响 |
|---|------|--------|------|
| M1 | 新增无安全检查 baseline demo（B1-a） | 中 | v3.1 Section 10 要求 |
| M2 | 新增消融 baseline demo（B1-b） | 中-大 | 同上 |
| M3 | 在 `demo_cross_process` 中补充 PVT owner mismatch 案例 | 小 | 完善 A4 验收覆盖 |

### 低优先级（代码质量）

| # | 条目 | 工作量 | 影响 |
|---|------|--------|------|
| L1 | 提取共享 `OpToString` 工具函数 | 极小 | 消除重复代码 |
| L2 | 删除 `demo_normal.cpp` 中未使用的 `ProgramEndVa` | 极小 | 代码整洁 |
| L3 | 修正 SPE 中 CALL/J/BEQ 的 `stage="decode"` 为 `"execute"` | 极小 | 标签准确性 |
| L4 | EWC Query 改为二分搜索 | 小 | 与排序意图一致 |
| L5 | 更新 dev_plan.md 中各 Issue 状态标记 | 小 | 文档卫生 |

---

## 12. 总结

代码仓库成功实现了 dev_plan.md 中 Issue 4-11 的全部目标，五类攻击叙事均有可运行的 demo 覆盖。安全执行管线的检查顺序正确（EWC → Decrypt → Execute → SPE），每个执行点都有 fail-safe 默认值和审计事件发射。

最值得关注的三个发现：

1. **两类审计事件 user_id=0**（`EWC_ILLEGAL_PC` 和 `PVT_MISMATCH`）—— 代码修改量很小，但能显著增强 C4（安全事件可观测性）的 claim 强度。根因是 `SecurityHardware` 缺少 `handle → user_id` 映射。

2. **Demo_Claim_Boundary_v3.1.md 过期** —— 三个证据标签需要从 [Scaffolded] 升级为 [Current]，三个脚注的依赖 Issue 均已完成。文档与代码现状不一致。

3. **缺少 Baseline demo** —— 文档承诺了无安全检查 baseline 和消融 baseline，但尚未实现。这些对论文 evaluation 部分的说服力很重要——它们证明安全模块不是"可有可无"的，每一层都有独立贡献。

其余发现（代码重复、标签不一致、死代码）为次要清理项，不影响功能或 claim 有效性。
