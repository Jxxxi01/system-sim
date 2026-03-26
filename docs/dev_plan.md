# 顶层开发计划

> 生成日期：2026-03-06
> 最后更新：2026-03-26（追加 Issues 12-15 最终迭代轮）
> 状态：已确认

---

## 一、已完成部分摘要

Issue 0-11 已完成全部核心模块开发和 5 类 Demo 场景。

- **Issue 0-3**：项目基础设施 + 核心执行流水线 + EWC Fetch Gate
- **Issue 4**：伪加解密（XOR encrypt/decrypt），pipeline Fetch→Decode 间插入 DecryptStage
- **Issue 5**：Gateway 模拟器 + SecureIR JSON 解析 + context_handle 分配
- **Issue 6A**：KernelProcessTable 基础设施（LoadProcess / ContextSwitch）
- **Issue 6B**：context switch + demo_cross_user
- **Issue 7**：PVT 页面验证表（PA ownership + expected_VA + page_type）
- **Issue 8**：SPE CFI L1/L2/L3 + bounds + permissions
- **Issue 9**：5 类 Demo 场景（normal / injection / cross_user / cross_process / rop）
- **Issue 10**：AuditCollector 结构化审计（seq_no + type + user_id + handle + pc + detail）
- **Issue 11A**：SecureIrPackage 格式 + 统一加载链路
- **Issue 11B**：SecureIrBuilder 库函数 + 5 个 Demo 重构消除手写 JSON

全部 8 套 CTest 测试通过，5 个 Demo 全绿。

---

## 二、后续 Issue 列表

### Issue 4：伪解密插入 Fetch→Decode（pseudo decrypt） ✅

| 项目 | 内容 |
|------|------|
| 一句话目标 | 在 Fetch→Decode 之间插入伪解密阶段，用 EWC 返回的 `key_id` 对指令进行 XOR 解密，错误 key 确定性触发 `DECRYPT_DECODE_FAIL` |
| 对应模块 | I-6 扩展（executor pipeline）+ I-1 L0（伪加解密） |
| 前置依赖 | Issue 3（EWC Fetch Gate） |
| 预期产出 | 修改：`include/core/executor.hpp`（`TrapReason` 扩展）, `src/core/executor.cpp`（DecryptStage 插入）, `tests/test_executor.cpp`, `demos/normal/demo_normal.cpp`；可能新增：`include/core/code_codec.hpp`, `src/core/code_codec.cpp` |
| 核心验收标准 | 1. 正确 key → 程序正常到 HALT；2. 错误 key → `DECRYPT_DECODE_FAIL` + audit 事件；3. Issue 0-3 全部旧测试继续通过 |

### Issue 5：Gateway 模拟器 + SecureIR 解析（gateway + secureir） ✅

| 项目 | 内容 |
|------|------|
| 一句话目标 | 实现 `gateway_load` 流程——解析 SecureIR 输入 → 验签（stub）→ 配置 EWC 窗口 → 分配 `context_handle` 并返回 |
| 对应模块 | I-2（Gateway）+ I-1（SecureIR schema 定义） |
| 前置依赖 | Issue 4（Gateway 需知道 `key_id` 以配置 EWC 窗口和解密参数） |
| 预期产出 | 新增：`include/security/gateway.hpp`, `src/security/gateway.cpp`, `tests/test_gateway.cpp`；修改：`demos/normal/demo_normal.cpp`, `CMakeLists.txt` |
| 核心验收标准 | 1. `gateway_load` 正确解析 SecureIR 并配置 EWC 窗口；2. `context_handle` 分配成功，`handle -> user_id` 映射表正确；3. `demo_normal` 改用 `gateway_load` 启动 |

### Issue 6：内核模拟器 + 上下文切换 + cross_user demo（kernel + ctx_switch） ✅

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

### Issue 7：PVT 模拟器 + secure_page_load（pvt） ✅

| 项目 | 内容 |
|------|------|
| 一句话目标 | 实现 PVT 页面注册与一致性校验，OS 通过 `secure_page_load` 注册安全页面，PVT 从 EWC 读取信息做反向验证 |
| 对应模块 | I-4（PVT） |
| 前置依赖 | Issue 6（内核模拟器提供 `secure_page_load` 调用入口） |
| 预期产出 | 新增：`include/security/pvt.hpp`, `src/security/pvt.cpp`, `tests/test_pvt.cpp`；修改：`src/kernel/kernel_emu.cpp`, `include/core/executor.hpp`（`TrapReason::PVT_MISMATCH`）, `CMakeLists.txt` |
| 核心验收标准 | 1. 正常注册通过；2. owner 不匹配 → `PVT_MISMATCH`；3. VA 不在执行窗或 `page_type` 不匹配 → `PVT_MISMATCH` |

### Issue 8：SPE 模拟器最小闭环（spe） ✅

| 项目 | 内容 |
|------|------|
| 一句话目标 | 实现 SPE CFI 检查（L1/L2/L3），挂在 Decode/Execute 阶段，违规触发 `SPE_VIOLATION` |
| 对应模块 | I-5（SPE） |
| 前置依赖 | Issue 5（Gateway 的 SPE 配置占位接口）；Issue 7（Memory 阶段 owner 检查，弱依赖） |
| 预期产出 | 新增：`include/security/spe.hpp`, `src/security/spe.cpp`, `tests/test_spe.cpp`；修改：`src/core/executor.cpp`, `src/security/gateway.cpp`, `include/core/executor.hpp`（`TrapReason::SPE_VIOLATION`）, `CMakeLists.txt` |
| 核心验收标准 | 1. CFI L3 下非法跳转目标触发 `SPE_VIOLATION`；2. CFI L1 下同样代码不触发；3. Gateway 配置的 `cfi_level` 正确传递到 SPE |

### Issue 9：I-7 演示场景扩展（demos） ✅

| 项目 | 内容 |
|------|------|
| 一句话目标 | 补齐 5 类 demo 场景：正常执行 / 代码注入 / 跨用户 / ROP / 跨进程 |
| 对应模块 | I-7 |
| 前置依赖 | Issue 6（跨用户）、Issue 7（代码注入/跨进程）、Issue 8（ROP） |
| 预期产出 | 新增：`demos/` 下每个场景一个目录和 `.cpp` 文件；修改：`CMakeLists.txt` |
| 核心验收标准 | 每个 demo 输出清晰的 trap reason + audit 事件流 |

### Issue 10：审计日志正规化（audit） ✅

| 项目 | 内容 |
|------|------|
| 一句话目标 | 将散落各模块的审计日志统一为 `AuditEvent{seq_no, type, user_id, context_handle, pc, detail}` 结构化格式 |
| 对应模块 | 跨模块 |
| 前置依赖 | Issue 9（所有 demo 场景可用后统一规范） |
| 预期产出 | 新增：`include/core/audit.hpp`, `src/core/audit.cpp`；修改：各模块的审计输出点 |
| 核心验收标准 | 所有 demo 的审计输出格式统一，支持 stdout + 可选 NDJSON 文件 |

### Issue 11：SecureIR 生成器（secureir_gen） ✅

| 项目 | 内容 |
|------|------|
| 一句话目标 | 从 toy asm + 配置文件自动生成 SecureIR 输入，避免 demo 中手写结构化数据 |
| 对应模块 | I-1 完整化 |
| 前置依赖 | Issue 5（SecureIR schema 已定义） |
| 预期产出 | Issue 11A: SecureIrPackage 格式 + 统一加载链路；Issue 11B: SecureIrBuilder 库函数 + Demo 重构 |
| 核心验收标准 | 为每个 demo 场景自动生成合法的 SecureIR 输入，所有 demo 消除手写 JSON |

---

## 三、最终迭代轮（Issues 12-15）

> 基于 Claude + Codex 双重代码审查报告（`docs/review_repo_byclaude.md`, `docs/review_repo_bycodex.md`），针对安全性缺陷、代码质量、demo 覆盖度进行最终一轮迭代。

### Issue 12：Hidden entry / saved_PC

| 项目 | 内容 |
|------|------|
| 一句话目标 | 将程序首次入口隐藏于硬件上下文中，OS 不再显式指定启动 PC，首次执行只能从 Gateway 初始化的 `saved_pc` 开始 |
| 安全动机 | 对齐 v4 的 hidden-entry 语义，防止 OS 通过显式 `entry_pc` 绕过初始化路径或指定任意入口 |
| 前置依赖 | Issue 11（当前代码基线） |
| 预期改动 | 1. `SecurityHardware` per-handle metadata 扩展（新增 `saved_pc` + `user_id` 字段）；2. `Gateway::Load` 初始化 `saved_pc`；`GatewayLoadResult` 中 `base_va` 保留但语义重定义为 code load address（供 PVT 页面注册使用），不再作为 entry point；3. `ExecuteProgram` 移除 `entry_pc` 参数，从 active context 的 `saved_pc` 读取；4. 所有 demo + 测试适配新接口 |
| 核心验收标准 | 1. 外部调用方不再显式传入 `entry_pc`；2. `ExecuteProgram` 从硬件上下文读 `saved_pc`；3. `GatewayLoadResult` 不暴露专门的 entry 字段（`base_va` 仅为 load address）；4. 增加恶意 OS 测试：证明无法指定任意首次入口（mid-function entry 被拒绝）；5. 全部测试继续通过 |

### Issue 13：Audit 修复 + 代码质量

| 项目 | 内容 |
|------|------|
| 一句话目标 | 修复审计归因不完整问题，并清理局部代码重复与标签不一致 |
| 前置依赖 | Issue 12（SecurityHardware 已持有 per-handle user_id） |
| 预期改动 | 1. 修复 `EWC_ILLEGAL_PC`（executor.cpp）审计中 user_id=0：executor 持有 `SecurityHardware` 引用，可直接查 active handle → user_id；2. 修复 `PVT_MISMATCH`（pvt.cpp）审计中 user_id=0：**需为 `PvtTable` 新增 handle→user_id 解析依赖**（当前 PvtTable 仅持有 `EwcTable&` / `AuditCollector&` / `PageAllocator&`，不具备 user_id 查询能力），建议注入 `std::function<uint32_t(ContextHandle)>` 类型的 resolver，由 SecurityHardware 在构造时提供；3. 合并重复的 `OpToString`（executor.cpp + spe.cpp）为共享 utility；4. 删除 demo_normal.cpp 中的死函数 `ProgramEndVa`；5. 修正 SPE stage 标签（CALL/J/BEQ 的 `"decode"` → `"execute"`）；6. SPE Policy struct 添加 bounds 字段占位 + 注释（设计尚未成熟，不做 demo） |
| 核心验收标准 | 1. `EWC_ILLEGAL_PC` 与 `PVT_MISMATCH` 审计均能正确反映 handle 归属用户；2. PvtTable 通过显式注入的 resolver 获取 user_id，而非直接依赖 SecurityHardware 整体；3. 无重复 OpToString；4. SPE stage 标签与实际检查阶段一致；5. 全部测试继续通过 |

### Issue 14A：Demo 重命名 + 真正的 same-user cross-process

| 项目 | 内容 |
|------|------|
| 一句话目标 | 修正现有 demo 命名偏差，并新增一个真正展示"同一 user_id、不同 context_handle 的代码执行隔离"的 demo |
| 前置依赖 | Issue 13（审计修复完成，demo 基线干净） |
| 预期改动 | 1. 重命名 `demos/cross_process/` → `demos/malicious_mapping/`（含 CMakeLists、README）；2. 新建 `demos/cross_process/demo_cross_process.cpp`：同一 user_id、不同 context_handle 的两个进程，进程 B 尝试执行进程 A 的代码地址，由 EWC 拦截（`EWC_ILLEGAL_PC`） |
| claim 边界 | 本 demo 证明 **same-user 下 EWC per-handle 代码执行窗口隔离**（per-process execution window）。不 claim 完整的 same-user cross-process data isolation（SPE bounds 尚未实现完整 data bounds） |
| 核心验收标准 | 1. demo_malicious_mapping 保持原有行为和测试绿灯；2. 新 demo_cross_process 明确使用 same-user / different-handle；3. 演示结果为 `EWC_ILLEGAL_PC`，证明 per-process execution window 生效；4. 全部测试继续通过 |

### Issue 14B：Ablation demo（配置 + 攻击准备驱动的安全逐步削弱）

| 项目 | 内容 |
|------|------|
| 一句话目标 | 用配置与 demo 场景组合，展示执行授权 / 代码完整性 / 行为合规逐步去掉后的退化路径 |
| 前置依赖 | Issue 14A（demo 目录结构稳定） |
| 设计要点 | 不修改 executor 安全语义，通过 demo 配置与硬件状态准备实现。以 cross_user 攻击场景为载体，三组逐步削弱 |
| 攻击载体定义 | **层 2/3 的攻击准备包含恶意 OS code_region 注入**。原因：executor 取指模型从 active handle 的 CodeRegion 读取，仅放宽 EWC 窗口不会让 Bob 自动读到 Alice 的代码，必须模拟恶意 OS 将 Alice 的密文注入 Bob handle 的 code_region。具体三层：1. **层 1（全安全）**：Bob 试图执行 Alice 地址 → EWC 拦截；2. **层 2（去掉 EWC，保留 key barrier）**：恶意 OS 将 Alice 密文作为 Bob 的 code_region 注入 + wildcard EWC 窗口 + 保留不同 key_id → Decrypt 拦截；3. **层 3（去掉 EWC + key barrier + SPE）**：同样注入 Alice code_region + 统一 key_id + cfi_level=0 → 攻击成功 |
| 预期产出 | 新增：`demos/ablation/demo_ablation.cpp` + README |
| 核心验收标准 | 1. 三层攻击结果与预期一致（EWC 拦截 / Decrypt 拦截 / 攻击成功）；2. 不修改 executor 安全语义，仅通过 demo 配置与硬件状态准备实现；3. 输出清晰展示安全层级递减叙事 |

### Issue 15：文档对齐

| 项目 | 内容 |
|------|------|
| 一句话目标 | 将代码变更同步到设计文档和进度追踪 |
| 前置依赖 | Issue 14B（所有代码变更完成） |
| 预期改动 | 1. `Demo_Claim_Boundary_v3.1.md`：三个 [Scaffolded] 项升级为 [Current]（代码页篡改、PVT inconsistency partial、ROP/CFI violation），更新脚注 ¹²³ 引用已完成 Issue；2. `dev_plan.md` 状态标记更新；3. `progress_tracking_v3.md` 同步 |
| 核心验收标准 | 文档与代码实际状态一致，无过期状态标记 |

---

## 四、依赖关系图

```
Issue 0 (Scaffold)       [done]
  -> Issue 1 (ISA)       [done]
    -> Issue 2 (Executor) [done]
      -> Issue 3 (EWC)    [done]
        -> Issue 4 (Pseudo Decrypt) [done]
          -> Issue 5 (Gateway + SecureIR) [done]
            -> Issue 6A (Kernel Emu infra) [done]
            |   -> Issue 6B (Context Switch + cross_user demo) [done]
            |   -> Issue 7 (PVT) [done]
            |   |   -> Issue 9 (5-demo expansion) [done]
            |   -> Issue 8 (SPE) [done]
            |       -> Issue 9 (5-demo expansion) [done]
            -> Issue 11 (SecureIR Generator) [done]
            -> Issue 10 (Audit normalization) [done]

--- 最终迭代轮 ---

Issue 11 (baseline)
  -> Issue 12 (Hidden entry / saved_PC)
    -> Issue 13 (Audit fix + code quality)
      -> Issue 14A (Demo rename + same-user cross-process)
        -> Issue 14B (Ablation demo)
          -> Issue 15 (Document alignment)
```

核心安全故事最小闭环（Issue 4-6）和完整安全演示闭环（Issue 4-9）均已完成。

最终迭代轮（Issue 12-15）聚焦：安全性增强（hidden entry）、审计正确性、demo 覆盖度（same-user cross-process + ablation）、文档对齐。

---

## 五、开发顺序评审

**Issues 4-11（已完成）**：实际执行顺序 4→5→6→7→8→9→10→11，与原计划一致。

**Issues 12-15（最终迭代）**：严格线性依赖，按编号顺序执行。理由：
1. **Issue 12 最先**：hidden entry 改变 `ExecuteProgram` 接口签名，后续所有 Issue 都基于新接口。
2. **Issue 13 紧随**：audit fix 依赖 Issue 12 为 SecurityHardware 添加的 per-handle user_id。
3. **Issue 14A 先于 14B**：目录重命名必须在新增 ablation demo 之前完成，否则 demo 目录结构不稳定。
4. **Issue 15 最后**：文档对齐必须等所有代码变更完成。

---

## 六、风险与已确认决策

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

## 七、里程碑摘要

| 里程碑 | 包含 Issue | 交付物 | 状态 |
|--------|-----------|--------|------|
| M1：Pipeline 安全闭环 | 4 | 伪加解密 + 正确/错误 key 演示 | ✅ |
| M2：加载安全闭环 | 4 + 5 | Gateway 自动配置 EWC，demo 不再手工注入 | ✅ |
| M2.5：内核编排层就位 | 4 + 5 + 6A | `KernelEmulator` 接管 demo 驱动 | ✅ |
| M3：跨用户安全闭环 | 4 + 5 + 6（A+B） | 上下文切换 + cross_user demo | ✅ |
| M4：完整安全演示 | 4-9 | PVT + SPE + 5 类 demo 全部可用 | ✅ |
| M5：打磨 | 10 + 11 | 审计正规化 + SecureIR 生成器 | ✅ |
| M6：安全性增强 | 12 | Hidden entry / saved_PC，OS 不可篡改入口 | 待开始 |
| M7：质量修复 | 12 + 13 | 审计 user_id 正确 + 代码重复清理 | 待开始 |
| M8：完整 Demo 覆盖 | 12-14B | same-user cross-process + ablation demo | 待开始 |
| M9：文档收尾 | 12-15 | 代码与文档完全对齐 | 待开始 |
