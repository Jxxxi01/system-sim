# 安全系统设计文档 v3

> 文档说明：
> - `[已确定]` 标记已经讨论确定的设计点
> - `[待讨论]` 标记需要进一步讨论的设计点
> - `[方案未定]` 标记已有多个候选方案但尚未选定的设计点
> - `[待补充]` 标记需要补充细节的部分
> - `[备选]` 标记已记录的备选方案
> - `[future work]` 标记明确推迟到未来的工作
> - `[需论文阐述]` 标记需要在论文中专门分析的设计特征
>
> v3 更新说明：
> - DP-8：同用户不同进程隔离（方式α + 两层隔离模型 + context_handle）
> - DP-6：Gateway→EWC密钥传递（方案A确定，片上专用互连直接传递）
> - SecureIR完整字段规范（含库预注册模型方向）
> - SPE交互全景 + 策略表逻辑结构
> - PVT条目完整格式（含page_type + shared_with_kernel独立bit）
> - ISA指令完整定义（8条，含SECURE_CONTEXT_SWITCH + context_handle）
> - 用户代码↔libc安全分析（维持选项B + 攻击路径分析）
> - bounds定位（跨进程隔离手段，非同进程内用户↔libc边界）
>
> v2 更新说明（保留）：
> - DP-1：加载地址决定机制（PVT、统一安全页面协议、freshness）
> - DP-2：user_id 传递策略（并行方案 + 共享库选项 B + SPE 可配置 CFI）
> - DP-3：控制流验证时机（EWC Fetch 时 + SPE Decode/Execute 时分阶段）
> - DP-4：中断处理时的 user_id（方式 Q：硬件自动切换 EWC）
> - DP-5：用户态↔内核态切换（S2+S3、SECURE_COPY、previous_user_id）

---

# 1. 威胁模型与安全目标

## 1.1 信任边界

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  信任边界                                                                    │
│                                                                              │
│  可信组件：                                                                  │
│    - 硬件（CPU、Gateway、EWC、SPE、PVT验证逻辑、审计模块）                 │
│    - 片上互连（专用互连，不经过共享总线）                                   │
│    - 用户自己的代码（用户对自己的代码负责）                                 │
│                                                                              │
│  不可信组件：                                                                │
│    - 操作系统（内核）                                                       │
│    - 共享库（运行时可能被攻破）                                             │
│    - 其他用户的代码                                                         │
│                                                                              │
│  注：OS厂商签名的软件（如libc）在"出厂状态"是可信的，                       │
│      但"运行时状态"不可信（可能被攻破）                                     │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 1.2 攻击者能力

| 能力 | OS是否具备 | 说明 |
|------|-----------|------|
| 读取任意物理内存 | 是，但只能读到密文 | 加密保护 |
| 写入安全用户的物理页 | **否** | [已确定] PVT + 统一写入协议阻止 |
| 写入非安全物理内存 | 是 | 内核自己的页面可写 |
| 控制进程调度 | 是 | 可造成DoS（已接受） |
| 配置页表 | 是，但受 PVT 验证 | [已确定] PVT 反向检查 |
| 伪造用户签名 | 否 | 密码学保护 |
| 修改Gateway/SPE/EWC配置 | 否 | 硬件保护 |
| 通过 SECURE_COPY 跨用户操作 | 否 | [已确定] previous_user_id 约束 |
| 操纵 context_handle 切换他人上下文 | 是，但等价于正常调度 | [已确定] 不超出OS已有调度能力 |

## 1.3 安全保证

本系统提供以下安全保证：

1. **机密性**：用户代码和数据对OS和其他用户不可见（加密保护）
2. **完整性**：用户代码和数据不可被篡改（认证加密 MAC 保护）
3. **Freshness**：用户数据不可被回滚到旧版本（OS不可写安全页 + 换入时version验证）
4. **可追溯性**：所有安全相关事件可追溯到具体用户（审计链+user_id）

## 1.4 [已确定] 两层隔离模型（v3 新增）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  两层隔离模型                                                                │
│                                                                              │
│  第一层：跨用户隔离（Alice vs Bob）                                         │
│    保护机制：PVT owner检查 + 密钥隔离 + EWC隔离                            │
│    强度：密码学+硬件强制（即使SPE完全失效仍成立）                           │
│    保证：Alice的数据对Bob和OS不可见、不可篡改                              │
│                                                                              │
│  第二层：同用户跨进程隔离（Alice的P1 vs Alice的P2）                         │
│    保护机制：EWC隔离（方式α）+ SPE bounds检查                              │
│    强度：硬件策略强制（依赖SPE bounds正确配置）                             │
│    保证：P1不能访问P2的数据段（bounds约束）                                │
│    限制：PVT层面两者owner相同、密钥相同                                     │
│                                                                              │
│  [需论文阐述] 两层强度差异是设计特征，非缺陷                                │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 1.5 接受的风险

1. **DoS攻击**：OS可以拒绝服务（不调度、不转发请求）
2. **资源公平性**：OS可以不公平地分配资源
3. **I/O路径数据可见**：I/O经过内核中转时，内核可见数据明文 → [future work] 安全I/O通道
4. **[待讨论] 侧信道攻击**：Cache侧信道等需要后续集中讨论

## 1.6 消除的攻击面（v3 更新）

| 攻击 | 防御机制 | 所属DP |
|------|---------|--------|
| 跨用户重映射 | PVT owner 检查 | DP-1 |
| 别名映射 | PVT expected_VA 检查 | DP-1 |
| 重放攻击 | OS不可写安全页 + 换入时version验证 | DP-1 |
| 加载时 TOCTOU | 统一写入协议（OS从不直接写PA） | DP-1 |
| Page-fault 侧信道 | PVT must_map 约束 | DP-1 |
| 代码注入 | EWC PC合法性检查 | DP-3 |
| 跨用户代码执行 | EWC 物理隔离（方式Q） | DP-4 |
| 用户→内核非法跳转 | 用户态EWC中无内核窗口 | DP-4 |
| 内核→用户非法跳转 | 内核态EWC中无用户窗口 | DP-4 |
| SECURE_COPY 跨用户注入 | previous_user_id 约束 | DP-5 |
| 同用户跨进程数据泄漏 | 方式α（EWC per-process） + SPE bounds | DP-8 |

## 1.7 [需论文阐述] 用户代码↔libc安全边界分析（v3 新增）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  选项B下用户代码↔libc的安全分析                                              │
│                                                                              │
│  前提：                                                                      │
│    W1(用户代码) owner=Alice, W2(libc) owner=Alice                           │
│    PVT owner相同, 密钥相同 → PVT级别无隔离                                 │
│                                                                              │
│  攻击路径分析：                                                              │
│                                                                              │
│  攻击1: ROP利用libc gadgets                                                 │
│    L1: 不阻止                                                               │
│    L2/L3: 影子栈/跳转白名单阻止  ✓                                         │
│                                                                              │
│  攻击2: 用户代码读libc内部数据（GOT表等）                                   │
│    无bounds: PVT允许（owner=Alice）  ✗                                      │
│    有per-segment bounds: W1 bounds不含libc数据范围  ✓                       │
│    但：per-segment bounds在此场景下不实际（见下文"协作型矛盾"）             │
│                                                                              │
│  攻击3: 用户代码写libc GOT（函数指针劫持）                                  │
│    同攻击2                                                                   │
│                                                                              │
│  攻击4: libc漏洞被利用后访问用户敏感数据                                    │
│    选项B: libc在Alice上下文中 → 可直接访问  ✗                              │
│    但：libc正常运行时就需要访问用户数据（printf读user_buf等）               │
│    → 完全隔离会破坏功能                                                      │
│                                                                              │
│  协作型矛盾：                                                                │
│    用户代码↔libc是协作型（cooperative）关系                                  │
│    正常执行时需要相互访问数据                                                │
│    完全的per-segment bounds隔离不可行                                        │
│    → 与跨用户的对抗型（adversarial）关系有本质区别                           │
│                                                                              │
│  结论：                                                                      │
│    选项B安全性 ≥ 传统进程内安全模型                                         │
│      + EWC阻止代码注入（传统模型没有）                                      │
│      + SPE L2/L3硬件CFI（比软件CFI更强）                                    │
│      + 加密保护代码完整性（传统模型没有）                                    │
│    intra-process compartmentalization = open problem                          │
│    → [future work] 可参考 CHERI/MPK 方向                                    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

# 2. 核心概念

## 2.1 用户绑定语义

### 设计概述

本系统将安全与用户绑定，user_id成为核心语义，实现：
- 安全策略可定制
- 数据产生可溯源
- 跨执行单元的一致保护

### ID概念区分

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  三种身份的区分                                                              │
│                                                                              │
│  1. Signer_ID（签名者身份）                                                  │
│     - 用途：标识"谁签名了这个SecureIR"                                      │
│     - 来源：签名者的公钥                                                    │
│     - 用于：Gateway验证签名、建立信任链                                     │
│     - 例子：Alice的程序由Alice签名，libc由OS_vendor签名                     │
│                                                                              │
│  2. Runtime_User_ID（运行时用户身份）                                        │
│     - 用途：标识"这段代码正在为谁执行"                                      │
│     - [已确定] 来源：PC所在执行窗的owner（并行方案）                        │
│     - 用于：资源访问控制（SPE/PVT检查）                                     │
│     - 例子：Alice调用printf时，runtime_user_id = Alice                      │
│            （选项B：libc执行窗owner=Alice）                                 │
│                                                                              │
│  3. Code_Owner_ID（代码拥有者身份）                                          │
│     - 用途：标识"这段代码的策略由谁定义"                                    │
│     - 来源：执行窗的code_policy来源                                         │
│     - 用于：CFI策略的选择                                                   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 用户定位与应用场景

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  1. 最适合场景（按优先级）                                                   │
│                                                                              │
│     (1) 高安全终端                                                          │
│         - 单用户设备，防止恶意软件/OS攻击                                   │
│         - 用户=设备所有者                                                   │
│                                                                              │
│     (2) 云计算/多租户                                                       │
│         - 多个互不信任的租户共享物理机                                      │
│         - 用户=云租户                                                       │
│         - 提供比SEV/TDX更细粒度的隔离                                       │
│                                                                              │
│     (3) 去中心化计算（需结合attestation）                                   │
│         - 无中央信任方的计算环境                                            │
│         - 用户=计算发起者                                                   │
│                                                                              │
│  2. "用户"的定义                                                            │
│     技术定义：密钥持有者，user_id = hash(user_pubkey)                       │
│     语义定义：有自己的身份和责任边界的实体（人、组织、或应用）              │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 2.2 身份标识生成与管理

### user_id的生成

```
核心原则：user_id = f(user_pubkey)
具体方案：user_id = truncate(hash(user_pubkey), 16 bits) = SHA256(user_pubkey)[0:15]
```

### 片外到片内映射与冲突处理

```
方案：Gateway维护映射表
  - 首次加载无冲突时：actual_user_id = pubkey_hash
  - 检测到冲突时：分配新的actual_user_id（重命名）
  - 存储位置：片上SRAM（接受用户数量限制）
```

### [已确定] context_handle（v3 新增）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  context_handle：硬件分配的不透明上下文句柄                                   │
│                                                                              │
│  用途：索引片上安全上下文槽                                                  │
│  分配：GATEWAY_LOAD成功后由硬件返回                                         │
│  释放：GATEWAY_RELEASE时传入                                                │
│  使用：SECURE_CONTEXT_SWITCH时传入目标句柄                                  │
│                                                                              │
│  设计哲学：                                                                  │
│    - 硬件不理解"进程"概念，只管理上下文槽                                   │
│    - OS负责 context_handle → 进程 的映射                                    │
│    - 硬件可见的身份概念只有 user_id                                         │
│    - context_handle 不是身份，是资源索引                                     │
│                                                                              │
│  安全分析：                                                                  │
│    OS可以操纵任何有效handle → 等价于OS已有的调度能力                         │
│    OS无法通过handle做超出调度权的事 → 安全性由EWC/PVT/加密保证              │
│    handle释放后失效 → 硬件拒绝对无效handle的操作                             │
│                                                                              │
│  与 user_id 的关系：                                                         │
│    一个 user_id 可对应多个 context_handle（多个进程）                        │
│    不同 user_id 的 context_handle 在同一个硬件上下文表中                     │
│    上下文切换时，硬件通过 handle 找到对应的安全上下文                        │
│    安全上下文中包含 user_id → EWC/SPE/PVT据此工作                          │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

# 3. 硬件模块设计

## 3.1 语义网关（Gateway）

### 总体描述

Gateway作为独立协处理器存在，负责加载时验证签名、做必要检查与安全策略配置。运行时不参与关键路径。

### 核心职责

- 验证SecureIR签名
- 解析SecureIR内容
- 配置执行窗（EWC）/ SPE / 用户标签
- 密钥管理与传递
- [已确定] **不负责**PVT管理和PA注册（保持加载时配置者角色）
- [已确定] **分配 context_handle** 并管理上下文槽（v3 新增）

### [已确定] 配置粒度（v3 新增）

```
Gateway配置粒度 = 进程

  一次 GATEWAY_LOAD = 一个进程实例的完整配置
  同一程序多次实例化 → 多次 GATEWAY_LOAD → 多个独立配置
  每次返回不同的 context_handle

  配置产出：
    - 一套 EWC 执行窗配置
    - 一套 SPE 策略配置
    - 密钥绑定
    - 安全上下文槽分配
```

### [已确定] 库处理模型（v3 新增）

```
模型2：库预注册 + 用户绑定

  阶段1（库预注册，可在OS启动时完成）：
    OS提交库的SecureIR → Gateway验证签名 → 缓存解析结果
    （代码布局、CFI策略等，与具体用户无关的信息）

  阶段2（用户加载时绑定）：
    Gateway取缓存的库解析结果 + 当前user_id
    → 生成 per-user 的 EWC 配置（owner=Alice）
    → 避免重复验证签名

  [待细化] 库注册/更新/注销的Gateway接口
```

### 作用时间点

```
Gateway = "程序加载时的验证+配置者"

时机：OS调用GATEWAY_LOAD → 触发Gateway
延迟：不在fetch关键路径，可以100+周期
方式：[已确定] 同步阻塞，完成后返回
职责：验证签名 + 配置EWC/SPE/用户标签 + 分配context_handle
输出：EWC执行窗配置（PVT验证逻辑的参考依据）+ context_handle
```

### 与CPU的接口

[已确定] 采用专用同步指令（v3 简化）：

```
新增ISA指令：
  GATEWAY_LOAD rd, rs1     ; rs1=参数结构体指针, rd=context_handle
  GATEWAY_RELEASE rs1      ; rs1=context_handle
```

## 3.2 执行窗控制器（EWC）

### [已确定] 简化后的 EWC 角色

```
EWC = "代码身份验证 + 解密控制"的门禁系统

输入：PC
输出：合法/非法 + key_id + 窗口元数据（owner, type, code_policy_id）
本质：PC → 执行窗属性 的快速查找表

检查时机：每个 Fetch 周期
延迟要求：1 周期完成（在 Fetch 关键路径上）

不负责：
  - 跳转目标合法性检查（下一次 Fetch 自然覆盖）
  - 窗口间可达矩阵（CFI 交给 SPE）
```

### EWC 功能

```
1. 窗口配置管理
   - 接收Gateway的配置命令
   - 维护执行窗表：(窗口ID, 地址范围, owner_user_id, key_id, type, code_policy_id)

2. PC合法性检查
   - 接收Fetch的PC
   - 检查PC是否在当前用户的合法窗口内
   - 返回：合法/非法 + key_id + 窗口元数据

3. [已确定] 为PVT提供参考信息（方式X）
   - PVT验证逻辑从EWC读取执行窗表
   - 用于验证OS的PA注册请求

4. 代码解密
   - 使用key_id获取密钥
   - 解密代码行（认证加密，MAC验证通过后才提交）

5. 状态管理
   - [已确定] 方式Q：中断/syscall时硬件自动保存/恢复EWC状态
   - [已确定] 方式α：同用户不同进程切换时EWC重配置（v3新增）
   - 用户态和内核态EWC配置物理隔离

6. 审计接口
   - 非法访问尝试 → 生成审计事件
```

### EWC 防御的攻击

```
✓ 代码注入（PC不在合法窗口 → Trap）
✓ 错误解密/未解密代码执行（错误密文解密为垃圾 → crash/trap）
✓ 跨用户代码执行（其他用户窗口不在EWC中）
✓ OS篡改执行窗配置（只有Gateway可写EWC）
✓ 同用户跨进程代码执行（方式α：其他进程窗口不在EWC中）（v3新增）
```

### 执行窗表结构（选项B下）

```
示例：Alice 加载了自己的代码和 libc

  窗口1: [0x400000, 0x410000)
         owner=Alice, key=K_alice, type=USER_CODE
         code_policy_id=alice_cfi_policy

  窗口2: [0x500000, 0x520000)
         owner=Alice, key=K_alice, type=TRUSTED_LIB
         signer=OS_vendor, code_policy_id=libc_cfi_policy

  两个窗口 owner 都是 Alice。
  区别在于 type 字段和 code_policy 来源。
```

## 3.3 可配置安全策略引擎（SPE）

### [已确定] 强制层与可配置层分离

```
┌─────────────────────────────────────────────────────┐
│                                                       │
│  强制层（EWC，所有用户，不可配置）：                    │
│    每个 Fetch 周期检查 PC 是否在合法执行窗内           │
│    基线保护：代码不能越出执行窗边界                    │
│                                                       │
│  可配置层（SPE，用户通过 SecureIR 声明粒度）：         │
│    层级 1：无额外 CFI（仅 EWC 保护）                  │
│    层级 2：影子栈（防 ROP）                            │
│    层级 3：完整细粒度 CFI                              │
│           （影子栈 + 间接跳转白名单 + 入口点约束）     │
│    用户风险自担，不影响其他用户隔离                    │
│                                                       │
│  独立于CFI的策略（所有层级）：                         │
│    Bounds检查：数据访问地址范围约束                    │
│    Permissions检查：访问类型约束                       │
│    [待补充] 后续可加入更多主流安全策略                 │
│                                                       │
│  分界线：                                              │
│    EWC："代码能不能在这里执行"（身份 + 解密）          │
│    SPE："代码能不能这样执行"（策略 + 合规）            │
│                                                       │
└─────────────────────────────────────────────────────┘
```

### [已确定] SPE 交互全景（v3 新增）

```
╔═══════════════════════════════════════════════════════════════════════════╗
║                        SPE 交互全景                                       ║
╠═══════════════════════════════════════════════════════════════════════════╣
║                                                                           ║
║  ┌─────────────────────────────────────────────────────────────────────┐ ║
║  │                     加载阶段（一次性配置）                            │ ║
║  │                                                                      │ ║
║  │  Gateway ──→ SPE                                                    │ ║
║  │    内容：cfi_level, call/jmp_targets, bounds_policy,                │ ║
║  │          code_policy_id, 关联的user_id                              │ ║
║  │    时机：GATEWAY_LOAD处理过程中                                      │ ║
║  │    通路：片上专用互连                                                │ ║
║  │                                                                      │ ║
║  └─────────────────────────────────────────────────────────────────────┘ ║
║                                                                           ║
║  ┌─────────────────────────────────────────────────────────────────────┐ ║
║  │                     运行时（每周期交互）                              │ ║
║  │                                                                      │ ║
║  │  ① Fetch阶段                                                       │ ║
║  │     EWC ──→ SPE                                                     │ ║
║  │       内容：code_policy_id, owner_user_id                           │ ║
║  │       作用：SPE加载该执行窗对应的策略表                              │ ║
║  │                                                                      │ ║
║  │  ② Decode阶段                                                      │ ║
║  │     Pipeline ──→ SPE                                                │ ║
║  │       内容：指令类型 + 操作数                                        │ ║
║  │       细分：                                                         │ ║
║  │         直接CALL → target地址 → L3:查call_targets                  │ ║
║  │         直接JMP  → target地址 → L3:查jmp_targets                   │ ║
║  │         CALL     → 返回地址   → L2/L3:影子栈push                   │ ║
║  │         间接CALL/JMP → 标记，等Execute提供实际target                │ ║
║  │         RET      → 标记，等Execute提供实际target                    │ ║
║  │                                                                      │ ║
║  │  ③ Execute阶段                                                     │ ║
║  │     Pipeline ──→ SPE                                                │ ║
║  │       内容：间接CALL/JMP/RET的实际目标地址                           │ ║
║  │       细分：                                                         │ ║
║  │         间接CALL/JMP → 实际target → L3:查call/jmp_targets          │ ║
║  │         RET          → 实际target → L2/L3:影子栈pop+比较           │ ║
║  │                                                                      │ ║
║  │  ④ Memory阶段                                                      │ ║
║  │     Pipeline ──→ SPE                                                │ ║
║  │       内容：访问地址 + 访问类型(Load/Store) + 数据大小              │ ║
║  │       动作：bounds检查 + permissions检查                             │ ║
║  │       说明：此检查独立于CFI层级，所有层级都执行                      │ ║
║  │                                                                      │ ║
║  └─────────────────────────────────────────────────────────────────────┘ ║
║                                                                           ║
║  ┌─────────────────────────────────────────────────────────────────────┐ ║
║  │                     SPE ──→ 其他模块                                 │ ║
║  │                                                                      │ ║
║  │  SPE ──→ Pipeline                                                   │ ║
║  │    内容：检查结果（通过/违规）                                        │ ║
║  │    违规时：Trap信号 + 清除pipeline                                   │ ║
║  │                                                                      │ ║
║  │  SPE ──→ 审计模块                                                   │ ║
║  │    内容：违规事件（违规类型, PC, user_id, 目标地址, 时间戳）         │ ║
║  │    时机：任何策略违规时                                               │ ║
║  │                                                                      │ ║
║  └─────────────────────────────────────────────────────────────────────┘ ║
║                                                                           ║
║  ┌─────────────────────────────────────────────────────────────────────┐ ║
║  │                     上下文切换时                                      │ ║
║  │                                                                      │ ║
║  │  硬件安全逻辑 ──→ SPE                                               │ ║
║  │    SYSCALL/中断时：SPE策略表 + 影子栈 保存到片上SRAM                 │ ║
║  │    SYSRET/中断返回时：从片上SRAM恢复                                 │ ║
║  │    SECURE_CONTEXT_SWITCH时：同上（方式α）                            │ ║
║  │                                                                      │ ║
║  └─────────────────────────────────────────────────────────────────────┘ ║
║                                                                           ║
║  ┌─────────────────────────────────────────────────────────────────────┐ ║
║  │                     SPE 不直接交互的模块                              │ ║
║  │                                                                      │ ║
║  │  PVT：SPE不直接查PVT。内存访问的owner检查由PVT独立完成（TLB fill时）│ ║
║  │       SPE的bounds检查和PVT的owner检查是并行的两道独立防线            │ ║
║  │                                                                      │ ║
║  │  密钥存储：SPE不涉及加解密，密钥由EWC（代码解密）和                  │ ║
║  │           内存加密引擎（数据加解密）使用                              │ ║
║  │                                                                      │ ║
║  └─────────────────────────────────────────────────────────────────────┘ ║
╚═══════════════════════════════════════════════════════════════════════════╝
```

### [已确定] SPE 策略表逻辑结构（v3 新增）

```
SPE 策略存储（per-process，上下文切换时保存/恢复）
═══════════════════════════════════════════════════

[策略元数据]
  code_policy_id           // 当前活跃的策略标识
  owner_user_id            // 关联用户
  cfi_level                // 1 | 2 | 3

[CFI目标表]  （仅层级3使用）
  call_targets[]:
    target_VA              // 合法CALL目标地址
  jmp_targets[]:
    target_VA              // 合法JMP目标地址

  查询语义：给定target_VA，判断是否在集合中（membership query）

[影子栈]  （层级2/3使用）
  shadow_stack[]:
    return_address         // 压入的返回地址
  shadow_stack_ptr         // 当前栈顶指针

  操作语义：
    CALL → push(return_address)
    RET  → pop() → 比较实际target，不匹配则违规

[Bounds表]  （所有层级，独立于CFI）
  bounds_entries[]:
    segment_id             // 关联的代码段（哪个执行窗）
    range[]:
      base_VA              // 可访问范围起始
      limit_VA             // 可访问范围终止
      permissions          // R | W | RW

  查询语义：给定(当前执行窗, 访问地址, 访问类型)，
           判断地址是否在该执行窗关联的range中且权限匹配

  定位：
    用于跨进程隔离（DP-8场景）
    不用于同进程内用户↔libc边界（协作型关系，见1.7）
    当前仅支持来源1（SecureIR静态声明）

  [future work] 动态bounds扩展：
    来源2：运行时硬件推导
      → 程序运行中通过SECURE_PAGE_LOAD注册新数据页时，
        硬件根据PVT中的owner和执行窗关联关系自动更新bounds
      → 适应动态内存（heap），需SPE bounds表可运行时更新
    来源3：运行时用户显式声明
      → 新增 SPE_UPDATE_BOUNDS 指令，允许用户代码声明新bounds范围
      → 硬件验证请求合法性（只能缩小或在自己页面范围内）
      → 最灵活但需新指令和运行时SPE状态更新
    这两个来源解决的核心问题：heap等动态分配内存的bounds覆盖

[扩展策略槽]  （待补充）
  reserved[]               // 未来新增策略的预留空间
                           // 可加入更多主流安全策略供用户选配
```

### SPE 与 Pipeline 的交互（查询类型汇总）

```
┌──────────┬─────────────────────────┬──────────────────────────┐
│ 阶段     │ Pipeline 通知 SPE 的信息 │ SPE 的动作               │
├──────────┼─────────────────────────┼──────────────────────────┤
│ Fetch    │ PC, code_policy_id      │ 加载对应策略表           │
│          │ （从 EWC 获取）          │                          │
├──────────┼─────────────────────────┼──────────────────────────┤
│ Decode   │ 直接 CALL/JMP target    │ L3: 检查target合法性     │
│          │                         │ L2/L3: 压影子栈          │
│          │ 间接 CALL/JMP           │ 标记等待 Execute         │
│          │ RET                     │ 标记等待 Execute         │
├──────────┼─────────────────────────┼──────────────────────────┤
│ Execute  │ 间接 CALL/JMP 的目标    │ L3: 检查target合法性     │
│          │ RET 的目标              │ L2/L3: 影子栈验证        │
├──────────┼─────────────────────────┼──────────────────────────┤
│ Memory   │ 访问地址 + 类型 + 大小  │ Bounds检查               │
│          │                         │ Permissions检查          │
│          │                         │ （独立于CFI层级）        │
└──────────┴─────────────────────────┴──────────────────────────┘

层级 1：SPE 不响应控制流通知，响应Memory通知
层级 2：SPE 只响应 CALL/RET（影子栈操作）+ Memory
层级 3：SPE 响应所有控制流转移指令 + Memory
```

### 策略分类

```
1. 资源策略：与 user_id 绑定（谁能访问什么）
   - 绑定到运行时 user_id（由执行窗 owner 决定）
   - 例：USER_BOUND(my_secret, owner=Alice)
   - 含 bounds 检查和 permissions 检查

2. 代码策略：与代码段绑定（代码内部的控制流）
   - 绑定到 code_policy_id（由执行窗元数据决定）
   - 例：CFI规则、边界检查
   - 用户可配置粒度（层级 1/2/3）
```

## 3.4 PVT验证逻辑（v3 更新）

### 总体描述

PVT（Physical page Verification Table）是片上反向映射表，类似SGX的EPCM和SEV-SNP的RMP。用于验证OS的PA管理行为，确保物理页面归属和映射的正确性。

### [已确定] PVT 条目结构（v3 完整版）

```
PVT 条目（per 物理页）：
  ┌──────────────────────────────────────────────────────────────┐
  │ PA_page_id          // 物理页号（隐含在表索引中）              │
  │ owner_user_id       // 归属用户（16 bits）                    │
  │ expected_VA         // 该 PA 应被映射到的 VA（防alias）        │
  │ permissions         // RX / RW / RO                           │
  │ page_type           // CODE / DATA / STACK / HEAP /           │
  │                     //   SHARED_PARAM                         │
  │ must_map            // 是否禁止 OS unmap（防侧信道）（1 bit） │
  │ shared_with_kernel  // S2共享参数区标记（1 bit）               │
  │ state               // FREE / ALLOCATED / LOCKED              │
  └──────────────────────────────────────────────────────────────┘

存储位置：片上 SRAM
参考信息来源：从 EWC 执行窗表读取（方式 X）
来源链：SecureIR → Gateway解析 → SECURE_PAGE_LOAD写入

双重校验（v3 新增）：
  page_type 与 permissions 一致性检查
    CODE  → 必须 RX
    DATA  → RW 或 RO
    STACK → 必须 RW
    HEAP  → 必须 RW
    SHARED_PARAM → 必须 RW + shared_with_kernel=1
  不一致 → SECURE_PAGE_LOAD 拒绝 + 安全异常

shared_with_kernel 语义（v3 新增）：
  独立 bit，不混入 permissions 字段
  语义正交：permissions描述"how"，shared_with_kernel描述"who"
  检查逻辑：
    访问者是owner → 查permissions → 允许/拒绝
    访问者是KERNEL且shared_with_kernel=1 → 允许
    访问者是KERNEL且shared_with_kernel=0 → 拒绝
```

### 运行时检查

```
每次 TLB miss：
  → 硬件走 OS 页表得到 VA→PA 映射
  → 硬件查 PVT：
    (a) PA 的 owner == 当前 user_id？
    (b) PA 对应的 expected_VA == OS 页表给出的 VA？
    (c) 权限是否一致？
    (d) must_map 标志位验证
  → 全部通过 → 填入 TLB
  → 任一不通过 → 安全异常
```

### 写保护机制

```
OS 永远不直接写安全页面的物理内存。

内存写入路径检查：
  用户代码写自己的页 → PVT owner 匹配 → 允许
  Cache eviction 写回 → cache line user_id tag 匹配 PVT owner → 允许
  OS 写安全页 → PVT owner 不匹配 → 拒绝
  DMA 写安全页 → PVT owner 不匹配 → 拒绝
```

## 3.5 Version 表

```
独立片上表，与 PVT 分离。

条目：page_id + version
用途：安全页面换入时的 freshness 验证
生命周期：
  SECURE_PAGE_EVICT 时写入（记录当前 version）
  SECURE_PAGE_LOAD 换入时比对（version 匹配才允许）
  SECURE_PAGE_RELEASE 时清除
```

## 3.6 审计系统

### 事件分类与记录

```
关键事件（必须记录）：
  - Gateway加载成功/失败
  - SPE策略违规（含违规类型、PC、user_id、目标地址）
  - EWC非法PC检测
  - PVT检查失败
  - SECURE_COPY 操作
  - SECURE_CONTEXT_SWITCH 操作（v3新增）
  - context_handle 分配/释放（v3新增）
  - 用户身份验证
  - 配置变更

普通事件（可采样/限流）：
  - 常规系统调用
  - 非安全关键的内存访问
  - 性能监控事件
```

### DoS防御

```
措施组合：Per-user配额 + 事件采样 + 区分触发源
```

---

# 4. 密钥管理

## 4.1 密钥层次

```
层级1（HRK）：硬件根密钥
  - 硬件内置，不可导出
  - 用于派生其他密钥

层级2（UMK）：用户主密钥
  - 用户生成对称密钥K
  - 用硬件公钥加密后传递给Gateway
  - Gateway解密获得K
  - 在该用户运行期间缓存在片上

层级3（WK）：执行窗密钥（可选）
  - 基础版不需要，用UMK即可
```

## 4.2 密钥传递总览

### 用户→硬件密钥传递

```
PhD阶段简化：假设用户已获得可信的hw_pk
方案：用户生成密钥K，用hw_pk加密后放在SecureIR中，Gateway用hw_sk解密获得K
```

### [已确定] Gateway→EWC密钥传递（v3 新增）

```
方案A：直接传递明文密钥

  物理通路：片上专用互连（Gateway ↔ EWC/密钥存储）
  安全依据：片上硬件全部可信（威胁模型1.1）
  传递内容：用户密钥K → 直接写入密钥存储

  [备选] 方案B（密钥ID+KSU）：适用于未来引入片外Gateway
  [备选] 方案C（加密传递）：适用于未来引入共享互连
```

## 4.3 认证加密

```
[已确定] 采用认证加密（如 AES-GCM）

关键约束：
  - 解密和MAC验证并行进行
  - 明文只有在MAC验证通过后才允许被提交到 cache/pipeline
  - EWC fetch路径：解密结果在MAC验证前不被Decode消费
  - 数据访问路径：解密数据在MAC验证前不被Execute使用
```

---

# 5. 编译链与SecureIR

## 5.1 [已确定] SecureIR完整格式定义（v3 新增）

### 用户SecureIR结构

```
User SecureIR Structure
═══════════════════════════════════════════════════

[身份与签名]
  user_pubkey                      // 用户公钥，user_id = hash(pubkey)
  signature                        // 对整个SecureIR的签名

[密钥交换]
  encrypted_K_code                 // 用 hw_pk 加密的用户代码密钥

[代码段描述]  （可有多个段）
  code_segments[]:
    offset                         // 相对起始的偏移
    size                           // 段大小
    type                           // USER_CODE | TRUSTED_LIB_REF
    permissions                    // RX（代码段）
    encrypted_content              // 加密后的代码（库引用时为空）
    mac                            // 认证标签

[数据段描述]  （可有多个段）
  data_segments[]:
    offset                         // 相对起始的偏移
    size                           // 段大小
    permissions                    // RW | RO
    encrypted_content              // 加密后的初始化数据（也需加密）
    mac                            // 认证标签

[信任声明]
  trusted_libs[]:
    signer_constraint              // 如 signed_by=OS_vendor
    [待细化] 功能标签/匹配方式     // 用于Gateway匹配库预注册表

[SPE策略声明]
  cfi_level                        // 1 | 2 | 3
  call_targets[]                   // 层级3：合法CALL目标列表
  jmp_targets[]                    // 层级3：合法JMP目标列表
  bounds_policy[]:                 // 来源1：静态声明
    segment_ref                    // 关联的代码段
    accessible_ranges[]            // 该代码段可访问的数据范围
  additional_policies[]            // [待补充] 后续加入更多安全策略

[资源约束]
  max_heap_size                    // 最大堆大小
  max_stack_size                   // 最大栈大小
  max_execution_windows            // 请求的最大执行窗数

[布局约束]
  alignment                        // 对齐要求
  relative_layout[]                // 段间相对位置约束
```

### 库SecureIR结构

```
Library SecureIR Structure
═══════════════════════════════════════════════════

[签名者信息]
  signer_pubkey                    // OS_vendor公钥
  signature                        // OS_vendor签名

[代码段描述]
  code_segments[]:
    offset, size, permissions(RX)
    encrypted_content, mac

[数据段描述]
  data_segments[]:
    offset, size, permissions
    encrypted_content, mac

[代码策略]
  cfi_targets[]                    // 库自身的CFI目标
  resource_binding: CALLER         // 资源归属：继承调用者

[布局约束]
  alignment, relative_layout[]

注意：
  库SecureIR不含user_pubkey（库不属于特定用户）
  resource_binding=CALLER表明库访问的资源按调用者权限检查
  Gateway在模型2下可预注册并缓存解析结果
```

## 5.2 编译流程

[已确定] 位置无关编译 + 地址无关加密（PIE/PIC）

## 5.3 信任层次与分级安全

```
层级1：普通用户 — 编译器自动提供信任元数据，SPE层级1
层级2：安全敏感用户 — 手动指定信任，SPE层级2
层级3：高安全用户 — 自己编译所有依赖，SPE层级3
```

---

# 6. 地址空间管理

## 6.1 地址空间层次

```
                用户/编译器
                     │
                     ▼
          ┌─────────────────────┐
          │  SecureIR + 加密代码 │  ← 相对偏移、约束条件
          └─────────────────────┘
                     │
                     ▼
          ┌─────────────────────┐
          │      Gateway        │  ← 验证约束、配置执行窗
          └─────────────────────┘
                     │
                     ▼
          ┌─────────────────────┐
          │   执行窗控制器(EWC)  │  ← 基于绝对VA的执行窗
          │   PVT验证逻辑参考    │
          └─────────────────────┘
                     │
                     ▼
          ┌─────────────────────┐
          │   PVT + 页表/MMU    │  ← OS建VA→PA映射，PVT验证
          └─────────────────────┘
                     │
                     ▼
          ┌─────────────────────┐
          │      物理内存       │  ← 加密代码/数据存储
          └─────────────────────┘
```

## 6.2 [已确定] VA层面

```
地址空间模型：Per-Process VA
职责划分：
  用户/编译器：SecureIR中指定约束（大小、对齐、相对偏移）
  OS：决定进程内的 base address
  Gateway：验证 base 是否满足约束，配置执行窗（base+偏移）
```

## 6.3 [已确定] PA层面

```
PA分配：OS负责
PA写入：OS不可直接写安全页面，通过 SECURE_PAGE_LOAD 指令
PA验证：PVT反向检查（owner, expected_VA, permissions, page_type）
PA回收：通过 SECURE_PAGE_RELEASE 指令
```

## 6.4 [已确定] 页表保护

```
方案 B-2：OS建页表 + PVT反向检查
  - OS继续管理页表
  - 每次 TLB fill 时硬件同时检查 PVT
  - must_map 标志位防止 OS 选择性 unmap（防page-fault侧信道）
```

## 6.5 统一安全页面协议

```
三条专用硬件指令：

SECURE_PAGE_LOAD (参数结构体指针)
  → 硬件验证 MAC
  → 读 EWC 确认 VA/owner/perm/page_type
  → page_type 与 permissions 双重校验（v3新增）
  → 原子写入 PA + 注册 PVT
  → 用于：首次加载 和 换入（换入时额外验证 version）

SECURE_PAGE_EVICT (参数结构体指针)
  → 硬件读明文（片上完成）
  → 加密 + MAC + 记录 version 到独立片上表
  → 返回 (密文, MAC) 给 OS
  → 清除 PA 内容 + 释放 PVT 条目
  → 用于：换出

SECURE_PAGE_RELEASE (PA)
  → 硬件清除 PA 内容
  → 清除 PVT 条目 + version 记录
  → 用于：程序退出时释放

安全保证：
  OS 全程不直接接触安全页面的物理内存
  消除 TOCTOU（验证和写入是同一条硬件指令内的原子操作）
  消除运行时重放（OS 无法写安全页）
  支持安全换出换入（硬件辅助协议 + version freshness）
```

---

# 7. 运行时机制

## 7.1 [已确定] user_id 传递策略

```
核心机制：并行方案

  - user_id 由 PC 所在执行窗的 owner 决定
  - 没有全局 current_user_id 状态
  - 不需要显式 user_id 切换
  - PC 变化自动带来 user_id 变化

共享库处理（选项 B）：
  - 每个用户有自己的共享库物理拷贝
  - 共享库执行窗 owner = 调用者
  - 保留独立信任链（Gateway分别验证签名）
  - 不需要 caller_user_id 追踪机制
  - [需论文阐述] 安全性分析见 1.7 节

[备选] 选项 A：共享库独立 shared 执行窗 + caller_user_id
  适用场景：未来如果需要物理页共享以节省内存
  需要：PVT shared 页面支持、caller_user_id 寄存器/影子栈、返回验证机制
```

## 7.2 资源分配与配额

```
硬件强制：
  每个user_id有硬件限制：
    - 最多N个SPE策略槽位
    - 最多M个执行窗
    - 影子栈深度D
    - 最多C个context_handle（上下文槽数）（v3新增）

Gateway检查：
  加载时检查用户SecureIR请求的资源是否超出配额
  context_handle分配时检查槽位是否可用
```

## 7.3 [已确定] 上下文切换

### 安全上下文内容

```
┌─────────────────────────────────────────────────────────────────────┐
│ 安全上下文（片上SRAM，OS不可访问）                                   │
│                                                                      │
│ ewc_state:          当前EWC执行窗配置快照                           │
│ shadow_stack[]:     影子栈内容（如果启用）                           │
│ shadow_ptr:         影子栈指针                                       │
│ spe_config[]:       SPE配置表快照                                    │
│ previous_user_id:   syscall发起者的user_id                          │
│ context_handle:     当前上下文句柄（v3新增）                         │
│                                                                      │
│ 普通上下文（内核保存）                                               │
│ GPR, PC, SP, 状态寄存器, 浮点寄存器                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### [已确定] 中断处理（方式Q）

```
中断发生时的硬件原子操作：
  (a) 暂停当前执行
  (b) 保存当前安全上下文到片上SRAM（按context_handle索引）
      （含 previous_user_id）
  (c) 清除 previous_user_id = INVALID
  (d) 加载内核EWC配置
  (e) PC → ISR入口，user_id = KERNEL

中断返回时的硬件原子操作：
  (a) 恢复之前保存的EWC配置
  (b) 恢复安全上下文（含 previous_user_id）
  (c) PC恢复到被中断位置
  (d) user_id由恢复后的执行窗决定

安全保证：
  用户态和内核态执行窗物理隔离（不同时存在于EWC）
  中断期间 previous_user_id = INVALID → ISR无法越权
```

### [已确定] Syscall 流程

```
SYSCALL 指令的硬件原子操作：
  (a) 保存用户安全上下文到片上SRAM
  (b) 设置 previous_user_id = 当前 user_id
  (c) EWC切换：用户窗口 → 内核窗口
  (d) PC → syscall入口，user_id = KERNEL

SYSRET 指令的硬件原子操作：
  (a) 清除 previous_user_id = INVALID
  (b) EWC切换：内核窗口 → 用户窗口
  (c) 恢复用户安全上下文
  (d) PC恢复，user_id恢复
```

### [已确定] 内核访问用户数据

```
S2（共享参数区）：
  - 每个用户有预定义的参数传递区
  - PVT标记 shared_with_kernel = true，page_type = SHARED_PARAM
  - 用于小参数传递

S3（SECURE_COPY）：
  格式：SECURE_COPY (参数结构体指针)
  硬件自动从 PVT 获取 owner 和密钥（方式II）
  访问控制：src/dst owner 必须是 KERNEL 或 previous_user_id
  单次最大 4KB，大缓冲区内核循环调用
  原子性：每次 4KB 内先验证后写入（双缓冲方案）
  双向支持：
    read: 内核PA → 用户PA（硬件解密内核数据 → 用用户密钥重加密）
    write: 用户PA → 内核PA（硬件解密用户数据 → 用内核密钥重加密）
```

### previous_user_id 寄存器

```
片上硬件寄存器，OS不可访问，不涉及加密

生命周期：
  正常用户态：INVALID
  SYSCALL时：设置为发起者user_id
  SYSRET时：清除为INVALID
  中断时：作为安全上下文一部分保存，清零为INVALID
  中断返回：恢复

嵌套场景：
  层0: previous_user_id = Alice（Alice的syscall）
  层1: previous_user_id = INVALID（中断1，已保存层0）
  层2: previous_user_id = INVALID（中断2，已保存层1）
  返回层1: 恢复INVALID
  返回层0: 恢复Alice
```

### [已确定] 同用户不同进程切换（v3 新增 — DP-8）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  同用户不同进程切换（方式α）                                                 │
│                                                                              │
│  机制：EWC per-process 重配置                                               │
│    - 同一user_id的不同进程有各自独立的EWC/SPE/安全上下文                    │
│    - 进程切换时通过 SECURE_CONTEXT_SWITCH 保存/恢复                         │
│    - 复用方式Q的基础设施（保存/恢复到片上SRAM）                             │
│                                                                              │
│  隔离保证：                                                                  │
│    P1运行时EWC中只有P1的执行窗                                              │
│    P2的执行窗不存在于EWC → P1无法跳转到P2代码                              │
│    SPE bounds进一步约束数据访问范围                                          │
│                                                                              │
│  与方式Q的区别：                                                             │
│    方式Q（user↔kernel）：特权级变化 + 安全上下文切换                        │
│    方式α（P1↔P2）：无特权级变化，仅安全上下文切换                          │
│    方式α通过 SECURE_CONTEXT_SWITCH 触发，方式Q通过 SYSCALL/SYSRET 触发     │
│                                                                              │
│  典型调度流程：                                                              │
│    P1(Alice)运行 → 中断 → SYSCALL语义自动触发                              │
│    → 内核态ISR → 内核决定切换到P2(Alice)                                   │
│    → SECURE_CONTEXT_SWITCH(handle_P2)                                       │
│    → SYSRET → P2(Alice)运行                                                │
│                                                                              │
│  [future work] 同用户进程间共享内存                                         │
│    涉及一致性问题（多进程同时访问共享数据页）                               │
│    当前不支持，每个进程数据完全隔离                                          │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Cache处理

```
推荐：根据安全需求选择
  普通场景：cache line保留user_id tag，不匹配则cache miss
  高安全场景：刷新cache或cache分区
```

---

# 8. 运行时软件支持

## 8.1 [已确定] 共享库处理（选项B）

```
每个用户有自己的共享库物理拷贝：
  Alice加载libc → Gateway验证libc的SecureIR（OS_vendor签名）
                → 配置执行窗：owner=Alice, type=TRUSTED_LIB
                → PVT: libc页面 owner=Alice
  Bob加载libc  → 同样流程，独立物理拷贝
                → PVT: libc页面 owner=Bob

信任链：
  用户SecureIR: trust libc@signed_by=OS_vendor
  Gateway: 分别验证用户签名和libc签名（模型2下库可预注册）
  运行时: libc执行窗owner=Alice → user_id=Alice（并行方案）
  数据访问: PVT owner=Alice → Alice密钥加解密

[需论文阐述] 选项B的安全性权衡：
  优势：PVT单owner兼容、硬件简化、无caller_user_id
  代价：用户代码↔libc无PVT级别隔离（见1.7节分析）
  定位：安全性 ≥ 传统进程内模型 + 硬件CFI增强
```

## 8.2 内核角色与受限功能

```
OS可以做：
  - 调度进程
  - 管理物理资源（PA分配）
  - 请求Gateway加载程序（GATEWAY_LOAD）
  - 通过 SECURE_PAGE_LOAD 注册安全页面
  - 通过 SECURE_PAGE_EVICT 换出安全页面
  - 通过 SECURE_COPY 在syscall中传递数据
  - 通过 SECURE_CONTEXT_SWITCH 切换进程上下文（v3新增）
  - 转发I/O请求
  - 管理 context_handle → 进程 的映射（v3新增）

OS不能做：
  - 直接写安全页面物理内存
  - 修改Gateway/SPE/EWC/PVT配置（硬件保护）
  - 读取解密后的代码/数据（加密保护）
  - 伪造用户签名（密码学保护）
  - 绕过SPE/PVT检查（硬件强制）
  - 通过SECURE_COPY访问第三方用户数据（previous_user_id约束）
  - 通过SECURE_CONTEXT_SWITCH做超出调度能力的事（v3新增）
```

## 8.3 [future work] Security Monitor

```
可能的职责：监控调度公平性、检测DoS行为
与"零软件TCB"目标的冲突：引入SM意味着引入软件TCB
当前状态：future work
```

---

# 9. 模块交互

## 9.1 Gateway↔CPU

```
CPU → Gateway：
  ✓ 加载程序请求（GATEWAY_LOAD，同步阻塞）
  ✓ 释放资源（GATEWAY_RELEASE）

Gateway → CPU：
  ✓ EWC执行窗配置
  ✓ SPE策略配置
  ✓ user_id/密钥传递
  ✓ context_handle 返回（v3新增）
  ✓ 解析结果/状态码
```

## 9.2 Gateway↔EWC

```
Gateway → EWC：
  执行窗表配置：(窗口ID, 地址范围, owner_user_id, key_id, type, code_policy_id)
  密钥传递：[已确定] 方案A，片上专用互连直接传递明文密钥
```

## 9.3 EWC↔Pipeline

```
每个 Fetch 周期：
  Pipeline → EWC：当前PC
  EWC → Pipeline：合法/非法 + key_id + 窗口元数据

  合法 → 解密 → Decode
  非法 → Trap
```

## 9.4 EWC↔PVT验证逻辑

```
[已确定] 方式X：PVT从EWC读取

PVT验证逻辑 → EWC：请求执行窗信息（VA范围、owner、permissions）
EWC → PVT验证逻辑：返回执行窗表数据

用途：
  PVT验证逻辑在处理 SECURE_PAGE_LOAD 时，
  读EWC表确认OS注册的PA信息与Gateway配置一致
```

## 9.5 Pipeline↔SPE

```
[已确定] SPE被pipeline各阶段喂信息（详见3.3节SPE交互全景）

Fetch  → SPE：PC, code_policy_id（经由EWC）
Decode → SPE：指令类型, 直接跳转目标
Execute → SPE：间接跳转/RET的实际目标
Memory → SPE：内存访问地址、类型和大小

SPE根据配置的CFI层级选择性响应
SPE bounds检查独立于CFI层级，所有层级都响应Memory通知
```

## 9.6 [已确定] SECURE_COPY 数据路径

```
SECURE_COPY (参数结构体指针)

硬件内部（逐cache line）：
  (a) 读src密文
  (b) 查PVT: src.owner → 获取src密钥
  (c) 解密 + 验证MAC
  (d) 明文在片上，软件不可见
  (e) 查PVT: dst.owner → 获取dst密钥
  (f) 用dst密钥加密 + 计算MAC
  (g) 写入dst PA
  (h) 更新dst的version（freshness）

访问控制：
  src.owner 和 dst.owner 都必须是 KERNEL 或 previous_user_id
```

---

# 10. 流图（v3）

## 10.1 程序加载流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  程序加载流程 v3（含context_handle）                                          │
│                                                                              │
│  用户     OS        Gateway     EWC     PVT验证逻辑  片上SRAM   SPE         │
│   │        │           │         │          │          │         │           │
│   │ 1.提交 │           │         │          │          │         │           │
│   │ 程序   │           │         │          │          │         │           │
│   │───────>│           │         │          │          │         │           │
│   │        │           │         │          │          │         │           │
│   │        │ 2.从磁盘  │         │          │          │         │           │
│   │        │ 读取密文  │         │          │          │         │           │
│   │        │ 到OS缓冲区│         │          │          │         │           │
│   │        │           │         │          │          │         │           │
│   │        │ 3.GATEWAY │         │          │          │         │           │
│   │        │ _LOAD     │         │          │          │         │           │
│   │        │ (同步阻塞)│         │          │          │         │           │
│   │        │──────────>│         │          │          │         │           │
│   │        │           │         │          │          │         │           │
│   │        │           │ 4.验证  │          │          │         │           │
│   │        │           │ 签名,   │          │          │         │           │
│   │        │           │ 解析    │          │          │         │           │
│   │        │           │         │          │          │         │           │
│   │        │           │ 5.分配  │          │          │         │           │
│   │        │           │ context │          │          │         │           │
│   │        │           │ _handle │          │          │         │           │
│   │        │           │         │          │──(分配槽)>│         │           │
│   │        │           │         │          │          │         │           │
│   │        │           │ 6.配置  │          │          │         │           │
│   │        │           │ 执行窗  │          │          │         │           │
│   │        │           │────────>│          │          │         │           │
│   │        │           │         │──(写入)─────────────>         │           │
│   │        │           │         │          │          │         │           │
│   │        │           │ 7.配置SPE策略 + 传递密钥      │         │           │
│   │        │           │────────────────────────────────────────>│           │
│   │        │           │         │          │          │         │           │
│   │        │ 8.返回    │         │          │          │         │           │
│   │        │ context   │         │          │          │         │           │
│   │        │ _handle   │         │          │          │         │           │
│   │        │<──────────│         │          │          │         │           │
│   │        │           │         │          │          │         │           │
│   │        │ 9.逐页：  │         │          │          │         │           │
│   │        │ SECURE_   │         │          │          │         │           │
│   │        │ PAGE_LOAD │         │          │          │         │           │
│   │        │ (PA,密文, │         │          │          │         │           │
│   │        │  MAC)     │         │          │          │         │           │
│   │        │─────────────────────────────>│          │         │           │
│   │        │           │         │          │          │         │           │
│   │        │           │         │  10.读EWC│          │         │           │
│   │        │           │         │<─────────│          │         │           │
│   │        │           │         │─────────>│          │         │           │
│   │        │           │         │          │          │         │           │
│   │        │           │         │          │ 11.验证  │         │           │
│   │        │           │         │          │ MAC+VA+  │         │           │
│   │        │           │         │          │ owner+   │         │           │
│   │        │           │         │          │ perm+    │         │           │
│   │        │           │         │          │ page_type│         │           │
│   │        │           │         │          │          │         │           │
│   │        │           │         │          │ 12.原子: │         │           │
│   │        │           │         │          │ 写PA +   │         │           │
│   │        │           │         │          │ 注册PVT  │         │           │
│   │        │           │         │          │─────────>│         │           │
│   │        │           │         │          │          │         │           │
│   │        │ 13.建页表 │         │          │          │         │           │
│   │        │ 映射      │         │          │          │         │           │
│   │        │           │         │          │          │         │           │
│   │ 14.加载│           │         │          │          │         │           │
│   │ 完成   │           │         │          │          │         │           │
│   │<───────│           │         │          │          │         │           │
│                                                                              │
│  Gateway只参与步骤3-8（加载时配置者，同步阻塞）                             │
│  PVT验证逻辑独立完成步骤9-12（含page_type双重校验）                        │
│  OS全程不直接写安全页面物理内存                                              │
│  OS获得context_handle用于后续进程管理                                       │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 10.2 运行时安全检查流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  运行时安全检查流程 v3                                                       │
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────┐     │
│  │                         CPU Pipeline                                │     │
│  │                                                                     │     │
│  │   ┌───────┐    ┌────────┐    ┌─────────┐    ┌────────┐            │     │
│  │   │ Fetch │───>│ Decode │───>│ Execute │───>│ Memory │            │     │
│  │   └───┬───┘    └───┬────┘    └────┬────┘    └───┬────┘            │     │
│  │       │             │              │              │                 │     │
│  │       │ PC          │ 指令类型     │ 间接目标     │ 访问地址       │     │
│  │       ▼             │ 直接目标     │ RET目标      │ 访问类型       │     │
│  │   ┌────────┐        │              │              │ 数据大小       │     │
│  │   │  EWC   │        ▼              ▼              ▼                 │     │
│  │   │ PC合法?│    ┌──────────────────────────────────────┐           │     │
│  │   │ →key_id│    │              SPE                      │           │     │
│  │   │ →owner │    │                                       │           │     │
│  │   │ →policy│    │  Decode阶段：                         │           │     │
│  │   │  _id   │    │    L3: 检查直接CALL/JMP目标合法性     │           │     │
│  │   └───┬────┘    │    L2/L3: CALL时压影子栈              │           │     │
│  │       │         │                                       │           │     │
│  │    合法│非法     │  Execute阶段：                        │           │     │
│  │       │  ↓      │    L3: 检查间接CALL/JMP目标合法性     │           │     │
│  │       │ Trap    │    L2/L3: RET时验证影子栈             │           │     │
│  │       ▼         │                                       │           │     │
│  │   ┌────────┐    │  Memory阶段：                         │           │     │
│  │   │ 认证   │    │    Bounds检查（所有层级）             │           │     │
│  │   │ 解密   │    │    Permissions检查（所有层级）        │           │     │
│  │   │ (AES-  │    │                                       │           │     │
│  │   │  GCM)  │    │  不合法 → Trap + 清除pipeline         │           │     │
│  │   │ MAC验证│    │         + 审计事件                    │           │     │
│  │   │ 后提交 │    └──────────────────────────────────────┘           │     │
│  │   └────────┘                                                       │     │
│  │                                                                     │     │
│  └────────────────────────────────────────────────────────────────────┘     │
│                                                                              │
│  数据访问路径：                                                              │
│    Load/Store地址 → PVT检查(TLB miss时) → owner匹配 → 允许               │
│                                          → 不匹配   → 安全异常             │
│                   → SPE bounds检查(并行) → 范围内 → 允许                    │
│                                          → 范围外 → Trap                    │
│                                                                              │
│  user_id确定方式：PC所在执行窗的owner（并行方案，无需切换）                  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 10.3 上下文切换流程（中断）

```
（同v2，此处省略，见system_design_v2.md 10.3节）
```

## 10.4 Syscall 流程

```
（同v2，此处省略，见system_design_v2.md 10.4节）
```

## 10.5 安全页面换出/换入流程

```
（同v2，此处省略，见system_design_v2.md 10.5节）
```

## 10.6 同用户进程切换流程（v3 新增）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  同用户进程切换流程（方式α）                                                 │
│                                                                              │
│  P1(Alice)    OS/内核     硬件安全逻辑    片上SRAM                          │
│     │           │              │              │                              │
│     │ 正常执行  │              │              │                              │
│     │ user_id   │              │              │                              │
│     │ =Alice    │              │              │                              │
│     │ handle    │              │              │                              │
│     │ =H1       │              │              │                              │
│     │           │              │              │                              │
│     │ ←中断触发─────────────>│              │                              │
│     │           │              │              │                              │
│     │           │    SYSCALL语义自动触发：    │                              │
│     │           │    保存P1安全上下文────────────────>│ 槽H1              │
│     │           │    previous_uid=Alice      │        │                    │
│     │           │    EWC切换→内核             │        │                    │
│     │           │              │              │                              │
│     │           │ ISR执行      │              │                              │
│     │           │ 决定切换到P2 │              │                              │
│     │           │              │              │                              │
│     │           │ SECURE_      │              │                              │
│     │           │ CONTEXT_     │              │                              │
│     │           │ SWITCH(H2)   │              │                              │
│     │           │─────────────>│              │                              │
│     │           │              │              │                              │
│     │           │    硬件操作：│              │                              │
│     │           │    (a) 保存内核上下文       │                              │
│     │           │    (b) 加载P2安全上下文<────────────│ 槽H2              │
│     │           │        (EWC/SPE/影子栈)    │        │                    │
│     │           │              │              │                              │
│     │           │ SYSRET       │              │                              │
│     │           │─────────────>│              │                              │
│     │           │              │              │                              │
│     │           │    previous_uid=INVALID     │                              │
│     │           │    EWC切换→P2(Alice)        │                              │
│     │           │    恢复P2安全上下文         │                              │
│     │           │              │              │                              │
│               P2(Alice) 运行                                                │
│               user_id=Alice, handle=H2                                      │
│                                                                              │
│  安全保证：                                                                  │
│    - P1和P2的EWC/SPE/影子栈完全隔离                                        │
│    - 切换期间OS无法访问安全上下文（片上SRAM）                               │
│    - P2无法访问P1的数据段（EWC中无P1执行窗 + SPE bounds）                  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

# 11. 框图

## SoC架构总览（v3）

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              SoC 架构 v3                                     │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                           CPU Core                                   │    │
│  │                                                                      │    │
│  │  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐          │    │
│  │  │  Fetch  │ →  │ Decode  │ →  │ Execute │ →  │ Memory  │          │    │
│  │  └────┬────┘    └────┬────┘    └────┬────┘    └────┬────┘          │    │
│  │       │              │              │              │                │    │
│  │       │ PC           │ 指令类型     │ 间接目标     │ 访问地址      │    │
│  │       │              │ 直接目标     │ RET目标      │ 大小/类型     │    │
│  │       ▼              ▼              ▼              ▼                │    │
│  │  ┌────────┐    ┌─────────────────────────────────────────┐         │    │
│  │  │  EWC   │    │                  SPE                     │         │    │
│  │  │(门禁)  │    │          (可配置安全策略引擎)             │         │    │
│  │  │        │    │                                          │         │    │
│  │  │PC→窗口 │    │  CFI检查(L1/L2/L3) + Bounds检查          │         │    │
│  │  │→key_id │    │  影子栈(L2/L3)   + Permissions检查       │         │    │
│  │  │→owner  │    │  [待补充] 扩展策略                        │         │    │
│  │  │→policy │    └─────────────────────────────────────────┘         │    │
│  │  └────────┘          ▲                                             │    │
│  │       ▲              │                                             │    │
│  │       │              │                                             │    │
│  │       │              │  配置接口                                    │    │
│  │       │              │                                             │    │
│  └───────┼──────────────┼─────────────────────────────────────────────┘    │
│          │              │                                                   │
│          │ 配置命令     │  片上专用互连（密钥直接传递）                     │
│          │              │                                                   │
│  ┌───────▼──────────────▼─────────────────────────────────────────────┐    │
│  │                      Gateway（独立协处理器）                         │    │
│  │                                                                      │    │
│  │  输入：SecureIR（从内存加载）                                        │    │
│  │  输出：配置命令 → EWC / SPE + context_handle                       │    │
│  │                                                                      │    │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐        │    │
│  │  │ 签名验证  │→ │ SecureIR  │→ │ 配置生成  │→ │ 配置写入  │        │    │
│  │  │ (Ed25519) │  │ 解析      │  │ +句柄分配 │  │           │        │    │
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘        │    │
│  │                                                                      │    │
│  │  ┌──────────────────┐                                               │    │
│  │  │ 库预注册缓存表   │  [方向已定，细节待细化]                        │    │
│  │  └──────────────────┘                                               │    │
│  └──────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                    PVT验证逻辑 + 安全页面写入引擎                     │   │
│  │                                                                       │   │
│  │  ┌───────────────┐   ┌──────────────────┐   ┌─────────────────┐     │   │
│  │  │ SECURE_PAGE_  │   │  PVT反向检查     │   │  SECURE_COPY    │     │   │
│  │  │ LOAD/EVICT/   │   │  (TLB fill时)    │   │  (syscall数据   │     │   │
│  │  │ RELEASE       │   │  +page_type校验  │   │   传递)          │     │   │
│  │  └───────────────┘   └──────────────────┘   └─────────────────┘     │   │
│  │         ↑ 读EWC                                     ↑ 读PVT         │   │
│  └─────────┼───────────────────────────────────────────┼────────────────┘   │
│            │                                           │                    │
│  ┌─────────▼───────────────────────────────────────────▼────────────────┐   │
│  │                          片上安全存储（SRAM）                          │   │
│  │                                                                       │   │
│  │  ┌────────────┐  ┌────────┐  ┌──────────┐  ┌────────┐  ┌─────────┐ │   │
│  │  │ EWC执行窗表│  │  PVT   │  │ Version表│  │ 密钥   │  │安全上下 │ │   │
│  │  │            │  │(+page  │  │  (独立)  │  │ 存储   │  │文存储   │ │   │
│  │  │            │  │ _type) │  │          │  │        │  │(多槽,按 │ │   │
│  │  │            │  │        │  │          │  │        │  │handle   │ │   │
│  │  │            │  │        │  │          │  │        │  │索引)    │ │   │
│  │  └────────────┘  └────────┘  └──────────┘  └────────┘  └─────────┘ │   │
│  │                                                                       │   │
│  │  ┌──────────────────────┐  ┌──────────────────────┐                  │   │
│  │  │ previous_user_id 寄存器│  │ 上下文句柄表           │                  │   │
│  │  └──────────────────────┘  │ (handle→槽映射)       │                  │   │
│  │                             └──────────────────────┘                  │   │
│  └───────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                          审计模块                                      │  │
│  │  - 事件记录（含PVT违规、SECURE_COPY操作、上下文切换）                │  │
│  │  - 哈希链                                                              │  │
│  │  - Per-user配额管理                                                    │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

# 12. 元数据流图（v3 更新）

## 12.1 user_id 流

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  user_id 的产生和传播                                                        │
│                                                                              │
│  离线阶段：                                                                  │
│    用户生成密钥对 → user_id = hash(pubkey) → 嵌入 SecureIR                  │
│                                                                              │
│  加载阶段：                                                                  │
│    SecureIR ──→ Gateway 验证 ──→ 写入 EWC 执行窗表 (owner=user_id)          │
│                                  ──→ 写入 SPE 策略表 (绑定user_id)          │
│                                  ──→ 分配 context_handle                    │
│                                                                              │
│  运行时：                                                                    │
│    PC ──→ EWC查找 ──→ 窗口.owner = user_id                                 │
│                        │                                                    │
│                        ├──→ SPE 资源策略检查 (user_id == data.owner?)       │
│                        ├──→ SPE bounds检查 (地址在范围内?)                  │
│                        ├──→ PVT 检查 (user_id == PA.owner?)                │
│                        └──→ 审计事件标记 (event.user_id)                    │
│                                                                              │
│  Syscall/中断时：                                                            │
│    SYSCALL ──→ previous_user_id = user_id ──→ SECURE_COPY 访问控制         │
│    中断    ──→ previous_user_id 保存/清零/恢复                              │
│                                                                              │
│  进程切换时（v3新增）：                                                       │
│    SECURE_CONTEXT_SWITCH(handle) ──→ 加载目标上下文                         │
│    ──→ user_id 由新上下文的EWC执行窗决定                                    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 12.2 密钥流

```
（同v2，增加Gateway→EWC方案A注释）

┌─────────────────────────────────────────────────────────────────────────────┐
│  密钥的产生和使用                                                            │
│                                                                              │
│  用户侧（离线）：                                                            │
│    用户生成 K_code（代码加密密钥）                                           │
│    用户用 K_code 加密代码                                                   │
│    用户用 hw_pk 加密 K_code → encrypted_K 放入 SecureIR                     │
│                                                                              │
│  加载阶段：                                                                  │
│    Gateway 用 hw_sk 解密 → 获得 K_code                                      │
│    Gateway 通过片上专用互连直接传递 K_code → 密钥存储（方案A）              │
│                                                                              │
│  运行时：                                                                    │
│    EWC Fetch解密：                                                           │
│      key_id ──→ 密钥存储 ──→ K_code ──→ AES-GCM解密 + MAC验证              │
│                                                                              │
│    数据加解密：                                                              │
│      内存读取 ──→ user密钥解密 + MAC验证 ──→ 明文进cache                    │
│      cache写回 ──→ user密钥加密 + MAC生成 ──→ 密文进内存                    │
│                                                                              │
│    SECURE_COPY：                                                             │
│      src密钥解密(片上) → dst密钥加密 → 写入dst PA                           │
│      密钥来源：硬件从PVT读owner → 从密钥存储取对应密钥                       │
│                                                                              │
│    SECURE_PAGE_EVICT/LOAD：                                                  │
│      换出：user密钥解密(片上) → 重新加密+MAC+version → 返回OS              │
│      换入：验证MAC+version → user密钥下写入PA                               │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 12.3 安全策略流

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  安全策略的产生和执行                                                        │
│                                                                              │
│  编译时：                                                                    │
│    用户/编译器 → SecureIR 中声明：                                           │
│      - CFI 层级（1/2/3）                                                    │
│      - 资源约束（bounds, 权限）                                             │
│      - 信任声明（trusted libraries）                                        │
│      - 如果层级3：CALL目标列表、JMP目标列表                                │
│                                                                              │
│  加载时：                                                                    │
│    SecureIR → Gateway 解析 → 配置 SPE：                                     │
│      spe_config.cfi_level = N                                               │
│      spe_config.call_targets = [...]     (层级3)                            │
│      spe_config.jmp_targets = [...]      (层级3)                            │
│      spe_config.shadow_stack = enabled   (层级2/3)                          │
│      spe_config.bounds_entries = [...]   (所有层级)                         │
│      spe_config.resource_bounds = [...]                                     │
│                                                                              │
│  运行时：                                                                    │
│    Pipeline各阶段 → 通知SPE → SPE根据配置响应：                             │
│      层级1: 忽略控制流通知                                                  │
│      层级2: 只响应CALL/RET（影子栈）                                        │
│      层级3: 响应所有控制流转移                                              │
│      所有层级: 响应Memory阶段的bounds+permissions检查                       │
│                                                                              │
│  上下文切换时（v3新增）：                                                    │
│    SPE策略表 + 影子栈 → 保存到片上SRAM（按context_handle索引）             │
│    恢复时 → 从片上SRAM加载目标进程的SPE配置                                 │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

# 附录A：设计点状态索引（v3 更新）

| 章节 | 设计点 | 状态 | 决定内容 |
|------|--------|------|---------|
| 1.2 | OS具体能力清单 | **已更新** | 含context_handle操纵分析 |
| 1.4 | 两层隔离模型 | **v3新增** | 跨用户(密码学) vs 同用户跨进程(策略) |
| 1.4 | 侧信道攻击防御 | 待后续集中讨论 | - |
| 1.7 | 用户↔libc安全分析 | **v3新增** | 维持选项B，4条攻击路径分析 |
| 2.2 | context_handle | **v3新增** | 不透明句柄，硬件管理上下文槽 |
| 3.1 | Gateway配置粒度 | **v3新增** | per-process |
| 3.1 | 库处理模型 | **v3新增** | 模型2方向（库预注册+用户绑定） |
| 3.2 | EWC职责简化 | **已确定** | 只检查PC合法性，不检查跳转目标 |
| 3.2 | 方式α | **v3新增** | 同用户进程切换时EWC重配置 |
| 3.3 | SPE交互全景 | **v3新增** | 完整交互图 |
| 3.3 | SPE策略表逻辑结构 | **v3新增** | 元数据+CFI目标表+影子栈+Bounds表 |
| 3.3 | SPE可配置CFI粒度 | **已确定** | 层级1/2/3用户选择 |
| 3.3 | Bounds定位 | **v3新增** | 跨进程隔离，非用户↔libc边界 |
| 3.4 | PVT完整格式 | **v3新增** | 含page_type + shared_with_kernel |
| 3.4 | PVT双重校验 | **v3新增** | page_type与permissions一致性 |
| 4.2 | Gateway→EWC密钥传递 | **v3确定** | 方案A，片上专用互连直接传递 |
| 5.1 | SecureIR完整格式 | **v3新增** | 字段清单确定 |
| 6.4 | 页表保护 | **已确定** | B-2 + must_map |
| 6.5 | 统一安全页面协议 | **已确定** | 三条专用指令 |
| 7.1 | user_id传递策略 | **已确定** | 并行方案 |
| 7.2 | 共享库处理 | **已确定** | 选项B（per-user拷贝） |
| 7.3 | 中断处理 | **已确定** | 方式Q（EWC物理隔离） |
| 7.3 | Syscall流程 | **已确定** | S2+S3, SECURE_COPY, previous_user_id |
| 7.3 | 同用户进程切换 | **v3确定** | 方式α + context_handle |
| 8.3 | Security Monitor | future work | - |
| 9.2 | Gateway↔EWC密钥传递 | **v3确定** | 方案A |

# 附录B：新增ISA指令汇总（v3 更新）

| 指令 | 操作数 | 用途 | 发起者 |
|------|--------|------|--------|
| GATEWAY_LOAD rd, rs1 | rs1=参数结构体指针, rd=context_handle | 提交SecureIR给Gateway（同步阻塞） | OS |
| GATEWAY_RELEASE rs1 | rs1=context_handle | 释放Gateway资源 | OS |
| SECURE_PAGE_LOAD rd, rs1 | rs1=参数结构体指针, rd=状态码 | 安全页面写入+PVT注册 | OS |
| SECURE_PAGE_EVICT rd, rs1 | rs1=参数结构体指针, rd=状态码 | 安全页面换出 | OS |
| SECURE_PAGE_RELEASE rd, rs1 | rs1=target_pa, rd=状态码 | 安全页面释放 | OS |
| SECURE_COPY rd, rs1 | rs1=参数结构体指针, rd=状态码 | syscall数据传递（≤4KB） | 内核 |
| SECURE_CONTEXT_SWITCH rs1 | rs1=目标context_handle | 进程间安全上下文切换 | 内核 |
| SYSCALL | 无额外操作数（扩展语义） | 进入内核态 | 用户代码 |
| SYSRET | 无额外操作数（扩展语义） | 返回用户态 | 内核 |

### 指令参数结构体定义

```
GATEWAY_LOAD参数：
  { secureir_addr (VA), secureir_len, base_va }

SECURE_PAGE_LOAD参数：
  { target_pa, ciphertext_addr (VA), mac, page_id }

SECURE_PAGE_EVICT参数：
  { source_pa, output_buf_addr (VA) }

SECURE_COPY参数：
  { src_pa, dst_pa, length }
```

### ISA指令与上下文切换的关系

```
SYSCALL/SYSRET：用户态↔内核态切换（特权级变化 + 安全上下文切换）
SECURE_CONTEXT_SWITCH：内核态内，进程间切换（仅安全上下文切换）

典型调度流程：
  P1(Alice)运行 → 中断 → SYSCALL语义自动触发
  → 内核态ISR → 内核决定切换到P2(Alice)
  → SECURE_CONTEXT_SWITCH(handle_P2)
  → SYSRET → P2(Alice)运行

跨用户切换：
  P1(Alice)运行 → 中断 → SYSCALL语义自动触发
  → 内核态ISR → 内核决定切换到P3(Bob)
  → SECURE_CONTEXT_SWITCH(handle_P3)
  → SYSRET → P3(Bob)运行
  （安全性由EWC/PVT/密钥保证，与同用户切换无异）
```

# 附录C：片上SRAM资源规划

| 组件 | 用途 | 估算大小 |
|------|------|---------|
| EWC执行窗表 | 存储当前进程的执行窗配置 | ~几KB |
| PVT | 每个安全物理页一条目（含page_type/shared_with_kernel） | 128MB安全内存 → ~640KB+ |
| Version表 | 换出页面的page_id+version | 取决于最大换出页数 |
| 密钥存储 | 用户密钥 + 内核密钥 | ~几KB |
| 安全上下文存储 | 多槽，按context_handle索引 | 取决于最大并发进程数×上下文大小 |
| SPE策略表 | CFI目标列表、bounds表、资源约束 | 取决于策略复杂度 |
| SECURE_COPY缓冲区 | 双缓冲（4KB原子操作） | 4KB |
| 上下文句柄表 | context_handle→槽映射 | ~几百B |
| Gateway库预注册缓存 | 已验证库的解析结果 | 取决于库数量 |

# 附录D：备选方案记录

| 方案 | 当前决定 | 备选 | 适用场景 |
|------|---------|------|---------|
| 共享库处理 | 选项B（per-user拷贝） | 选项A（shared执行窗+caller_uid） | 需要物理页共享时 |
| 内核访问用户数据 | S2+S3 | S1（临时授权表TAT） | 需要更灵活的授权时 |
| freshness | OS不可写+version表 | F-3（Merkle Tree） | 需要更强防重放时 |
| Gateway→EWC密钥传递 | 方案A（直接传递） | B(密钥ID+KSU)/C(加密传递) | 片外Gateway或共享互连时 |
| 上下文标识 | 不透明句柄(context_handle) | 显式process_id | 硬件需理解进程语义时 |
| GATEWAY_LOAD模式 | 同步阻塞 | 异步非阻塞(+会话ID+STATUS) | 需要并行加载优化时 |
| bounds来源 | 来源1（SecureIR静态声明） | 来源2(硬件推导)/来源3(运行时声明) | 需要动态内存bounds时 |

# 附录E：Future Work 详细说明（v3 新增）

## E.1 同用户进程间共享内存

```
问题：同一user_id的P1和P2需要共享部分数据页
难点：PVT中页面只有一个owner，多进程并发访问需要一致性保证
可能方向：
  - PVT增加shared标记（同user_id内共享）
  - 引入进程粒度的访问控制列表
  - 通过SECURE_COPY做进程间数据传递（类似syscall模式）
```

## E.2 动态内存Bounds（来源2和来源3）

```
问题：当前bounds仅支持SecureIR静态声明，无法覆盖heap等动态分配内存
  
来源2（运行时硬件推导）：
  思路：SECURE_PAGE_LOAD注册新数据页时，硬件根据PVT中的owner
       和执行窗关联关系自动更新SPE bounds表
  优点：对用户透明，无需新指令
  难点：SPE bounds表需要支持运行时更新，增加硬件复杂度
       自动推导规则需要精确定义

来源3（运行时用户显式声明）：
  思路：新增 SPE_UPDATE_BOUNDS 指令，用户代码运行时声明新bounds
  语义：SPE_UPDATE_BOUNDS rs1  ; rs1=参数结构体(segment_id, base, limit, perm)
  约束：硬件验证请求合法性
       - 只能在自己的执行窗对应的bounds_entry中操作
       - 只能添加自己拥有的页面（PVT owner匹配）
       - 只能缩小已有范围或在已拥有页面范围内新增
  优点：最灵活，用户完全控制
  难点：新指令、运行时SPE状态更新、安全验证逻辑
```

## E.3 Intra-process Compartmentalization

```
问题：同进程内用户代码↔libc的细粒度隔离
核心矛盾：协作型关系（libc需要访问用户数据）使完全隔离不可行

当前防御覆盖：
  EWC：阻止代码注入
  SPE L2/L3：硬件CFI防ROP/JOP
  加密：保护代码完整性
  → 安全性 ≥ 传统进程内模型

不足：
  L1下ROP利用libc gadgets无法阻止
  用户代码可读写libc内部数据（GOT等）
  libc漏洞被利用后可访问用户全部数据

相关工作方向：
  CHERI capabilities：基于capability的细粒度内存访问控制
  Intel MPK：Memory Protection Keys，用户态页面权限域
  ARM MTE：Memory Tagging Extension
  这些都是intra-process compartmentalization的不同方法
  与本系统的跨用户隔离是正交的增强维度

未来可能方向：
  在SPE中引入capability-like的细粒度数据访问控制
  允许用户代码和libc有各自的capability域
  libc通过受控接口访问用户数据（类似CHERI的capability传递）
```

## E.4 安全I/O通道

```
问题：I/O数据经过内核中转时，内核可见明文
方向：设备到用户的直接安全通道，绕过内核
```

## E.5 其他

```
- Security Monitor（监控调度公平性/DoS）
- 微架构侧信道防御（Cache/TLB侧信道）
- 多核一致性（多核下PVT/EWC/SPE的一致性维护）
- 选项A共享库物理页共享（PVT shared支持+caller_uid）
```
