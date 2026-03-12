# 顶层开发计划

> 生成日期：2026-03-06
> 状态：已确认

---

## 一、已完成部分摘要

Issue 0-3 已建立完整的项目基础设施和核心执行流水线。脚手架（Issue 0）提供 CMake + CTest + header-only 测试框架；Toy ISA / Assembler（Issue 1）实现 12 条 opcode 的文本汇编，支持 label 解析和 pc-relative 偏移；Executor（Issue 2）实现 Fetch→Decode→Execute→Mem→Commit 五阶段流水线，含结构性 PC 校验、LD/ST 内存语义、SYSCALL 日志和死循环保护；EWC Fetch Gate（Issue 3）在 Fetch 阶段插入 mandatory EWC query，deny 时触发 `EWC_ILLEGAL_PC` trap 并写入 audit 事件。当前全部 3 个 CTest 测试通过，`demo_normal` 演示 allow 和 deny 两条路径。

---

## 二、后续 Issue 列表

### Issue 4：伪解密插入 Fetch→Decode（pseudo decrypt）

| 项目 | 内容 |
|------|------|
| 一句话目标 | 在 Fetch→Decode 之间插入伪解密阶段，用 EWC 返回的 `key_id` 对指令进行 XOR 解密，错误 key 确定性触发 `DECRYPT_DECODE_FAIL` |
| 对应模块 | I-6 扩展（executor pipeline）+ I-1 L0（伪加解密） |
| 前置依赖 | Issue 3（EWC Fetch Gate） |
| 预期产出 | 修改：`include/core/executor.hpp`（`TrapReason` 扩展）, `src/core/executor.cpp`（DecryptStage 插入）, `tests/test_executor.cpp`, `demos/normal/demo_normal.cpp`；可能新增：`include/core/code_codec.hpp`, `src/core/code_codec.cpp` |
| 核心验收标准 | 1. 正确 key → 程序正常到 HALT；2. 错误 key → `DECRYPT_DECODE_FAIL` + audit 事件；3. Issue 0-3 全部旧测试继续通过 |

### Issue 5：Gateway 模拟器 + SecureIR 解析（gateway + secureir）

| 项目 | 内容 |
|------|------|
| 一句话目标 | 实现 `gateway_load` 流程——解析 SecureIR 输入 → 验签（stub）→ 配置 EWC 窗口 → 分配 `context_handle` 并返回 |
| 对应模块 | I-2（Gateway）+ I-1（SecureIR schema 定义） |
| 前置依赖 | Issue 4（Gateway 需知道 `key_id` 以配置 EWC 窗口和解密参数） |
| 预期产出 | 新增：`include/security/gateway.hpp`, `src/security/gateway.cpp`, `tests/test_gateway.cpp`；修改：`demos/normal/demo_normal.cpp`, `CMakeLists.txt` |
| 核心验收标准 | 1. `gateway_load` 正确解析 SecureIR 并配置 EWC 窗口；2. `context_handle` 分配成功，`handle -> user_id` 映射表正确；3. `demo_normal` 改用 `gateway_load` 启动 |

### Issue 6：内核模拟器 + 上下文切换 + cross_user demo（kernel + ctx_switch）

本 Issue 分两个 phase 实施。Phase A 建立 `kernel_emu` 基础设施骨架，Phase B 在骨架上实现 context switch 具体功能。拆分理由：`kernel_emu` 是后续 Issue 7/8/9 共用的 OS 侧操作入口层，其接口设计不应被 context switch 单一需求带偏；同时两个 phase 代码量均不大，保留在同一 Issue 内避免额外协调开销。

#### Phase A：kernel_emu 基础设施

| 项目 | 内容 |
|------|------|
| 一句话目标 | 建立 `KernelEmulator` 类，作为所有 OS 侧安全操作的统一入口层，接管 demo 中原先直接调用 Gateway/Executor 的编排逻辑 |
| 对应模块 | I-6 扩展（kernel emulator） |
| 前置依赖 | Issue 5（Gateway 提供 `context_handle` 分配机制） |
| 预期产出 | 新增：`include/kernel/kernel_emu.hpp`, `src/kernel/kernel_emu.cpp`, `tests/test_kernel.cpp`；修改：`demos/normal/demo_normal.cpp`（改用 `KernelEmulator` 驱动）, `CMakeLists.txt` |
| 核心验收标准 | 1. `KernelEmulator` 持有 `context_handle` 注册表，接收 `gateway_load` 结果并注册 handle；2. 提供 `launch(handle)` 封装 Executor 调用；3. 为后续 Issue 预留接口声明（`secure_page_load()`, `secure_context_switch()`, `secure_copy()` 等），Phase A 中为 `not_implemented` stub；4. `demo_normal` 改用 `KernelEmulator::launch()` 驱动，所有 Issue 0-5 旧测试继续通过 |

#### Phase B：context switch + cross_user demo

| 项目 | 内容 |
|------|------|
| 一句话目标 | 实现 `secure_context_switch(handle)` 切换 active context，并实现 cross_user demo 验证跨用户隔离语义 |
| 对应模块 | I-6 扩展（kernel emulator） |
| 前置依赖 | Phase A（`KernelEmulator` 骨架就位） |
| 预期产出 | 修改：`src/kernel/kernel_emu.cpp`（`secure_context_switch` 实现）, `include/core/executor.hpp`；新增：`demos/cross_user/demo_cross_user.cpp`, `tests/test_kernel.cpp`（追加 context switch 测试用例） |
| 核心验收标准 | 1. `secure_context_switch` 正确切换 active context（保存当前 → 恢复目标 → EWC 重配置）；2. 切换后 EWC query 使用新 `context_handle`；3. cross_user demo：Alice 正常执行 → 切到 Bob → Alice 代码触发 `EWC_ILLEGAL_PC`；4. Phase A 全部测试继续通过 |

### Issue 7：PVT 模拟器 + secure_page_load（pvt）

| 项目 | 内容 |
|------|------|
| 一句话目标 | 实现 PVT 页面注册与一致性校验，OS 通过 `secure_page_load` 注册安全页面，PVT 从 EWC 读取信息做反向验证 |
| 对应模块 | I-4（PVT） |
| 前置依赖 | Issue 6（内核模拟器提供 `secure_page_load` 调用入口） |
| 预期产出 | 新增：`include/security/pvt.hpp`, `src/security/pvt.cpp`, `tests/test_pvt.cpp`；修改：`src/kernel/kernel_emu.cpp`, `include/core/executor.hpp`（`TrapReason::PVT_MISMATCH`）, `CMakeLists.txt` |
| 核心验收标准 | 1. 正常注册通过；2. owner 不匹配 → `PVT_MISMATCH`；3. VA 不在执行窗或 `page_type` 不匹配 → `PVT_MISMATCH` |

### Issue 8：SPE 模拟器最小闭环（spe）

| 项目 | 内容 |
|------|------|
| 一句话目标 | 实现 SPE CFI 检查（L1/L2/L3），挂在 Decode/Execute 阶段，违规触发 `SPE_VIOLATION` |
| 对应模块 | I-5（SPE） |
| 前置依赖 | Issue 5（Gateway 的 SPE 配置占位接口）；Issue 7（Memory 阶段 owner 检查，弱依赖） |
| 预期产出 | 新增：`include/security/spe.hpp`, `src/security/spe.cpp`, `tests/test_spe.cpp`；修改：`src/core/executor.cpp`, `src/security/gateway.cpp`, `include/core/executor.hpp`（`TrapReason::SPE_VIOLATION`）, `CMakeLists.txt` |
| 核心验收标准 | 1. CFI L3 下非法跳转目标触发 `SPE_VIOLATION`；2. CFI L1 下同样代码不触发；3. Gateway 配置的 `cfi_level` 正确传递到 SPE |

### Issue 9：I-7 演示场景扩展（demos）

| 项目 | 内容 |
|------|------|
| 一句话目标 | 补齐 5 类 demo 场景：正常执行 / 代码注入 / 跨用户 / ROP / 跨进程 |
| 对应模块 | I-7 |
| 前置依赖 | Issue 6（跨用户）、Issue 7（代码注入/跨进程）、Issue 8（ROP） |
| 预期产出 | 新增：`demos/` 下每个场景一个目录和 `.cpp` 文件；修改：`CMakeLists.txt` |
| 核心验收标准 | 每个 demo 输出清晰的 trap reason + audit 事件流 |

### Issue 10（可选）：审计日志正规化（audit）

| 项目 | 内容 |
|------|------|
| 一句话目标 | 将散落各模块的审计日志统一为 `AuditEvent{seq_no, type, user_id, context_handle, pc, detail}` 结构化格式 |
| 对应模块 | 跨模块 |
| 前置依赖 | Issue 9（所有 demo 场景可用后统一规范） |
| 预期产出 | 新增：`include/core/audit.hpp`, `src/core/audit.cpp`；修改：各模块的审计输出点 |
| 核心验收标准 | 所有 demo 的审计输出格式统一，支持 stdout + 可选 NDJSON 文件 |

### Issue 11（可选）：SecureIR 生成器（secureir_gen）

| 项目 | 内容 |
|------|------|
| 一句话目标 | 从 toy asm + 配置文件自动生成 SecureIR 输入，避免 demo 中手写结构化数据 |
| 对应模块 | I-1 完整化 |
| 前置依赖 | Issue 5（SecureIR schema 已定义） |
| 预期产出 | 新增：`tools/secureir_gen.cpp` 或库函数 |
| 核心验收标准 | 为每个 demo 场景自动生成合法的 SecureIR 输入 |

---

## 三、依赖关系图

```
Issue 0 (Scaffold)       [done]
  -> Issue 1 (ISA)       [done]
    -> Issue 2 (Executor) [done]
      -> Issue 3 (EWC)    [done]
        -> Issue 4 (Pseudo Decrypt)
          -> Issue 5 (Gateway + SecureIR)
            -> Issue 6A (Kernel Emu infra)
            |   -> Issue 6B (Context Switch + cross_user demo)
            |   -> Issue 7 (PVT) [depends on 6A's stub interface]
            |   |   -> Issue 9 (5-demo expansion) [partial: injection, cross-process]
            |   -> Issue 8 (SPE) [weak dep on Issue 7 for Memory-stage owner check]
            |       -> Issue 9 (5-demo expansion) [partial: ROP]
            -> Issue 11 (SecureIR Generator) [optional, independent]

Issue 9 -> Issue 10 (Audit normalization) [optional]
```

核心安全故事最小闭环：**Issue 4 + 5 + 6（A+B）**。完成后具备：伪加密/解密 + Gateway 自动配置 EWC + 上下文切换 + cross_user demo。

注：Issue 7 仅依赖 Issue 6 Phase A（`secure_page_load` stub 入口），不依赖 Phase B（context switch）。若需加速可在 Phase B 完成前启动 Issue 7。

完整安全演示闭环：**Issue 4-9**。补齐 PVT/SPE 后可演示全部 5 类安全场景。

---

## 四、开发顺序评审

草案中的顺序 **4 -> 5 -> 6 -> 7/8 -> 9 -> 10/11** 是合理的，无需调整。理由：

1. **Issue 4 先于 Issue 5**：伪解密是 pipeline 内部改动，不依赖 Gateway。Gateway 需要知道 `key_id` 如何使用，所以先实现解密逻辑再做 Gateway 配置是对的。

2. **Issue 5 先于 Issue 6**：内核模拟器的 `secure_context_switch` 需要 `context_handle`，而 `context_handle` 由 Gateway 分配。

3. **Issue 7 和 Issue 8 可部分并行**：SPE 的 CFI 部分（阶段 A）不依赖 PVT，只有 Memory 阶段 owner 检查（阶段 B）依赖 PVT。如果需要加速，可以先做 Issue 8 阶段 A 再做 Issue 7，然后做 Issue 8 阶段 B。

4. **Issue 10/11 标为可选**：合理。审计正规化和 SecureIR 生成器是锦上添花，不影响安全故事的完整性。

---

## 五、风险与已确认决策

### R-1：SecureIR 格式选择（影响 Issue 5）— 已确认

`spec_i2_i7.md` 建议 SecureIR 用 JSON 格式，但项目约束为 **stdlib only, zero external deps**。C++17 stdlib 不包含 JSON 解析器。

**决策：手写最小 JSON 子集解析器。** 只需支持本项目用到的 schema（嵌套 object/array + string/number），约 200-300 行。JSON 可读性好，与 spec 一致，复杂度可控。

### R-2：Executor 中途切换 context（影响 Issue 6）— 已确认

当前 `ExecuteProgram()` 接收固定的 `ExecuteOptions`（含 `context_handle`），执行到 HALT 或 trap 为止。cross_user demo 需要在执行过程中切换 context。

**决策：在 demo 层面模拟。** 先执行 Alice 的程序（到 HALT），再执行 Bob 的程序（用 Alice 的 code image 但 Bob 的 `context_handle`）。不需要改 executor 内部逻辑。这足以演示安全语义（Bob 尝试执行 Alice 的代码被 EWC 拦截）。

### R-3：内存模型与页面结构（影响 Issue 7）— 已确认

当前内存模型是 flat `vector<uint8_t>`，没有页面概念。PVT 需要 page metadata。

**决策：在 flat memory 之上叠加独立的 page metadata 结构。** PVT 作为独立的元数据表存在（按 VA 范围维护 owner, expected_VA, page_type 等），不改变现有内存访问逻辑。对现有代码侵入最小。

### R-4：SPE CFI L3 目标白名单来源（影响 Issue 8）

CFI L3 需要 `call_targets` / `jmp_targets` 白名单，来自 SecureIR 通过 Gateway 配置到 SPE。

**决策：在 Issue 5 的 SecureIR schema 设计中提前预留 `cfi_level` + `call_targets` + `jmp_targets` 字段。** Issue 5 可以不解析这些字段，但 schema 中必须有位置，避免 Issue 8 回头修改。

### R-5：MEE 定位

MEE 不单独开 Issue。Issue 4 的伪解密已覆盖 MEE 核心语义（key-based encrypt/decrypt + 错误 key 检测）。如后续论文需要演示 data-at-rest 加密，可在 Memory 阶段复用 `code_codec` 逻辑。

### R-6：模块完整性

审查草案后未发现多余拆分或遗漏模块。8 个 Issue（4-11）粒度合适，每个对应一个明确的功能闭环。`system_design_v3.md` 中所有硬件模块（Gateway/EWC/SPE/PVT/审计）均有对应 Issue 覆盖。

---

## 六、里程碑摘要

| 里程碑 | 包含 Issue | 交付物 |
|--------|-----------|--------|
| M1：Pipeline 安全闭环 | 4 | 伪加解密 + 正确/错误 key 演示 |
| M2：加载安全闭环 | 4 + 5 | Gateway 自动配置 EWC，demo 不再手工注入 |
| M2.5：内核编排层就位 | 4 + 5 + 6A | `KernelEmulator` 接管 demo 驱动，后续 Issue 7 可并行启动 |
| M3：跨用户安全闭环 | 4 + 5 + 6（A+B） | 上下文切换 + cross_user demo |
| M4：完整安全演示 | 4-9 | PVT + SPE + 5 类 demo 全部可用 |
| M5：打磨（可选） | 10 + 11 | 审计正规化 + SecureIR 生成器 |
