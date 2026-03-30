# Demo Claim Boundary (v3)

## 1. 文档目的与应用场景

**核心目的**
本文档用于定义当前 prototype/demo 的证据边界：明确当前 demo 能支撑哪些 claim、不能支撑哪些 claim、应以什么评估单位和 baseline 解释结果，从而避免 prototype scope、evaluation scope 与 paper claim 发生漂移。

**应用场景**
本文档面向四类场景使用：
- **原型开发**：约束"哪些实验结果可以拿来支撑 claim，哪些不能"；
- **论文写作**：避免把 prototype scope 写成 paper scope，尤其是在 intro、evaluation、limitations 里；
- **评审防守**：当 reviewer 质疑"这不就是 simulator 里的规则吗"时，能明确回答 claim 的边界和证据层级；
- **后续扩展**：任何新叙事、新攻击案例，只有在不破坏本文档逻辑的前提下，才能被吸收进当前 evidence set。

**不是什么**
本文档不是系统总设计文档，不是论文路线图，不是 future work 汇总容器。它的唯一职责是约束"当前 demo 的证据能说到哪里为止"。

---

## 2. 当前 demo 的定位

当前 demo 不是完整系统复现，而是一个**最小、可执行、可观测的安全 contract skeleton**。

它的直接目标是打通以下最小闭环：

- Gateway 加载安全语义；
- context_handle 驱动上下文切换；
- Fetch 阶段强制执行 EWC 检查；
- 合法路径继续进入 decrypt / decode / execute；
- 非法路径触发 trap；
- 审计链记录关键事件；
- 用户上下文与 violation 能被对应起来。

换句话说，这个 demo 的角色不是"证明整套系统已经工程落地"，而是：

> 证明用户授权 contract 至少存在一个最小可执行骨架，并且该骨架中的关键 violation 能被硬件语义化地拦截和记录。

---

## 3. 证据分层标签

本文档在表格和叙事列表中使用以下三类标签区分证据成熟度：

| 标签 | 含义 | 在论文中的使用方式 |
|---|---|---|
| **[Current]** | 当前 prototype 已能直接支撑，有可运行的 demo 或测试 | 可作为 evaluation 的直接证据 |
| **[Scaffolded]** | 接口/语义已进入骨架，但 demo 尚不完整或依赖后续 Issue | 可在论文中描述机制设计，但不能声称已验证 |
| **[Future]** | 当前仅作为扩展方向，不进入当前 evidence set | 只能出现在 future work 中 |

---

## 4. Prototype Claim

### C1. 控制路径存在性（control-path existence）
demo 证明：从 `gateway_load` 到 `secure_context_switch` 再到 `Fetch → EWC → Decrypt → Decode → Execute` 的最小控制路径是可执行的。

### C2. 非法执行入口可被结构化拦截
demo 证明：非法 PC 进入不会退化为"随机崩溃"或"解密垃圾后失败"，而是在 Fetch 阶段先经过 EWC 的显式检查，并触发明确 trap reason（如 `EWC_ILLEGAL_PC`）。

### C3. 用户上下文切换会改变有效执行语义
demo 证明：`context_handle` 不是装饰性变量，而是会真实改变 active execution windows，进而影响后续 EWC 判定结果。

### C4. 关键安全事件可观测
demo 证明：系统不会只在内部阻断 violation，而是会输出与 contract 对应的可观测事件，例如 `GATEWAY_LOAD_OK`、`CTX_SWITCH`、`EWC_ILLEGAL_PC`、`DECRYPT_DECODE_FAIL`、`PVT_MISMATCH`。

### C5. 最小 attack-driven 叙事可成立

当前 demo 支撑的攻击叙事均锚定到公认攻击原语。"representative prior art" 指代表该类威胁/需求背景的已有工作，不意味着 demo 复现了该论文或等价实现了该机制。

| Demo 叙事 | 对应攻击类型 | Attack motivation / representative prior art | 证据标签 |
|---|---|---|---|
| 正常执行 | baseline | — | **[Current]** |
| 跨用户非法执行 | 跨隔离域访问 / confused deputy | WESEE [S&P'24]; CWE-284 (Improper Access Control) | **[Current]** |
| 代码页篡改（密文-key 不匹配） | OS 篡改物理页面内容 | WESEE [S&P'24]; CWE-345 (Insufficient Verification of Data Authenticity) | **[Current]** ¹ |
| PVT owner/VA inconsistency | OS 构造恶意映射 | CounterSEVeillance [NDSS'25]; CWE-269 (Improper Privilege Management) | **[Current]** ² |
| ROP / CFI 违规 | 控制流劫持 | SLAM [S&P'24]; CWE-416 (Use After Free) / CWE-787 (Out-of-bounds Write) | **[Current]** ³ |

> ¹ 已由 Issue 4（pseudo decrypt）+ Issue 9（demo_injection）实现；Issue 14B ablation Layer 2 额外演示 key barrier 退化路径。
> ² 已由 Issue 7（PVT）+ Issue 9（demo_malicious_mapping）实现 owner mismatch 拦截；反 alias 检查在 1:1 VA=PA flat memory 下仍无法演示。
> ³ 已由 Issue 8（SPE）+ Issue 9（demo_rop）实现 CFI L3 违规拦截。

---

## 5. Non-Claims（明确不证明的内容）

当前 demo **不证明**以下内容：

### N1. 不证明真实密码学可落地
当前仅为 pseudo encryption / pseudo decryption，不代表真实签名、真实密钥管理、真实机密性方案已成立。

### N2. 不证明完整 ISA / runtime 兼容性
当前 demo 不是完整 RISC-V，也不证明复杂运行时、共享库、系统调用生态、动态装载已经被覆盖。

### N3. 不证明性能竞争力
当前 demo 不是 cycle-accurate，也不适合与 SGX / SEV / CHERI / PUMP 等系统做 runtime overhead 横向比较。

### N4. 不证明完整 side-channel 安全
当前 demo 只能表达"设计约束"或"未来分析接口"，不能证明 cache / predictor / debug / PMC / timing 路径都已被系统解决。

### N5. 不证明 full-system semantics
多核一致性、安全 I/O、anti-rollback 完整协议、真实 policy update、安全共享库 provenance 等仍属于 full design claim，而不是 prototype claim。

---

## 6. Simplification Register

当前 prototype 采用了以下简化，需要逐项评估其对安全语义正确性的影响：

| 简化项 | 真实系统对应 | 是否影响安全语义 | 理由 |
|---|---|---|---|
| Toy ISA（12 opcodes） | 真实 RISC-V / ARM ISA | 不影响 | EWC 检查 PC 范围与指令集无关；SPE CFI 检查逻辑与 opcode 种类无关 |
| Pseudo XOR 加解密 | AES-GCM 认证加密 | 不影响控制路径正确性；但不证明密码学安全性 | 解密失败的行为语义相同（产出垃圾→decode fail）；MAC 验证语义由 pseudo 校验替代 |
| 1:1 VA=PA identity mapping | 真实 MMU 页表翻译 | 不影响 EWC / SPE；**但无法演示 PVT 反 alias 检查** | EWC 只看 PC 数值；PVT 的 expected_VA ≠ OS 给出的 VA 这一检查在 1:1 映射下恒等成立 |
| 地址空间互不重叠 | 真实多进程共享 VA 空间 | 不影响跨用户隔离语义 | 跨用户检查由 EWC 窗口匹配完成，不依赖 VA 是否重叠 |
| 单核 | 多核并发 | **可能影响** | 并发上下文切换的原子性未验证 |
| 无真实中断 / syscall | 真实中断 + 系统调用 | **可能影响** | 中断时的安全上下文保存/恢复语义未被 demo 覆盖 |

---

## 7. 攻击覆盖矩阵

### 表 A：攻击场景 × 安全检查语义（拦截点定位）

每行是一个攻击场景，每列是一个抽象安全检查语义。**TRAP** 标记的列为该攻击的第一个拦截点，`—` 表示执行已终止不再经过后续检查。

| 攻击场景 | 执行授权 | 代码完整性 | 数据归属 | 行为合规 | 审计 | 最终结果 | 证据标签 |
|---|---|---|---|---|---|---|---|
| 正常执行 | pass | pass | pass | pass | recorded | **HALT** | [Current] |
| 跨用户非法执行 | **TRAP** | — | — | — | recorded | **EWC_ILLEGAL_PC** | [Current] |
| 代码页篡改（密文-key 不匹配） | pass | **TRAP** | — | — | recorded | **DECRYPT_DECODE_FAIL** | [Current] |
| PVT owner/VA inconsistency | pass | pass | **TRAP** | — | recorded | **PVT_MISMATCH** | [Current] |
| ROP / CFI 违规 | pass | pass | pass | **TRAP** | recorded | **SPE_VIOLATION** | [Current] |

> **当前 prototype 实现映射**：执行授权 = EWC @ Fetch 阶段，代码完整性 = MAC 验证 @ Decrypt 阶段，数据归属 = PVT @ TLB-miss 阶段，行为合规 = SPE @ Decode/Execute 阶段。

### 表 B：模块缺失时的行为退化（ablation 视角）

每列是"去掉某个安全检查"，加粗格子标识行为退化点——即该检查对该攻击不可或缺。

| 攻击场景 | 完整系统 | 去掉执行授权 | 去掉代码完整性 | 去掉数据归属 | 去掉行为合规 | 去掉审计 | 证据标签 |
|---|---|---|---|---|---|---|---|
| 跨用户非法执行 | EWC_ILLEGAL_PC | **undefined** ¹ | 不变 | 不变 | 不变 | 拦截但无记录 | [Current] |
| 代码页篡改（密文-key 不匹配） | DECRYPT_DECODE_FAIL | DECRYPT_DECODE_FAIL | **静默执行垃圾或崩溃** | 不变 | 不变 | 拦截但无记录 | [Current] |
| PVT owner/VA inconsistency | PVT_MISMATCH | 不变 | 不变 | **攻击成功** | 不变 | 拦截但无记录 | [Current] |
| ROP / CFI 违规 | SPE_VIOLATION | 不变 | 不变 | 不变 | **攻击成功** | 拦截但无记录 | [Current] |

> ¹ **undefined**：行为取决于非法 PC 地址处存储的值——若恰好是合法 opcode 则静默错误执行（最危险），否则触发 UNKNOWN_OPCODE。这种不确定性正是执行授权检查需要存在的原因。

---

## 8. 评估单位（Evaluation Unit）

当前 demo 的评估单位不是"整系统是否完美"，而是以下四个单位：

### E1. Path correctness
给定输入 contract / context / pc，系统是否走到预期路径：
- allow path
- deny path
- decode-fail path
- pvt-mismatch path

### E2. Violation-to-trap correctness
每类 violation 是否映射到明确 trap reason，而非模糊失败。

### E3. Violation-to-event correctness
每类 violation 是否留下可对应的 audit event，且 event 中包含足够的 user/context 信息。

### E4. Context-sensitive enforcement correctness
相同 PC 在不同 active context 下是否表现不同，从而证明 enforcement 确实与 user-bound context 绑定。

---

## 9. 当前 demo 的最低验收标准

### A1. demo_normal [Current]
输入合法 SecureIR 与合法上下文后：
- 程序应成功执行到 `HALT`
- 不应出现 `EWC_ILLEGAL_PC`
- 输出应包含至少：load 相关事件、`CTX_SWITCH`、最终 `HALT`

### A2. demo_cross_user [Current]
加载 Alice 与 Bob 后，在 Bob 的 active context 中尝试执行 Alice window 内地址：
- 必须触发 `EWC_ILLEGAL_PC`
- 审计中必须出现 `CTX_SWITCH` 与 `EWC_ILLEGAL_PC`
- trace 中必须能看出 trap 发生时的 active handle

### A3. 代码页篡改（密文-key 不匹配） [Current]
OS 替换物理页面内容后，正确 key 解密产出垃圾：
- 必须触发 `DECRYPT_DECODE_FAIL`
- 不能静默执行，也不能被误归类为一般非法内存错误
- 依赖 Issue 4（pseudo decrypt）完成

### A4. PVT owner/VA inconsistency [Current]
当代码页 owner 或 VA 与 window 语义不一致时：
- 必须触发 `PVT_MISMATCH`
- 审计必须保留对应事件
- 依赖 Issue 7（PVT）完成；当前 1:1 VA=PA 下反 alias 检查无法演示

---

## 10. Baseline Strategy

### B1. Internal baseline（当前阶段必须有）

#### B1-a No-enforcement baseline
关闭全部安全检查，仅保留 toy CPU 执行。
目的：说明安全模块确实改变了系统行为，而不是"可有可无"。

#### B1-b Ablation baseline（双维度）

**维度一：模块语义绑定（逐步启用）**

| 配置 | 当前 prototype 对应 |
|---|---|
| CPU only | 无安全检查 |
| + 执行授权 | CPU + Gateway + EWC |
| + 代码完整性 | 上述 + Decrypt/MAC |
| + 数据归属 | 上述 + PVT |
| + 行为合规 | 上述 + SPE |
| + 审计 | 上述 + Audit |

**维度二：攻击场景 × 模块配置（覆盖矩阵）**

即 Section 7 的表 A 和表 B。

### B2. Semantic baseline（论文写作中使用）
不是跑性能，而是做"能力边界对照"。

- P1 对照：SGX / SEV / TDX / Keystone / Capstone / EKC / CURE / Dorami
- P2 对照：VSP / INCOGNITOS / AMi / Libra
- P3 对照：PUMP / CCTAG / BliMe / HDFI
- P4 对照：OMNILOG / eAudit / Ariadne / ROTE

目的：说明本文 demo 支撑的是哪一类语义，不是哪一类语义。

### B3. Performance baseline（当前阶段不启用）
在 prototype 仍为 toy ISA + pseudo crypto + 非 cycle-accurate 的前提下，不做外部性能 baseline。
若必须报告，只允许报告：
- instruction step count
- trap latency in steps
- audit event count
- per-module relative slowdown（内部消融）

---

## 11. 推荐实验组织方式

### G1. Mechanism tests（机制单测）
- EWC allow / deny [Current]
- Fetch-stage mandatory enforcement [Current]
- context switch changes active windows [Current]
- correct key / wrong key decryption behavior [Current]
- PVT consistency checks [Current]
- audit event emission correctness [Current]

### G2. Narrative demos（最小攻击叙事）
- normal execution [Current]
- cross-user illegal execution [Current]
- 代码页篡改（密文-key 不匹配） [Current]
- PVT owner/VA inconsistency [Current]
- ROP / CFI violation [Current]

### G3. Future workload shell（后续再扩展）
未来若引入公开 demo / 测试集，不以"吞吐 benchmark"为首要目标，而以"让 contract 语义进入更真实程序形态"为目标。
例如：
- toy library call sequence
- toy RPC / syscall path
- small interpreter / parser
- bounded control-flow benchmarks
- synthetic code confidentiality cases

原则是：先服务语义验证，再服务性能评估。

---

## 12. 结果解释原则

### R1. 不把"demo 跑通"等同于"论文主张全部成立"
当前 demo 仅支撑 skeleton claim，不支撑全部系统 claim。

### R2. 不把 trap 的存在等同于安全性已经充分
必须同时说明：
- trap 发生在何处（对应表 A 的哪个安全检查语义）；
- 为什么这个位置比更晚失败更强；
- 它与 user-bound contract 的关系是什么。

### R3. 不把 audit 的存在等同于可归责性已经完整成立
当前只能证明"事件可记录"与"事件可绑定到 context/user"；
tamper-evidence、hash chain、anti-rollback 需要进一步独立做实。

### R4. 不把 toy CPU 行为等同于真实硬件性能
一切性能数字都只能解释为 prototype-local 指标，不作为工业可部署性证据。

### R5. 不把 [Scaffolded] 证据等同于 [Current] 证据
表 A/B 和验收标准中标记为 [Scaffolded] 的行，表示机制语义已设计但 demo 尚未完整支撑。在论文中可以描述其设计，但不能声称已通过实验验证。

---

## 13. Future Extensibility Note

当前 prototype 将 user-bound 执行授权、数据归属与审计语义落在 CPU skeleton 上。这些安全检查语义（执行授权 / 代码完整性 / 数据归属 / 行为合规 / 审计）在原则上不绑定于特定计算架构，理论上可推广到异构计算与 AI 数据处理场景，且该方向与数据主权相关法规趋势有潜在对齐。但当前 prototype 不提供这些扩展场景的实现或实验性证据，因此该方向仅作为 future work，不进入当前 claim boundary。

详细分析见独立文档 `AI_Security_Extension_Analysis.md`。

---

## 14. 一句话结论

当前 demo 的意义不是"证明完整系统已经实现"，而是：

> 它证明了一个围绕用户授权 contract 的最小执行骨架在当前 CPU prototype 中是可运行、可拦截、可观测、可复现实验化的，并为后续攻击驱动扩展提供了统一的语义底盘。

这是后续攻击驱动评估、语义扩展和论文收敛的基础。
