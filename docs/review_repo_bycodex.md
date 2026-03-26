# 仓库审查报告（by Codex）

> 审查时间：2026-03-26  
> 审查范围：`system_design_v4.md`、`dev_plan.md`、`Demo_Claim_Boundary_v3.1.md`、`progress_tracking_v3.md`、`project_log.md`、`spec_i2_i7.md` 与当前仓库实现  
> 审查方式：文档对照 + 源码静态审查 + 构建/测试 + demo 实跑

---

## 1. 执行摘要

### 1.1 总体判断

当前仓库**已经实现了一个可运行、可观测、且测试基本完整的 prototype**。如果以 `docs/spec_i2_i7.md` 作为当前原型的最高优先级规范，仓库整体上是**符合要求且明显超出最小范围**的：

- `Gateway / EWC / PVT / SPE / Audit / context_handle / demo` 均已有实现；
- `Fetch -> EWC -> Decrypt -> Decode -> Execute` 的关键路径已真实存在；
- `ctest` 全绿；
- 5 个 demo 可运行，并分别展示 `HALT`、`EWC_ILLEGAL_PC`、`DECRYPT_DECODE_FAIL`、`PVT_MISMATCH`、`SPE_VIOLATION`。

但如果把 `docs/system_design_v4.md` 也作为严格实现目标，则当前仓库**还不能算完全符合 v4 全部语义**。主要缺口有三类：

1. **v4 的 hidden entry / saved_PC 启动语义未实现**。  
2. **DP-8 中“同用户不同进程隔离 = EWC per-process + SPE bounds”只实现了前半，未实现 bounds。**  
3. **`Demo_Claim_Boundary_v3.1.md` 与当前仓库证据集发生漂移。**

### 1.2 结论分级

#### 可以明确判定“已做到”的部分

- C1：最小控制路径存在性
- C2：非法执行入口被结构化拦截
- C3：`context_handle` 会改变执行语义
- C4：关键安全事件可观测
- A1：`demo_normal`
- A2：`demo_cross_user`
- A3：代码页篡改导致 `DECRYPT_DECODE_FAIL`
- A4：PVT 不一致导致 `PVT_MISMATCH`
- ROP / CFI 违规导致 `SPE_VIOLATION`

#### 不能说“完全做到”的部分

- v4 中“OS 不可见 entry / Gateway 初始化 saved_PC / 首次执行从 saved_PC 恢复”
- DP-8 中“同用户不同进程数据隔离依赖 SPE bounds”
- Claim boundary 中要求的 baseline / ablation 体系

---

## 2. 我实际做了什么检查

### 2.1 文档对照

我对照阅读了：

- `docs/system_design_v4.md`
- `docs/dev_plan.md`
- `docs/Demo_Claim_Boundary_v3.1.md`
- `docs/progress_tracking_v3.md`
- `docs/project_log.md`
- `docs/spec_i2_i7.md`

重点核对了以下问题：

- claim 是否有代码与 demo 证据支撑；
- 关键 trap 是否在正确阶段触发；
- `context_handle` 是否真实参与安全判定；
- PVT / SPE 是否为真实机制而非纯占位；
- 当前代码是否和 v4 中的关键设计点冲突。

### 2.2 构建与测试

已执行：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

结果：

- `8/8` 测试通过：
  - `sanity`
  - `isa_assembler`
  - `executor`
  - `gateway`
  - `kernel_process`
  - `pvt`
  - `spe`
  - `securir_builder`

### 2.3 demo 实跑

已执行：

```bash
./build/demo_normal
./build/demo_cross_user
./build/demo_cross_process
./build/demo_injection
./build/demo_rop
```

实跑结果与程序返回码一致，均成功完成预期叙事。

---

## 3. claim 与代码证据映射

本节回答一个最核心的问题：

> “最典型的功能实现和 claim，到底是在哪部分代码做的？”

---

### 3.1 C1：最小控制路径存在性

**claim**  
`gateway_load -> secure_context_switch -> Fetch -> EWC -> Decrypt -> Decode -> Execute`

**证据代码**

#### 入口编排

- `Gateway::Load()`：加载 SecureIR、配置 EWC/SPE、存 code region  
  位置：`src/security/gateway.cpp`
- `KernelProcessTable::ContextSwitch()`：切换 active handle 并记录 `CTX_SWITCH`  
  位置：`src/kernel/process.cpp`
- `ExecuteProgram()`：执行主循环  
  位置：`src/core/executor.cpp`

**关键代码路径**

1. `Gateway::Load()`  
   - 解析 metadata  
   - 构造 `ExecWindow`  
   - `hardware_.GetEwcTable().SetWindows(handle, ...)`
   - `hardware_.GetSpeTable().ConfigurePolicy(...)`
   - `hardware_.StoreCodeRegion(handle, secure_ir.base_va, ...)`

2. `KernelProcessTable::ContextSwitch(handle)`  
   - `hardware_.SetActiveHandle(handle)`
   - 记录 `CTX_SWITCH`

3. `ExecuteProgram(entry_pc, options)`  
   - 读取 active handle  
   - 读取该 handle 对应 code region  
   - 进入循环：
     - `FetchStage(...)`
     - `DecodeStage(...)`
     - `switch(instr.op)`
     - `SpeTable::CheckInstruction(...)`

**结论**

这个最小控制路径不是“文档中的意图”，而是仓库里真实存在且可运行的执行链。

---

### 3.2 C2：非法执行入口被结构化拦截

**claim**  
非法 PC 不会退化成随机崩溃，而是在 Fetch 阶段先被 EWC 显式拦截，并产生 `EWC_ILLEGAL_PC`。

**证据代码**

#### Fetch 阶段优先做 EWC

`src/core/executor.cpp` 中的 `FetchStage(...)`：

```cpp
const sim::security::EwcQueryResult query_result = ewc.Query(pc, context_handle);
if (!query_result.allow) {
  *trap = Trap{TrapReason::EWC_ILLEGAL_PC, pc, MakeEwcDenyMsg(...)};
  *deny_detail = MakeEwcDenyDetail(query_result);
  return false;
}
```

这说明：

- 先查 EWC；
- deny 直接 trap；
- 后续地址下溢、对齐、越界检查只在 EWC allow 后才进行。

#### 审计事件同步写出

`ExecuteProgram()` 中：

```cpp
if (!FetchStage(...)) {
  if (!deny_detail.empty()) {
    LogAudit(audit, "EWC_ILLEGAL_PC", 0, context_handle, fetch_trap.pc, std::move(deny_detail));
  }
  result.trap = std::move(fetch_trap);
  break;
}
```

**测试证据**

- `tests/test_executor.cpp`
  - `Execute_EwcDenies_AtEntry_TrapsEwcIllegalPc`
  - `Execute_EwcSubsetWindow_JumpOut_TrapsEwcIllegalPc`

**demo 证据**

- `demo_cross_user`
  - Bob 跳 Alice 地址
  - 恶意 OS 直接用 Alice 地址作为 entry
  - 两种都落到 `EWC_ILLEGAL_PC`

**结论**

这条 claim 是**成立且证据强**的。实现顺序正确，测试和 demo 都覆盖了。

---

### 3.3 C3：`context_handle` 会改变执行语义

**claim**  
`context_handle` 不是装饰变量，而是会真实改变 EWC 判定结果。

**证据代码**

#### EWC 的状态按 `context_handle` 分桶

`include/security/ewc.hpp`

```cpp
std::unordered_map<ContextHandle, std::vector<ExecWindow>> windows_by_context_;
```

`src/security/ewc.cpp`

```cpp
auto it = windows_by_context_.find(context_handle);
```

也就是说，同一个 PC 在不同 handle 下，查的是不同窗口表。

#### context switch 真正改变 active handle

`src/kernel/process.cpp`

```cpp
hardware_.SetActiveHandle(handle);
active_handle_ = handle;
audit_.LogEvent("CTX_SWITCH", ...);
```

#### ExecuteProgram 从 hardware 读取 active handle

`src/core/executor.cpp`

```cpp
const std::optional<ContextHandle> active_handle = options.hardware->GetActiveHandle();
```

这意味着执行器不是拿调用者传进来的“假 handle”，而是以硬件当前 active handle 为准。

**demo 证据**

- `demo_cross_user`
  - Alice context 下运行 Alice 代码：`HALT`
  - Bob context 下执行 Alice 地址：`EWC_ILLEGAL_PC`

**结论**

这条 claim 也是**强成立**的。`context_handle` 的确改变了 active execution windows。

---

### 3.4 C4：关键安全事件可观测

**claim**  
关键事件可输出为审计流，而不只是内部静默拦截。

**证据代码**

#### 审计结构

`include/security/audit.hpp`

```cpp
struct AuditEvent {
  std::uint64_t seq_no;
  std::string type;
  std::uint32_t user_id;
  std::uint32_t context_handle;
  std::uint64_t pc;
  std::string detail;
};
```

#### 各模块写审计

- Gateway：
  - `GATEWAY_LOAD_OK`
  - `GATEWAY_LOAD_FAIL`
  - `GATEWAY_RELEASE`
- Kernel：
  - `CTX_SWITCH`
- Executor：
  - `EWC_ILLEGAL_PC`
  - `DECRYPT_DECODE_FAIL`
- PVT：
  - `PVT_MISMATCH`
- SPE：
  - `SPE_VIOLATION`

#### demo 输出

各 demo 均遍历：

```cpp
for (const auto& event : audit.GetEvents()) {
  std::cout << "AUDIT " << FormatAuditEvent(event) << '\n';
}
```

**结论**

“可观测性”是成立的，而且做得比较统一。

---

### 3.5 代码页篡改 claim：`DECRYPT_DECODE_FAIL`

**当前仓库状态**

虽然 `Demo_Claim_Boundary_v3.1.md` 仍写 `[Scaffolded]`，但**代码和 demo 事实上已经支撑这条 claim**。

**证据代码**

#### 伪加解密与校验

`src/security/code_codec.cpp`

- `EncryptProgram(...)`
- `DecryptInstr(...)`

关键行为：

- `key_check` 不匹配 -> `key_check_mismatch`
- `tag` 不匹配 -> `tag_mismatch`
- 明文反序列化失败 -> `decode_mismatch`

#### 执行器把解密失败映射成明确 trap

`src/core/executor.cpp`

```cpp
if (!decrypt_result.ok) {
  *trap = Trap{TrapReason::DECRYPT_DECODE_FAIL, ...};
  *decrypt_detail = ...
  return false;
}
```

#### 审计

```cpp
LogAudit(audit, "DECRYPT_DECODE_FAIL", fetched.owner_user_id, context_handle, decode_trap.pc, ...);
```

**测试证据**

- `Execute_PseudoDecrypt_CorrectKey_RunsToHalt`
- `Execute_PseudoDecrypt_WrongKey_TrapsDecryptDecodeFail`
- `Execute_PseudoDecrypt_TamperedCiphertext_TrapsDecryptDecodeFail`

**demo 证据**

- `demo_normal` 的 wrong-key case
- `demo_injection`
  - full tamper -> `DECRYPT_DECODE_FAIL`
  - partial tamper -> `DECRYPT_DECODE_FAIL`

**结论**

这条 claim 实际上应从 `[Scaffolded]` 升级为 **`[Current]`**。

---

### 3.6 PVT owner / VA inconsistency claim：`PVT_MISMATCH`

**当前仓库状态**

这条也已经不只是 scaffold。虽然仍受 1:1 VA=PA 限制，**无法证明 alias 类检查**，但“owner/VA 与窗口不一致会被 PVT 拒绝并审计”这部分已经成立。

**证据代码**

#### PVT 从 EWC 读取可信窗口信息

`src/security/pvt.cpp`

```cpp
const EwcQueryResult query_result = ewc_.Query(va, handle);
```

如果没有窗口：

```cpp
audit_.LogEvent("PVT_MISMATCH", ... "reason=missing_window");
return PvtRegisterResult{false, ...};
```

如果 `page_type` 与 permissions 不匹配：

```cpp
audit_.LogEvent("PVT_MISMATCH", ... "reason=page_type_permissions");
return PvtRegisterResult{false, ...};
```

**测试证据**

- `RegisterPage_CodePage_NoWindow_Fails`
- `RegisterPage_CodePage_OwnerMismatch_Fails`
- `RegisterPage_PageTypePermissionsMismatch_Fails`

**demo 证据**

- `demo_cross_process`
  - CASE_B：Bob 尝试把 Alice 页注册到 Bob handle
  - 结果：`PVT_MISMATCH`

**结论**

这条 claim 也应视为**部分 Current**：

- `owner/window mismatch -> PVT_MISMATCH`：已做到
- `反 alias 检查`：尚未做到

更准确的说法应是：

> PVT 的最小一致性检查已 Current，但基于真实 VA/PA 分离的 alias 语义仍未覆盖。

---

### 3.7 ROP / CFI claim：`SPE_VIOLATION`

**当前仓库状态**

仓库里的 SPE 已经不是 placeholder，而是一个可运行的最小 CFI 机制。

**证据代码**

#### SPE 状态

`include/security/spe.hpp`

```cpp
struct Policy {
  std::uint32_t user_id;
  std::uint32_t cfi_level;
  std::vector<std::uint64_t> call_targets;
  std::vector<std::uint64_t> jmp_targets;
  std::vector<std::uint64_t> shadow_stack;
};
```

#### SPE 检查逻辑

`src/security/spe.cpp`

- `CALL`：
  - L3 校验 `call_targets`
  - push shadow stack
- `J / BEQ(taken)`：
  - L3 校验 `jmp_targets`
- `RET`：
  - L2/L3 校验 shadow stack

违反时：

```cpp
audit_collector_.LogEvent("SPE_VIOLATION", ...);
```

#### 执行器接入点

`src/core/executor.cpp`

```cpp
const SpeCheckResult spe_result =
    options.hardware->GetSpeTable().CheckInstruction(...);
if (!spe_result.allow) {
  result.trap = Trap{TrapReason::SPE_VIOLATION, ...};
  break;
}
```

**测试证据**

- `SPE_L3_CallIllegalTarget_TriggersTrap`
- `SPE_L3_JumpIllegalTarget_TriggersTrap`
- `SPE_L2_TamperedReturn_TriggersTrap`
- `SPE_L1_IllegalJump_DoesNotTrap`

**demo 证据**

- `demo_rop`
  - L3 正常：`HALT`
  - L3 ROP：`SPE_VIOLATION`
  - L1 窗口内：`HALT`
  - L1 窗口外：`EWC_ILLEGAL_PC`

**结论**

这条 claim 也已经应从 `[Scaffolded]` 升级为 **`[Current]`**。

---

## 4. 当前实现最不完善的地方

本节按“影响实际 claim 严谨性”的程度排序。

---

### 4.1 未实现 v4 的 hidden entry / saved_PC 语义

#### 为什么说这是缺口

`system_design_v4.md` 明确要求：

- `entry_offset` 在加密部分；
- `entry_va` 对 OS 不可见；
- Gateway 初始化 `saved_PC = entry_va`；
- OS 不直接拿入口 VA 启动程序。

但当前实现是：

- SecureIR metadata 里明文携带 `base_va`；
- Gateway 返回 `base_va`；
- demo 直接 `ExecuteProgram(base_va, ...)`。

这与 v4 的“OS 只知道形状，不知道真实入口点”的语义不同。

#### 证据

文档：

- `docs/system_design_v4.md`
  - `entry_va OS 无法计算`
  - `saved_PC = entry_va`
  - `不返回 entry_va`

实现：

- `src/security/gateway.cpp`
  - `secure_ir.base_va = ...`
  - `result.base_va = secure_ir.base_va`
- `demos/normal/demo_normal.cpp`
  - `ExecuteProgram(base_va, ...)`

#### 风险

这会导致：

- 如果以后你要把仓库说成“符合 v4 启动语义”，会被 reviewer 很容易抓住；
- 当前 demo 证明的是“OS 知道入口并直接启动”的模型，而不是 v4 的 hidden-entry 模型。

#### 改进建议

建议后续按下面方向改：

1. SecureIR builder 增加 `entry_offset` 概念。
2. `Gateway::Load()` 解析后只返回 `context_handle` 与布局信息，不返回真实入口点。
3. `SecurityHardware` 或 `SecurityContext` 中增加 `saved_pc`。
4. `KernelProcessTable::ContextSwitch()` 后，由执行器从 active context 的 `saved_pc` 启动，而不是从外部显式传 `entry_pc`。

这是当前最重要的架构级改进点。

---

### 4.2 DP-8 只做了 EWC per-process，没有实现 SPE bounds

#### 为什么说不完整

`progress_tracking_v3.md` 与 `system_design_v4.md` 都把同用户不同进程隔离描述为：

> EWC per-process + SPE bounds

当前仓库确实实现了：

- per-handle EWC window 隔离；
- per-handle SPE CFI / shadow stack；

但**没有任何 bounds 数据结构，也没有 Memory 阶段 bounds 检查**。

#### 证据

`include/security/spe.hpp` 里 `Policy` 只有：

- `cfi_level`
- `call_targets`
- `jmp_targets`
- `shadow_stack`

没有：

- `bounds_entries`
- `resource_bounds`
- 内存访问权限范围

`src/core/executor.cpp` 中的 SPE 调用也发生在指令语义执行之后，只根据控制流结果检查，不对 `LD/ST` 做额外资源边界约束。

#### 风险

这意味着当前仓库可以证明：

- “跨 handle 的代码执行授权变化”
- “CFI 违规被拦截”

但还不能证明：

- “同用户不同进程数据段被 bounds 独立隔离”

#### 改进建议

建议把这件事从“概念上支持”改为“最小可演示支持”：

1. 在 SPE policy 中增加最小 `bounds` 项。
2. 对 `LD/ST` 接入 bounds 检查。
3. 增加一个真正的 same-user two-process demo：
   - 两个进程 `user_id` 相同；
   - `context_handle` 不同；
   - 一个进程试图访问另一个进程的数据区；
   - 由 SPE bounds 拦截。

如果不打算近期实现，那就不要把当前仓库叙述成已经完成 DP-8 的全部语义。

---

### 4.3 `demo_cross_process` 名字和语义不完全一致

#### 为什么说不完善

从文件名看，`demo_cross_process` 应该主要证明：

- 同用户不同进程；
- 两层隔离模型中的“第二层”。

但当前 demo 里的两个配置是：

- Alice: `user_id = 1001`
- Bob: `user_id = 1002`

所以它更接近：

- 跨用户错误映射；
- PVT + EWC defense-in-depth；

而不是严格意义上的 same-user cross-process。

#### 风险

这会在演示或写论文时产生语义混淆：

- 文件名说 cross-process；
- 实际叙事更像 cross-user / malicious remap。

#### 改进建议

二选一更清晰：

1. 保留现有内容，但把 demo 重命名为更准确的名字，例如 `demo_pvt_mapping`。
2. 另做一个真正的 same-user cross-process demo，复用 `user_id`，只切换 `context_handle`。

---

### 4.4 `EWC_ILLEGAL_PC` 的 audit `user_id=0`

#### 为什么说不完善

当前 `EWC_ILLEGAL_PC` 的审计事件写入时：

```cpp
LogAudit(audit, "EWC_ILLEGAL_PC", 0, context_handle, ...);
```

也就是说：

- `context_handle` 有；
- `pc` 有；
- 但 `user_id` 被固定写成了 `0`。

这不影响功能拦截，但会影响 claim boundary 中的：

- `Violation-to-event correctness`
- “事件可绑定到 user/context”

#### 风险

现在需要依赖前一条 `CTX_SWITCH` 才能推断归属。  
如果单独看 `EWC_ILLEGAL_PC` 事件，归属不完整。

#### 改进建议

推荐做法：

1. 在 `SecurityHardware` 中提供 `active_handle -> user_id` 查询；
2. 或者让 EWC query 在 deny 时也能带出当前 active context 的 owner；
3. 至少在 `LogAudit(EWC_ILLEGAL_PC, ...)` 时填入 active process 的 user_id。

这属于“小改动、高收益”的一致性修复项。

---

### 4.5 Claim Boundary 文档已经落后于代码

#### 为什么说不完善

当前 `docs/Demo_Claim_Boundary_v3.1.md` 中仍标记：

- 代码页篡改：`[Scaffolded]`
- PVT owner/VA inconsistency：`[Scaffolded]`
- ROP / CFI：`[Scaffolded]`

但仓库里已经有：

- 对应测试；
- 对应 demo；
- 对应 trap；
- 对应审计输出。

反过来，文档里写“当前阶段必须有 baseline / ablation”，仓库里却没有对应 demo 或 target。

#### 风险

这会直接影响你后续对外叙述：

- 低估已经完成的东西；
- 或者高估 baseline 的完备程度。

#### 改进建议

建议后续单独更新 claim 文档：

1. 把已被 demo 支撑的条目升级为 `[Current]`。
2. 明确保留的限制：
   - 无 alias 检查
   - 无 hidden entry
   - 无 bounds
3. 如果短期不实现 baseline，就把 “must have baseline” 改成 “recommended next-step baseline”。

---

## 5. 代码质量与风格层面的观察

这部分不是功能性 blocker，但对后续维护有影响。

---

### 5.1 优点

#### 模块边界比较清楚

- `Gateway` 负责 load / release
- `EwcTable` 负责执行窗判定
- `PvtTable` 负责页归属校验
- `SpeTable` 负责 CFI
- `SecurityHardware` 负责聚合硬件状态
- `KernelProcessTable` 负责 OS 侧编排

这让仓库虽然是 toy prototype，但结构不是“全塞在 executor 里”。

#### trap / audit 命名基本统一

`TrapReason`、`AuditEvent.type`、demo 输出名称高度一致，利于读者理解。

#### 测试覆盖方向合理

测试不是只测 happy path，而是覆盖了：

- Gateway 回滚
- EWC deny
- 解密失败
- PVT mismatch
- SPE violation

这对 prototype 很重要。

---

### 5.2 一般性建议

#### 建议 1：让 demo 名称和语义更严格对齐

特别是 `cross_process`，当前名字会误导。

#### 建议 2：减少 demo 中重复的“打印 artifacts”样板

多个 demo 都有重复的：

- `PrintArtifacts`
- `RunProgram`
- `MakeConfig`

后续可抽一个 `demo_common.hpp`，不会改变功能，但会减少噪音。

#### 建议 3：把“安全语义”和“运行入口”进一步收口

当前 `ExecuteProgram(entry_pc, options)` 仍让外部决定起始 PC。  
这和 v4 的方向不一致。后续如果做 hidden-entry，建议统一改成：

```cpp
ExecuteActiveProgram(options)
```

由硬件安全上下文决定入口。

---

## 6. 关于 Demo_Claim_Boundary_v3.1 的具体建议

下面是我认为最应该更新的几条：

### 6.1 应从 `[Scaffolded]` 升级为 `[Current]`

- 代码页篡改（密文-key 不匹配）
- ROP / CFI 违规

### 6.2 建议升级为“部分 Current / 有限制的 Current”

- PVT owner/VA inconsistency

原因：

- `missing_window / page_type_permissions` 已可演示；
- 但 alias 相关仍未覆盖。

### 6.3 不应写成“当前必须有”，除非仓库补齐

- `B1-a No-enforcement baseline`
- `B1-b Ablation baseline`

当前仓库没有对应 baseline executable 或测试矩阵，写成“已要求、当前必须有”不够准确。

---

## 7. 推荐的后续改进优先级

如果你准备继续迭代，我建议按下面顺序推进：

### P0：最优先

1. 实现 hidden entry / `saved_PC`
2. 修复 `EWC_ILLEGAL_PC` 审计中的 `user_id=0`
3. 更新 `Demo_Claim_Boundary_v3.1.md`，让文档和当前事实一致

### P1：高优先

1. 实现最小 SPE bounds
2. 增加真正的 same-user cross-process demo
3. 把 `cross_process` 与 `cross_user` 的叙事边界彻底拉开

### P2：中优先

1. 增加 baseline / ablation 模式
2. 提取 demo 公共辅助函数
3. 进一步把启动入口从 `entry_pc` 显式传参改为硬件上下文恢复

---

## 8. 最终结论

当前仓库不是“只搭了骨架”，而是已经具备了比较完整的 prototype 说服力。  
尤其在以下几件事上，证据已经很强：

- EWC 在 Fetch 的强制门控；
- 错误 key / 篡改密文导致 `DECRYPT_DECODE_FAIL`；
- `context_handle` 真实改变执行语义；
- PVT / SPE 都不是空壳；
- demo 输出具备 trap + audit + context trace 三种可观测性。

但如果要把仓库进一步对齐到 `system_design_v4.md` 的更完整语义，或者要把它作为论文主证据集来讲，当前最需要补的是：

- hidden entry / `saved_PC`
- SPE bounds
- claim boundary 文档与实现现状的对齐

一句话总结：

> 这份仓库已经能支撑“prototype-level 的安全 contract skeleton 已被跑通并可观测”这一主张；但若要上升到 v4 更完整的启动语义和同用户跨进程数据隔离主张，还需要继续补关键机制，而不是只补文案。

