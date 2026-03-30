# 系统设计讨论进度追踪 v4

> 最后更新：2026-03-30
> 状态说明：
> - ✅ 已确定（有明确结论，已写入 system_design_v3.md）
> - 📝 方向已定（方向确定但细节待细化）
> - ⏳ 待讨论（未开始或需要进一步讨论）
> - 🔮 Future Work（明确推迟，需在论文中标注）
> - 📌 备选方案已记录
> - 🔴 需论文阐述（需在论文中专门分析的设计特征）

---

## 一、核心设计点状态

### DP-1 加载地址决定机制 ✅ 全部完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| VA地址空间模型 | ✅ | Per-Process VA | v1已确定 |
| VA base address决定 | ✅ | OS决定，Gateway验证约束 | v1已确定 |
| 编译模式 | ✅ | PIE + 地址无关加密 | v1已确定 |
| PA分配职责 | ✅ | OS负责PA分配 | v1已确定 |
| 页表保护方案 | ✅ | B-2（反向检查表/PVT），非B-1（自主页表） | v2第2-3轮 |
| PVT存储位置 | ✅ | 片上SRAM | v2第3轮 |
| PVT参考信息来源 | ✅ | 方式X：PVT从EWC读取执行窗表 | v2第5轮 |
| Page-fault侧信道 | ✅ | PVT must_map标志位 | v2第3轮 |
| 重放攻击防御 | ✅ | 根源防御：OS不可写安全页面 | v2第6-7轮 |
| 加载时TOCTOU | ✅ | 统一写入协议：OS从不直接写PA | v2第8轮 |
| 安全页面写入 | ✅ | SECURE_PAGE_LOAD专用指令 | v2第8轮 |
| 安全页面换出 | ✅ | SECURE_PAGE_EVICT专用指令 | v2第8轮 |
| 安全页面释放 | ✅ | SECURE_PAGE_RELEASE专用指令 | v2第8轮 |
| Freshness/Version存储 | ✅ | 独立片上表，与PVT分离（思路B） | v2第9轮 |

**关键设计决定链**：
```
OS管PA但不可信 → 需要PVT反向检查 → PVT片上SRAM
                → OS不可写安全页 → 需要统一写入协议
                                  → 三条专用指令
                                  → 消除TOCTOU和运行时重放
                → 换出需要freshness → 独立version表
```

### DP-2 user_id传递策略 ✅ 全部完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| 传递方案选择 | ✅ | 并行方案（PC→执行窗→user_id） | v2 DP-2第3轮 |
| 共享库处理 | ✅ | 选项B：per-user物理拷贝 | v2 DP-2第1轮 |
| 共享库信任链 | ✅ | Gateway分别验证用户签名和库签名 | v2 DP-2第1轮 |
| caller_user_id需求 | ✅ | 不需要（选项B下owner已是调用者） | v2 DP-2第2轮 |
| CFI职责划分 | ✅ | EWC强制层 + SPE可配置层 | v2 DP-2第3轮 |
| SPE CFI粒度 | ✅ | 用户可选层级1/2/3 | v2 DP-2第3轮 |
| 窗口可达矩阵 | ✅ | 不需要（CFI由SPE负责） | v2 DP-3第2轮 |

**关键设计决定链**：
```
共享库选项B → libc执行窗owner=调用者 → 并行方案无矛盾
           → 不需要caller_user_id → 简化硬件
           → 不需要窗口可达矩阵 → EWC简化为纯门禁
```

**📌 备选**：选项A（shared执行窗+caller_user_id），适用于未来需要物理页共享时

### DP-3 控制流验证时机 ✅ 全部完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| EWC检查时机 | ✅ | 每个Fetch周期检查PC合法性 | v2 DP-3第1轮 |
| EWC检查内容 | ✅ | 只检查PC，不检查跳转目标 | v2 DP-3第2轮 |
| SPE CFI检查时机 | ✅ | Decode/Execute阶段分阶段检查 | v2 DP-3第3轮 |
| 认证解密提交时序 | ✅ | MAC验证后才提交到pipeline | v2 DP-3第3轮 |

### DP-4 中断处理时的user_id ✅ 全部完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| 中断处理方案 | ✅ | 方式Q：硬件自动保存/恢复EWC | v2 DP-4第1-3轮 |
| 用户/内核EWC隔离 | ✅ | 物理隔离（不同时存在于EWC） | v2 DP-4第3轮 |
| 安全上下文存储 | ✅ | 片上SRAM，OS不可访问 | v2 DP-4第3轮 |

### DP-5 用户态↔内核态切换 ✅ 全部完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| Syscall数据传递 | ✅ | S2（共享参数区）+ S3（SECURE_COPY） | v2 DP-5 |
| SECURE_COPY语义 | ✅ | 硬件原子转加密，≤4KB | v2 DP-5 |
| previous_user_id | ✅ | 片上寄存器，SYSCALL设置/SYSRET清除 | v2 DP-5 |
| 嵌套中断处理 | ✅ | previous_user_id作为安全上下文一部分保存/恢复 | v2 DP-5 |

### DP-6 Gateway→EWC密钥传递 ✅ v3完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| 传递方案 | ✅ | 方案A：片上专用互连直接传递明文密钥 | v3 |
| 安全依据 | ✅ | 片上硬件全部可信（威胁模型1.1） | v3 |

**📌 备选**：方案B(密钥ID+KSU)/方案C(加密传递)，适用于片外Gateway或共享互连

### DP-8 同用户不同进程隔离 ✅ v3完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| Gateway配置粒度 | ✅ | per-process（每次GATEWAY_LOAD = 一个进程实例） | v3 |
| 隔离方案 | ✅ | 方式α：EWC per-process重配置 | v3 |
| 两层隔离模型 | ✅ | 跨用户(密码学+硬件) vs 同用户跨进程(硬件策略) | v3 |
| 进程标识方案 | ✅ | 不透明context_handle（硬件不理解"进程"概念） | v3 |
| handle安全分析 | ✅ | 与显式process_id安全性无差异 | v3 |
| user_id溯源语义 | ✅ | 不受影响，user_id统一 | v3 |

**🔮 同用户进程间共享内存 → future work**

---

## 二、格式与规范

### SecureIR格式 ✅ v3完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| 用户SecureIR字段清单 | ✅ | 完整定义（身份/密钥/代码段/数据段/信任/SPE策略/资源/布局） | v3 |
| 库SecureIR结构 | ✅ | 含signer_pubkey + resource_binding=CALLER | v3 |
| 初始化数据 | ✅ | 也需加密（encrypted_content + mac） | v3 |
| 库处理模型 | 📝 | 模型2（库预注册+用户绑定），方向确定 | v3 |
| 信任声明匹配方式 | 📝 | signer_constraint为核心，功能标签待细化 | v3 |

### SPE策略编码 ✅ v3完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| SPE交互全景 | ✅ | 完整交互图（加载/Fetch/Decode/Execute/Memory/上下文切换） | v3 |
| 策略表逻辑结构 | ✅ | 元数据 + CFI目标表(L3) + 影子栈(L2/3) + Bounds表 + 扩展预留 | v3 |
| 5类运行时查询 | ✅ | 直接CALL/JMP、间接CALL/JMP、CALL压栈、RET验证、内存访问 | v3 |
| Bounds定位 | ✅ | 用于跨进程隔离（DP-8），不用于用户↔libc边界 | v3 |
| Bounds来源 | ✅ | 当前来源1（静态声明） | v3 |
| SPE策略扩展 | 📝 | 预留扩展槽，后续加入更多主流安全策略 | v3 |

**🔮 Bounds来源2(硬件推导)/来源3(运行时声明) → future work，需详细标注**

### PVT条目格式 ✅ v3完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| 完整字段 | ✅ | PA_page_id, owner, expected_VA, permissions, page_type, must_map, shared_with_kernel, state | v3 |
| page_type枚举 | ✅ | CODE/DATA/STACK/HEAP/SHARED_PARAM | v3 |
| shared_with_kernel | ✅ | 独立bit（不混入permissions），语义正交 | v3 |
| 双重校验 | ✅ | page_type与permissions一致性检查 | v3 |

### ISA指令定义 ✅ v3完成

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| 指令总数 | ✅ | 8条（含扩展SYSCALL/SYSRET） | v3 |
| GATEWAY_LOAD模式 | ✅ | 同步阻塞，返回context_handle | v3 |
| GATEWAY_STATUS | ✅ | 取消（同步模型不需要） | v3 |
| GATEWAY_RELEASE | ✅ | 传入context_handle | v3 |
| SECURE_CONTEXT_SWITCH | ✅ | 新增，内核态内进程间安全上下文切换 | v3 |
| 多操作数方案 | ✅ | 统一用参数结构体指针 | v3 |
| 参数结构体定义 | ✅ | 4条指令各自的结构体字段已定义 | v3 |

**📌 备选**：异步非阻塞GATEWAY_LOAD(+会话ID+STATUS)，适用于并行加载优化

---

## 三、安全分析

### 用户代码↔libc安全边界 🔴 需论文阐述

| 子项 | 状态 | 结论 | 讨论轮次 |
|------|------|------|---------|
| 选项B维持 | ✅ | libc继承调用者user_id，PVT单owner兼容 | v3 |
| 4条攻击路径分析 | ✅ | ROP(L2/L3防)、读libc数据、写GOT、libc漏洞利用 | v3 |
| 协作型矛盾 | ✅ | libc需访问用户数据，完全隔离不可行 | v3 |
| 安全性定位 | ✅ | ≥ 传统进程内模型 + 硬件CFI增强 | v3 |
| per-segment bounds | ✅ | 不适用于用户↔libc边界（协作型关系） | v3 |

**🔴 论文中需要：**
- 完整4条攻击路径的formal分析
- 与选项A的安全性对比
- 明确说明这是设计特征而非缺陷
- 引用CHERI/MPK作为future work方向

**🔮 Intra-process compartmentalization → future work**

### 两层隔离模型 🔴 需论文阐述

| 子项 | 状态 | 结论 |
|------|------|------|
| 跨用户隔离强度 | ✅ | PVT owner + 密钥隔离 + EWC隔离（密码学+硬件强制） |
| 同用户跨进程隔离强度 | ✅ | EWC隔离(方式α) + SPE bounds（硬件策略强制） |
| 强度差异 | ✅ | 跨用户 > 同用户跨进程（设计特征，非缺陷） |

**🔴 论文中需要：**
- 两层强度差异的形式化描述
- 每层的攻击面分析
- 为什么这种分层是合理的设计权衡

---

## 四、待讨论/待完成事项

### 设计层面

| 编号 | 事项 | 优先级 | 依赖 | 说明 |
|------|------|--------|------|------|
| D-1 | 库预注册模型（模型2）接口细节 | 中 | SecureIR✅ | 库注册/更新/注销的Gateway接口 |
| D-2 | 信任声明匹配方式 | 中 | SecureIR✅ | signer_constraint + 功能标签格式 |
| D-3 | SPE扩展策略 | 低 | SPE✅ | 后续加入哪些主流安全策略 |
| D-4 | 侧信道攻击防御集中讨论 | 高 | 全部DP | Cache/TLB/投机执行侧信道 |
| D-5 | 多核一致性 | 低 | 全部DP | PVT/EWC/SPE的多核一致性维护 |
| D-6 | 形式化安全属性定义 | 高 | 全部DP | TLA+或类似语言定义核心安全属性 |

### 论文写作层面

| 编号 | 事项 | 优先级 | 状态 | 说明 |
|------|------|--------|------|------|
| W-1 | 威胁模型与信任边界图（P0-Fig.1） | 高 | ⏳ | 学术化图绘制 |
| W-2 | SoC架构总览图（P0-Fig.2） | 高 | ⏳ | 学术化图绘制 |
| W-3 | 程序加载流程图（P0-Fig.3） | 高 | ⏳ | 学术化图绘制 |
| W-4 | 运行时安全检查流程图（P0-Fig.4） | 高 | ⏳ | 学术化图绘制 |
| W-5 | Syscall流程图（P1-Fig.5） | 中 | ⏳ | 学术化图绘制 |
| W-6 | 两层隔离模型图（P1-Fig.6） | 中 | ⏳ | 学术化图绘制 |
| W-7 | 元数据流图（P1-Fig.7） | 中 | ⏳ | 学术化图绘制 |
| W-8 | TEE对比表 | 高 | ⏳ | SGX/SEV/CCA/Keystone vs 本系统（参考task2 §1.3） |
| W-9 | 用户↔libc安全分析正文 | 高 | ⏳ | 4条攻击路径formal分析 |
| W-10 | 两层隔离模型正文 | 高 | ⏳ | 形式化描述 + 设计权衡论证 |
| W-11 | Related Work章节（Paper 1） | 高 | ⏳ | 直接对比：Capstone/EKC/CURE/Dorami；方法论：HW-SW Contracts/Universal Contracts/Verified KVM（参考task1 §2.1 + task2 Paper 1引用表） |
| W-12 | Related Work章节（Paper 2） | 中 | ⏳ | 直接对比：VSP/INCOGNITOS/AMi/Libra；元数据：HashTag/CCTAG/BliMe（参考task1 §2.1 + task2 Paper 2引用表） |
| W-13 | Gap Analysis正文 | 高 | ⏳ | 基于task2 §3.1的G1-G7，配合CSV统计数据（参考task2 §2.1-§2.6） |

### PPT制作

用途：导师/组会汇报 + 项目进度展示（合作者）

#### Part 1: 问题与动机（3-4页）

| 编号 | 页面内容 | 优先级 | 状态 | 数据来源 | 具体内容 |
|------|---------|--------|------|---------|---------|
| P-1a | 现有TEE局限性 | 高 | ⏳ | task2 §1.3 + defense_papers.csv | TEE对比表：SGX/SEV-SNP/CCA/TDX/Keystone vs 本系统，维度含TCB/隔离粒度/策略灵活性/审计能力/用户绑定 |
| P-1b | 攻击面分类总览 | 高 | ⏳ | attack_papers.csv (10篇) | 按Root_Cause分布饼图（Side Channel 5 / Logic 3 / Speculative 3 / Race 1）；按Primary_Attack_Impact分布；每类1-2句关键takeaway |
| P-1c | 真实攻击案例 | 中 | ⏳ | task1 §1.1 高度相关5篇 | 2-3个具体攻击叙事：WESEE（中断注入→confused deputy）、Code Confidentiality（IR放大侧信道）、CounterSEVeillance（PMC→指令级恢复），直接映射到我们的设计动机 |
| P-1d | 核心问题陈述 | 高 | ⏳ | paper_plan.md | 三大问题：(1)跨层语义断裂 (2)OS被攻破后保证丧失 (3)策略与用户身份脱钩 |

#### Part 2: 系统设计（5-7页）

| 编号 | 页面内容 | 优先级 | 状态 | 说明 |
|------|---------|--------|------|------|
| P-2a | 威胁模型与信任边界（Fig.1） | 高 | ⏳ | 可信/不可信组件、攻击者能力、两层隔离模型 |
| P-2b | SoC架构总览（Fig.2） | 高 | ⏳ | Gateway/EWC/SPE/PVT/审计模块 + 片上SRAM |
| P-2c | 核心机制简述 | 高 | ⏳ | 每个模块1-2句话功能定义 + 职责边界 |
| P-2d | 程序加载流程（Fig.3） | 高 | ⏳ | SecureIR→Gateway验证→EWC/SPE配置→SECURE_PAGE_LOAD |
| P-2e | 运行时安全检查（Fig.4） | 高 | ⏳ | Pipeline各阶段与EWC/SPE交互 + PVT并行检查 |
| P-2f | 两层隔离模型 | 中 | ⏳ | 跨用户(密码学) vs 同用户跨进程(策略) 对比图 |
| P-2g | ISA指令汇总 | 低 | ⏳ | 8条指令表格（备用页） |

#### Part 3: 相关工作分析（4-5页）— 基于task1/task2数据

| 编号 | 页面内容 | 优先级 | 状态 | 数据来源 | 具体内容 |
|------|---------|--------|------|---------|---------|
| P-3a | 防御机制landscape定位 | 高 | ⏳ | task2 §3.2 | 四轴定位图（信任模型/策略灵活性/审计能力/跨层一致性），标注本系统位置 vs 现有工作位置 |
| P-3b | TCB分布 + 密文边界分布 | 高 | ⏳ | task2 §2.1 + §2.3 | 柱状图/饼图：仅hardware(15篇) vs hardware+OS(19篇) vs 含OS(34篇)；on-chip only(7篇) vs off-chip allowed(34篇)，突出我们的位置稀缺性 |
| P-3c | 审计 + 防回滚gap | 高 | ⏳ | task2 §2.4 + §2.5 | 饼图：66篇无审计 / 87篇无防回滚；用数据说明Paper 4的差异化空间 |
| P-3d | 7大Gap汇总 | 高 | ⏳ | task2 §3.1 | G1-G7表格：Gap描述 + 最接近工作 + 我们的方案如何填补 |
| P-3e | 核心相关工作对比表 | 中 | ⏳ | task1 §2.1 (24篇) | 按Paper 1-4分组的核心related work，每组3-5篇最重要的，标注对比维度和差异 |

#### Part 4: 当前进度与计划（2-3页）

| 编号 | 页面内容 | 优先级 | 状态 | 说明 |
|------|---------|--------|------|------|
| P-4a | 设计点完成度 | 高 | ⏳ | 已完成(DP-1~DP-8全部✅) + 格式规范(SecureIR/SPE/PVT/ISA全部✅) |
| P-4b | 待完成事项 | 高 | ⏳ | 侧信道、形式化、图绘制、论文写作 |
| P-4c | Demo计划 | 中 | ⏳ | Layer 1 scope + 6模块 + 5演示场景 + C/C++→Rust路线 |
| P-4d | 论文时间线 | 中 | ⏳ | Paper 1-4的目标venue + 时间规划 |

#### PPT数据可视化清单

以下图表需要在PPT中制作：

| 图表编号 | 类型 | 数据来源 | 内容 |
|---------|------|---------|------|
| Chart-1 | 饼图 | attack_papers.csv Root_Cause | 攻击根因分布（Side Channel / Logic / Speculative / Race） |
| Chart-2 | 矩阵图 | attack_papers.csv Impact×Victim | 攻击影响 × 受害面 热力图 |
| Chart-3 | 对比表 | defense_papers.csv + 手动 | TEE对比表（SGX/SEV/CCA/Keystone/本系统 × 8+维度） |
| Chart-4 | 四轴定位图 | task2 §3.2 | 系统在解决方案谱系中的位置 |
| Chart-5 | 柱状图 | defense_papers.csv TCB列 | TCB分布（6类） |
| Chart-6 | 柱状图 | defense_papers.csv Cleartext | 密文边界分布（5类） |
| Chart-7 | 饼图 | defense_papers.csv Audit | 审计能力分布（66篇无审计突出） |
| Chart-8 | 饼图 | defense_papers.csv Anti_Replay | 防回滚分布（87篇未讨论突出） |
| Chart-9 | Gap表 | task2 §3.1 | G1-G7 gap矩阵 |
| Chart-10 | 时间线 | 手动 | 项目时间线（已完成→当前→计划） |

### Demo实现

| 编号 | 事项 | 优先级 | 状态 | 说明 |
|------|------|--------|------|------|
| I-1 | SecureIR工具链（签名+序列化） | 高 | ✅ | Issue 5（Gateway + SecureIR 解析）+ Issue 11（SecureIrBuilder 库函数） |
| I-2 | Gateway模拟器 | 高 | ✅ | Issue 5 |
| I-3 | EWC模拟器 | 高 | ✅ | Issue 3 |
| I-4 | PVT模拟器 | 高 | ✅ | Issue 7 |
| I-5 | SPE模拟器 | 高 | ✅ | Issue 8 |
| I-6 | 简单CPU模拟循环 | 高 | ✅ | Issue 2（Executor pipeline）+ Issue 4（Decrypt 阶段） |
| I-7 | 5个演示场景 + 扩展 | 高 | ✅ | Issue 9（5类 demo）+ Issue 14A（same-user cross-process）+ Issue 14B（ablation） |
| I-8 | Rust重写 | 低 | ⏳ | 在C/C++ demo稳定后进行 |

### 最终迭代轮完成摘要（Issues 12-15）

> 2026-03-06 至 2026-03-30 完成，基于 Claude + Codex 双重代码审查报告。

| Issue | 主题 | 完成日期 | 关键改动 |
|-------|------|---------|---------|
| 12 | Hidden entry / saved_PC | 2026-03-20 | ExecuteProgram 不再接收 entry_pc，从硬件上下文 saved_pc 启动 |
| 13 | Audit 修复 + 代码质量 | 2026-03-24 | AuditCollector resolver 注入、OpToString 合并、SPE stage 标签修正 |
| 14A | Demo 重命名 + same-user cross-process | 2026-03-27 | cross_process→malicious_mapping 重命名、MonotonicPageAllocator、新 same-user demo |
| 14B | Ablation demo | 2026-03-30 | 三层安全退化叙事（EWC→Decrypt→全去掉），纯配置��动 |
| 15 | 文档对齐 | 2026-03-30 | Demo_Claim_Boundary [Scaffolded]→[Current]、dev_plan 状态更新、本文档 |

---

## 五、Future Work 清单

| 编号 | 事项 | 来源 | 相关工作 |
|------|------|------|---------|
| FW-1 | 同用户进程间共享内存 | DP-8 | 一致性问题 |
| FW-2 | 动态内存Bounds（来源2/3） | SPE格式讨论 | heap等动态分配 |
| FW-3 | Intra-process compartmentalization | 用户↔libc分析 | CHERI/MPK/MTE |
| FW-4 | 安全I/O通道 | 威胁模型1.5 | 设备到用户直接通道 |
| FW-5 | Security Monitor | v2已标记 | 调度公平性/DoS监控 |
| FW-6 | 微架构侧信道防御 | 待讨论D-4 | Cache/TLB侧信道 |
| FW-7 | 多核一致性 | 待讨论D-5 | PVT/EWC/SPE多核 |
| FW-8 | 选项A共享库物理页共享 | DP-2备选 | PVT shared + caller_uid |

---

## 六、备选方案索引

| 方案 | 当前决定 | 备选 | 适用场景 |
|------|---------|------|---------|
| 共享库处理 | 选项B（per-user拷贝） | 选项A（shared执行窗+caller_uid） | 需要物理页共享时 |
| 内核访问用户数据 | S2+S3 | S1（临时授权表TAT） | 需要更灵活授权时 |
| freshness | OS不可写+version表 | F-3（Merkle Tree） | 需要更强防重放时 |
| Gateway→EWC密钥 | 方案A（直接传递） | B(KSU)/C(加密传递) | 片外Gateway/共享互连 |
| 上下文标识 | 不透明句柄 | 显式process_id | 硬件需理解进程语义 |
| GATEWAY_LOAD模式 | 同步阻塞 | 异步非阻塞(+会话ID) | 并行加载优化 |
| Bounds来源 | 来源1（静态声明） | 来源2(硬件推导)/来源3(运行时声明) | 动态内存bounds |

---

## 七、关键设计决定链总览

```
v1-v2 设计决定链：
  OS不可信 → PVT反向检查 → 统一安全页面协议 → 3条专用指令
  共享库选项B → 并行方案 → EWC简化为纯门禁
  CFI分层 → EWC强制层(Fetch) + SPE可配置层(Decode/Execute/Memory)
  中断方式Q → 硬件自动保存/恢复EWC → 用户/内核物理隔离
  Syscall → S2+S3 → SECURE_COPY + previous_user_id

v3 新增设计决定链：
  同用户多进程 → Gateway per-process配置 → context_handle
  → 方式α(EWC重配置) → 两层隔离模型

  密钥传递 → 片上全可信 → 方案A直接传递

  SecureIR → 库预注册模型2 → 用户加载时只做owner绑定

  SPE bounds → 静态声明(来源1) → 跨进程隔离 → 不用于用户↔libc

  用户↔libc → 选项B维持 → 安全性≥传统模型+硬件CFI
  → intra-process compartmentalization = future work

  PVT → 含page_type + shared_with_kernel → 双重校验

  ISA → 8条指令 + 同步GATEWAY_LOAD + SECURE_CONTEXT_SWITCH
```

---

## 八、文档版本历史

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| system_design_v1.md | 2026-02 | 初始架构：Gateway/EWC/SPE/PVT概念，三种user_id方案 |
| progress_tracking.md | 2026-02 | 初版进度追踪（v1设计点状态记录） |
| system_design_v2.md | 2026-02 | DP-1~DP-5完成：PVT、统一写入协议、并行方案、方式Q、SECURE_COPY |
| progress_tracking_v2.md | 2026-02 | 进度追踪v2（含DP-1~DP-5全部子项状态和关键设计决定链） |
| task1_relevant_papers.md | 2026-02 | 攻防论文筛选报告（10篇攻击选9，100篇防御选62，按关联度分级） |
| task2_defense_survey.md | 2026-02 | 防御论文结构化综述（6个问题域 + CSV多维度统计 + 7大Gap + 系统定位） |
| system_design_v3.md | 2026-03-01 | DP-6/DP-8完成：密钥传递、两层隔离、context_handle、SecureIR/SPE/PVT/ISA完整规范 |
| progress_tracking_v3.md | 2026-03-01 | 进度追踪v3，含图/PPT（含攻防数据可视化）/Demo规划 |
| progress_tracking_v4.md | 2026-03-30 | 进度追踪v4：Demo I-1~I-7 全部完成（✅），新增最终迭代轮（Issues 12-15）完成摘要 |
