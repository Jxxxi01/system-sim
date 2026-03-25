## Issue 0：scaffold（方案确认）
**日期：** 2026-03-03  
**分支：** issue-0-scaffold  
**状态：** 方案已确认

### 初始需求（用户提出）
- 在仓库根目录创建 C++17 + CMake 脚手架，支持 Debug 构建。
- 创建最小目录结构：`include/{core,isa,security}`、`src/{core,isa,security,kernel}`、`tests`、`demos/{normal,cross_user}`。
- 提供 header-only 测试框架（不使用第三方依赖）并至少有一个 sanity 测试。
- 通过 CTest 集成，`ctest` 必须通过。
- 实现后给出构建/测试命令与变更摘要，不直接执行 `git push`。

### 额外补充/优化需求（对话新增）
- 需要遵守 `AGENTS.md`：所有 shell 命令先提议再由用户批准执行。
- 交付时要求分支、提交、推送流程明确，并避免影响无关改动。

### Coding 前最终方案
#### 文件与模块清单
- 新增：
  - `CMakeLists.txt`
  - `include/core/scaffold.hpp`
  - `include/isa/scaffold.hpp`
  - `include/security/scaffold.hpp`
  - `src/core/scaffold.cpp`
  - `src/isa/scaffold.cpp`
  - `src/security/scaffold.cpp`
  - `src/kernel/scaffold.cpp`
  - `tests/test_harness.hpp`
  - `tests/test_sanity.cpp`
  - `demos/normal/README.md`
  - `demos/cross_user/README.md`
- 修改：
  - 无（仅新增文件）

#### 关键接口/数据结构（签名级）
- `tests/test_harness.hpp`：
  - `sim::test::Register(name, fn)`
  - `sim::test::RunAll()`
  - `SIM_TEST(...)` / `SIM_EXPECT_TRUE(...)` / `SIM_EXPECT_EQ(...)`
- CMake：
  - `simulator_core` 静态库
  - `test_sanity` 测试目标
  - `add_test(NAME sanity COMMAND test_sanity)`

#### 语义/不变量（必须测死，后续不得漂移）
- 只使用 C++17 + stdlib。
- 构建类型默认 Debug。
- 测试框架 header-only，无第三方库。
- `ctest` 作为最小验收门槛必须通过。

#### 测试计划（测试名 + 核心断言点）
- `Sanity_ConstantsAreDefined`：校验基础常量和类型可用，保证脚手架可编译、可链接、可执行。

#### 验收命令（仅列出，将由用户批准后执行）
- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
- cmake --build build
- ctest --test-dir build
- (Issue 0 无独立 demo 可执行程序)

### 实现复盘
**状态：** 已推送  
**提交：** 684ca62, c16d7a0  
**远端：** origin/issue-0-scaffold

#### 改动摘要（diff 风格）
- `issue 0: scaffold + green tests`：12 files changed, 196 insertions(+)
- `chore: ignore build artifacts and swap files`：2 files changed, 6 insertions(+)

#### 关键文件逐条复盘
- `CMakeLists.txt`：建立最小工程、`simulator_core`、`sanity` 测试与 CTest 集成。
- `include/*/scaffold.hpp` 与 `src/*/scaffold.cpp`：提供后续模块扩展入口，先保证可编译链接。
- `tests/test_harness.hpp`：实现零依赖测试注册/断言/运行器。
- `tests/test_sanity.cpp`：验证脚手架基本可用。
- `demos/normal/README.md`、`demos/cross_user/README.md`：预留 I-7 demo 目录。
- `.gitignore`：忽略 `build/` 与 `*.swp`，减少无关噪音。

#### 行为变化总结
- 新增能力：
  - 仓库具备完整构建/测试基础设施。
  - 可通过 `ctest` 统一运行测试。
- 失败模式/Trap：
  - 本 Issue 未引入执行器与 trap 语义。

#### 测试与运行
- 建议/已执行命令：
  - cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
  - cmake --build build
  - ctest --test-dir build --output-on-failure
- 已通过测试：
  - `sanity`（1/1 通过）

#### 已知限制 & 下一步建议
- 仅完成脚手架，尚未实现 ISA/assembler/executor。
- 下一步进入 Issue 1，补齐 Toy ISA 与最小 assembler。

## Issue 1：isa（方案确认）
**日期：** 2026-03-03  
**分支：** issue-1-isa  
**状态：** 方案已确认

### 初始需求（用户提出）
- 实现 Toy ISA 指令表示与最小汇编/解析。
- 支持 opcode：`NOP, LI, ADD, XOR, LD, ST, J, BEQ, CALL, RET, HALT, SYSCALL`。
- 支持 label 与 pc-relative 解析。
- 输出连续代码镜像或 `(va, Instr)` 视图。
- 增加 CTest 单元测试，覆盖解析、字段、label 偏移。

### 额外补充/优化需求（对话新增）
- `J/CALL/BEQ`：
  - `target=label` 使用 next-PC 规则计算偏移。
  - `target=数字` 直接视为最终 pc-relative 偏移。
- `opcode` 大小写不敏感；`label` 与寄存器大小写敏感。
- `InstrVa` 越界必须抛异常。
- `LD/ST` 内存操作数错误分类需稳定为 `bad mem operand`。
- 错误信息必须含行号与原始行片段。

### Coding 前最终方案
#### 文件与模块清单
- 新增：
  - `include/isa/opcode.hpp`
  - `include/isa/instr.hpp`
  - `include/isa/assembler.hpp`
  - `src/isa/assembler.cpp`
  - `tests/test_isa_assembler.cpp`
- 修改：
  - `CMakeLists.txt`

#### 关键接口/数据结构（签名级）
- `enum class Op { ... }`
- `struct Instr { Op op; int rd, rs1, rs2; int64_t imm; }`
- `struct AsmProgram { uint64_t base_va; std::vector<Instr> code; }`
- `AsmProgram AssembleText(std::string_view text, uint64_t base_va);`
- `uint64_t InstrVa(const AsmProgram&, size_t index);`
- `std::vector<LocatedInstr> ToLocated(const AsmProgram&);`

#### 语义/不变量（必须测死，后续不得漂移）
- 指令步长固定 `sim::isa::kInstrBytes == 4`。
- `AsmProgram` 为连续 image，无 code 洞。
- `base_va` 必须对齐到 4 字节。
- `J/CALL/BEQ`：
  - label 目标 `imm = target_va - (current_va + 4)`。
  - 数字目标直接作为最终偏移量。
- 解析错误统一 `std::runtime_error`，消息含 `asm:<line>` 与原始行片段。

#### 测试计划（测试名 + 核心断言点）
- `AssembleProgramWithLabels_Succeeds`：label + 基本指令组装成功。
- `PcRelativeResolution_NextPcConvention_Correct`：跳转偏移按 next-PC 规则。
- `InstructionFields_AfterParse_Correct`：字段映射正确。
- `UndefinedLabel_Fails`：未定义 label 报错。
- `CommentsAndWhitespace_Succeeds`：空行、注释、空格变体可解析。
- `NumericTargetIsPcRelativeOffset_Succeeds`：数字 target 语义锁定。
- `OpcodeCaseInsensitive_LabelAndRegCaseSensitive_BehaviorLocked`：大小写行为锁定。
- `InstrVa_OutOfRange_Throws`：越界抛异常。
- `BadMemOperand_FailsWithKind`：内存操作数错误分类稳定。

#### 验收命令（仅列出，将由用户批准后执行）
- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
- cmake --build build
- ctest --test-dir build
- (Issue 1 无独立 demo 可执行程序)

### 实现复盘
**状态：** 已推送  
**提交：** 30f7356  
**远端：** origin/issue-1-isa

#### 改动摘要（diff 风格）
- 7 files changed, 709 insertions(+), 8 deletions(-)

#### 关键文件逐条复盘
- `include/isa/opcode.hpp`：定义最小 opcode 集。
- `include/isa/instr.hpp`：定义 `Instr` 与指令步长常量。
- `include/isa/assembler.hpp`：声明 assembler 接口与语义注释。
- `src/isa/assembler.cpp`：实现两遍组装、label 解析、错误报告。
- `tests/test_isa_assembler.cpp`：覆盖成功路径与关键失败路径。
- `CMakeLists.txt`：接入 assembler 源文件与 `isa_assembler` 测试目标。

#### 行为变化总结
- 新增能力：
  - 可将 toy assembly 文本组装成 `AsmProgram`。
  - 可输出 VA 视图以支持调试与后续执行器对接。
- 失败模式/Trap：
  - 汇编阶段抛出结构化错误（unknown opcode、bad reg、bad mem operand、undefined label 等）。

#### 测试与运行
- 建议/已执行命令：
  - cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
  - cmake --build build
  - ctest --test-dir build --output-on-failure
- 已通过测试：
  - `sanity`
  - `isa_assembler`（2/2 通过）

#### 已知限制 & 下一步建议
- 尚未实现 CPU 执行语义与 trap。
- 下一步进入 Issue 2，补齐执行器、内存模型、demo_normal。

## Issue 2：executor（方案确认）
**日期：** 2026-03-04  
**分支：** issue-2-executor  
**状态：** 方案已确认

### 初始需求（用户提出）
- 实现最小 CPU 执行循环（不含 EWC），可运行 `AsmProgram` 到 `HALT` 或 trap。
- 实现 CPU 状态：`pc`、`x0..x31`、最小 byte-addressable 内存。
- 实现 `NOP/LI/ADD/XOR/LD/ST/J/BEQ/CALL/RET/HALT/SYSCALL` 语义。
- Fetch 必须按 `base_va + i*4` 取指，且越界/未对齐等触发 trap。
- 新增执行器测试与 `demo_normal`。

### 额外补充/优化需求（对话新增）
- `INVALID_PC` 需覆盖 underflow/misaligned/oob，消息必须含 `pc/base_va/index/code_size`。
- `LD/ST` 地址计算用 `__int128` 中间值；`addr<0` 或 `addr+7>=mem.size()` 触发 `INVALID_MEMORY`。
- `SYSCALL` 为 no-op，但必须记录稳定日志格式：`SYSCALL imm=<n> pc=<pc>`。
- 默认 `mem_size=64KiB`，测试需覆盖小内存边界场景。
- 显式保留 `Fetch/Decode/Execute/Mem/Commit` 分段与 Issue 3/4 插桩点。
- 额外边角语义：
  - `HALT` 的 `trap.pc` 记录 HALT 指令 PC（停止点），`FINAL_PC` 统一输出停止点。
  - 执行阶段增加寄存器索引合法性检查，非法寄存器 `bad_reg` 立即失败。
  - `LI` 对负数 immediate 使用 `static_cast<uint64_t>(imm)` 语义。
  - 增加 `max_steps` 死循环保护，超限返回 `step_limit_exceeded`。

### Coding 前最终方案
#### 文件与模块清单
- 新增：
  - `include/core/executor.hpp`
  - `src/core/executor.cpp`
  - `tests/test_executor.cpp`
  - `demos/normal/demo_normal.cpp`
- 修改：
  - `CMakeLists.txt`

#### 关键接口/数据结构（签名级）
- `enum class TrapReason { HALT, INVALID_PC, INVALID_MEMORY, SYSCALL_FAIL, UNKNOWN_OPCODE, STEP_LIMIT }`
- `struct Trap { TrapReason reason; uint64_t pc; std::string msg; }`
- `struct CpuState { uint64_t pc; std::array<uint64_t,32> regs; std::vector<uint8_t> mem; }`
- `struct ExecResult { Trap trap; CpuState state; std::vector<std::string> audit_log; std::vector<std::string> context_trace; std::vector<std::string> syscall_log; }`
- `ExecResult ExecuteProgram(const sim::isa::AsmProgram&, uint64_t entry_pc, size_t mem_size=64*1024, size_t max_steps=1000000);`
- `const char* TrapReasonToString(TrapReason);`
- `void PrintRunSummary(const ExecResult&, std::ostream&);`

#### 语义/不变量（必须测死，后续不得漂移）
- Fetch 校验：
  - `pc < base_va` -> `INVALID_PC(underflow)`
  - `(pc-base_va)%4 != 0` -> `INVALID_PC(misaligned)`
  - index 越界 -> `INVALID_PC(oob)`
- 控制流：
  - `next_pc = pc + 4`
  - `J/CALL/BEQ(taken)`：`pc = next_pc + imm`
  - `BEQ(not taken)`：`pc = next_pc`
  - `CALL`：`x1 = next_pc`
  - `RET`：`pc = x1`
- `HALT`：
  - `trap.reason = HALT`
  - `trap.pc = HALT 指令 pc`
  - Summary 的 `FINAL_PC` 统一输出 `trap.pc`
- `x0` 每条指令提交后强制为 0。
- `LD/ST`：
  - 地址用 `__int128` 计算与边界校验
  - 8-byte little-endian 读写
- `SYSCALL`：
  - no-op，不触发 `SYSCALL_FAIL`
  - 写入稳定格式日志
- 执行步数超过 `max_steps` -> `STEP_LIMIT`，`msg` 含 `step_limit_exceeded`。

#### 测试计划（测试名 + 核心断言点）
- `Execute_SimpleArithmetic_ToHalt`：算术路径到 HALT，含负 immediate。
- `Execute_JumpAndBranch_PcRelativeCorrect`：`J/BEQ` 的 next-PC 语义。
- `Execute_CallRet_Works`：`CALL` 写 `x1`、`RET` 跳回。
- `Execute_LoadStore_InvalidAddr_Traps`：小内存场景越界 trap。
- `Execute_InvalidPc_Cases_TrapWithDiagnostics`：underflow/misaligned/oob 诊断字段完整。
- `Execute_Syscall_LogsAndContinues`：`SYSCALL` 日志格式与继续执行。
- `Execute_StepLimit_TriggersTrap`：死循环保护生效。
- `Execute_BadRegister_InInstr_Traps`：非法寄存器立即失败。
- `PrintRunSummary_UsesTrapPcAsFinalPc`：`FINAL_PC` 使用停止点 PC。

#### 验收命令（仅列出，将由用户批准后执行）
- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
- cmake --build build
- ctest --test-dir build
- ./build/demo_normal

### 实现复盘
**状态：** 已实现（未推送）  
**提交：** TBD  
**远端：** 未推送

#### 改动摘要（diff 风格）
- 新增执行器公共接口与实现，新增执行器测试，新增 `demo_normal`，更新 CMake 目标。

#### 关键文件逐条复盘
- `include/core/executor.hpp`：定义 trap/state/result 与执行器 API。
- `src/core/executor.cpp`：实现 Fetch/Decode/Execute/Mem/Commit 骨架、trap 语义、内存访问、summary 输出。
- `tests/test_executor.cpp`：覆盖执行语义、边界条件与可观测性格式。
- `demos/normal/demo_normal.cpp`：组装并运行最小程序，打印固定 summary。
- `CMakeLists.txt`：接入 executor 源文件、`test_executor` 与 `demo_normal` 目标。

#### 行为变化总结
- 新增能力：
  - 从 `AsmProgram` 执行到 `HALT`/trap。
  - 支持最小寄存器与内存语义、控制流、syscall 日志。
  - 支持固定 summary 输出与后续 audit/context trace 扩展位。
- 失败模式/Trap：
  - `INVALID_PC`、`INVALID_MEMORY`、`UNKNOWN_OPCODE`、`STEP_LIMIT`。
  - `SYSCALL_FAIL` 已预留但本 Issue 不触发。

#### 测试与运行
- 建议/已执行命令：
  - cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
  - cmake --build build
  - ctest --test-dir build --output-on-failure
  - ./build/demo_normal
- 已通过测试：
  - `sanity`
  - `isa_assembler`
  - `executor`（3/3 通过）
- demo 输出：
  - `FINAL_REASON=HALT`
  - `FINAL_PC=4112`
  - `SYSCALL_COUNT=1`
  - `AUDIT_COUNT=0`
  - `CTX_TRACE_COUNT=0`

#### 已知限制 & 下一步建议
- 尚未接入 EWC gate（Issue 3 在 Fetch 插入）。
- 尚未接入伪解密与 decode fail trap（Issue 4 在 Decode 插入）。
- 建议下一步创建 `issue-2-executor` 分支并完成提交/推送，再继续 Issue 3。

#### 推送状态更新
**状态：** 已推送  
**远端：** origin/issue-2-executor

## Issue 3：ewc-fetch-gate（方案确认）
**日期：** 2026-03-04  
**分支：** issue-3-ewc-fetch-gate  
**状态：** 方案已确认

### 初始需求（用户提出）
- 在 Fetch 阶段实现 EWC mandatory gate。
- deny 时必须 trap `EWC_ILLEGAL_PC`，并写入 audit 事件 `EWC_ILLEGAL_PC`。
- 保持执行器流水线结构 `Fetch -> Decode -> Execute -> Mem -> Commit`。
- 本 Issue 不引入 Gateway/PVT/SPE，EWC 由测试/demo 手动注入。

### 额外补充/优化需求（对话新增）
- Fetch 顺序修订：先做结构性 PC 校验（underflow/misaligned/oob），再做 EWC query，避免误报。
- 保留旧 `ExecuteProgram` 签名作为 wrapper，但 wrapper 内也必须走 EWC query（allow-all window）。
- `EwcTable` 当前原型禁止重叠窗口；`SetWindows` 需排序并检测 overlap，错误信息包含 `context_handle/window_id/range`。
- `misaligned` 判定按 `(pc - base_va) % sim::isa::kInstrBytes != 0`。
- wrapper 的 allow-all end 计算固定为 `base_va + code_size * sim::isa::kInstrBytes`（end exclusive）。

### Coding 前最终方案
#### 文件与模块清单
- 新增：
  - `include/security/ewc.hpp`
  - `src/security/ewc.cpp`
- 修改：
  - `include/core/executor.hpp`
  - `src/core/executor.cpp`
  - `tests/test_executor.cpp`
  - `demos/normal/demo_normal.cpp`
  - `CMakeLists.txt`

#### 关键接口/数据结构（签名级）
- `ExecWindow{window_id,start_va,end_va,owner_user_id,key_id,type,code_policy_id}`
- `EwcQueryResult{allow,matched_window,key_id,window_id,owner_user_id,code_policy_id}`
- `class EwcTable { SetWindows(...); Query(...); }`
- `TrapReason` 新增 `EWC_ILLEGAL_PC`
- `ExecuteOptions{mem_size,max_steps,context_handle,ewc}`
- 新增 `ExecuteProgram(program, entry_pc, const ExecuteOptions&)`
- 保留旧签名 `ExecuteProgram(program, entry_pc, mem_size, max_steps)` 作为 wrapper

#### 语义/不变量（必须测死，后续不得漂移）
- Fetch 顺序固定：结构性 PC 校验 -> EWC query -> 取指。
- EWC deny：
  - `trap.reason == EWC_ILLEGAL_PC`
  - `audit_log` 写入 `EWC_ILLEGAL_PC ...`
  - `trap.msg` 至少包含 `pc/context_handle/window_id`
- wrapper 也必须触发 EWC query（通过 allow-all window）。
- 同一 context 的 EWC windows 不允许重叠。

#### 测试计划（测试名 + 核心断言点）
- `Execute_EwcAllows_ProgramRunsToHalt`：allow 场景可到 HALT。
- `Execute_EwcDenies_AtEntry_TrapsEwcIllegalPc`：entry 即 deny，断言 reason/audit/msg。
- `Execute_EwcSubsetWindow_JumpOut_TrapsEwcIllegalPc`：跳出允许窗口后 deny，且 PC 仍在 image 内。
- `Ewc_SetWindows_OverlapRejected`：重叠窗口配置失败并返回可诊断错误信息。
- 兼容回归：现有执行器测试继续可过（通过旧接口 wrapper）。

#### 验收命令（仅列出，将由用户批准后执行）
- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
- cmake --build build
- ctest --test-dir build
- ./build/demo_normal

### 实现复盘
**状态：** 已实现（未推送）  
**提交：** TBD  
**远端：** 未推送

#### 改动摘要（diff 风格）
- 新增 EWC 模块（`include/security/ewc.hpp`、`src/security/ewc.cpp`）。
- 扩展执行器接口与实现以接入 EWC gate（`include/core/executor.hpp`、`src/core/executor.cpp`）。
- 扩展执行器测试以覆盖 EWC allow/deny/jump-out/overlap（`tests/test_executor.cpp`）。
- 更新 demo 展示 allow 与 deny 两种路径（`demos/normal/demo_normal.cpp`）。
- 更新构建接入 EWC 源文件（`CMakeLists.txt`）。

#### 关键文件逐条复盘
- `include/security/ewc.hpp`：定义 `ExecWindow`、`EwcQueryResult`、`EwcTable` 接口，支持按 `context_handle` 管理窗口。
- `src/security/ewc.cpp`：实现 `SetWindows/Query`，并在 `SetWindows` 中排序+重叠检测，拒绝 overlap 配置。
- `include/core/executor.hpp`：新增 `TrapReason::EWC_ILLEGAL_PC`、`ExecuteOptions`、新 `ExecuteProgram` 签名；保留旧签名 wrapper。
- `src/core/executor.cpp`：Fetch 阶段顺序固定为“结构性 PC 校验 -> EWC query -> 取指”；deny 路径写 trap + audit；旧签名 wrapper 内构造 allow-all window 并强制走 EWC。
- `tests/test_executor.cpp`：新增 4 组 EWC 测试并保留既有执行器回归测试。
- `demos/normal/demo_normal.cpp`：新增 `[CASE_A_ALLOW]` 与 `[CASE_B_DENY]` 演示，打印 summary 与 deny 审计。
- `CMakeLists.txt`：将 `src/security/ewc.cpp` 纳入 `simulator_core`。

#### 行为变化总结
- 新增能力：
  - EWC 成为 Fetch 必经 gate。
  - 支持按 `context_handle` 的窗口查询与 deny 拦截。
  - deny 时产生 `EWC_ILLEGAL_PC` trap，并写入 audit 事件。
  - 旧执行器调用路径仍可运行（wrapper 注入 allow-all EWC）。
- 失败模式/Trap：
  - `EWC_ILLEGAL_PC`（EWC deny）
  - `INVALID_PC`（underflow/misaligned/oob）
  - 其余既有 trap 语义保持不变（如 `INVALID_MEMORY`、`STEP_LIMIT` 等）。

#### 测试与运行
- 建议/已执行命令：
  - cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
  - cmake --build build
  - ctest --test-dir build --output-on-failure
  - ./build/demo_normal
- 已通过测试：
  - `sanity`
  - `isa_assembler`
  - `executor`（3/3 通过）
- demo 输出（关键）：
  - `[CASE_A_ALLOW] FINAL_REASON=HALT`
  - `[CASE_B_DENY] FINAL_REASON=EWC_ILLEGAL_PC`
  - 审计包含：`EWC_ILLEGAL_PC pc=... context_handle=... window_id=none`

#### 已知限制 & 下一步建议
- 目前 EWC 配置仍由测试/demo 手动注入，尚未接入 Gateway（Issue 4+）。
- 尚未引入伪解密与 decode fail 路径（后续在 Decode 阶段插入）。

#### 推送状态更新
**状态：** 已推送
**远端：** origin/issue-3-ewc-fetch-gate

## Issue 4：pseudo-decrypt（方案确认）
**日期：** 2026-03-08
**分支：** issue-4-decrypt-before-decode
**状态：** 方案已确认

### 初始需求（用户提出）
- 在 Fetch→Decode 之间插入伪解密阶段，作为 EWC 的子模块。
- EWC Query 返回 `key_id` 后，用该 key 解密密文指令；校验失败触发 `DECRYPT_DECODE_FAIL` + 审计事件。
- 代码在内存中始终是密文（密文模式开启时），每次 fetch 时解密。
- 向后兼容：不提供密文时行为与 Issue 3 完全一致。

### 额外补充/优化需求（对话新增）
- 解密逻辑在架构上属于 EWC 的子功能（设计文档 §3.2：EWC = "代码身份验证 + 解密控制"），不是独立安全模块。以子模块形式实现，不集成到 `EwcTable` 中。
- 加密函数（工具链侧）和解密函数（硬件侧）放在同一文件中，共享序列化格式。代码中注释标注各自归属（工具链侧 vs 硬件侧）。
- 命名空间：`sim::security`（安全原语）。

### Coding 前最终方案
#### 文件与模块清单
- 新增：
  - `include/security/code_codec.hpp`（EWC 解密子模块接口 + 工具链侧加密函数）
  - `src/security/code_codec.cpp`（实现）
- 修改：
  - `include/core/executor.hpp`（`TrapReason` 新增 `DECRYPT_DECODE_FAIL` + `ExecuteOptions` 扩展密文数据引用）
  - `src/core/executor.cpp`（EWC allow 后插入解密+校验步骤）
  - `tests/test_executor.cpp`（新增伪解密测试组）
  - `demos/normal/demo_normal.cpp`（演示正确/错误 key 两条路径）
  - `CMakeLists.txt`（纳入 `code_codec` 源文件）

#### 语义接口（WHAT，签名由 Codex 决定）
- **code_codec 解密（硬件侧）**：接收密文单元 + `key_id` + `pc`，解密并校验 tag/MAC。成功还原 `Instr`，失败报告校验不通过。模拟 EWC 在每次 Fetch 时的解密行为（设计文档 §9.3："合法 → 解密 → Decode"）。
- **code_codec 加密（工具链侧）**：接收 `AsmProgram` + `key_id`，输出密文表示。模拟签名时的加密过程。
- **TrapReason 扩展**：新增 `DECRYPT_DECODE_FAIL`，`TrapReasonToString` / `PrintRunSummary` 同步支持。
- **ExecuteOptions 扩展**：新增密文数据的可选指针（默认 `nullptr`）。`nullptr` = 明文模式，非 `nullptr` = 密文模式。

#### 语义/不变量（必须测死，后续不得漂移）
- Pipeline 顺序固定：PC 校验 → EWC query(返回 `key_id`) → fetch 密文 → 解密(`key_id`, `pc`) → 校验 tag → `Instr` → Execute。
- `DECRYPT_DECODE_FAIL` 优先级：仅在 PC 合法且 EWC allow 之后触发。
- 确定性：相同 `(key_id, pc, ciphertext)` 必须产生相同结果；正确 key 必定还原原始 `Instr`；错误 key 必定失败。
- 向后兼容：密文为空时行为与 Issue 3 完全一致，旧测试零改动全绿。
- 审计：`DECRYPT_DECODE_FAIL` 必须写入 `audit_log`，内容含 `pc`、`context_handle`、`key_id`。
- 密文长度必须与 `program.code.size()` 一致（由执行器校验）。
- `ExecuteOptions` 新增成员必须用指针或有默认值的类型，不可用引用（兼容现有调用点）。

#### 测试计划（测试名 + 核心断言点）
- `Execute_PseudoDecrypt_CorrectKey_RunsToHalt`：EWC allow + 正确 `key_id` → 程序正常到 `HALT`。
- `Execute_PseudoDecrypt_WrongKey_TrapsDecryptDecodeFail`：同一密文用错误 `key_id` 执行 → `trap.reason == DECRYPT_DECODE_FAIL` + `audit_log` 非空 + `msg` 含 `pc`/`context_handle`/`key_id`。
- `Execute_PseudoDecrypt_TamperedCiphertext_TrapsDecryptDecodeFail`：破坏密文字节 → 解密后校验失败 → `DECRYPT_DECODE_FAIL`。
- 兼容回归：Issue 0-3 全部旧测试继续通过（`ciphertext = nullptr` 路径）。

#### 验收命令（仅列出，将由用户批准后执行）
- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
- cmake --build build
- ctest --test-dir build
- ./build/demo_normal

#### Codex 可行性检查结果摘要
- `EwcQueryResult` 已包含 `key_id`，executor 未消费——需要让 `FetchPacket` 携带 `key_id`。
- `FetchStage → DecodeStage` 之间插入点存在，`DecodeStage` 有 Issue 4 hook 注释。但 `DecodeStage` 当前不能失败，需改造。
- `ExecuteOptions` 可安全扩展（指针/默认值成员）。
- 审计路径需新增（当前只有 `EWC_ILLEGAL_PC` 一条）。
- CMakeLists.txt 无冲突，命名空间干净。

### 实现复盘
**状态：** 已推送
**提交：** 34d8137
**远端：** origin/issue-4-decrypt-before-decode

#### 改动摘要（diff 风格）
- `include/security/code_codec.hpp`：新增，定义 `CipherInstrUnit` / `CipherProgram` / `EncryptProgram` / `DecryptInstr`
- `src/security/code_codec.cpp`：新增，约 190 行，实现共享密文格式、toy XOR 掩码、`key_check` 与 `tag` 校验
- `include/core/executor.hpp`：扩展 `TrapReason::DECRYPT_DECODE_FAIL`，`ExecuteOptions` 新增 `ciphertext` 指针
- `src/core/executor.cpp`：改造 Fetch/Decode 管线，消费 EWC `key_id`，新增解密失败 trap/audit 与密文长度校验
- `tests/test_executor.cpp`：新增 3 个伪解密测试，旧测试保持通过
- `demos/normal/demo_normal.cpp`：改为演示“正确 key → HALT / 错误 key → DECRYPT_DECODE_FAIL”
- `CMakeLists.txt`：纳入 `src/security/code_codec.cpp`

#### 关键文件逐条复盘
- `include/security/code_codec.hpp`：定义密文单元与加解密接口；同一格式同时服务工具链侧加密和硬件侧解密。
- `src/security/code_codec.cpp`：把 `Instr` 序列化到固定 payload，使用按 `(key_id, pc, byte_index)` 生成的确定性 XOR 掩码加密，并用 `key_check` + `tag` 保证错 key 和篡改都会失败。
- `include/core/executor.hpp`：公开新的 trap 原因与可选密文输入，保持 `nullptr` 明文路径向后兼容。
- `src/core/executor.cpp`：固定执行顺序为“PC 校验 → EWC query → 取密文/明文 → 解密校验 → Execute”；解密失败时生成 `DECRYPT_DECODE_FAIL` trap 和审计日志；同时记录 `context_handle` trace。
- `tests/test_executor.cpp`：覆盖正确 key、错误 key、篡改密文三条路径，并验证 trap/audit/msg 诊断字段。
- `demos/normal/demo_normal.cpp`：输出 `FINAL_REASON`、审计流和 `CTX context_handle=...`，满足 demo 可观测性要求。
- `CMakeLists.txt`：让核心库、测试和 demo 都链接到新 codec 实现。

#### 行为变化总结
- 新增能力：
  - 执行器支持可选密文模式；EWC allow 后会基于返回的 `key_id` 在 Decode 前解密指令。
  - 新增 `DECRYPT_DECODE_FAIL` trap，并把 `pc`、`context_handle`、`key_id` 写入审计日志。
  - `demo_normal` 现在能演示“正确 key 正常执行”和“错误 key 解密失败”两条路径。
- 失败模式/Trap：
  - `ciphertext` 长度与 `program.code.size()` 不一致时抛 `std::runtime_error`。
  - 错误 `key_id` 会命中 `detail=key_check_mismatch`。
  - 篡改密文会命中 `detail=tag_mismatch`。

#### 测试与运行
- 建议/已执行命令：
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
  - `cmake --build build`
  - `ctest --test-dir build`
  - `./build/demo_normal`
- 已通过测试：
  - `sanity`
  - `isa_assembler`
  - `executor`（3/3 通过）
  - `demo_normal`：`[CASE_A_ALLOW] FINAL_REASON=HALT`，`[CASE_B_WRONG_KEY] FINAL_REASON=DECRYPT_DECODE_FAIL`

#### 已知限制 & 下一步建议
- 当前仅实现 L0 toy scheme，不提供真实密码学强度。
- `key_check` 与 `tag` 只服务原型验证，后续若进入更高保真版本，需要替换为正式的签名/MAC 机制。
- `context_trace` 目前记录执行入口的 `context_handle`，真正的跨上下文切换细节仍待后续 demo/调度路径接入。

### 实现复盘
**状态：** 已推送
**提交：** 34d8137
**远端：** origin/issue-4-decrypt-before-decode

#### 改动摘要（diff 风格）
- `tests/test_executor.cpp`：新增 1 个测试，约 30 行，覆盖 `ciphertext_size_mismatch` 异常路径
- `docs/project_log.md`：追加本次实现复盘

#### 关键文件逐条复盘
- `tests/test_executor.cpp`：新增 `Execute_PseudoDecrypt_CiphertextSizeMismatch_Throws`，先构造 3 条指令的 `AsmProgram`，再通过 `EncryptProgram` 生成密文并删掉 1 个单元，验证 `ExecuteProgram` 抛出 `std::runtime_error` 且消息包含 `ciphertext_size_mismatch`。
- `docs/project_log.md`：记录本次增量测试补充、执行命令与验证结果，保留历史实现记录不变。

#### 行为变化总结
- 新增能力：
  - 锁定伪解密入口的密文长度校验行为，防止 `CipherProgram` 与 `AsmProgram` 长度不一致时静默进入执行流程。
- 失败模式/Trap：
  - 当 `options.ciphertext->size() != program.code.size()` 时，`ExecuteProgram` 抛出包含 `ciphertext_size_mismatch` 的 `std::runtime_error`。

#### 测试与运行
- 建议/已执行命令：
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
  - `cmake --build build`
  - `ctest --test-dir build`
- 已通过测试：
  - `sanity`
  - `isa_assembler`
  - `executor`（3/3 通过）

#### 已知限制 & 下一步建议
- 本次只补充异常路径测试，没有修改执行器逻辑。
- 若后续继续扩展密文校验，建议补充 `code_size=0`、空密文和更细粒度消息字段断言。

## Issue 5：Gateway + SecureIR（方案确认）
**日期：** 2026-03-09
**分支：** issue-5-gateway-secureir
**状态：** 方案已确认

### 初始需求（用户提出）
- 实现 Gateway 模拟器：解析 SecureIR → 验签(stub) → 配置 EWC 窗口 → 分配 `context_handle` → 返回。
- 引入 `SecurityHardware` 类统一持有片上安全硬件状态（EwcTable、AuditCollector）。
- 引入 `AuditCollector` 作为统一审计落点，Gateway 和 Executor 都写入它。
- `demo_normal` 改用 Gateway 启动，不再手工注入 EWC 窗口。

### 额外补充/优化需求（对话新增）
- SecureIR 与程序代码分离（选项 A）：JSON 只包含元数据，程序代码仍以汇编文本写在 demo 中。后续 Issue 11 可考虑统一。
- JSON 解析器是 Gateway 内部实现，对应硬件 Gateway 解析二进制 blob 的状态机。整个模拟器阶段使用 JSON 格式。
- `context_handle` 使用递增计数器，永不复用，避免残留引用攻击和审计混淆。
- SecureIR schema 使用 `start_va/end_va`（与 `ExecWindow` 字段名一致），含 `pages` 占位字段为 Issue 7 PVT 预留。
- Gateway 构造时注入 `SecurityHardware&`，语义上 Gateway 是芯片的一部分。
- `ExecuteOptions::ewc` 保持 `const EwcTable*`，新增 `AuditCollector* audit`。Executor 只依赖它实际需要的组件，不依赖整个 `SecurityHardware`。
- 删除 `ExecResult::audit_log`，统一从 `AuditCollector` 读取审计事件。
- 窗口读回接口不加，留注释说明后续必要时再加。

### Coding 前最终方案
#### 文件与模块清单
- 新增：
  - `include/security/hardware.hpp`（SecurityHardware 类）
  - `include/security/audit.hpp`（AuditCollector 类）
  - `src/security/audit.cpp`
  - `include/security/gateway.hpp`（Gateway 类）
  - `src/security/gateway.cpp`（含内部 JSON 解析器）
  - `tests/test_gateway.cpp`
- 修改：
  - `include/core/executor.hpp`（ExecuteOptions 新增 `AuditCollector*`，删除 `ExecResult::audit_log`）
  - `src/core/executor.cpp`（审计事件改写 AuditCollector）
  - `tests/test_executor.cpp`（适配审计接口变更）
  - `demos/normal/demo_normal.cpp`（改用 Gateway 启动）
  - `CMakeLists.txt`（纳入新增源文件和测试）

#### 关键接口/数据结构（语义级）
- `SecurityHardware`：
  - 持有 `EwcTable` 和 `AuditCollector`
  - `EwcTable& GetEwcTable()` / `const EwcTable& GetEwcTable() const`
  - `AuditCollector& GetAuditCollector()` / `const AuditCollector& GetAuditCollector() const`
- `AuditCollector`：
  - `LogEvent(const std::string& event)`
  - `GetEvents() -> const std::vector<std::string>&`
  - `Clear()`
- `Gateway`：
  - `Gateway(SecurityHardware& hardware)`
  - `Load(const std::string& json) -> ContextHandle`
  - `Release(ContextHandle handle)` [stub，接口占位]
  - `GetUserIdForHandle(ContextHandle) -> std::optional<uint32_t>`
- `ExecuteOptions` 扩展：
  - 新增 `AuditCollector* audit = nullptr`
- `ExecResult` 变更：
  - 删除 `audit_log` 字段

#### SecureIR L0 JSON Schema
```json
{
  "program_name": "demo_normal",
  "user_id": 1,
  "signature": "stub-valid",
  "base_va": 4096,
  "windows": [
    {"window_id": 1, "start_va": 4096, "end_va": 8192, "key_id": 11, "type": "CODE", "code_policy_id": 1}
  ],
  "pages": [],
  "cfi_level": 0,
  "call_targets": [],
  "jmp_targets": []
}
```
- `signature`：stub 字段，Gateway 检查非空即通过
- `pages`：占位字段，Issue 5 解析但不消费，为 Issue 7 PVT 预留
- `cfi_level` / `call_targets` / `jmp_targets`：为 Issue 8 SPE 预留，Issue 5 解析但不消费

#### 语义/不变量（必须测死，后续不得漂移）
- `gateway.Load()` 成功后，对应 `context_handle` 的 EWC query 在合法 PC 范围内必须 allow。
- `context_handle` 单调递增且永不复用，即使 `Release()` 后也不回收数值。
- `handle → user_id` 映射在 load 后可通过 `GetUserIdForHandle()` 查询。
- `signature` 为空或缺失 → 抛异常 + `GATEWAY_LOAD_FAIL` 审计。
- `windows` 字段缺失或为空 → 抛异常 + `GATEWAY_LOAD_FAIL` 审计。
- `windows` 重叠 → EWC 拒绝 → 抛异常 + `GATEWAY_LOAD_FAIL` 审计。
- 非 `CODE` 类型 → 配置错误 → 抛异常 + `GATEWAY_LOAD_FAIL` 审计。
- Gateway try/catch `SetWindows` 异常，先写审计再 rethrow；handle 映射在 `SetWindows` 成功后才写入。
- 顶层 `user_id` 填充到每个 `ExecWindow.owner_user_id`。
- 所有审计事件（Gateway/Executor）写入同一个 `AuditCollector`。
- 向后兼容：Issue 0-4 全部旧测试适配后继续通过。

#### 测试计划（测试名 + 核心断言点）
- `Gateway_Load_ValidSecureIR_ConfiguresEwcAndReturnsHandle`：合法 JSON → handle 有效 → EWC query allow。
- `Gateway_Load_MultipleCalls_UniqueHandles`：多次 load → handle 单调递增不重复。
- `Gateway_Load_EmptySignature_Fails`：signature 为空 → 异常 + `GATEWAY_LOAD_FAIL` 审计。
- `Gateway_Load_MissingWindows_Fails`：无 windows 字段 → 异常。
- `Gateway_Load_EmptyWindows_Fails`：windows 为空数组 → 异常。
- `Gateway_HandleToUserIdMapping_Correct`：load 后查 user_id 映射正确。
- `Gateway_Load_OverlappingWindows_Fails`：重叠窗口 → 异常 + 审计。
- `Gateway_Load_InvalidType_Fails`：非 CODE 类型 → 异常 + 审计。
- `AuditCollector_UnifiedEvents`：Gateway 和 Executor 事件都出现在同一个 collector 中。
- 兼容回归：Issue 0-4 全部旧测试适配后继续通过。

#### 验收命令（仅列出，将由用户批准后执行）
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build`
- `./build/demo_normal`

#### 设计决策记录

**D-1：SecureIR 与程序代码分离**
- 决策：SecureIR JSON 只包含元数据，不包含程序代码。
- 原因：减少 Issue 5 改动范围，聚焦 Gateway 核心逻辑。
- 后续：Issue 11 SecureIR 生成器可考虑统一格式。

**D-2：JSON 解析器是 Gateway 内部实现**
- 决策：JSON 解析器作为 Gateway 私有实现，不独立成模块。
- 原因：对应硬件 Gateway 解析二进制 blob 的状态机；整个模拟器阶段使用 JSON 格式。

**D-3：AuditCollector 是统一审计落点**
- 决策：引入 AuditCollector，Gateway/Executor 都写入它，删除 ExecResult::audit_log。
- 原因：对应片上统一审计链，所有安全模块写入同一条链。

**D-4：context_handle 递增永不复用**
- 决策：简单递增计数器，永不复用。
- 原因：避免残留引用攻击和审计混淆；handle 可预测但安全（OS 可见但无法滥用）。
- 后续：真实硬件应使用 64 位或 generation 机制。

**D-5：SecureIR schema 字段命名与预留**
- 决策：使用 `start_va/end_va`（与 ExecWindow 一致），含 `pages` 占位字段。
- 原因：减少转换，为 Issue 7 PVT 预留。

**D-6：SecurityHardware 统一持有片上状态**
- 决策：引入 SecurityHardware 类持有 EwcTable 和 AuditCollector。
- 原因：语义准确——代表芯片内部安全子系统状态；后续可扩展 PVT、SPE。

**D-7：Gateway 构造时注入 SecurityHardware**
- 决策：`Gateway(SecurityHardware&)` 构造时绑定。
- 原因：Gateway 是芯片的一部分，始终操作同一个硬件实例。

**D-8：ExecuteOptions 保持细粒度指针**
- 决策：`ExecuteOptions::ewc` 保持 `const EwcTable*`，新增 `AuditCollector* audit`，不改成 `SecurityHardware*`。
- 原因：Executor 只依赖它实际需要的组件，保持最小依赖。

**D-9：窗口读回接口暂不添加**
- 决策：不为 EwcTable 添加窗口读回接口，留注释说明。
- 原因：非功能必需；测试通过 Query 行为间接验证。
- 后续：必要时再加只读快照接口。

### 实现复盘
**状态：** 已实现（未推送）
**提交：** TBD
**远端：** 未推送

#### 改动摘要（diff 风格）
- 新增 6 个文件，修改 6 个文件
- `include/security/{audit.hpp,hardware.hpp,gateway.hpp}`：新增统一审计器、片上安全硬件聚合和 Gateway 接口
- `src/security/{audit.cpp,gateway.cpp}`：新增审计实现与 SecureIR L0 JSON 解析 / Gateway load 流程
- `include/core/executor.hpp`、`src/core/executor.cpp`：删除 `ExecResult::audit_log`，新增 `ExecuteOptions::audit`，Executor 改写统一审计器
- `tests/test_executor.cpp`、`tests/test_gateway.cpp`：适配新审计接口并新增 Gateway 测试覆盖
- `demos/normal/demo_normal.cpp`、`CMakeLists.txt`：demo 改用 `SecurityHardware + Gateway` 启动，CMake 接入新源码与新测试

#### 关键文件逐条复盘
- `include/security/audit.hpp`、`src/security/audit.cpp`：实现 `AuditCollector::LogEvent/GetEvents/Clear`，作为 Gateway 和 Executor 的统一审计落点。
- `include/security/hardware.hpp`：实现 `SecurityHardware`，统一持有 `EwcTable` 和 `AuditCollector`，为后续 PVT/SPE 扩展预留聚合点。
- `include/security/gateway.hpp`、`src/security/gateway.cpp`：实现 `Gateway(SecurityHardware&)`、`Load/Release/GetUserIdForHandle`；内部手写最小 JSON 解析器，校验 SecureIR L0 schema，填充 `owner_user_id`，调用 `SetWindows`，分配单调递增且永不复用的 `context_handle`，统一写入 `GATEWAY_LOAD_OK/FAIL` 审计。
- `include/core/executor.hpp`、`src/core/executor.cpp`：移除 `ExecResult::audit_log`，将 `EWC_ILLEGAL_PC` 和 `DECRYPT_DECODE_FAIL` 事件改写到 `options.audit`；`PrintRunSummary` 不再从结果对象统计 audit 数量。
- `tests/test_executor.cpp`：改为显式创建 `AuditCollector` 并断言 `GetEvents()` 内容，保持 Issue 0-4 语义回归。
- `tests/test_gateway.cpp`：新增 Gateway 成功路径、空签名、缺失/空 windows、重叠窗口、非法 type、handle→user_id 映射、统一审计链等测试。
- `demos/normal/demo_normal.cpp`：不再手工注入 EWC；通过 Gateway 生成 `context_handle`，并打印统一审计流与 `context_handle` trace。
- `CMakeLists.txt`：接入 `audit.cpp`、`gateway.cpp` 和 `test_gateway`。

#### 行为变化总结
- 新增能力：
  - 可通过 `Gateway::Load()` 直接从 SecureIR JSON 配置 EWC，并返回 `context_handle`。
  - `SecurityHardware` 统一承载安全硬件状态，demo/test 不再各自拼装散落组件。
  - Gateway 与 Executor 的审计事件进入同一个 `AuditCollector`。
- 失败模式/Trap：
  - `signature` 缺失或为空、`windows` 缺失或为空、窗口 `type != CODE`、EWC 窗口重叠/非法区间都会触发 `GATEWAY_LOAD_FAIL` 并抛出 `std::runtime_error`。
  - Executor 在 Fetch deny 和伪解密失败时继续分别触发 `EWC_ILLEGAL_PC` / `DECRYPT_DECODE_FAIL`，但事件改为写入 `AuditCollector`。

#### 测试与运行
- 建议/已执行命令：
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`
  - `./build/demo_normal`
- 已通过测试：
  - `sanity`
  - `isa_assembler`
  - `executor`
  - `gateway`
  - `demo_normal` 运行成功，输出 allow / wrong-key 两条路径的 trap reason、audit stream、context trace

#### 已知限制 & 下一步建议
- `Gateway::Release()` 仍是 stub/no-op，占位到后续 issue 再接入真正的上下文释放与硬件状态清理。
- 当前 JSON 解析器只覆盖 SecureIR L0 需要的 object/array/string/无符号整数子集，不支持通用 JSON 全量语法。
- `pages`、`cfi_level`、`call_targets`、`jmp_targets` 目前只解析校验、不参与运行时行为；后续分别由 PVT / SPE issue 消费。

### 实现复盘
**状态：** 已实现（未推送）
**提交：** TBD
**远端：** 未推送

#### 改动摘要（diff 风格）
- 本次代码评审修复实际触达 9 个文件：`include/security/audit.hpp`、`src/security/audit.cpp`、`include/security/ewc.hpp`、`src/security/ewc.cpp`、`include/security/gateway.hpp`、`src/security/gateway.cpp`、`src/core/executor.cpp`、`tests/test_executor.cpp`、`tests/test_gateway.cpp`
- `audit`：`vector<string>` 升级为 `vector<AuditEvent>`，新增单调 `seq_no` 与结构化 `LogEvent(...)`
- `gateway/ewc`：补齐 `Release()`、`EwcTable::ClearWindows()`、`kMaxContextHandles = 256` 并发上限
- `executor/tests/demo`：所有审计写入点与断言改为结构化字段；`demo_normal` 输出格式改为结构化审计事件

#### 关键文件逐条复盘
- `include/security/audit.hpp`、`src/security/audit.cpp`：新增 `AuditEvent{seq_no,type,user_id,context_handle,pc,detail}`，collector 统一分配递增序号；`Clear()` 只清空缓存，不回绕序号。
- `include/security/ewc.hpp`、`src/security/ewc.cpp`：新增 `ClearWindows(context_handle)`，供 Gateway release 清理上下文窗口。
- `include/security/gateway.hpp`、`src/security/gateway.cpp`：加入 `kMaxContextHandles = 256` 模拟上限；`Load()` 在活跃 handle 达上限时抛 `gateway_capacity_exceeded`；`Release()` 现在会移除 `handle_to_user_` 映射、清空 EWC 窗口并记录 `GATEWAY_RELEASE`。
- `src/core/executor.cpp`：`EWC_ILLEGAL_PC` 与 `DECRYPT_DECODE_FAIL` 改为结构化审计；解密失败事件现在携带窗口 owner 的 `user_id`、`pc`、`context_handle` 和 `key_id` 细节。
- `tests/test_gateway.cpp`：旧字符串匹配断言改为字段级断言；新增 release 后映射/窗口清理和 handle 永不复用测试；新增 256 并发上限溢出测试。
- `tests/test_executor.cpp`：改为断言 `AuditEvent.type/user_id/context_handle/pc/detail`，锁定 Fetch deny 与伪解密失败的结构化输出。

#### 行为变化总结
- 新增能力：
  - `Gateway::Release()` 真正释放上下文相关状态，并写入 `GATEWAY_RELEASE`。
  - Gateway 最多允许 256 个并发活跃 `context_handle`；超过上限时拒绝新 load。
  - 审计日志现在是结构化事件，调用方可直接按字段检查，不再依赖字符串拼接格式。
- 失败模式/Trap：
  - 活跃 handle 数达到 256 后，`Gateway::Load()` 抛出 `std::runtime_error`，并记录 `GATEWAY_LOAD_FAIL`，`detail` 含 `gateway_capacity_exceeded`。
  - `Release()` 后，原 handle 的 EWC 查询不再命中窗口，`GetUserIdForHandle()` 返回 `nullopt`；后续新 load 分配新 handle，不回收旧值。
  - `EWC_ILLEGAL_PC` / `DECRYPT_DECODE_FAIL` trap 语义不变，但审计载荷从自由字符串改为固定字段 + `detail` kv。

#### 测试与运行
- 建议/已执行命令：
  - `cmake --build build`
  - `ctest --test-dir build`
- 已通过测试：
  - `sanity`
  - `isa_assembler`
  - `executor`
  - `gateway`

#### 已知限制 & 下一步建议
- 当前 `GATEWAY_LOAD_FAIL` 在解析失败和容量溢出路径下统一写 `user_id=0`、`pc=0`；如果后续需要更强可观测性，可以在 parse 前后分阶段携带更多上下文。
- `detail` 仍是 kv 字符串而非独立 map，适合当前原型与 stdout 输出；若后续引入 NDJSON，可再升级为稳定序列化器。

### Push 状态
**已推送：** 2026-03-12
**分支：** issue-5-gateway-secureir
**提交：** issue 5: Gateway + SecureIR L0 + structured audit

## Issue 6A：统一内存模型 + Executor 取指改造（方案确认）
**日期：** 2026-03-23
**分支：** issue-6-kernel-emu-cross-user
**状态：** 方案已确认

### 初始需求（用户提出）
- 当前 Executor 用对象索引取指（`program.code[index]`），导致跨用户跳转在 EWC 安全检查之前就触发数组越界，无法演示真正的硬件安全拦截链。
- 引入统一内存模型：代码以密文字节形式写入 `code_memory[]`，Executor 从字节数组取指。
- 代码存储（`code_memory`）和数据存储（`data_memory`）物理分离，建模硬件级 W⊕X 不变量。

### 额外补充/优化需求（对话新增）
- `region_base_va` 放在 `ExecuteOptions` 中传入。
- 提供辅助函数 `BuildCodeMemory`（`CipherProgram → vector<uint8_t>`），供 demo 和兼容 wrapper 使用。
- FetchPacket 诊断消息中 `index` 替换为 `pc`。
- 删除 `CiphertextSizeMismatch` 测试，校验责任交给上层调用方。
- VA 步长 4（kInstrBytes）、物理步长 32（kCipherUnitBytes），映射封装在 FetchStage 内部。

### Coding 前最终方案
#### 文件与模块清单
- 修改：
  - `include/security/code_codec.hpp`（新增 `kCipherUnitBytes`、`SerializeCipherUnit`、`DeserializeCipherUnit`、`BuildCodeMemory`）
  - `src/security/code_codec.cpp`（实现上述接口）
  - `include/core/executor.hpp`（`ExecuteOptions` 新增 `region_base_va`/`code_memory`/`code_memory_size`，移除 `ciphertext`）
  - `src/core/executor.cpp`（FetchStage 改造、FetchPacket 精简、诊断消息调整、兼容 wrapper 内部构建 code_memory）
  - `tests/test_executor.cpp`（密文测试适配新接口，删除 `CiphertextSizeMismatch` 测试）
  - `tests/test_gateway.cpp`（1 处调用点适配）
  - `demos/normal/demo_normal.cpp`（用 `BuildCodeMemory` 构建字节数组）

#### 语义接口（WHAT，签名由 Codex 决定）
- **code_codec 序列化**：`CipherInstrUnit` ↔ 32 字节（payload 24B + key_check 4B + tag 4B），little-endian。
- **code_codec 辅助函数**：接收 `CipherProgram`，输出 `vector<uint8_t>`，逐个序列化连续写入。
- **FetchStage**：从 `code_memory[byte_offset]` 读 32 字节 → `DeserializeCipherUnit` → `DecryptInstr` → `Instr`。明文路径移除，始终走密文。
- **ExecuteOptions**：新增 `region_base_va`（uint64_t）、`code_memory`（const uint8_t*）、`code_memory_size`（size_t）；移除 `ciphertext`。
- **FetchPacket**：移除 `index`/`has_cipher`/`Instr instr`；始终填充 `CipherInstrUnit cipher`。
- **兼容 wrapper**：旧 4 参数 `ExecuteProgram` 内部用 `key_id=0` 加密 + `BuildCodeMemory` + 调用新路径。

#### 语义/不变量（必须测死，后续不得漂移）
- 取指流程顺序固定：对齐检查 → VA→物理偏移计算 → 边界检查 → EWC query → 读 32 字节 → 反序列化 → 解密。
- PC bounds check 降为内存可寻址性检查（`pc` 对齐且物理偏移在 `code_memory` 范围内），安全语义全部由 EWC 承担。
- 代码/数据分离（W⊕X）：FetchStage 只读 `code_memory`，LD/ST 只访问 `data_memory`，无交叉访问路径。
- VA→物理偏移映射：`byte_offset = (pc - region_base_va) / kInstrBytes * kCipherUnitBytes`，封装在 FetchStage 内部。
- 兼容 wrapper 中 `key_id=0` 加密路径无特殊分支，行为自洽。
- 向后兼容：Issue 0-5 旧测试通过兼容 wrapper 全部无 regression。

#### 测试计划（测试名 + 核心断言点）
- `SerializeDeserializeCipherUnit_Roundtrip`：序列化 → 反序列化 → 与原始一致。
- `FetchStage_FromCodeMemory_RunsToHalt`：code_memory 取指 → 解密 → 正常执行到 HALT。
- `FetchStage_MisalignedPc_TrapsInvalidPc`：未对齐 PC → INVALID_PC。
- `FetchStage_OutOfBoundsPc_TrapsInvalidPc`：越界 PC → INVALID_PC。
- 删除 `Execute_PseudoDecrypt_CiphertextSizeMismatch_Throws`。
- 兼容回归：Issue 0-5 全部旧测试继续通过（通过 4 参数 wrapper）。
- `demo_normal` 使用新取指路径，行为与改造前一致。

#### 验收命令（仅列出，将由用户批准后执行）
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build`
- `./build/demo_normal`

#### 设计决策记录

**D-1：密文字节存储**
- 决策：`code_memory[]` 存 `CipherInstrUnit` 序列化字节（32B/指令）。
- 原因：可演示多层防御——EWC 拦截 + 错误 key 解密失败。

**D-2：代码/数据存储分离**
- 决策：`code_memory`（FetchStage 只读）与 `data_memory`（LD/ST 读写）物理分离。
- 原因：改动最小；天然建模 W⊕X 不变量。

**D-3：PC bounds check 降级**
- 决策：FetchStage 的 PC 检查从"安全检查"降为"内存可寻址性检查"，安全语义全部由 EWC 承担。
- 原因：解决跨用户跳转在 EWC 检查前触发数组越界的核心问题。

**D-4：VA 步长 4，物理步长 32**
- 决策：映射公式封装在 FetchStage 内部，EWC 和 Executor 上层无感知。
- 原因：toy ISA 未做指令编码压缩，安全语义不受影响。

**D-5：保留旧 wrapper 兼容**
- 决策：4 参数 `ExecuteProgram` 作为兼容层，内部用 `key_id=0` 加密 + 序列化。
- 原因：避免同时迁移所有测试调用点，模糊 regression 来源。

**D-6：删除 CiphertextSizeMismatch 测试**
- 决策：移除 `ciphertext` 字段后，长度校验责任交给上层调用方。
- 原因：`code_memory` 是调用方构建的字节数组，executor 只需确保取指不越界。

**D-7：region_base_va 通过 ExecuteOptions 传入**
- 决策：`ExecuteOptions` 新增 `region_base_va` 字段。
- 原因：FetchStage 需要做 VA→物理偏移计算，当前单 region 场景下等于程序 `base_va`。

#### Codex 可行性检查结果摘要
- `program.code[index]` 在 executor.cpp 中只有 1 处直接取指（L171），间接依赖 `program.code.size()` 有多处。
- `FetchPacket.index` 在 FetchStage 外只有 3 处 `pc_relative_overflow` 诊断引用。
- `ExecuteOptions.ciphertext` 移除后影响：test_executor.cpp 4 处 + test_gateway.cpp 1 处 + demo_normal.cpp 2 处。
- 序列化接口无命名冲突：现有 `SerializeInstr`/`DeserializeInstr` 在匿名命名空间内。
- `key_id=0` 无特殊分支，兼容 wrapper 路径自洽。
- `program.base_va` 在 FetchStage 外仍有 `pc_relative_overflow` 诊断和 wrapper 中的 EWC window 生成依赖。

### 实现复盘
**状态：** 已实现（已推送）
**提交：** 0aba535
**远端：** origin/issue-6a-unified-code-memory（2026-03-24 推送）

#### 改动摘要（diff 风格）
- `.gitignore | 2 +`
- `CLAUDE.md | 70 +++++++++++--------`
- `demos/normal/demo_normal.cpp | 15 +++--`
- `docs/project_log.md | 148 ++++++++++++++++++++++++++++++++++++++++`
- `include/core/executor.hpp | 12 ++--`
- `include/security/code_codec.hpp | 5 ++`
- `src/core/executor.cpp | 128 +++++++++++++--------------------------`
- `src/security/code_codec.cpp | 57 ++++++++++++++++++`
- `tests/test_executor.cpp | 130 ++++++++++++++++++++++++----------------`
- `tests/test_gateway.cpp | 8 ++-`
- `docs/Demo_Claim_Boundary_v3.1.md (new)`
- `docs/system_design_v4.md (new)`
- `12 files changed, 2958 insertions(+), 181 deletions(-)`

#### 关键文件逐条复盘
- `include/security/code_codec.hpp`：新增 `kCipherUnitBytes`、`SerializeCipherUnit`、`DeserializeCipherUnit`、`BuildCodeMemory` 声明，统一 code image 的字节级表示。
- `src/security/code_codec.cpp`：实现密文单元序列化/反序列化与 `BuildCodeMemory`，把 `CipherProgram` 落成连续 `code_memory` 字节数组。
- `include/core/executor.hpp`：`ExecuteOptions` 新增 `region_base_va`、`code_memory`、`code_memory_size`，移除 `ciphertext`；`ExecuteProgram` 签名收敛为 `(entry_pc, options)`，删除 4 参数兼容 wrapper。
- `src/core/executor.cpp`：FetchStage 改为按 `region_base_va + code_memory` 映射取 32B 密文，先做地址/边界检查，再过 EWC，再反序列化并解密；同时删除明文取指路径和 options 版本中的 `program` 死参数。
- `tests/test_executor.cpp`：新增 `RunAllowAll` helper，下沉 allow-all 逻辑；所有执行器测试改走 `BuildCodeMemory + ExecuteOptions` 新接口，并删除 `CiphertextSizeMismatch` 旧测试。
- `tests/test_gateway.cpp`：适配新的 `ExecuteOptions` 字段和 `ExecuteProgram` 调用签名，维持 gateway 审计路径回归。
- `demos/normal/demo_normal.cpp`：demo 改为显式构建 `code_memory` 并调用收敛后的执行接口，继续覆盖 allow 与 wrong-key 两条演示路径。

#### 行为变化总结
- 新增能力：
  - Fetch 统一从 `code_memory` 字节数组取指，明文对象数组路径已移除，执行器始终走密文反序列化 + 解密链路。
  - `code_codec` 现在可把 `CipherProgram` 稳定转换为连续字节内存，demo、测试与执行器共享同一套表示。
  - `ExecuteProgram` 调用面收敛为 `(entry_pc, options)`，allow-all 兼容逻辑不再留在生产接口，而是下沉到测试 helper `RunAllowAll`。
- 失败模式/Trap：
  - 配置错误改为在执行前直接抛 `std::runtime_error`：`code_memory_not_configured`、`code_memory_empty`、`code_memory_size_not_aligned`。
  - Fetch 的 `INVALID_PC` 诊断字段改为围绕 `pc/base_va/code_memory_size`；错误 key 或被篡改密文继续落到 `DECRYPT_DECODE_FAIL`。

#### 测试与运行
- 建议/已执行命令：
  - `git diff --stat`
  - `git diff --stat -- demos/normal/demo_normal.cpp include/core/executor.hpp include/security/code_codec.hpp src/core/executor.cpp src/security/code_codec.cpp tests/test_executor.cpp tests/test_gateway.cpp`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`
  - `./build/demo_normal`
- 已通过测试：
  - `sanity`
  - `isa_assembler`
  - `executor`
  - `gateway`
  - `demo_normal`：allow 路径输出 `HALT`，wrong-key 路径输出 `DECRYPT_DECODE_FAIL`

#### 已知限制 & 下一步建议
- 当前 `region_base_va -> code_memory` 仍是单连续 region 映射，尚未覆盖多段 code image 或稀疏装载场景。
- `RunAllowAll` 只保留在测试侧，后续若还需要无安全硬件的便捷运行入口，应由上层显式决定是否补新的 helper，而不是回退到 executor 对外兼容 wrapper。

## Issue 6B：内核进程模型（方案确认）
**日期：** 2026-03-24
**分支：** issue-6b-kernel-process-model
**状态：** 方案已确认

### 初始需求（用户提出）
- 在 Gateway 之上构建内核进程管理层，为后续 cross-user demo（6C）提供基础设施。
- 实现进程上下文表、context_switch、审计事件。
- 不修改 Executor，Executor 集成留给 6C。

### 额外补充/优化需求（对话新增）
- `ProcessContext` 不包含 `program_name`（真实硬件无此概念）。
- code_memory 所有权归硬件层（SecurityHardware），不归内核 ProcessContext——内核不可信，不应持有受保护的数据。
- ProcessContext 只持有 {handle, user_id, base_va}——内核合理需要的调度和地址空间管理信息。
- base_va 由内核从 SecureIR JSON 明文头提取（DP-9：布局元数据明文）。
- Gateway 不新增持久存储——Gateway 是"检查+配置"硬件，不是存储器。
- `ContextSwitch` 幂等：切换到已 active 的同一 handle 静默成功，仍发 `CTX_SWITCH` 审计。
- `LoadProcess` 接收 (secureir_json, code_memory) 两参数，code_memory 未嵌入 SecureIR 是已知简化（完整 SecureIR 包属于 I-1 范畴）。

### Coding 前最终方案
#### 文件与模块清单
- 新增：
  - `include/kernel/process.hpp`（`ProcessContext` + `KernelProcessTable` 声明）
  - `src/kernel/process.cpp`（实现）
  - `tests/test_kernel_process.cpp`（单元测试）
- 修改：
  - `include/security/hardware.hpp`（SecurityHardware 新增 per-handle code region 存储）
  - `CMakeLists.txt`（接入新源码 + 测试）

#### 语义接口（WHAT，签名由 Codex 决定）
- **SecurityHardware 扩展**：新增 per-handle code region 存储（code_memory 字节 + base_va），提供存储和查询方法。模拟硬件保护的物理内存。
- **`ProcessContext`**：持有 context_handle、user_id、base_va。不持有 code_memory（code_memory 归硬件层）。代表内核视角的进程信息。
- **`KernelProcessTable`**：
  - 构造时接收 `Gateway&`、`SecurityHardware&`、`AuditCollector&`。
  - `LoadProcess`：接收 SecureIR JSON + code_memory（move 语义）→ 从 JSON 明文头提取 user_id 和 base_va → 把 code_memory move 给 SecurityHardware → 调用 `gateway.Load(json)` 获得 handle → 存储 ProcessContext{handle, user_id, base_va} → 返回 handle。
  - `ReleaseProcess`：调用 `gateway.Release(handle)` → 从 SecurityHardware 移除 code region → 从进程表移除；若为 active，重置 active 为"无"。
  - `ContextSwitch`：验证 handle 在进程表中存在 → 更新 active handle → 通过 AuditCollector 发出 `CTX_SWITCH` 审计事件（含 user_id + handle）。幂等：同一 handle 重复切换静默成功，仍发审计。
  - `GetActiveProcess`：返回当前 active 进程的 ProcessContext 指针（无 active 时返回 nullptr）。
  - `GetProcess`：按 handle 查询特定进程（不存在返回 nullptr）。
- **namespace**：`sim::kernel`。

#### 语义/不变量（必须测死，后续不得漂移）
- `LoadProcess` 成功后，返回的 handle 在进程表中可查，`GetProcess(handle)` 返回的 user_id 和 base_va 与 SecureIR JSON 中的值一致。
- `LoadProcess` 成功后，code_memory 存储在 SecurityHardware 中，可通过 handle 查询到，内容与输入一致。
- `ContextSwitch` 仅接受进程表中存在的 handle，否则报错。
- 每次 `ContextSwitch` 产生恰好一条 `CTX_SWITCH` 审计事件，包含目标 handle 和对应 user_id。
- `ReleaseProcess` 后该 handle 从进程表消失，对应 code region 从 SecurityHardware 移除；若为 active，active 重置为"无"。
- Gateway 错误（JSON 解析失败、签名无效等）透传给调用方，进程表和 SecurityHardware 不产生脏状态。
- 信任边界：内核层（ProcessContext）不持有 code_memory；code_memory 只存在于硬件层（SecurityHardware）。

#### 测试计划（测试名 + 核心断言点）
- `LoadProcess_Success_ProcessQueryable`：合法输入 → handle 有效 → GetProcess 返回正确 user_id、base_va。
- `LoadProcess_Success_CodeMemoryInHardware`：load 后 SecurityHardware 可通过 handle 查到 code_memory，内容与输入一致。
- `LoadProcess_MultiUser_HandleIsolation`：Alice 和 Bob 各自 load → handle 不同 → 各自 GetProcess 返回各自 user_id。
- `ContextSwitch_UpdatesActive_EmitsAudit`：切换 → GetActiveProcess 返回目标进程 → 审计链含 CTX_SWITCH。
- `ContextSwitch_InvalidHandle_Throws`：无效 handle → 异常 → active 不变。
- `ContextSwitch_Idempotent`：同一 handle 连续切换两次 → 成功 → 两条 CTX_SWITCH 审计。
- `ReleaseProcess_RemovesFromTable`：release → GetProcess 返回 nullptr。
- `ReleaseProcess_ActiveHandle_ResetsActive`：release active handle → GetActiveProcess 返回 nullptr。
- `ReleaseProcess_CodeMemoryRemovedFromHardware`：release 后 SecurityHardware 查不到该 handle 的 code region。
- `ReleaseProcess_ThenContextSwitch_Throws`：release 后切换 → 异常。
- `LoadProcess_GatewayError_NoDirtyState`：无效 JSON → 异常 → 进程表和 SecurityHardware 无残留。

#### 验收命令（仅列出，将由用户批准后执行）
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build`

#### 设计决策记录

**D-1：namespace sim::kernel**
- 决策：使用 `sim::kernel`，与现有 `src/kernel/scaffold.cpp` 一致。
- 原因：内核层是软件模拟，与 `sim::security`（硬件模拟）语义分离。

**D-2：ContextSwitch 幂等**
- 决策：切换到已 active 的同一 handle 静默成功，仍发 `CTX_SWITCH` 审计。
- 原因：硬件不区分"重复切换"，简化调用方逻辑。

**D-3：ProcessContext 不含 program_name**
- 决策：去掉 `program_name`，只保留功能必需字段。
- 原因：真实硬件无 program_name 概念，硬件只认 handle/user_id/密钥/地址。

**D-4：code_memory 归硬件层，不归内核**
- 决策：code_memory 存储在 SecurityHardware 中，ProcessContext 不持有。
- 原因：内核不可信，不应持有受保护的数据。code_memory 是密文，模拟硬件保护的物理内存。

**D-5：base_va 归内核持有**
- 决策：base_va 放在 ProcessContext 中。
- 原因：内核需要管理虚拟地址空间映射（页表），base_va 是 DP-9/DP-10 允许内核知道的布局信息。

**D-6：Gateway 不新增持久存储**
- 决策：不向 Gateway 添加 GetBaseVaForHandle 等新 getter。
- 原因：Gateway 是"检查+配置"硬件，处理完 SecureIR 后不应持续存储程序元数据。

**D-7：内核从 JSON 明文头提取 metadata**
- 决策：KernelProcessTable::LoadProcess 内部做轻量 JSON 提取获得 user_id 和 base_va。
- 原因：DP-9 规定 SecureIR 布局元数据为明文，内核可读；避免依赖 Gateway 暴露额外 getter。

**D-8：两参数 LoadProcess 是已知简化**
- 决策：LoadProcess(json, code_memory) 接收两个分离的参数。
- 原因：完整 SecureIR 包（metadata + 加密代码捆绑）属于 I-1 范畴，6B 不做。

**D-9：不修改 Executor 和 demo**
- 决策：6B 只新增内核层 + 扩展 SecurityHardware，不改动 Executor 和 demo_normal。
- 原因：Executor 集成和 demo 改造属于 6C 范围。

#### Codex 可行性检查结果摘要
- `Gateway::GetUserIdForHandle()` 可在 Load 后查 user_id（但 D-7 决定内核自行从 JSON 提取，不依赖此 getter）。
- `sim::kernel` namespace 和 `include/kernel/` 路径无命名冲突。
- `src/kernel/scaffold.cpp` 已在 `simulator_core` 静态库中编译，新增 `src/kernel/process.cpp` 同样接入即可。
- `EwcTable::SetWindows()` 已在 `Gateway::Load()` 内调用，KernelProcessTable 不需要直接操作 EWC。
- `include/` 已设为 public include dir，新增 `include/kernel/process.hpp` 直接可用。
- `AuditCollector::LogEvent()` 是 public，`sim::kernel` 可跨 namespace 调用。

### 实现复盘
**状态：** 已实现（已推送）
**提交：** 88e64e5
**远端：** origin/issue-6b-kernel-process-model（2026-03-24 推送）

#### 改动摘要（diff 风格）
- `CMakeLists.txt | 12 ++++++++++++`
- `include/security/hardware.hpp | 42 ++++++++++++++++++++++++++++++++++++++++++`
- `include/kernel/process.hpp (new) | 41 +++++++++++++++++++++++++++++++++`
- `src/kernel/process.cpp (new) | 366 ++++++++++++++++++++++++++++++++++++`
- `tests/test_kernel_process.cpp (new) | 269 +++++++++++++++++++++++++++++`
- `5 files changed, 730 insertions(+)`

#### 关键文件逐条复盘
- `include/security/hardware.hpp`：新增 `CodeRegion` 和 per-handle code region 存储接口 `StoreCodeRegion` / `GetCodeRegion` / `RemoveCodeRegion`，把 code_memory 所有权下沉到 `SecurityHardware`，强化内核与受保护代码的 trust boundary。
- `include/kernel/process.hpp`：新增 `ProcessContext` 与 `KernelProcessTable` 声明；`ProcessContext` 只保留 `{handle, user_id, base_va}`，不持有 `program_name` 或 code_memory。
- `src/kernel/process.cpp`：实现 `KernelProcessTable` 全部语义；内置最小 JSON parser（沿用 `gateway.cpp` 的轻量模式）从 SecureIR 明文头提取 `user_id/base_va`，并在 `LoadProcess` 失败路径中回滚 `SecurityHardware` 与 `Gateway`，避免脏状态残留。
- `tests/test_kernel_process.cpp`：新增 11 个单元测试，覆盖 load/query、multi-user isolation、`ContextSwitch` 审计、`ReleaseProcess` 清理，以及 `LoadProcess_GatewayError_NoDirtyState` rollback 行为。
- `CMakeLists.txt`：把 `src/kernel/process.cpp` 接入 `simulator_core`，新增 `test_kernel_process` target 并注册到 `ctest`；`demo_normal` 保持不变。

#### 行为变化总结
- 新增能力：
  - 在 `Gateway` 之上新增 `KernelProcessTable`，现在可以 load/release/query process，并维护 active process 与 `CTX_SWITCH` 审计流。
  - `SecurityHardware` 现在可按 `context_handle` 持有 `CodeRegion{base_va, code_memory}`，为后续 6C 的 executor/context switch 集成提供硬件侧代码存储。
  - `LoadProcess` 会先解析 SecureIR 明文头，再调用 `Gateway` 建立 handle，并把 code_memory 移交给硬件层；内核侧只保留最小调度元数据。
- 失败模式/Trap：
  - `ContextSwitch` 对不存在的 handle 抛 `std::runtime_error("kernel_process_invalid_handle ...")`，active process 不会被污染。
  - `LoadProcess` 遇到 JSON parse/field 校验失败时抛 `kernel_process_json_parse_error`、`kernel_process_missing_field` 或 `kernel_process_invalid_field`；若 `Gateway::Load()` 或后续插入过程失败，会回滚 `SecurityHardware` code region、`Gateway` handle 与进程表状态。
  - `demo_normal` 与 executor 行为本次未改动；6B 只增加 kernel process model，不改变现有 demo 路径。

#### 测试与运行
- 建议/已执行命令：
  - `git diff --stat -- CMakeLists.txt include/security/hardware.hpp`
  - `git diff --no-index --stat -- /dev/null include/kernel/process.hpp`
  - `git diff --no-index --stat -- /dev/null src/kernel/process.cpp`
  - `git diff --no-index --stat -- /dev/null tests/test_kernel_process.cpp`
  - `ctest --test-dir build --output-on-failure`
  - `./build/test_kernel_process`
- 已通过测试：
  - `ctest`：5/5 通过
  - `sanity`
  - `isa_assembler`
  - `executor`
  - `gateway`
  - `kernel_process`
  - `kernel_process` 内新增 11 个 test cases 全部通过：
    `LoadProcess_Success_ProcessQueryable`、`LoadProcess_Success_CodeMemoryInHardware`、`LoadProcess_MultiUser_HandleIsolation`、`ContextSwitch_UpdatesActive_EmitsAudit`、`ContextSwitch_InvalidHandle_Throws`、`ContextSwitch_Idempotent`、`ReleaseProcess_RemovesFromTable`、`ReleaseProcess_ActiveHandle_ResetsActive`、`ReleaseProcess_CodeMemoryRemovedFromHardware`、`ReleaseProcess_ThenContextSwitch_Throws`、`LoadProcess_GatewayError_NoDirtyState`

#### 已知限制 & 下一步建议
- `Gateway` 目前仍持有 `handle_to_user_` 映射；这是既有简化，不是 6B 新引入的问题，但意味着 user metadata 仍有一份存放在 gateway 层。
- 当前 `KernelProcessTable` 只负责进程表、硬件 code region 存储与 `CTX_SWITCH` 审计；尚未把 active handle 进一步接到 executor fetch/runtime 路径，这部分留给 6C。

## Issue 6C：cross-user 隔离 demo（方案确认）
**日期：** 2026-03-24
**分支：** issue-6c-cross-user-demo
**状态：** 方案已确认

### 初始需求（用户提出）
- 利用 6B 的 KernelProcessTable 构建 cross-user 隔离演示（demo_cross_user），包含 Executor 集成。
- 展示两种攻击路径被硬件拦截：用户态代码跳转 + 恶意 OS 设置 entry_pc。

### 额外补充/优化需求（对话新增）
- Executor 集成采用方案 C：Executor 接收 `SecurityHardware*`，fetch stage 内部从硬件闭环获取安全状态，OS 无法伪造。
- `SecurityHardware` 新增 active context register（`SetActiveHandle`/`GetActiveHandle`/`ClearActiveHandle`），`SetActiveHandle` 必须做硬件侧校验（handle 必须有对应 CodeRegion）。
- `KernelProcessTable::ContextSwitch` 同步写硬件 active context register，模拟"OS 执行特权指令写硬件寄存器"。
- `ReleaseProcess` 若释放的是 active handle，同步调用 `ClearActiveHandle`。
- Demo 包含三个 case：Alice 正常执行、Bob JMP 攻击、恶意 OS entry_pc 攻击。
- 恶意 OS 场景的安全分析：OS 写 active context register 等价于调度权（所有架构中 OS 都拥有），安全性由硬件校验 handle 有效性 + 硬件强制执行隔离边界保证。

### Coding 前最终方案
#### 文件与模块清单
- 修改：
  - `include/security/hardware.hpp`（新增 active context register 接口）
  - `src/kernel/process.cpp`（ContextSwitch 写硬件 + ReleaseProcess 清硬件）
  - `include/core/executor.hpp`（ExecuteOptions 新增 `hardware` 字段）
  - `src/core/executor.cpp`（fetch stage 新增硬件查询路径）
  - `CMakeLists.txt`（新增 demo_cross_user target）
- 新增：
  - `demos/cross_user/demo_cross_user.cpp`（cross-user demo）

#### 语义接口（WHAT，签名由 Codex 决定）
- **SecurityHardware 扩展**：
  - `SetActiveHandle(handle)`：校验 `GetCodeRegion(handle) != nullptr`，通过则设置 active，否则抛异常。模拟片上 active context register 的写入。
  - `GetActiveHandle()`：返回当前 active handle（无 active 时返回空）。
  - `ClearActiveHandle()`：清除 active（供 ReleaseProcess 调用）。
- **KernelProcessTable 调整**：
  - `ContextSwitch`：现有逻辑不变，新增调用 `hardware_.SetActiveHandle(handle)`。
  - `ReleaseProcess`：若释放的是 active handle，新增调用 `hardware_.ClearActiveHandle()`。
- **Executor 集成**：
  - `ExecuteOptions` 新增 `SecurityHardware* hardware = nullptr`。
  - 当 `hardware` 非空时，`ExecuteProgram` 内部从硬件获取：active handle（`GetActiveHandle`）→ code region（`GetCodeRegion`）→ EWC（`GetEwcTable`）→ audit（`GetAuditCollector`）。忽略手动传入的 ewc/audit/code_memory/context_handle 字段。
  - 当 `hardware` 为空时，完全走现有手动字段路径（向后兼容）。
- **demo_cross_user**：
  - Setup：创建硬件栈 + KernelProcessTable，Load Alice @ 0x1000、Load Bob @ 0x2000。
  - Case A：ContextSwitch(Alice) → ExecuteProgram(0x1000) → 预期 HALT。
  - Case B：ContextSwitch(Bob) → ExecuteProgram(0x2000) → Bob 程序含 JMP 0x1000 → 预期 EWC_ILLEGAL_PC。
  - Case C：ContextSwitch(Bob) → ExecuteProgram(0x1000) → 预期 EWC_ILLEGAL_PC（首条 fetch 即被拦截）。
  - 每个 case 输出 run summary + audit events + context trace。

#### 语义/不变量（必须测死，后续不得漂移）
- `SetActiveHandle` 拒绝无 CodeRegion 的 handle，抛异常；有效 handle 成功设置。
- `ReleaseProcess` 若释放 active handle，硬件侧 active 同步被清除，`GetActiveHandle` 返回空。
- `hardware` 非空时，Executor 只从 SecurityHardware 获取安全状态，不使用手动字段。
- `hardware` 为空时，Executor 行为与现有完全一致（向后兼容）。
- Executor 硬件路径下无 active handle → 合理错误（trap 或异常）。
- Case B/C 中 Bob 的 handle 对应的 EWC windows 不覆盖 0x1000 → EWC_ILLEGAL_PC。
- 每次 ContextSwitch 产生 CTX_SWITCH 审计，EWC 拦截产生 EWC_ILLEGAL_PC 审计。

#### 测试计划（测试名 + 核心断言点）
- `SetActiveHandle_Valid_Succeeds`：有效 handle → GetActiveHandle 返回该 handle。
- `SetActiveHandle_InvalidHandle_Throws`：无 CodeRegion 的 handle → 异常。
- `ReleaseProcess_ActiveHandle_ClearsHardwareActive`：release active handle → GetActiveHandle 返回空。
- `Executor_HardwarePath_NormalExecution`：hardware 非空 + 有效 active handle → 正常执行，功能等价手动路径。
- `Executor_HardwarePath_NoActiveHandle_Error`：hardware 非空 + 无 active handle → 合理错误。
- `Demo_CaseA_AliceNormal`：Alice 正常执行 → HALT。
- `Demo_CaseB_BobJmpAttack`：Bob JMP 到 Alice 地址 → EWC_ILLEGAL_PC。
- `Demo_CaseC_MaliciousOS`：entry_pc 设为 Alice 地址但 active 是 Bob → EWC_ILLEGAL_PC。

#### 验收命令（仅列出，将由用户批准后执行）
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build`
- `./build/demo_cross_user`

#### 设计决策记录

**D-1：方案 C — Executor 从硬件闭环获取安全状态**
- 决策：`ExecuteOptions` 新增 `SecurityHardware*`，非空时 Executor 内部从硬件读取全部安全参数。
- 原因：模拟真实硬件中 CPU pipeline 直接访问片上安全硬件的语义；防止 OS 伪造 code_memory 等安全参数。

**D-2：SecurityHardware 新增 active context register**
- 决策：active handle 存储在硬件层而非仅在内核。
- 原因：active handle 在真实硬件中是片上寄存器，OS 可以写（调度权），CPU 直接读。存在内核侧不符合 OS-untrusted 信任模型。

**D-3：SetActiveHandle 硬件侧校验**
- 决策：SetActiveHandle 必须校验 handle 有效性（有对应 CodeRegion）。
- 原因：防止恶意 OS 设置已 release 或从未 load 的 handle，避免 use-after-free 或未初始化访问。

**D-4：OS 写 active context register 的安全性**
- 决策：允许 OS 自由写（只要 handle 有效），不做额外权限控制。
- 原因：OS 拥有调度权是所有架构的共识（SGX/TrustZone 同理）。安全性由硬件强制执行隔离边界保证，而非限制 OS 的调度能力。

**D-5：向后兼容**
- 决策：`hardware == nullptr` 时完全走旧路径，demo_normal 和已有测试不改动。
- 原因：6C 只新增硬件路径，不破坏已有功能。

**D-6：demo 三个 case 展示两种攻击维度**
- 决策：Case A（正常执行）+ Case B（用户态 JMP 攻击）+ Case C（恶意 OS entry_pc 攻击）。
- 原因：同时展示 OS-untrusted 信任模型的两个维度——用户态代码不可越界 + OS 不可伪造执行入口。

#### Codex 可行性检查结果摘要
- `ExecuteOptions` 新增 `SecurityHardware*` 无命名冲突，include `security/hardware.hpp` 不会循环依赖。
- `FetchStage` 签名需调整以接收硬件路径参数，或在 `ExecuteProgram` 入口处解析后传入局部变量。
- `GetCodeRegion` 返回 `CodeRegion*`，`GetEwcTable()`/`GetAuditCollector()` 返回引用（需取地址转指针）。
- EWC windows 已按 context_handle 隔离（`unordered_map<ContextHandle, vector<ExecWindow>>`）。
- `ContextSwitch` 只更新内核 `active_handle_`，不驱动 EWC 查询——通过新增硬件 active register 解决。
- 现有测试全部使用"默认构造+逐字段赋值"，新增尾部字段不破坏兼容性。

### 实现复盘
**状态：** 已实现（未推送）
**提交：** TBD
**远端：** 未推送

#### 改动摘要（diff 风格）
- `include/security/hardware.hpp | active context register + 校验逻辑`
- `include/core/executor.hpp | ExecuteOptions 新增 hardware 字段`
- `src/core/executor.cpp | Executor 硬件路径 + fetch 优先 EWC 检查`
- `src/kernel/process.cpp | ContextSwitch/ReleaseProcess 同步硬件 active handle`
- `demos/cross_user/demo_cross_user.cpp (new) | 3 个 cross-user case`
- `CMakeLists.txt | 新增 demo_cross_user target`
- `tests/test_kernel_process.cpp | 新增 3 个硬件 active handle 相关测试`
- `tests/test_executor.cpp | 新增 2 个 Executor 硬件路径测试`

#### 关键文件逐条复盘
- `include/security/hardware.hpp`：新增 active context register 接口 `SetActiveHandle` / `GetActiveHandle` / `ClearActiveHandle`，并在硬件侧校验 handle 必须存在对应 `CodeRegion`，避免 OS 写入无效或已释放 handle。
- `include/core/executor.hpp`：为 `ExecuteOptions` 增加 `SecurityHardware* hardware`，为 Executor 提供从硬件闭环读取安全状态的入口，同时保留 `hardware == nullptr` 的旧路径兼容性。
- `src/core/executor.cpp`：实现 Executor 硬件路径；当 `hardware` 非空时，从 `SecurityHardware` 解析 active handle、code region、EWC 和 audit，并在 fetch 阶段先做 EWC 检查、后做 bounds 检查，确保跨上下文访问优先得到 `EWC_ILLEGAL_PC`。
- `src/kernel/process.cpp`：`ContextSwitch` 先调用 `hardware_.SetActiveHandle()` 再更新内核状态与审计，修复切换提交顺序；`ReleaseProcess` 在释放 active handle 时同步 `ClearActiveHandle()`，避免硬件残留脏状态。
- `demos/cross_user/demo_cross_user.cpp`：新增 cross-user demo，覆盖 Alice 正常执行、Bob 通过 `JMP` 越权访问、恶意 OS 伪造 `entry_pc` 三个场景，并输出 trap、audit stream 和 context_handle 切换轨迹。
- `CMakeLists.txt`：注册 `demo_cross_user` target，纳入现有构建流程。
- `tests/test_kernel_process.cpp`：新增 `SetActiveHandle_Valid_Succeeds`、`SetActiveHandle_InvalidHandle_Throws`、`ReleaseProcess_ActiveHandle_ClearsHardwareActive`，补足 active register 的行为约束测试。
- `tests/test_executor.cpp`：新增 `Executor_HardwarePath_NormalExecution` 与 `Executor_HardwarePath_NoActiveHandle_Error`，覆盖硬件路径正常执行与缺失 active handle 的失败场景。

#### 行为变化总结
- 新增能力：
  - `SecurityHardware` 现在显式维护 active context register，CPU/Executor 可直接从硬件读取当前活动上下文。
  - Executor 在 `hardware` 路径下可完全脱离手动注入的安全状态参数，由硬件统一提供 active handle、代码区、EWC 与审计对象。
  - 新增 `demo_cross_user`，可直接演示正常执行、用户态跨用户跳转攻击、恶意 OS 入口伪造三种场景。
- 失败模式/Trap：
  - 对无 `CodeRegion` 的 handle 调用 `SetActiveHandle` 会抛异常，阻止无效 active register 写入。
  - `hardware` 路径下若无 active handle，Executor 会报错，避免在未绑定上下文时继续执行。
  - Bob 上下文访问 `0x1000` 时，无论通过 `JMP` 还是恶意 `entry_pc` 注入，fetch 首先触发 EWC 拦截并记录 `EWC_ILLEGAL_PC`。
  - `demo_normal` 保持原有路径，`hardware == nullptr` 时行为与改动前一致。

#### 测试与运行
- 建议/已执行命令：
  - `ctest --test-dir build`
  - `./build/demo_cross_user`
- 已通过测试：
  - `ctest`：5/5 通过
  - `demo_cross_user`：Case A = `HALT`，Case B = `EWC_ILLEGAL_PC`，Case C = `EWC_ILLEGAL_PC`
  - `demo_normal`：保持可运行，向后兼容未回归
  - `test_kernel_process`：共 14 个 test cases 通过（原有 11 个 + 本次新增 3 个）
  - `test_executor`：新增 2 个硬件路径测试通过
  - 评审回合修复情况：Round 1 发现的 `ContextSwitch` 提交顺序问题与缺失测试已在 Round 2 修复，最终全绿

#### 已知限制 & 下一步建议
- `demo_cross_user` 已覆盖两类攻击维度，但仍属于最小原型验证，后续若继续扩展，可增加更多上下文切换与多进程交错执行场景。

**推送状态：** 已推送
**提交：** 5b9d299
**远端：** origin/issue-6c-cross-user-demo（2026-03-24 推送）

## Issue 7：PVT 模拟器 + secure_page_load（方案确认）
**日期：** 2026-03-25
**分支：** issue-7-pvt
**状态：** 方案已确认

### 初始需求（用户提出）
- 实现 PVT（Physical page Verification Table）模拟器，OS 通过 `secure_page_load` 注册安全页面时，硬件侧 PVT 从 EWC 读取执行窗信息做反向校验。
- 确保 OS 不能伪造页面归属/映射。

### 额外补充/优化需求（对话新增）
- `RegisterPage` 签名为 `RegisterPage(handle, va, page_type)`，**不含 owner_user_id 参数**。PVT 内部从 EWC 查询 owner（模拟硬件互联，方式 X），与 system_design_v4 流程图 10.1 一致：OS 提供 PA + 密文 + MAC，PVT 自己从 EWC 读 owner。
- EWC 的 `ExecWindow` / `EwcQueryResult` 新增 `permissions` 字段（方案 a），支持 page_type 与 permissions 双重校验，与 v4 §3.4 和 §9.4 一致。
- 物理页分配器预留接口，当前实现从 VA 派生（`va / PAGE_SIZE`），利用当前模型"地址空间互不重叠"简化项。
- 不做独立 demo，仅单测覆盖，demo 留到 Issue 9。

### Coding 前最终方案
#### 文件与模块清单
- 新增：
  - `include/security/pvt.hpp`（PvtTable 类 + PvtEntry 结构体）
  - `src/security/pvt.cpp`（RegisterPage / RemovePage / LookupPage 实现）
  - `tests/test_pvt.cpp`（PVT 单元测试）
- 修改：
  - `include/security/ewc.hpp`（ExecWindow 新增 permissions 字段，EwcQueryResult 新增 permissions 字段）
  - `src/security/gateway.cpp`（Gateway 写入窗口时赋值 permissions）
  - `include/security/hardware.hpp`（SecurityHardware 集成 PvtTable 成员 + GetPvtTable 访问器）
  - `include/core/executor.hpp`（TrapReason 新增 PVT_MISMATCH）
  - `src/core/executor.cpp`（TrapReasonToString 新增 PVT_MISMATCH 分支）
  - `src/kernel/process.cpp`（LoadProcess 解析 SecureIR pages 字段，调用 PVT 注册）
  - `CMakeLists.txt`（新增 pvt.cpp 到 simulator_core，新增 test_pvt）

#### 语义接口（WHAT，签名由 Codex 决定）
- **PvtEntry 结构体**：
  - `pa_page_id`：物理页号（当前 = va / PAGE_SIZE）
  - `owner_user_id`：归属用户（PVT 从 EWC 读取）
  - `expected_va`：该 PA 应被映射到的 VA
  - `permissions`：RX / RW / RO
  - `page_type`：CODE / DATA
  - `state`：FREE / ALLOCATED
- **PvtTable**：
  - 构造时接收 `const EwcTable&` 引用（模拟片上硬件互联）
  - `RegisterPage(handle, va, page_type)`：
    1. 通过内部 PageAllocator 获取 pa_page_id（当前 = va / PAGE_SIZE）
    2. 若 page_type == CODE：调用 `ewc_.Query(va, handle)`，检查 `matched_window == true`，从结果读取 `owner_user_id` 和 `permissions`
    3. page_type 与 permissions 双重校验：CODE → 必须 RX，DATA → RW 或 RO
    4. 校验通过 → 写入 PvtEntry，返回成功
    5. 校验失败 → 返回错误 + audit PVT_MISMATCH
  - `RemovePage(pa_page_id)`：移除条目
  - `LookupPage(pa_page_id)`：查询条目
- **ExecWindow / EwcQueryResult 扩展**：
  - 新增 `permissions` 字段（枚举或整数）
  - Gateway 写入窗口时赋值（当前 CODE 窗口 → RX）
- **SecurityHardware 集成**：
  - 新增 `PvtTable pvt_table_` 成员，声明在 `ewc_table_` 之后，构造时传入 `ewc_table_` 引用
  - 新增 `GetPvtTable()` / `const GetPvtTable()` 访问器
- **KernelProcessTable::LoadProcess() 集成**：
  - Gateway 配置 EWC 窗口后，解析 SecureIR 的 pages 字段
  - 逐页调用 `hardware_.GetPvtTable().RegisterPage(handle, va, page_type)`
  - 注册失败 → 回滚（Release handle）+ 抛异常
- **PageAllocator 接口**：
  - 预留 PageAllocator 概念（函数或简单类），当前实现 `va / PAGE_SIZE`
  - 未来可替换为真实物理页分配器

#### 语义/不变量（必须测死，后续不得漂移）
- CODE 页注册时，VA 必须在 handle 的 EWC 窗口内；否则 PVT_MISMATCH。
- CODE 页注册时，PVT 条目的 owner_user_id 来自 EWC 窗口（非 OS 提供）。
- page_type 与 permissions 不一致 → 注册拒绝。
- 注册成功后 LookupPage 返回正确的 PvtEntry。
- RemovePage 后 LookupPage 返回空。
- EWC 的 ExecWindow 新增 permissions 字段不破坏现有测试（当前所有窗口为 CODE/RX）。
- TrapReason::PVT_MISMATCH 新增不破坏现有 TrapReasonToString。
- LoadProcess 在 pages 不为空时执行 PVT 注册；现有 pages=[] 的测试继续通过。

#### 测试计划（测试名 + 核心断言点）
- `RegisterPage_CodePage_ValidWindow_Succeeds`：CODE 页 VA 在 EWC 窗口内 → 注册成功，LookupPage 返回正确条目。
- `RegisterPage_CodePage_OwnerFromEwc`：注册成功后条目的 owner_user_id 等于 EWC 窗口的 owner，非外部提供。
- `RegisterPage_CodePage_NoWindow_Fails`：CODE 页 VA 不在任何 EWC 窗口内 → PVT_MISMATCH。
- `RegisterPage_CodePage_OwnerMismatch_Fails`：VA 在窗口内但 handle 对应的 EWC 窗口 owner 与预期不符 → PVT_MISMATCH（跨用户重映射攻击）。
- `RegisterPage_PageTypePermissionMismatch_Fails`：page_type 与 permissions 不一致 → 拒绝。
- `RemovePage_Succeeds`：注册后移除 → LookupPage 返回空。
- `LoadProcess_WithPages_RegistersPvt`：LoadProcess 带 pages 字段 → PVT 条目已注册。

#### 验收命令（仅列出，将由用户批准后执行）
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build`
- `ctest --test-dir build`

#### 设计决策记录

**D-1：PVT 从 EWC 读取 owner（方式 X，硬件互联）**
- 决策：`RegisterPage` 不接受 OS 提供的 owner_user_id，PVT 内部通过 `ewc_.Query()` 获取。
- 原因：与 system_design_v4 §9.4 和流程图 10.1 一致。OS 是 untrusted，owner 必须从可信源（EWC，由 Gateway 从签名 SecureIR 配置）获取。

**D-2：EWC 新增 permissions 字段**
- 决策：ExecWindow 和 EwcQueryResult 新增 permissions 字段，Gateway 写入时赋值。
- 原因：v4 §9.4 明确写 "PVT 从 EWC 读取 VA 范围、owner、permissions"，双重校验需要 permissions 数据。

**D-3：PageAllocator 接口预留**
- 决策：定义 PageAllocator 接口，当前实现 va / PAGE_SIZE（利用地址空间不重叠简化项）。
- 原因：当前 1:1 VA=PA 模型下 VA 天然唯一；接口预留给未来非简化模型。

**D-4：SecureIR 明文/加密分区**
- 决策：pages 布局信息（va, size, page_type, permissions）属于 SecureIR 明文区域，OS 可读。
- 原因：v4 §5.1 明确定义布局元数据明文（OS 需要用于内存分配和页表配置），内容和策略加密。安全性不依赖布局保密，依赖签名完整性 + PVT 对 EWC 的反向校验。

**D-5：secure_page_load 语义嵌入 LoadProcess**
- 决策：不新增独立 secure_page_load 方法，语义内嵌在 KernelProcessTable::LoadProcess() 流程中。
- 原因：当前 kernel 层实现为 KernelProcessTable（非 KernelEmulator），LoadProcess 已是程序加载的统一入口。secure_page_load 作为加载流程的一个步骤自然嵌入。

#### Codex 可行性检查结果摘要
- PvtTable 构造时接收 const EwcTable& 引用，SecurityHardware 内初始化顺序兼容（ewc_table_ 先于 pvt_table_ 声明）。
- EwcTable::Query(va, handle) 已返回 owner_user_id，PVT 可直接使用，不需新增 EWC 接口。
- TrapReason 枚举可安全追加 PVT_MISMATCH，需同步 TrapReasonToString()。
- LoadProcess 中 Gateway.Load() 之后有明确插入点（当前第 301 行之后），回滚逻辑可覆盖新增失败路径。
- 当前 Gateway 解析 pages 但丢弃内容；LoadProcess 需独立解析 pages 字段。
- CMakeLists.txt 无冲突，直接追加 pvt.cpp 和 test_pvt。
- 当前所有测试 pages 字段为 "[]"，新增 PVT 注册逻辑不影响现有测试路径。

### 实现复盘
**状态：** 已实现
**提交：** 926b26d
**远端：** origin/issue-7-pvt（2026-03-25 推送）

#### 改动摘要（diff 风格）
- `include/security/pvt.hpp (new) | 新增 PvtTable / PvtEntry / PageAllocator 接口与 PVT 相关枚举、结果结构`
- `src/security/pvt.cpp (new) | 实现 PVT 注册、移除、查询、permissions 校验与默认 IdentityMappedPageAllocator`
- `tests/test_pvt.cpp (new) | 新增 PVT 单元测试，覆盖 owner 来源、权限匹配、缺窗拒绝与 owner mismatch`
- `include/security/ewc.hpp | 新增 MemoryPermissions 枚举与 EWC 查询/窗口 permissions 字段`
- `src/security/ewc.cpp | Query 返回 permissions，支撑 PVT 从 EWC 读取 owner + permissions`
- `src/security/gateway.cpp | Gateway 配置 CODE 窗口时写入 RX permissions`
- `include/security/hardware.hpp | SecurityHardware 集成 PvtTable 成员、构造初始化与 GetPvtTable 访问器`
- `include/core/executor.hpp | TrapReason 新增 PVT_MISMATCH`
- `src/core/executor.cpp | TrapReasonToString 新增 PVT_MISMATCH 分支`
- `include/kernel/process.hpp | ProcessContext 新增 pvt_page_ids，支撑 load 失败回滚与 release 清理`
- `src/kernel/process.cpp | LoadProcess 解析 pages、调用 PVT 注册，并在失败路径回滚 PVT / handle 状态`
- `CMakeLists.txt | 接入 pvt.cpp，新增 test_pvt 并纳入 ctest`

#### 关键文件逐条复盘
- `include/security/pvt.hpp`：定义 `PvtPageType`、`PvtEntryState`、`PvtEntry`、`PvtRegisterResult`、`PageAllocator` 和 `PvtTable` 接口；构造函数显式接收 `const EwcTable&`，落实“PVT 从可信 EWC 读取 owner”的硬件互联语义。
- `src/security/pvt.cpp`：实现 `RegisterPage(handle, va, page_type)` 主路径；内部先通过 `PageAllocator` 生成 `pa_page_id`，再查询 `ewc_.Query()` 获取 owner 和 permissions，执行 `page_type` 与 permissions 双重校验（`CODE -> RX`，`DATA -> RW/RO`），失败时 audit `PVT_MISMATCH` 并返回错误；同时提供默认 `IdentityMappedPageAllocator`（`va / kPageSize`）。
- `tests/test_pvt.cpp`：新增 PVT 专项测试，覆盖成功注册、owner 来自 EWC、无窗口拒绝、owner mismatch 攻击拒绝、page_type 与 permissions 不一致拒绝、移除后查空等核心不变量。
- `include/security/ewc.hpp`：为 `ExecWindow` 与 `EwcQueryResult` 增加 `permissions` 字段，并新增 `MemoryPermissions` 枚举，给 PVT 双重校验提供统一数据模型。
- `src/security/ewc.cpp`：更新 EWC 查询返回值，使 `Query()` 在命中窗口时一并返回 permissions，保证 PVT 不需要额外 owner/permission 输入参数。
- `src/security/gateway.cpp`：Gateway 在加载 CODE 执行窗时写入 `RX` permissions，使现有执行窗配置与 PVT 语义一致，并保持 demo / 旧测试兼容。
- `include/security/hardware.hpp`：在 `SecurityHardware` 中加入 `PvtTable pvt_table_`，构造时绑定已有 `ewc_table_` 与 `audit_collector_`，同时暴露 `GetPvtTable()` 访问器供 kernel 加载路径调用。
- `include/core/executor.hpp`：扩展 `TrapReason`，增加 `PVT_MISMATCH` 枚举值，为 secure page load 失败提供统一 trap reason 编码。
- `src/core/executor.cpp`：补齐 `TrapReasonToString()` 的 `PVT_MISMATCH` 分支，避免字符串化遗漏。
- `include/kernel/process.hpp`：在 `ProcessContext` 中加入 `pvt_page_ids`，记录当前进程已注册的 PVT 页，便于失败回滚和后续释放。
- `src/kernel/process.cpp`：在 `LoadProcess()` 中解析 SecureIR `pages` 字段，按页调用 `hardware_.GetPvtTable().RegisterPage()`；若中途失败，回滚已注册页面并释放 handle，避免 PVT / process table 残留脏状态。
- `CMakeLists.txt`：把 `src/security/pvt.cpp` 接入 `simulator_core`，新增 `test_pvt` target 并注册到 `ctest`，使新功能进入标准构建与测试路径。

#### 行为变化总结
- 新增能力：
  - 新增 PVT 模拟器，OS 在 secure page load 路径中只能提交 `handle + va + page_type`，owner 由 PVT 内部从 EWC 读取，不能伪造。
  - 新增 `PageAllocator` 抽象，当前默认 `IdentityMappedPageAllocator` 采用 `va / kPageSize`，为后续真实物理页分配模型预留替换点。
  - `LoadProcess()` 现在会消费 SecureIR 的 `pages` 字段并注册 PVT 条目，形成 Gateway/EWC 到 PVT 的闭环校验链路。
  - EWC 现在显式携带 permissions，PVT 可执行 `page_type` 与 permissions 双重检查，而不仅仅判断窗口是否存在。
- 失败模式/Trap：
  - `RegisterPage()` 若查询不到匹配 EWC 窗口，会拒绝注册并记录 `PVT_MISMATCH` 审计事件。
  - `page_type` 与 permissions 不一致时会拒绝注册；当前规则为 `CODE -> RX`，`DATA -> RW/RO`。
  - 跨用户 owner mismatch 或错误 handle 导致的窗口不匹配会被 PVT 拒绝，不能通过伪造映射绕过。
  - `LoadProcess()` 若任一页注册失败，会回滚已注册 PVT 页面并释放对应 handle，不留下半完成状态。
  - `demo_normal` 路径保持向后兼容，本次未引入行为回归。

#### 测试与运行
- 建议/已执行命令：
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
  - `cmake --build build`
  - `ctest --test-dir build`
- 已通过测试：
  - `ctest`：6/6 通过
  - 全部测试用例：60 个 test cases 全绿
  - `test_pvt`：新增 PVT 覆盖全部通过
  - `demo_normal`：保持可运行，向后兼容未回归
  - 评审回合：
    - Round 1：2 项失败，问题为缺少 `PageAllocator` 抽象、缺少 owner-mismatch 测试
    - Round 1 修复：补充 `PageAllocator` 接口与默认实现，并新增 owner-mismatch 测试
    - Round 2：全部通过，60 个 test cases 绿灯

#### 已知限制 & 下一步建议
- 当前默认物理页分配仍采用 `IdentityMappedPageAllocator`（`va / kPageSize`），依赖“地址空间互不重叠”的原型前提；若后续引入真实物理页管理，需要替换 allocator 实现并补充冲突测试。
- 目前 PVT 集成在 `LoadProcess()` 的 secure page load 路径中，尚未扩展为独立 demo；按既定计划，演示路径留到后续 Issue 9。

---

## Issue 8：SPE 模拟器最小闭环（Phase A — 方案确认）
**日期：** 2026-03-25
**分支：** issue-8-spe
**状态：** 方案已确认

### 目标
实现 SPE（可配置安全策略引擎）CFI 检查（L1/L2/L3），挂载到 Executor pipeline，违规触发 `SPE_VIOLATION`。附带清理 `ExecuteOptions` 遗留字段，统一走 `SecurityHardware` 路径。

### Scope
- **做**：CFI L1/L2/L3（Decode/Execute 阶段 shadow stack + 目标白名单）；`ExecuteOptions` 遗留字段清理
- **不做**：Memory 阶段 bounds 检查；独立 demo（留 Issue 9）

### 架构决策

**D1：SpeTable 宿主与接入方式**
- `SpeTable` 作为 `SecurityHardware` 成员（与 EwcTable、PvtTable 并列），通过 `GetSpeTable()` 访问
- Gateway `Load()` 配置：`hardware_.GetSpeTable().ConfigurePolicy(handle, ...)`
- Gateway `Release()` 清理：`hardware_.GetSpeTable().ClearPolicy(handle)`
- Executor 通过 `hardware->GetSpeTable()` 访问

**D2：Pipeline 单一插入点**
- SPE 检查在 switch 块（Decode+Execute）之后、`has_trap` 检查之后、PC 提交之前，作为单一插入点
- 仅当 switch 未产生 trap 时才调用 SPE（避免对无效 `committed_pc` 误检查）
- 架构依据：模拟器为顺序执行，不存在推测执行，单一插入点功能等价于分阶段检查
- SPE 内部根据指令类型标注逻辑阶段（`stage=decode` / `stage=execute`），写入 audit detail
- Audit 归属：SpeTable 在 `ConfigurePolicy` 时存储 `user_id`，违规时由 SpeTable 自行写 audit（与 EWC 模式一致），executor 不参与 SPE audit 写入

**D3：CFI Level 语义**
- L1：无 CFI 检查（仅 EWC 保护），SPE 对所有指令返回 pass
- L2：shadow stack only — CALL push return addr，RET pop + 比对
- L3：shadow stack + call_targets/jmp_targets 白名单

**D4：Toy ISA 控制流映射**

| 指令 | 目标来源 | 逻辑阶段 | L1 | L2 | L3 |
|------|---------|---------|----|----|------|
| CALL | pc-relative (imm) | decode | pass | push shadow stack | push + check target ∈ call_targets |
| J | pc-relative (imm) | decode | pass | pass | check target ∈ jmp_targets |
| BEQ (taken) | pc-relative (imm) | decode | pass | pass | check target ∈ jmp_targets |
| RET | register (r1) | execute | pass | pop + compare | pop + compare |
| 其他 | — | — | pass | pass | pass |

BEQ 未跳转时 `committed_pc == fetched.next_pc`，SPE 据此判断 not-taken，直接 pass。

**D5：ExecuteOptions 遗留字段清理**
- 删除：`ewc`, `audit`, `context_handle`, `region_base_va`, `code_memory`, `code_memory_size`
- 保留：`mem_size`, `max_steps`, `hardware`
- 所有调用点统一通过 `hardware` 获取 EWC/SPE/Audit/CodeRegion/ActiveHandle
- `hardware == nullptr` 时：函数开头直接 `throw std::runtime_error("hardware_not_configured")`（编程错误，非运行时安全事件，不写 audit）

**D6：Gateway 回滚**
- `Load()` 执行顺序：SetWindows → ConfigurePolicy → handle_to_user_ → LogEvent
- 回滚覆盖范围：从首个硬件 side effect（SetWindows）到成功返回之前的整个区间
- 现有 try-catch 框架扩展：catch 中统一 `ClearPolicy(handle)` + `ClearWindows(handle)`（无论哪步失败都安全调用，clear 对不存在的条目应为 no-op）

### 新增文件
- `include/security/spe.hpp` — SpeTable 类定义
- `src/security/spe.cpp` — SpeTable 实现
- `tests/test_spe.cpp` — SPE 单元测试

### 修改文件
- `include/security/hardware.hpp` — 新增 SpeTable 成员 + GetSpeTable()
- `include/core/executor.hpp` — 新增 TrapReason::SPE_VIOLATION；删除 ExecuteOptions 遗留字段
- `src/core/executor.cpp` — switch 后新增 SPE 检查点；删除遗留双路径逻辑；TrapReasonToString 新增 SPE_VIOLATION
- `src/security/gateway.cpp` — SecureIr 扩展 call_targets/jmp_targets；Load() 配置 SPE + 回滚；Release() 清理 SPE
- `tests/test_executor.cpp` — 适配新 ExecuteOptions
- `tests/test_gateway.cpp` — 适配新 ExecuteOptions
- `demos/normal/demo_normal.cpp` — 适配新 ExecuteOptions
- `CMakeLists.txt` — 新增 spe.cpp + test_spe.cpp

### 测试目标
1. L3 下 CALL 非法目标 → SPE_VIOLATION
2. L3 下 J 非法目标 → SPE_VIOLATION
3. L3 下合法目标 → 正常执行
4. L2 下 RET 地址被篡改（模拟 ROP）→ SPE_VIOLATION（shadow stack 不匹配）
5. L2 下正常 CALL/RET → 通过
6. L1 下同样的非法跳转 → 不触发 SPE
7. Gateway 配置的 cfi_level/call_targets/jmp_targets 正确传递到 SpeTable
8. 遗留字段清理后，所有老测试继续通过
9. Audit 事件包含逻辑阶段标注（stage=decode / stage=execute）
10. Gateway 回滚：ConfigurePolicy 失败后 EWC + SPE 无残留半配置状态
11. `hardware == nullptr` 时 ExecuteProgram 抛异常而非 crash

### Feasibility 检查结论（Step 2）
- session_id: `019d230a-1ba0-7470-a950-e2b2fb5319b0`
- 无阻塞项
- ExecuteOptions 遗留字段使用点完整确认：test_executor.cpp(5), test_gateway.cpp(1), demo_normal.cpp(2)
- cross_user demo 已走 hardware 路径，不受影响
- SecurityHardware 构造顺序支持 SpeTable（声明在 audit_collector_ 之后）
- Gateway Release() 需同步清理 SPE

---

## Issue 8：SPE 模拟器最小闭环（Phase B — 实现复盘）
**日期：** 2026-03-25
**状态：** 实现完成

### 实现摘要
- 完成 `SpeTable` 最小闭环实现：支持 L1/L2/L3 三档 CFI 语义，覆盖 `CALL` / `J` / `BEQ(taken)` / `RET` 的控制流约束；内部按 `context_handle` 维护 shadow stack，并在违规时由 `SpeTable` 自行写入 `SPE_VIOLATION` 审计事件。
- 完成 `ExecuteOptions` 清理：删除遗留字段 `ewc`、`audit`、`context_handle`、`region_base_va`、`code_memory`、`code_memory_size`，统一改为只通过 `SecurityHardware* hardware` 读取 EWC / SPE / Audit / CodeRegion / ActiveHandle。
- 完成 Executor pipeline 接入：在 Decode+Execute 后、`has_trap` 检查后、PC commit 前插入单一 SPE 检查点；`hardware == nullptr` 时改为直接抛出 `std::runtime_error("hardware_not_configured")`。
- 完成 Gateway SPE 配置与回滚：`Load()` 在 `SetWindows` 后调用 `ConfigurePolicy()`，`Release()` 清理 SPE 策略；若 `ConfigurePolicy()` 或后续步骤失败，catch 中统一执行 `ClearPolicy(handle)` + `ClearWindows(handle)`，避免半配置残留。

### 文件变更清单
- 新增文件：
  - `include/security/spe.hpp`：新增 `SpeTable` / `SpeCheckResult` 接口定义。
  - `src/security/spe.cpp`：实现 L1/L2/L3 CFI 检查、shadow stack、违规 detail 生成与 `SPE_VIOLATION` 审计写入。
  - `tests/test_spe.cpp`：新增 11 个 SPE 测试，覆盖 T1-T11 全部目标。
- 修改文件：
  - `include/security/hardware.hpp`：新增 `SpeTable spe_table_` 成员与 `GetSpeTable()` 访问器，更新构造初始化顺序。
  - `include/core/executor.hpp`：新增 `TrapReason::SPE_VIOLATION`，精简 `ExecuteOptions` 只保留 `mem_size` / `max_steps` / `hardware`。
  - `src/core/executor.cpp`：删除旧双路径初始化；统一从 `hardware` 取 EWC / Audit / CodeRegion / ActiveHandle；加入 SPE 检查点并补充 `TrapReasonToString()` 分支。
  - `src/security/gateway.cpp`：`SecureIr` 扩展 `call_targets` / `jmp_targets` 解析；`Load()` 配置 SPE；`Release()` 清理 SPE；失败路径补齐回滚。
  - `tests/test_executor.cpp`：旧执行器测试全部改走 `SecurityHardware` 路径，并同步更新 EWC 前置门控下的 trap 断言。
  - `tests/test_gateway.cpp`：执行器调用迁移到 `hardware` 路径，保持 Gateway + Executor 统一审计链验证。
  - `demos/normal/demo_normal.cpp`：demo 启动流程改为 `Gateway + SecurityHardware + StoreCodeRegion + SetActiveHandle`。
  - `CMakeLists.txt`：将 `src/security/spe.cpp` 加入 `simulator_core`，新增 `test_spe` target 并纳入 `ctest`。

### 测试结果
- 全部测试：71 个 test cases，全绿。
- `ctest`：7/7 通过（`sanity`、`isa_assembler`、`executor`、`gateway`、`kernel_process`、`pvt`、`spe`）。
- 新增 SPE 测试：11 个，完整覆盖 T1-T11：
  - T1 `L3 CALL 非法目标`
  - T2 `L3 J 非法目标`
  - T3 `L3 合法目标`
  - T4 `L2 RET 篡改 / shadow stack mismatch`
  - T5 `L2 正常 CALL/RET`
  - T6 `L1 非法跳转不触发 SPE`
  - T7 `Gateway 策略正确传递到 SpeTable`
  - T8 `ExecuteOptions 清理后硬件路径正常执行`
  - T9 `Audit detail 含 stage=decode / stage=execute`
  - T10 `Gateway 回滚后 EWC + SPE 无残留`
  - T11 `hardware == nullptr` 抛异常
- 已执行命令：
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
  - `cmake --build build`
  - `ctest --test-dir build`

### 评审回合
- Round 1：ALL PASS（无需返工）

### 已知限制 & 下一步建议
- 当前未实现 Memory-stage bounds check；本 Issue 仅覆盖 Fetch/Decode/Execute 相关的最小 CFI 闭环，Memory 阶段边界约束留待后续 issue 扩展。
- SPE 独立 demo 未纳入本 Issue；按既定计划，SPE 演示路径延后到 Issue 9。

### Push 状态
- 分支：`issue-8-spe`
- 已推送至 `origin/issue-8-spe`
- 日期：2026-03-25
