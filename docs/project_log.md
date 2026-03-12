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
