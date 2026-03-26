# `dev_plan.md` 最终迭代轮审查意见（by Codex）

> 审查对象：`docs/dev_plan.md` 中 `Issue 12` 到 `Issue 15`  
> 审查基线：当前仓库代码、`docs/system_design_v4.md`、`docs/review_repo_bycodex.md`

---

## 1. 总体判断

`Issue 12-15` 这一轮迭代的**大方向是对的**，而且顺序基本合理：

- 先补 `hidden entry / saved_PC`
- 再修审计正确性和代码质量
- 再补 demo 覆盖
- 最后做文档对齐

这条主线是自洽的，核心目标也抓得比较准：

1. 把当前仓库从“prototype 已可跑”进一步推进到“更贴近 v4 语义”。
2. 解决我在仓库审查里指出的几个真实缺口：
   - hidden entry 未实现
   - 审计归因不完整
   - demo 命名与语义有偏差
   - claim boundary 与代码状态不一致

但是，当前计划文本里有几处地方**写得比现有代码现实更超前**，如果不先收紧表述，后续实现时容易出现两种问题：

- 计划里默认某个语义已经存在，但代码里其实没有；
- 验收标准写得太宽，做完后无法证明真正的安全 claim。

我的结论是：

> **这轮计划可以继续推进，但建议先把 `Issue 12/13/14A/14B` 的表述改严谨，再开始写代码。**

---

## 2. 主要问题

---

### 2.1 `Issue 12` 方向正确，但验收标准偏弱

#### 当前计划

`Issue 12` 目前写的是：

- `SecurityHardware` 增加 `saved_pc + user_id`
- `Gateway::Load` 写入 `saved_pc`
- `ExecuteProgram` 不再接收 `entry_pc`
- 所有 demo 和测试适配

#### 为什么这还不够

这只能证明：

- 接口变了；
- 启动点由硬件上下文提供。

但还**不能充分证明**：

- OS 真的看不到 entry；
- OS 不能指定任意入口；
- v4 里“hidden entry / 首次执行从 saved_PC 恢复”的安全语义真的成立。

#### 建议

`Issue 12` 应该额外补两条验收标准：

1. `GatewayLoadResult` 不暴露真实 entry 信息。
2. 增加恶意 OS 测试：
   - 不能通过 API 指定 mid-function 入口；
   - 首次启动只能从 Gateway 预设的 `saved_pc` 开始。

#### 结论

`Issue 12` 应保留，但需要把“API 改造”提升为“安全语义验收”。

---

### 2.2 `Issue 13` 对 `PVT_MISMATCH user_id` 的修复路径写得太直接

#### 当前计划

`Issue 13` 里写：

- 修复 `EWC_ILLEGAL_PC` 和 `PVT_MISMATCH` 中 `user_id=0`
- 从 `SecurityHardware` 查 `handle -> user_id`

#### 为什么这里有问题

`EWC_ILLEGAL_PC` 这条修复方向没问题，因为它发生在执行器里，执行器拿得到 active handle，也更容易接到 `SecurityHardware`。

但 `PVT_MISMATCH` 现在发生在 `PvtTable::RegisterPage()` 内部。  
当前 `PvtTable` 只持有：

- `const EwcTable&`
- `AuditCollector&`
- `PageAllocator`

它**并不知道**：

- `SecurityHardware`
- `Gateway`
- `handle -> user_id` 映射

所以“顺手从 SecurityHardware 查 user_id”在当前架构里不是现成能力，需要先设计一层依赖注入。

#### 建议

把 `Issue 13` 拆清楚：

1. `EWC_ILLEGAL_PC user_id` 修复：直接做。
2. `PVT_MISMATCH user_id` 修复：明确为一个小的接口改造项，例如：
   - 给 `PvtTable` 注入 `HandleUserResolver`
   - 或让 `SecurityHardware` 构造 `PvtTable` 时一起注入 handle/user 查询函数

#### 结论

`Issue 13` 可以做，但计划文本里应明确：

> `PVT_MISMATCH user_id` 修复不是“查一下就完”，而是需要补一层依赖。

---

### 2.3 `Issue 14A` 中“真正的 same-user cross-process”目标是对的，但 claim 口径需要收紧

#### 当前计划

`Issue 14A` 写的是：

- 把当前 `demo_cross_process` 重命名为 `malicious_mapping`
- 新建真正的 same-user cross-process demo
- 同一 `user_id`、不同 `context_handle`
- 由 `PVT/EWC` 拦截

#### 问题在哪里

这个 issue 的大方向没问题，当前仓库确实缺一个真正的 same-user cross-process demo。

但“由 `PVT/EWC` 拦截”这句需要更精确。

当前代码基线下：

- **运行时代码执行隔离**：主要由 `EWC per-handle` 证明。
- **PVT**：主要发生在 page register / load 路径。
- **SPE bounds**：当前并没有真正实现 data bounds，只实现了 CFI / shadow stack。

所以如果新 demo 是：

- 进程 B 试图执行进程 A 的代码地址

那么这条 demo 证明的是：

- same-user + different handle 下，**EWC 仍然按 process 隔离代码执行权限**

它还**不能证明**：

- 同用户跨进程数据隔离已经完整成立；
- PVT/SPE bounds 在运行时提供了完整的第二层隔离。

#### 建议

建议把验收标准改成：

1. 新 demo 使用**相同 `user_id` + 不同 `context_handle`**。
2. 叙事聚焦于：
   - process B 尝试执行 process A 的代码地址；
   - `EWC_ILLEGAL_PC` 发生；
   - 证明 per-process execution window 生效。
3. 暂时不要把这个 demo 写成“完整证明 same-user cross-process data isolation”。

#### 结论

`Issue 14A` 应保留，但 claim 应缩准到当前代码真能证明的范围。

---

### 2.4 `Issue 14B` 的 ablation 叙事需要明确攻击载体，否则“第三阶段攻击成功”不稳定

#### 当前计划

`Issue 14B` 现在设计成三组配置：

1. 全安全：攻击被拦截
2. 禁用 EWC（wildcard window）+ 保留不同 key_id：解密失败
3. 禁用 EWC + 统一 key_id + 禁用 SPE：攻击成功

而且计划里强调：

> 不修改 executor，仅配置驱动

#### 风险在哪里

当前执行器的取指模型是：

- 先从 active handle 取 `CodeRegion`
- 再用 `pc` 相对 `region.base_va` 映射到 `code_memory`

这意味着：

- 单纯把 EWC 放宽到全地址，不等于“Bob 会自然读到 Alice 的代码”
- 如果 `pc` 跑到 Alice 区间而 active handle 仍是 Bob，可能先撞上：
  - `INVALID_PC`
  - region/base 不匹配
  - 或者根本不是你想表达的“攻击成功”

所以第三阶段“统一 key + 禁用 SPE -> 攻击成功”只有在某个前提下才稳定：

- 恶意 OS 还把 Bob 的 `code_region` 换成 Alice 的密文
- 或者让 Bob handle 指向 Alice 的 code region

这已经不是“纯配置”，而是“配置 + 恶意 OS 注入/重绑定 code region”。

#### 建议

`Issue 14B` 需要补一段“攻击载体定义”：

推荐明确写成：

1. **层 1：全安全**
   - Bob 试图执行 Alice 地址
   - EWC 拦截

2. **层 2：去掉 EWC**
   - 恶意 OS 将 Alice 密文作为 Bob 的 code region 注入
   - 但 key 不同，Decrypt 拦截

3. **层 3：去掉 EWC + key barrier + SPE**
   - 同样注入 Alice code region
   - 统一 key_id 且 `cfi_level=0`
   - 攻击成功

如果你坚持“完全不动 executor，只改配置”，那也可以，但计划必须写清：

> 第 2/3 阶段默认允许恶意 OS 在 demo 里覆写 Bob 的 `code_region`，否则叙事不稳定。

#### 结论

`Issue 14B` 是好想法，但一定要把“攻击是怎么载入执行路径的”写死。

---

## 3. 对当前计划的总体判断

### 3.1 可以继续推进

我认为这轮计划**值得做**，而且做完以后，仓库会比现在更接近一个可以对外讲清楚的 prototype：

- `Issue 12` 能补上最关键的 v4 缺口；
- `Issue 13` 能修复审计归因问题；
- `Issue 14A` 能把 demo 叙事从“有些混”变成“边界清楚”；
- `Issue 14B` 能显著增强 `Demo_Claim_Boundary_v3.1` 中 baseline / ablation 叙事；
- `Issue 15` 能把文档和代码重新对齐。

### 3.2 不建议直接照当前文字开工

如果不先细化，后面实现时最容易卡住的是：

- `PVT_MISMATCH user_id` 修复路径不清；
- same-user cross-process 的 claim 超出当前代码能力；
- ablation 第三阶段“攻击成功”不稳定。

所以我的建议不是否定这轮计划，而是：

> **保留 issue 列表和顺序，但把其中几处表述改成更贴近当前代码实际。**

---

## 4. 一版更严谨的建议稿

下面是一版我认为更稳的改写方案。不是要求你完全照抄，而是给出一个“更容易落地、也更不容易过度 claim”的版本。

---

### Issue 12：Hidden entry / saved_PC

#### 建议表述

| 项目 | 内容 |
|------|------|
| 一句话目标 | 将程序首次入口隐藏于硬件上下文中，OS 不再显式指定启动 PC，首次执行只能从 Gateway 初始化的 `saved_pc` 开始 |
| 安全动机 | 对齐 v4 的 hidden-entry 语义，防止 OS 通过显式 `entry_pc` 绕过初始化路径或指定任意入口 |
| 前置依赖 | Issue 11 |
| 预期改动 | 1. `SecurityHardware` per-handle metadata 扩展：`saved_pc` + `user_id`；2. `Gateway::Load` 初始化 `saved_pc`；3. `ExecuteProgram` 移除 `entry_pc` 参数，从 active context 读取 `saved_pc`；4. 所有 demo / 测试适配新接口 |
| 核心验收标准 | 1. 外部调用方不再显式传入 `entry_pc`；2. `GatewayLoadResult` 不暴露真实入口；3. 增加恶意 OS 测试，证明无法指定任意首次入口；4. 全部测试继续通过 |

---

### Issue 13：Audit 修复 + 代码质量

#### 建议表述

| 项目 | 内容 |
|------|------|
| 一句话目标 | 修复审计归因不完整问题，并清理局部代码重复与标签不一致 |
| 前置依赖 | Issue 12 |
| 预期改动 | 1. 修复 `EWC_ILLEGAL_PC` 审计中的 `user_id=0`；2. 为 `PVT_MISMATCH` 补充 handle->user 解析能力，再修复其 `user_id=0`；3. 合并重复 `OpToString`；4. 删除 demo 中的死代码；5. 修正 SPE stage 标签；6. 为 SPE policy 增加 bounds 占位字段和注释，但不承诺本轮实现完整 bounds |
| 核心验收标准 | 1. `EWC_ILLEGAL_PC` 与 `PVT_MISMATCH` 审计均能正确反映 handle 归属用户；2. 无重复 `OpToString`；3. SPE stage 标签与实际检查阶段一致；4. 全部测试继续通过 |

---

### Issue 14A：Demo 重命名 + 真正的 same-user cross-process

#### 建议表述

| 项目 | 内容 |
|------|------|
| 一句话目标 | 修正现有 demo 命名偏差，并新增一个真正展示“同一 user_id、不同 context_handle 的代码执行隔离”的 demo |
| 前置依赖 | Issue 13 |
| 预期改动 | 1. 将现有 `demo_cross_process` 重命名为更准确的 `demo_malicious_mapping`；2. 新建真正的 `demo_cross_process`：同一 `user_id`、两个不同 `context_handle`，进程 B 尝试执行进程 A 的代码地址，由 EWC 拦截 |
| 核心验收标准 | 1. `demo_malicious_mapping` 保持原行为；2. 新 `demo_cross_process` 明确使用 same-user / different-handle；3. 演示结果为 `EWC_ILLEGAL_PC`，用于证明 per-process execution window 生效；4. 全部测试继续通过 |

---

### Issue 14B：Ablation demo

#### 建议表述

| 项目 | 内容 |
|------|------|
| 一句话目标 | 用配置与 demo 场景组合，展示执行授权 / 代码完整性 / 行为合规逐步去掉后的退化路径 |
| 前置依赖 | Issue 14A |
| 设计要点 | 使用同一攻击叙事，但明确攻击载体：当需要展示“去掉 EWC 后仍被 Decrypt 拦截”或“进一步攻击成功”时，允许 demo 中模拟恶意 OS 向目标 handle 注入攻击代码对应的 `code_region` |
| 预期产出 | 新增 `demos/ablation/demo_ablation.cpp` + README |
| 核心验收标准 | 1. 全安全：EWC 拦截；2. 去掉执行授权但保留 key barrier：Decrypt 拦截；3. 去掉执行授权 + key barrier + SPE：攻击成功；4. 不修改 executor 安全语义，只通过 demo 配置与硬件状态准备实现 |

---

### Issue 15：文档对齐

#### 建议表述

| 项目 | 内容 |
|------|------|
| 一句话目标 | 将代码变更同步到 claim 边界、开发计划和进度追踪文档 |
| 前置依赖 | Issue 14B |
| 预期改动 | 1. `Demo_Claim_Boundary_v3.1.md` 将已被 demo 支撑的条目标为 `[Current]`；2. 对仍有限制的条目补充限制说明；3. 更新 `dev_plan.md` 状态；4. 更新 `progress_tracking_v3.md` |
| 核心验收标准 | 文档对代码状态无明显漂移，无过期 claim 或错误完成状态 |

---

## 5. 最终建议

### 5.1 我建议你保留当前这轮计划

因为：

- 大方向是对的；
- 顺序也是合理的；
- 这轮 issue 确实击中了仓库当前最真实的缺口。

### 5.2 我建议你在真正开做前，先修正三处文字

优先修正：

1. `Issue 12`：补强 hidden entry 的安全验收标准
2. `Issue 13`：明确 `PVT_MISMATCH user_id` 修复需要额外接口
3. `Issue 14A/14B`：把 same-user claim 和 ablation 攻击载体写清楚

### 5.3 一句话判断

> 这轮计划**可以做，而且值得做**；但若按当前文本直接执行，最容易出问题的是 `Issue 13` 和 `Issue 14B`，建议先把计划描述收紧到与现有代码能力严格一致的范围。

