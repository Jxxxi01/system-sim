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

---

## Issue 9：攻击场景演示扩展（Phase A — 方案冻结）
**日期：** 2026-03-25
**分支：** issue-9-attack-demos
**状态：** 方案已确认，分 3 轮小循环实施

### 总体目标
补齐 5 类 demo 场景中缺失的 3 个攻击演示，使 Demo_Claim_Boundary v3.1 中所有 [Scaffolded] 场景升级为 [Current]。

### 前置依赖
- Issue 4（pseudo decrypt）→ demo_injection
- Issue 7（PVT）→ demo_cross_process
- Issue 8（SPE L1/L2/L3）→ demo_rop
- 全部已完成。

### 实施顺序
每个 demo 走独立 Step 2-7 小循环 + git commit：
1. demo_injection（代码页篡改）
2. demo_rop（ROP / CFI 违规）
3. demo_cross_process（PVT 跨进程篡改）

### Demo 设计

#### demo_injection — 代码页篡改
- **叙事**：OS 在 Gateway 合法加载后篡改物理内存中的密文内容，解密产出垃圾 → DECRYPT_DECODE_FAIL
- **与 demo_normal CASE_B 的区别**：demo_normal 用错误 key_id，本 demo key_id 正确但密文被篡改
- **Cases**：
  - CASE_A (baseline)：正常加载执行 → HALT
  - CASE_B (full tamper)：Gateway Load 后覆写 code_memory → DECRYPT_DECODE_FAIL
  - CASE_C (partial tamper)：只篡改中间某条指令密文，前几步正常，到被篡改指令时 DECRYPT_DECODE_FAIL
- **文件**：`demos/injection/demo_injection.cpp`（新增）

#### demo_rop — ROP / CFI 违规
- **叙事**：程序被 L3 CFI 策略保护，攻击者通过"篡改返回地址"使 RET 跳到非白名单目标
- **模拟方式**：用 `LI x1, <bad_addr>` 模拟 buffer overflow 覆写返回地址（toy ISA 语义等价）
- **Cases**：
  - CASE_A (L3 正常)：CALL→正常 RET，白名单覆盖 → HALT
  - CASE_B (L3 ROP)：CALL 后 LI x1 篡改返回地址 → RET → SPE_VIOLATION
  - CASE_C (L1 对照)：同样攻击路径，cfi_level=1 无 CFI → 不触发 SPE_VIOLATION（ablation 对照）
- **文件**：`demos/rop/demo_rop.cpp`（新增）

#### demo_cross_process — PVT 跨进程篡改
- **叙事**：恶意 OS 尝试将 Alice 代码页注册到 Bob 上下文 → PVT 用 EWC 反向验证拦截
- **拦截时机**：页面注册阶段（RegisterPage），非执行阶段。这反映真实硬件中 PVT 在 TLB miss/page walk 时校验的行为
- **Cases**：
  - CASE_A (正常)：Alice 正常 page load + 执行 → HALT
  - CASE_B (恶意映射)：OS 调用 RegisterPage(bob_handle, alice_va, CODE) → PVT 返回 ok=false
  - CASE_C (对照)：展示无 PVT 检查时相同操作不被拦截
- **文件**：`demos/cross_process/demo_cross_process.cpp`（新增）

### 文件范围（总）
- 新增：`demos/injection/demo_injection.cpp`, `demos/rop/demo_rop.cpp`, `demos/cross_process/demo_cross_process.cpp`
- 修改：`CMakeLists.txt`（新增 3 个 demo 目标）
- 不修改已有 demo_normal.cpp 和 demo_cross_user.cpp

### 验收标准
1. 3 个新 demo 编译通过，运行输出清晰的 trap reason + audit 事件流
2. 每个 demo return 0（所有 case 结果符合预期）
3. ctest 现有 7 个测试全绿（不回归）
4. 每个 demo 输出与 Demo_Claim_Boundary v3.1 表 A 预期一致

### demo_injection Phase B — 实现回顾
#### 变更文件
- 新增：`demos/injection/demo_injection.cpp`
- 修改：`CMakeLists.txt`（新增 `demo_injection` 目标）

#### 实现摘要
- CASE_A (baseline)：正常 Gateway Load + 执行 → HALT
- CASE_B (full tamper)：StoreCodeRegion 后全段 `code_memory` 异或 `0xFF` → 首条指令即 `DECRYPT_DECODE_FAIL`（`key_check_mismatch`）
- CASE_C (partial tamper)：只篡改第 3 条指令(0-based)的 payload 24 字节 → 前 3 步正常，第 4 步 `DECRYPT_DECODE_FAIL`（`tag_mismatch`）

#### 验证结果
- `ctest` 7/7 绿（71 test cases）
- `demo_injection` 三个 case 输出符合预期

#### 评审回合
- Round 1：ALL PASS（无需返工）

### demo_rop Phase B — 实现回顾
#### 变更文件
- 新增：`demos/rop/demo_rop.cpp`
- 修改：`CMakeLists.txt`（新增 `demo_rop` 目标）

#### 实现摘要
- CASE_A (L3 正常)：CALL→函数体→RET→HALT，`cfi_level=3` + `call_targets` 白名单覆盖 → HALT
- CASE_B (L3 ROP)：`LI x1` 篡改返回地址→RET→shadow stack mismatch → `SPE_VIOLATION`
- CASE_C1 (L1 窗口内)：`cfi_level=1`，`bad_addr` 在窗口内→ROP 攻击静默成功 → HALT
- CASE_C2 (L1 窗口外)：`cfi_level=1`，`bad_addr` 在窗口外→EWC fetch 阶段兜底 → `EWC_ILLEGAL_PC`
- defense-in-depth 演示：L3 在 RET 时直接拦截，L1 窗口内攻击不可见，窗口外由 EWC 兜底

#### 验证结果
- `ctest` 7/7 绿（71 test cases）
- `demo_rop` 四个 case 输出符合预期

#### 评审回合
- Round 1：ALL PASS（无需返工）

### demo_cross_process Phase B — 实现回顾
#### 变更文件
- 新增：`demos/cross_process/demo_cross_process.cpp`
- 修改：`CMakeLists.txt`（新增 `demo_cross_process` 目标）

#### 实现摘要
- CASE_A (正常)：Alice 通过 `KernelProcessTable.LoadProcess` 加载含 CODE 页的程序 → PVT 注册成功 → HALT
- CASE_B (恶意映射)：恶意 OS 调用 `RegisterPage(bob_handle, alice_va, CODE)` → PVT 返回 `ok=false`（`reason=missing_window`）+ `PVT_MISMATCH` audit
- CASE_C (defense-in-depth)：Bob 以 `pages=[]` 加载跳过 PVT，OS 直接 `StoreCodeRegion` 绕过 PVT → 执行 `alice_va` 时 EWC 兜底 → `EWC_ILLEGAL_PC`
- PVT 的 ownership 检查通过 EWC 窗口匹配实现：`missing_window` 即 ownership 不成立

#### 验证结果
- `ctest` 7/7 绿（71 test cases）
- `demo_cross_process` 三个 case 输出符合预期

#### 评审回合
- Round 1：ALL PASS（无需返工）

### Push 状态
- 分支：`issue-9-attack-demos`
- 已推送至 `origin/issue-9-attack-demos`
- 日期：2026-03-25

## Issue 11A：SecureIR Package + 加载链路统一（方案确认）
**日期：** 2026-03-26
**分支：** issue-11-secureir-gen
**状态：** 方案已确认

### 背景与动机
Issue 5 D-1 和 Issue 6B D-8 明确记录：SecureIR metadata 与 encrypted code_memory 当前分离传递是已知简化，完整 SecureIR 包属于 I-1 范畴，留待后续统一。同时，5 个 demo 中存在 3 个变体的手写 `MakeSecureIrJson()`，高度重复。

Issue 11 分 A/B 两阶段：
- **11A**（本阶段）：定义 SecureIrPackage 逻辑包 + 改造加载链路（Gateway 拆包、LoadProcess 单参数）+ Demo 统一走 KernelProcessTable
- **11B**（下阶段）：SecureIrBuilder 库函数 + Demo 重构消除手写 JSON

### 初始需求（用户提出）
- SecureIR metadata + encrypted code_memory 捆绑为统一 Package，不再分离传递
- Gateway（硬件）作为唯一拆包者，内核只转发
- 所有 demo 统一走 `KernelProcessTable::LoadProcess()` 单一入口

### 额外补充/优化需求（对话新增）
- Package 内部表示采用 C++ struct 逻辑包（非 JSON 内嵌 base64），因为 JSON 本身是真实二进制格式的简化
- 加载顺序选方案 Y：内核转发整包 → Gateway 拆包 + 验签 + 配 EWC/SPE + 存 code_memory → 返回 {handle, layout_info} → 内核用返回的布局信息注册 PVT
- 方案 Y 安全性分析：布局信息明文（DP-9）且签名保护完整性。内核即使读到布局也无法作恶——篡改被签名拦截、错误映射被 PVT 拦截、owner 伪造被 EWC 反向验证拦截、代码内容加密不可读、DoS 是所有架构的公认不防范项
- process.cpp 中重复的 mini JSON parser 可完全删除（Gateway 返回所有内核所需信息）
- demo_normal / demo_injection / demo_rop 原先直接调用 gateway.Load()（历史原因：创建时 KernelProcessTable 尚未存在或不需要 context switch），本阶段统一改为走 KernelProcessTable

### Coding 前最终方案

#### 文件与模块清单
- 新增：`include/security/securir_package.hpp`（SecureIrPackage + GatewayLoadResult 定义）
- 修改：`include/security/gateway.hpp`（Load 签名变更）
- 修改：`src/security/gateway.cpp`（Load 内部拆包 + 存 code_memory + 返回布局信息 + 回滚路径补 RemoveCodeRegion）
- 修改：`include/kernel/process.hpp`（LoadProcess 签名变更）
- 修改：`src/kernel/process.cpp`（单参数 LoadProcess + 删除 mini JSON parser + 消费 Gateway 返回值）
- 修改：`tests/test_gateway.cpp`（11 个测试适配新签名）
- 修改：`tests/test_kernel_process.cpp`（12 个测试适配新签名）
- 修改：`tests/test_pvt.cpp`、`tests/test_spe.cpp`（适配 LoadProcess / gateway.Load 签名变更）
- 修改：5 个 demo `.cpp`（统一走 KernelProcessTable::LoadProcess，构造 SecureIrPackage 传入）
- 修改：`CMakeLists.txt`（如需接入新头文件）

#### 语义接口（WHAT，签名由 Codex 决定）

- **SecureIrPackage**：
  - 逻辑包，包含 metadata（std::string，当前为 JSON）+ code_memory（加密后的字节向量）
  - 命名空间 `sim::security`

- **GatewayLoadResult**（或等效返回结构）：
  - 包含 handle、user_id、base_va、pages 布局信息（va + page_type，使用强类型 PvtPageType）
  - Gateway::Load 的返回值

- **Gateway::Load 改造**：
  - 输入：SecureIrPackage
  - 职责：验签 → 解析 metadata → 配 EWC/SPE → 存 code_memory 到 SecurityHardware → 返回 GatewayLoadResult
  - 回滚路径补全：失败时除清理 EWC/SPE 外，还需 RemoveCodeRegion

- **KernelProcessTable::LoadProcess 改造**：
  - 输入：SecureIrPackage（单参数，move 语义）
  - 职责：整包转发 Gateway → 用 Gateway 返回的布局信息注册 PVT → 存储 ProcessContext → 返回 handle
  - 删除 process.cpp 内全部 mini JSON parser 代码（JsonValue/JsonParser/Require*/ParseProcessLoadSpec/ParsePageType）

- **Demo 统一**：
  - demo_normal / demo_injection / demo_rop 从直接调 gateway.Load() + 手动 StoreCodeRegion 改为走 KernelProcessTable::LoadProcess(SecureIrPackage)
  - demo_cross_user / demo_cross_process 适配新 LoadProcess 签名
  - demo_cross_process CASE_C 的手动 StoreCodeRegion 保留（有意的攻击模拟）

#### 语义/不变量（必须测死，后续不得漂移）
- Gateway 是唯一拆包者：code_memory 从 Package 进入 Gateway，由 Gateway 存入 SecurityHardware，内核全程不接触 code_memory
- Gateway 返回的布局信息来自签名保护的 metadata，与 ParseSecureIr 解析结果一致
- PVT 仍从 EWC 读 owner（硬件互联），不依赖 Gateway 返回值中的 user_id 做所有权判定
- Gateway::Load 失败时无脏状态：EWC 窗口、SPE 策略、code_region 全部回滚
- LoadProcess 单参数后语义不变：成功后 handle 在进程表可查，code_memory 在 SecurityHardware 中，ProcessContext 的 user_id/base_va 与 metadata 一致
- 所有现有测试语义保持（接口适配后继续通过）

#### 测试计划
- Gateway 接受 SecureIrPackage → 配置 EWC/SPE + 存 code_memory + 返回正确 GatewayLoadResult
- GatewayLoadResult 中 user_id / base_va / pages（va + page_type）与 metadata JSON 中的值一致
- Gateway::Load 失败 → code_region + EWC + SPE 全部回滚（新增断言）
- Gateway::Load 遇非法 page_type → 失败 + 全回滚（ParsePageType 校验从 process.cpp 搬入 Gateway 后需钉死）
- LoadProcess(SecureIrPackage) → 走通完整链路（Gateway 拆包 → PVT 注册 → ProcessContext 正确）
- LoadProcess 中 PVT 注册失败 → 完整回滚（Gateway 已成功 + code_region 已落硬件 → 需验证 code_region / EWC / SPE / 进程表全部清理）
- 所有原有 test_gateway / test_kernel_process / test_pvt / test_spe 测试适配后绿灯
- 5 个 demo 编译运行输出不变

#### 设计决策记录

**D-1：Package 内部表示为 C++ struct 逻辑包**
- 决策：不在 JSON 中嵌入 base64 编码的 code_memory，而是用 C++ struct 捆绑 metadata + code_memory。
- 原因：JSON 本身是真实硬件二进制格式的简化，逻辑包更灵活且改动更小。

**D-2：加载顺序选方案 Y（Gateway 返回布局信息）**
- 决策：内核转发整包给 Gateway，Gateway 拆包后返回 {handle, layout_info}，内核用返回值做后续操作。
- 原因：工程上消除 process.cpp 中重复的 mini JSON parser；安全性与方案 X 等价（DP-9 保证布局明文 + 签名保护完整性）。

**D-3：Demo 统一走 KernelProcessTable**
- 决策：demo_normal / demo_injection / demo_rop 从直接调 gateway.Load() 改为走 KernelProcessTable::LoadProcess()。
- 原因：3 个 demo 直接调 Gateway 是历史遗留（创建时 KernelProcessTable 不存在或不需要 context switch），统一后加载路径收口为单一入口，为 Phase B builder 简化铺路。

**D-4：Gateway 返回 pages 使用强类型 PvtPageType**
- 决策：GatewayLoadResult 中 pages 的 page_type 使用 `sim::security::PvtPageType` 枚举。
- 原因：Gateway 已在 metadata 解析阶段完成字符串→枚举转换，process.cpp 的 ParsePageType 可一并删除。

### Phase B：实现复盘

#### 1. 变更文件清单
- 新增 `include/security/securir_package.hpp`：定义 `SecureIrPackage`、`GatewayPageLayout`、`GatewayLoadResult`，把 metadata 与 `code_memory` 作为统一逻辑包传递。
- 修改 `include/security/gateway.hpp`：`Gateway::Load` 改为接收 `SecureIrPackage` 并返回 `GatewayLoadResult`。
- 修改 `src/security/gateway.cpp`：`Load` 内部完成拆包、`pages` 解析和 `code_memory` 落硬件；新增 `ParsePageType`；失败路径补 `RemoveCodeRegion`；`Release` 统一清理 `code_region`、SPE、EWC。
- 修改 `include/kernel/process.hpp`：`KernelProcessTable::LoadProcess` 改为单参数 `SecureIrPackage`。
- 修改 `src/kernel/process.cpp`：删除 mini JSON parser，改为消费 `GatewayLoadResult` 注册 PVT；PVT 失败时通过 `gateway_.Release` 做完整回滚。
- 修改 `tests/test_gateway.cpp`：全部适配新 `gateway.Load` 签名，新增 `Gateway_Load_InvalidPageType_Fails` 覆盖非法 `page_type` 回滚。
- 修改 `tests/test_kernel_process.cpp`：全部适配新 `LoadProcess` 签名，新增 `LoadProcess_PvtFailure_FullRollback` 覆盖 PVT 失败后的全回滚。
- 修改 `tests/test_pvt.cpp`：适配 `SecureIrPackage` / 单参数 `LoadProcess` 调用链。
- 修改 `tests/test_spe.cpp`：适配 `SecureIrPackage` / 新 `gateway.Load` 调用方式。
- 修改 `demos/normal/demo_normal.cpp`：两条加载路径统一走 `KernelProcessTable::LoadProcess`。
- 修改 `demos/injection/demo_injection.cpp`：统一由进程表加载，再做整段/局部密文篡改演示。
- 修改 `demos/rop/demo_rop.cpp`：各 case 统一通过进程表加载并切换上下文。
- 修改 `demos/cross_user/demo_cross_user.cpp`：Alice/Bob 两个进程统一走 `LoadProcess`。
- 修改 `demos/cross_process/demo_cross_process.cpp`：CASE_A/CASE_B 统一走 `LoadProcess`，CASE_C 仅保留故意的 `StoreCodeRegion` 攻击注入。

#### 2. 关键实现细节
- `Gateway` 成为唯一拆包者：`SecureIrPackage.metadata` 的解析、`pages` 布局提取、`page_type` 字符串到 `PvtPageType` 的转换都收口到 `src/security/gateway.cpp`，`process.cpp` 内的 mini JSON parser 已完全删除。
- `GatewayLoadResult` 返回 `handle`、`user_id`、`base_va` 和强类型 `pages`，内核只消费这些已解析布局信息来注册 PVT，不再重复解析 metadata。
- `code_region` 的生命周期管理从内核侧回收到 `Gateway`：`Load` 成功时由 Gateway 存入硬件，失败时回滚 `RemoveCodeRegion`，`Release` 时与 `ClearPolicy`、`ClearWindows` 一起对称清理。
- `KernelProcessTable::LoadProcess` 在 Gateway 成功后逐页注册 PVT；若中途失败，会先移除已注册页，再调用 `gateway_.Release(handle)`，确保 `code_region`、SPE、EWC、handle 映射和进程表状态一起回滚。
- 5 个 demo 现已统一经由 `KernelProcessTable::LoadProcess` 建立进程上下文；`demo_cross_process` 的 CASE_C 继续保留手动 `StoreCodeRegion`，作为“恶意 OS 绕过 PVT 写入，但仍被 EWC 拦截取指”的特意攻击场景。

#### 3. 测试覆盖
- 新增测试 `Gateway_Load_InvalidPageType_Fails`：验证 `Gateway` 侧 `ParsePageType` 拒绝非法 `page_type`，并确认 `GATEWAY_LOAD_FAIL`、`code_region`/EWC/SPE 全回滚。
- 新增测试 `LoadProcess_PvtFailure_FullRollback`：验证 Gateway 已成功加载后，如果 PVT 注册因为 `missing_window` 失败，`KernelProcessTable` 会触发完整回滚，不留下进程表项、`code_region` 或 handle 映射。
- `tests/test_gateway.cpp` 现有用例已全部适配 `SecureIrPackage` 入参，并继续覆盖 handle 分配、签名校验、窗口重叠、容量上限、`Release` 清理等语义。
- `tests/test_kernel_process.cpp` 现有用例已全部适配单参数 `LoadProcess`，继续覆盖成功加载、上下文切换、释放后的活跃句柄清理、Gateway 失败无脏状态等行为。
- `tests/test_pvt.cpp` 与 `tests/test_spe.cpp` 已完成接口适配，继续沿用原有语义断言；5 个 demo 的加载入口也全部对齐到统一链路。

#### 4. 与 Phase A 方案的偏差
- 无偏差。
- 实施过程中评审发现过 3 个缺口，但都已修正并回到 Phase A 目标语义：`code_region` 所有权从内核收归 `Gateway::Release`；补上 `Gateway_Load_InvalidPageType_Fails`；补上 `LoadProcess_PvtFailure_FullRollback`。

### Push 状态
**已推送：** 2026-03-26
**分支：** issue-11-secureir-gen

---

## Issue 11B：SecureIrBuilder + Demo 重构
**日期：** 2026-03-26
**分支：** issue-11-secureir-gen
**状态：** Phase A 方案已确认

### Phase A：计划冻结

#### 目标
提供 SecureIrBuilder 库函数，从 AsmProgram + 安全配置一站式生成 SecureIrPackage（含加密），消除 5 个 demo 中重复的 MakeSecureIrJson + 手动 EncryptProgram/BuildCodeMemory 样板代码。

#### 文件范围
- 新增 `include/security/securir_builder.hpp`：Builder 公开接口
- 新增 `src/security/securir_builder.cpp`：Builder 实现
- 新增 `tests/test_securir_builder.cpp`：Builder 单元测试 + round-trip 验证
- 修改 `CMakeLists.txt`：添加新源文件到 simulator_core 库 + 新测试目标
- 修改 `demos/normal/demo_normal.cpp`：删除 MakeSecureIrJson，改用 builder
- 修改 `demos/injection/demo_injection.cpp`：同上
- 修改 `demos/cross_user/demo_cross_user.cpp`：同上
- 修改 `demos/rop/demo_rop.cpp`：同上
- 修改 `demos/cross_process/demo_cross_process.cpp`：同上

#### 语义接口

**SecureIrBuilder 模块**（namespace `sim::security`）：
- 输入：`sim::isa::AsmProgram` + 安全配置参数
- 配置维度：program_name, user_id, key_id, window_id, signature（默认 "stub-valid"）, cfi_level（默认 0）, call_targets（默认空）, jmp_targets（默认空）, pages（默认空）
- 内部流程：调用 EncryptProgram → BuildCodeMemory 生成 code_memory；生成符合 Gateway parser 要求的 metadata JSON 字符串
- 输出：`SecureIrPackage{metadata, code_memory}`
- base_va 和 end_va 从 AsmProgram 自动推导
- 默认便捷路径：自动生成单窗口（覆盖当前全部 demo 场景）
- 多窗口接口：允许调用者手动指定多个窗口描述，此时不自动推导窗口

**不变量**：
- Builder 生成的 SecureIrPackage 必须能被 Gateway::Load 正常解析，语义等价于当前手写的 MakeSecureIrJson + EncryptProgram + BuildCodeMemory 路径
- JSON 字段全部输出（pages, cfi_level, call_targets, jmp_targets 即使为默认值也必须输出——Gateway parser 全必填）
- 数字以十进制输出（不能用 0x 前缀）
- Builder 是纯工具链侧组件，不涉及硬件模拟状态

**Demo 重构**：
- 删除每个 demo 中的 MakeSecureIrJson 函数及其辅助函数（NumberArrayJson, PagesJson 等）
- 删除每个 demo 中的手动 EncryptProgram + BuildCodeMemory 调用
- 改用 builder 一步生成 SecureIrPackage，直接传入 LoadProcess
- 攻击场景的后续操作不变：demo_injection 的 XorWholeCiphertext/XorPayloadBytes、demo_cross_process Case C 的 StoreCodeRegion 保留

#### 测试目标
1. 基础生成：简单 AsmProgram + 基本配置 → Build → Gateway::Load 成功 → GatewayLoadResult 各字段正确
2. CFI 字段传递：设置 cfi_level + call/jmp targets → 验证 Gateway Load 后 SPE 配置正确
3. Pages 字段传递：添加 page specs → 验证 Gateway Load 后 pages 布局正确
4. Round-trip 验证：Builder → Load → Executor 执行 → HALT，端到端正确
5. 多窗口路径：手动指定 2 个不重叠窗口 → Gateway Load 成功
6. Demo 等价性：5 个 demo 重构后 ctest 全绿 + demo 输出行为不变

#### 设计决策记录

**D-1：API 风格交由 Codex 选择**
- 决策：不限定 builder pattern 或 config struct + 自由函数，由 Codex 在实现阶段自行选择。
- 原因：两种风格功能等价，属于 HOW 层面的实现细节。

**D-2：多窗口接口保留，单 key 加密**
- 决策：Builder 接口支持多窗口描述（metadata 的 windows 数组可含多条目），但当前实现只做单 key 加密（所有窗口共享 AsmProgram 级别的 key_id）。
- 原因：多 key 分段加密需改造 EncryptProgram，超出本 Issue 范围。接口不受限，实现保持简单。

**D-3：Builder 不处理攻击构造**
- 决策：Builder 只负责正常的 SecureIrPackage 生成。攻击场景（密文篡改、恶意 code_region 注入）在 LoadProcess 之后由各 demo 自行处理。
- 原因：攻击操作作用于已加载到硬件的数据，与 package 构建阶段无关。
**提交：** issue 11A: SecureIrPackage + unified loading chain

### Phase B：实现复盘
**状态：** 已实现（未推送）
**提交：** TBD
**远端：** 未推送

#### 1. 变更文件清单
- 新增 `include/security/securir_builder.hpp`：定义 `SecureIrBuilderConfig`、`SecureIrWindowSpec`、`SecureIrPageSpec` 和 `SecureIrBuilder::Build` 静态入口。
- 新增 `src/security/securir_builder.cpp`：实现 `ResolveWindows`（单窗口自动推导 / 多窗口显式配置）、`key_id` 一致性校验、`EncryptProgram` + `BuildCodeMemory` 封装以及 metadata JSON 生成。
- 新增 `tests/test_securir_builder.cpp`：新增 7 个 Builder 测试，覆盖基础构建、CFI 字段、`pages` 字段、单/多窗口 round-trip 和异常路径。
- 修改 `CMakeLists.txt`：将 `src/security/securir_builder.cpp` 加入 `simulator_core`，新增 `test_securir_builder` 测试目标。
- 修改 `demos/normal/demo_normal.cpp`：删除手写 `MakeSecureIrJson`；改用 builder；`CASE_B` 改为构造正确包与错误 key 包后交换 `code_memory` 模拟 wrong-key。
- 修改 `demos/injection/demo_injection.cpp`：删除手写 JSON 构造，统一改用 builder；保留 `XorWholeCiphertext` / `XorPayloadBytes` 攻击辅助逻辑。
- 修改 `demos/cross_user/demo_cross_user.cpp`：删除手写 JSON 构造，统一改用 builder。
- 修改 `demos/rop/demo_rop.cpp`：删除 `MakeSecureIrJson` / `NumberArrayJson`，改由 builder 配置 `cfi_level`、`call_targets`、`jmp_targets`。
- 修改 `demos/cross_process/demo_cross_process.cpp`：删除 `MakeSecureIrJson` / `PagesJson` / 本地 `SecureIrPageSpec`，改由 builder 配置 `pages`。

#### 2. 关键实现细节
- Builder API 采用 `SecureIrBuilderConfig` + `SecureIrBuilder::Build` 静态方法风格，符合 Phase A 中对 API 风格开放、由实现阶段落地的决策。
- 单窗口便捷路径已落地：当 `config.windows` 为空时，Builder 会根据 `AsmProgram` 的 `base_va` 和指令长度自动推导单个执行窗口，覆盖当前普通 demo 场景。
- 多窗口路径使用调用方传入的 `SecureIrWindowSpec` 列表；实现中强制校验所有窗口 `key_id` 一致，否则抛出 `securir_builder_inconsistent_window_key_ids`；加密时使用窗口上的 `key_id`，不再沿用 `config.key_id`。
- metadata JSON 由 `std::ostringstream` 直接生成，所有数值统一按十进制输出；字符串字段对引号和反斜杠做显式转义，确保兼容 Gateway 现有 parser。
- `demo_normal` 的 `CASE_B` 为保留 wrong-key 语义，改为分别生成 `key=11` 的正确包和 `key=99` 的错误包，再交换 `code_memory` 制造 metadata / 密文不匹配；这样既保留演示效果，也不破坏 builder 对一致性的强约束。

#### 3. 测试覆盖与运行结果
- 新增 `tests/test_securir_builder.cpp` 7 个测试：
  - 基础构建成功
  - CFI 字段透传
  - `pages` 字段透传
  - 单窗口 round-trip 执行
  - 多窗口加载成功
  - 不一致 `key_id` 抛异常
  - 多窗口 round-trip 执行
- 全量测试结果：8 个 test suites 全绿，总计约 70 个 test cases 通过。
- demo 验证结果：5 个 demo 全部通过，且 trap reason 输出与预期一致。
- 已执行命令：
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
  - `cmake --build build`
  - `ctest --test-dir build`

#### 4. 与 Phase A 方案的偏差
- 实施过程中评审发现一个多窗口 `key_id` 语义缺口：初版实现即使走多窗口路径，仍使用 `config.key_id` 做加密，和 D-2“单 key 加密但由窗口语义约束”不完全一致。该问题已修正为“使用窗口 `key_id` 加密 + 全窗口一致性校验”，最终语义与 Phase A 对齐。
- `demo_normal` 的 wrong-key 演示实现方式与 Phase A 的抽象描述不同：由于 builder 现在显式阻止 metadata / key 配置不一致，最终采用交换 `code_memory` 的方式保留 `CASE_B`。这是实现层面的演示手法调整，不改变外部可观察行为。

#### 5. 已知限制 & 下一步建议
- 当前多窗口能力仍限定为”所有窗口共享同一 `key_id`”；多 key 分段加密依然不在本 Issue 范围内。
- metadata JSON 仍是手工串接输出，字段集继续扩展时需要同步补齐转义和测试覆盖。

### Push 状态
**已推送：** 2026-03-26
**分支：** issue-11-secureir-gen

## Issue 12：Hidden entry / saved_PC
**日期：** 2026-03-26
**分支：** issue-12-hidden-entry
**状态：** Phase A 方案已确认

### Phase A：计划冻结

#### 目标
将程序首次入口隐藏于硬件上下文中，OS 不再显式指定启动 PC。首次执行只能从 Gateway 在 Load 时初始化的 `saved_pc` 开始。同时将 `user_id` 从 Gateway 私有映射迁移到 SecurityHardware per-handle 元数据中（其设计归属位置）。

#### 安全动机
对齐 v4 的 hidden-entry 语义。当前 `ExecuteProgram` 接受外部显式传入的 `entry_pc`，OS 可以指定任意入口地址绕过安全初始化路径。

#### 文件范围
- 修改 `include/security/hardware.hpp`：per-handle 元数据扩展（saved_pc + user_id），新增独立的写入接口（仅由 Gateway 调用），saved_pc 不放入 CodeRegion（CodeRegion 返回可写指针，会破坏隐藏性）
- 修改 `include/security/gateway.hpp` / `src/security/gateway.cpp`：Load 时写入 saved_pc + user_id 到 SecurityHardware；移除 `handle_to_user_` 私有映射，`GetUserIdForHandle` 改为代理查询 SecurityHardware；Release 和回滚路径同步清理新元数据
- 修改 `include/security/securir_package.hpp`：`GatewayLoadResult.base_va` 语义重定义为 code load address（供 PVT 页面注册），不再作为 entry point
- 修改 `include/security/securir_builder.hpp` / `src/security/securir_builder.cpp`：Builder config 新增 entry offset 字段（默认 0），支持 entry != base_va
- 修改 SecureIR metadata 格式（Gateway 解析侧）：解析 entry offset 字段
- 修改 `include/core/executor.hpp` / `src/core/executor.cpp`：`ExecuteProgram` 移除 `entry_pc` 参数，从 active context 的硬件状态读取 saved_pc
- 修改 `include/kernel/process.hpp` / `src/kernel/process.cpp`：适配新接口，`ProcessContext.base_va` 语义明确为 load address
- 修改 5 个 demo（normal / injection / cross_user / cross_process / rop）：适配新接口，不再传 entry_pc
- 修改相关测试文件：适配新接口 + 新增恶意 OS 测试

#### 语义接口

**SecurityHardware per-handle 元数据**：
- 每个 handle 关联 saved_pc 和 user_id，仅由 Gateway 在 Load 时写入
- 独立于 CodeRegion 存储——CodeRegion 保持现有语义（code_memory 可被攻击场景修改），saved_pc 不可被外部篡改
- Release 时同步清理

**Gateway**：
- `Load()` 计算 `saved_pc = base_va + entry_offset`，写入 SecurityHardware per-handle 元数据
- `handle_to_user_` 移除，`GetUserIdForHandle()` 保留接口但改为查询 SecurityHardware
- 容量检查适配新的数据来源

**SecureIR / SecureIrBuilder**：
- metadata 格式新增 entry_offset 字段
- Builder config 新增 entry_offset（默认 0，保持向后兼容）
- `saved_pc = base_va + entry_offset`

**ExecuteProgram**：
- 移除 `entry_pc` 参数，签名变为 `ExecuteProgram(const ExecuteOptions&)`
- 从 active context 的 saved_pc 读取首次入口
- 错误路径（no_active_context / missing_active_region）的 trap PC 填 0

**GatewayLoadResult**：
- `base_va` 保留，语义为 code load address（供 PVT 页面注册使用）
- 不暴露 entry 信息

**cross_user demo CASE_C**：
- 改写为新 API 下的攻击演示：Bob ContextSwitch 后调 ExecuteProgram → 从 Bob 自己的 saved_pc 启动 → Bob 代码中跳转到 Alice 地址 → EWC 拦截
- 攻击面从”OS 指定 entry_pc”变为”用户代码越界跳转”，叙事仍为跨用户隔离

#### 不变量
1. saved_pc 只由 Gateway 在 Load 时写入，无外部写入 API
2. user_id 的权威来源是 SecurityHardware per-handle 元数据（符合设计文档：安全上下文中包含 user_id）
3. ExecuteProgram 不接受外部指定的入口地址
4. GatewayLoadResult 不暴露 entry 信息

#### 测试目标
1. 正常路径：Gateway load → ContextSwitch → ExecuteProgram → 从 saved_pc 启动 → HALT
2. Entry offset：非零 entry_offset → saved_pc != base_va → 正确执行
3. 恶意 OS：外部无法通过 API 指定任意首次入口（新增测试用例，放在 test_executor.cpp）
4. cross_user CASE_C：Bob 从自己的 saved_pc 启动，代码越界跳转被 EWC 拦截
5. 回归：全部现有测试继续通过

#### 设计决策记录

**D-1：saved_pc 独立于 CodeRegion 存储**
- 决策：在 SecurityHardware 中新增独立的 per-handle 元数据结构，不扩展 CodeRegion。
- 原因：CodeRegion 通过 GetCodeRegion() 返回可写指针，demo_injection 和 cross_process 直接修改它。如果 saved_pc 放入 CodeRegion，恶意 OS 路径可篡改入口，违背安全目标。

**D-2：Gateway handle_to_user_ 直接移除**
- 决策：移除 Gateway 的 handle_to_user_ 私有映射，GetUserIdForHandle 改为代理查询 SecurityHardware。
- 原因：user_id 按设计文档应存于硬件安全上下文。保留两份是冗余的双真值源。Gateway 持有 SecurityHardware& 引用，查询代价为零。

**D-3：本轮新增 entry_offset**
- 决策：SecureIR metadata 和 SecureIrBuilder 新增 entry_offset 字段，saved_pc = base_va + entry_offset。
- 原因：仅做 saved_pc == base_va 不足以完整展示 hidden entry 的表达能力。entry_offset 默认 0 保持向后兼容。

**D-4：错误路径 trap PC 填 0**
- 决策：ExecuteProgram 在 no_active_context / missing_active_region 时 trap.pc = 0。
- 原因：这些错误发生时尚未从硬件读取到有效 PC，0 作为”PC 未知”的 sentinel。

#### Codex 可行性评估摘要（Step 2）
- session_id: 019d297e-c329-73e3-894f-b44b3bfc5a36
- 总体判断：方案可行，改动量中等，无硬阻塞
- ExecuteProgram 调用点共 16 处（5 demo + 4 测试文件）
- GatewayLoadResult.base_va 当前无生产路径直接用作 entry_pc，语义重定义影响低
- ProcessContext.base_va 仅用于审计和 PVT，不参与执行入口选择
- 风险已在 Step 3 讨论中逐条解决并形成 D-1 至 D-4 决策

### Phase B：实现复盘
**状态：** 已实现（未推送）
**提交：** TBD

#### 1. 变更文件清单
- 修改 `include/security/hardware.hpp`：新增 `HandleMetadata{saved_pc,user_id}`、硬件侧只读查询接口，以及仅供 `Gateway` 调用的元数据写入/清理路径；`SetActiveHandle()` 现在要求 handle 同时具备 code region 和 metadata。
- 修改 `include/core/executor.hpp`：`ExecuteProgram` 签名收敛为 `ExecuteProgram(const ExecuteOptions&)`，移除外部 `entry_pc` 输入。
- 修改 `include/security/gateway.hpp`：删除 `handle_to_user_` 私有映射依赖，保留 `GetUserIdForHandle()` 但语义改为代理查询硬件。
- 修改 `include/security/securir_builder.hpp`：`SecureIrBuilderConfig` 新增 `entry_offset` 字段，默认值为 0。
- 修改 `include/security/securir_package.hpp`：明确 `GatewayLoadResult.base_va` 只是 code load address，不再承担 entry point 语义。
- 修改 `src/core/executor.cpp`：执行入口改为从 active handle 的 `saved_pc` 读取；`no_active_context` / `missing_active_region` trap 的 `pc` 统一填 0；`EWC_ILLEGAL_PC` 审计改为使用硬件中的 `user_id`。
- 修改 `src/security/gateway.cpp`：解析 `entry_offset`，计算并写入 `saved_pc`，移除 Gateway 自身的 user 映射；`Release()` / load 失败回滚会同步清理 metadata；后续补入 `entry_offset` 的 load-time 对齐与越界校验。
- 修改 `src/security/securir_builder.cpp`：metadata JSON 新增 `entry_offset` 输出，支持 hidden entry round-trip。
- 修改 `demos/normal/demo_normal.cpp`：移除显式 entry 参数，统一从 active context 的 `saved_pc` 启动。
- 修改 `demos/injection/demo_injection.cpp`：同上，攻击演示仍通过篡改 `code_memory` 完成，但启动入口改为 hidden entry。
- 修改 `demos/rop/demo_rop.cpp`：同上，所有 ROP case 从 hardware-saved `saved_pc` 启动。
- 修改 `demos/cross_user/demo_cross_user.cpp`：移除外部 entry 传参；CASE_C 改为 Bob 使用非零 `entry_offset` 的 hidden entry 启动，再跳向 Alice 地址触发 EWC。
- 修改 `demos/cross_process/demo_cross_process.cpp`：移除外部 entry 传参；CASE_C 改为恶意 OS 覆写 Bob 的 code region，但执行仍从 Bob 自己的 `saved_pc` 起跑，再由 EWC 拦截跨进程跳转。
- 修改 `tests/test_executor.cpp`：重构 helper，统一通过 Gateway 建立硬件 metadata；适配无 `entry_pc` 接口；新增“非零 `entry_offset` 正确执行”和“恶意 OS 无法指定首次入口”用例；运行期坏 PC 用例改为真正的执行期跳转错误。
- 修改 `tests/test_gateway.cpp`：适配 `entry_offset` 字段与 hidden entry 语义；新增 `saved_pc` 写入/清理校验；新增 misaligned / out-of-range `entry_offset` 在 Load 阶段被拒绝的回归。
- 修改 `tests/test_securir_builder.cpp`：新增 builder + `entry_offset` 的 round-trip 测试，验证从 metadata 到 Gateway 再到 Executor 的 hidden entry 链路。
- 修改 `tests/test_spe.cpp`：将 1 条直接手工构造硬件状态的执行测试改为先走 Gateway Load，确保 metadata 初始化符合新语义。
- 修改 `docs/project_log.md`：追加本次 Issue 12 的实现复盘。

#### 2. 测试与运行
- 已执行命令：
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
  - `cmake --build build`
  - `ctest --test-dir build`
- 结果：
  - 8 个 test suites 全绿，`100% tests passed, 0 tests failed out of 8`
  - hidden entry、`entry_offset`、Gateway metadata 回滚/释放、cross-user hidden-entry 叙事相关回归均已覆盖

#### 3. 与 Phase A 方案的偏差
- `include/kernel/process.hpp` / `src/kernel/process.cpp` 最终未发生代码改动。Phase A 里预估需要适配 `ProcessContext.base_va` 语义，但现有实现本身只把 `base_va` 当 load address 使用，不参与入口选择，因此无需实际修改。
- 实现阶段新增了一项 Phase A 中未单列的收口：`Gateway::Load()` 对 `entry_offset` 增加了 load-time 校验（必须按 `kInstrBytes` 对齐，且必须落在代码范围内），把原本可能推迟到运行期的问题前移为配置错误。
- `test_executor.cpp` 中原有一条“通过非法起始 PC 触发运行期 misaligned trap”的旧断言不再成立；在 hidden-entry 语义下，这类非法首次入口现在会被 Gateway 在 Load 阶段拒绝，因此测试改写为执行期 `J` 指令把 PC 跳坏，以继续覆盖运行期 `INVALID_PC` 行为。

#### 4. 已知限制 & 下一步建议
- 当前 `Gateway` 对 code size 的判断优先使用真实序列化后的 `code_memory`，对手写测试包则回退到 window span；这兼容了当前原型与测试，但长期更稳妥的做法仍是让 SecureIR 明确携带 code size 或 segment layout，避免双路径推导。
- `saved_pc` 目前是“每次调用 `ExecuteProgram()` 的启动 PC”，还没有建模“执行中断后恢复到上次运行现场”的语义；如果后续需要模拟 preemption / resume，需要额外区分 immutable `saved_pc` 与 runtime PC。
- Demo 已全部适配 hidden entry，但没有把每个 demo 的 stdout 文案完全统一成”load address vs hidden entry”的术语；若后续继续打磨演示材料，可再统一输出措辞，避免把 `base_va` 误读为入口地址。

### Push 状态
**已推送：** 2026-03-27
**分支：** issue-12-hidden-entry
**提交：** issue 12: hidden entry / saved_PC — OS can no longer specify startup PC

## Issue 13：Audit 修复 + 代码质量
**日期：** 2026-03-27
**分支：** issue-13-audit-fix
**状态：** Phase A 方案已确认

### Phase A：计划冻结

#### 目标
修复审计归因不完整问题（PVT_MISMATCH 审计中 user_id=0），并清理局部代码重复与标签不一致。

#### 前置依赖
Issue 12（SecurityHardware 已持有 per-handle user_id）。

#### 改动清单

**改动 1：修复 PVT_MISMATCH 审计中 user_id=0 — AuditCollector resolver 方案**

PvtTable 在 PVT_MISMATCH missing_window 分支写审计时，user_id 来源是 EwcQueryResult.owner_user_id，无匹配窗口时默认为 0。根本原因是 PvtTable 无 handle→user_id 查询能力。

修复方案：在 AuditCollector 中新增 handle→user_id resolver。SecurityHardware 构造完成后将自身查询能力注入 AuditCollector。LogEvent 收到 user_id=0 + 有效 context_handle 时通过 resolver 补全；查不到则保持 0。

此方案优于 PvtTable 注入方案：resolver 在审计公共落点，所有写审计的模块自动受益；PvtTable 接口不变。

resolver 逻辑放在 LogEvent(AuditEvent) 层（公共落点），而非只在字符串重载中。

**改动 2：合并重复 OpToString**

executor.cpp 和 spe.cpp 各有一份逐 case 映射完全一致的 OpToString。合并为 isa/ 下的共享实现，两个消费方改为调用共享版本。

**改动 3：删除 demo_normal.cpp 死函数 ProgramEndVa**

demo_normal.cpp:16 的 ProgramEndVa 在该文件内零调用。securir_builder.cpp 中有独立的同名实现（带溢出保护），不受影响。

**改动 4：修正 SPE stage 标签**

spe.cpp 中 CALL/J/BEQ 违规的审计 stage 标签写为 "decode"，但 Executor 调用 CheckInstruction 的位置在执行语义完成之后（executor.cpp:466）。改为 "execute"。RET 的 "execute" 标签不变。

**改动 5：SPE Policy struct 添加 bounds 占位**

Policy struct 新增 bounds 占位字段 + 注释，不参与运行时逻辑，不影响 ConfigurePolicy 公开接口。

#### 文件范围

| 文件 | 改动 | 关联改动项 |
|------|------|-----------|
| `include/security/audit.hpp` | 修改（新增 resolver 成员 + setter） | 1 |
| `src/security/audit.cpp` | 修改（LogEvent 补全逻辑） | 1 |
| `include/security/hardware.hpp` | 修改（构造函数体注入 resolver） | 1 |
| `include/isa/opcode.hpp` | 修改（新增 OpToString 声明） | 2 |
| `src/isa/opcode.cpp` | 新建（OpToString 共享实现） | 2 |
| `src/core/executor.cpp` | 修改（删除私有 OpToString，改用共享版） | 2 |
| `src/security/spe.cpp` | 修改（删除私有 OpToString + stage 标签修正） | 2, 4 |
| `include/security/spe.hpp` | 修改（Policy bounds 占位） | 5 |
| `demos/normal/demo_normal.cpp` | 修改（删除死函数 ProgramEndVa） | 3 |
| `tests/test_pvt.cpp` | 修改（新增走 Gateway 的集成测试验证 user_id） | 1 |
| `tests/test_spe.cpp` | 修改（stage 标签断言从 decode 改为 execute） | 4 |
| `CMakeLists.txt` | 修改（新增 src/isa/opcode.cpp） | 2 |

#### 不变量

1. AuditCollector 对 user_id 非零的调用方无影响——只在 user_id=0 + 有效 handle 时尝试补全
2. user_id=0 作为"缺失值"语义须持续成立（当前无 user_id=0 的合法业务身份）
3. GATEWAY_LOAD_FAIL / GATEWAY_RELEASE 失败路径先删 metadata 再写审计，resolver 查不到，保持 0——不会误补全
4. OpToString 合并后行为不变，覆盖全部 12 个 Op 枚举值
5. bounds 占位不影响任何运行时行为

#### 测试目标

1. PVT_MISMATCH 审计事件中 user_id 非零且正确（新增集成测试，走 Gateway load 路径，放 test_pvt.cpp）
2. 现有 test_pvt.cpp 裸测试继续通过（不断言 user_id，不受影响）
3. SPE stage 标签断言从 decode 改为 execute（test_spe.cpp 3 处）
4. OpToString 合并后现有 SPE / Executor 测试回归通过
5. 全部 8 套 CTest 继续通过

#### 设计决策记录

**D-1：resolver 放 AuditCollector 而非 PvtTable**
- 决策：handle→user_id 补全逻辑放在 AuditCollector 公共落点。
- 原因："根据 handle 补全审计归因"是审计职责，不是 PVT 职责。所有写审计的模块自动受益，无需逐模块注入。PvtTable 接口零改动。

**D-2：user_id=0 视为缺失值**
- 决策：LogEvent 中 user_id=0 + 有效 context_handle → 尝试 resolver 补全；查不到保持 0。
- 原因：当前无 user_id=0 的合法业务身份。生产路径中 Gateway load 先写 metadata，补全一定成功。裸测试中无 metadata，保持 0 是正确的预期行为。

**D-3：OpToString 新建 src/isa/opcode.cpp**
- 决策：新建独立实现文件，不放在 opcode.hpp 中 inline。
- 原因：src/isa/ 下无现成 opcode utility 文件。独立 .cpp 与现有 assembler.cpp 并列，保持一致性。

**D-4：原第 1 项（EWC_ILLEGAL_PC audit user_id=0）从范围中移除**
- 决策：不在 Issue 13 中修复此项。
- 原因：Codex 可行性评估确认 Issue 12 已将 executor.cpp 中 EWC_ILLEGAL_PC 审计改为使用 metadata->user_id，当前基线不存在此问题。

#### Codex 可行性评估摘要（Step 2 + Step 3 复查）
- session_id: 019d2af7-ce9c-7a00-8f78-d681a2f3a3e8
- 总体判断：方案可行，无硬阻塞
- 第 1 轮（Step 2）：发现 EWC_ILLEGAL_PC 已在 Issue 12 修复；PVT resolver 方案可行但 test_pvt.cpp 有测试可达性风险
- 第 2 轮（Step 3 复查）：确认 AuditCollector resolver 方案可行，误补全风险极低，现有测试不受影响
- 初始化顺序安全：构造函数体执行时 handle_metadata_ 已完成，SpeTable/PvtTable 构造时不写审计
- LogEvent(AuditEvent) 层是正确的公共落点

### Phase B：实现复盘
**状态：** 已实现（未推送）
**提交：** TBD
**远端：** 未推送

#### 1. 变更文件清单
- 修改 `include/security/audit.hpp`：为 `AuditCollector` 增加 handle→`user_id` resolver 类型、setter 和成员存储。
- 修改 `src/security/audit.cpp`：在 `LogEvent(AuditEvent)` 公共落点实现 `user_id=0 + context_handle>0` 的补全逻辑；查不到时保持 0。
- 修改 `include/security/hardware.hpp`：在 `SecurityHardware` 构造函数体内注入 resolver，复用现有 per-handle metadata 查询能力。
- 修改 `include/isa/opcode.hpp`：新增共享 `OpToString` 声明。
- 新增 `src/isa/opcode.cpp`：落地 12 个 `Op` 枚举值到字符串的共享实现。
- 修改 `src/core/executor.cpp`：删除私有 `OpToString`，改为调用 `sim::isa::OpToString`。
- 修改 `src/security/spe.cpp`：删除私有 `OpToString`，改用共享实现；将 `CALL` / `J` / `BEQ` 违规审计的 `stage` 从 `decode` 修正为 `execute`。
- 修改 `include/security/spe.hpp`：给 `Policy` 增加 `bounds` 占位字段和注释，不参与运行时逻辑。
- 修改 `demos/normal/demo_normal.cpp`：删除未使用的死函数 `ProgramEndVa`。
- 修改 `tests/test_pvt.cpp`：新增走 `Gateway::Load()` 建立 metadata 后触发 `PVT_MISMATCH missing_window` 的集成测试，并补一条裸硬件路径 `user_id==0` 断言，锁定 resolver 的正/负路径。
- 修改 `tests/test_spe.cpp`：将 3 处 `stage=decode` 断言改为 `stage=execute`。
- 修改 `tests/test_gateway.cpp`：为现有 `GATEWAY_LOAD_FAIL` 测试显式补 `user_id==0` 断言；新增 `GATEWAY_RELEASE` 缺失 handle 的负路径测试，锁定“先删 metadata 再写 audit”时 resolver 不误补全。
- 修改 `tests/test_isa_assembler.cpp`：新增 `OpToString` 全覆盖回归测试，逐一校验 12 个枚举值的稳定字符串输出。
- 修改 `CMakeLists.txt`：将 `src/isa/opcode.cpp` 加入 `simulator_core`。
- 修改 `docs/project_log.md`：追加本次 Issue 13 的实现复盘。

#### 2. 测试与运行
- 已读取/确认：
  - `git diff --stat`
- 已执行命令：
  - `cmake --build build`
  - `ctest --test-dir build`
- 结果：
  - `git diff --stat` 显示当前代码改动集中在 14 个实现/测试文件，核心范围与 Issue 13 的 audit 修复、opcode 去重、SPE 标签修正及测试补强一致。
  - `cmake --build build` 输出 `ninja: no work to do.`，说明当前工作区代码已处于最新构建状态。
  - `ctest --test-dir build` 结果为 `100% tests passed, 0 tests failed out of 8`。

#### 3. 与 Phase A 方案的偏差
- 运行时代码实现与 Phase A 方案一致：`PvtTable` 接口未改，resolver 仍放在 `AuditCollector` 公共落点，`OpToString` 仍采用独立 `src/isa/opcode.cpp` 方案，`bounds` 仍只作为占位字段。
- 实现阶段在代码审查后额外补了 2 组低风险测试覆盖：`tests/test_gateway.cpp` 的 resolver 负路径断言，以及 `tests/test_isa_assembler.cpp` 的 `OpToString` 全覆盖回归。这两项不改变运行时行为，只是把 Phase A 中“误补全不会发生”和“12 个枚举值映射完整”这两条不变量显式锁进测试。

#### 4. 已知限制 & 下一步建议
- 当前把 `user_id=0` 固定当作“缺失值”处理；如果后续需要支持合法业务 `user_id=0`，Audit resolver 的触发条件需要改成显式 `optional` / tri-state 语义，而不是复用数值 0。
- `OpToString` 已有全覆盖回归，但未来若扩充 `Op` 枚举，仍需同步更新 `opcode.cpp` 和测试，否则默认分支会回落到 `"UNKNOWN"`。
- `Policy.bounds` 目前只是结构占位，没有任何校验或执行语义；后续若接入 bounds 规则，需要再补对应的配置入口、审计事件和测试。

### Push 状态
**已推送：** 2026-03-27
**分支：** issue-13-audit-fix
**提交：** issue 13: audit fix + code quality — AuditCollector resolver, OpToString merge, SPE stage labels

---

## Issue 14A：Demo 重命名 + same-user cross-process（方案确认）
**日期：** 2026-03-27
**分支：** issue-14a-demo-cross-process
**状态：** 方案已确认

### 初始需求（用户提出）
- 修正现有 demo 命名偏差：当前 `demos/cross_process/` 实际演示跨用户恶意映射（不同 user_id），与目录名不符
- 新增一个真正展示"同一 user_id、不同 context_handle 的代码执行隔离"的 demo

### 额外补充/优化需求（对话新增）
- PVT `IdentityMappedPageAllocator` 存在 same-VA 冲突风险：两个进程注册同一 VA 时 `pa_page_id = va / 4096` 相同，第二次 RegisterPage 会覆盖第一次。真实系统中 OS 分配不同物理页，identity mapping 打破了这一保证。为避免 Issue 14A demo 只因选用不同 base_va 而"碰巧 work"，将 PageAllocator 从 identity mapping 改为 monotonic counter（模拟 OS 分配独立物理页），纳入本 Issue 范围。
- 两个 demo 的 README 都详细重写。

### Coding 前最终方案

#### 改动 1：PVT PageAllocator 修复

将 `IdentityMappedPageAllocator`（`pa_page_id = va / kPageSize`）替换为 `MonotonicPageAllocator`（递增计数器），模拟"OS 给每个虚拟页分配独立物理页"。

影响面分析：
- 生产代码（process.cpp、gateway.cpp）均使用 `RegisterPage` 返回的 `pa_page_id`，不依赖 identity mapping 公式——无需改动
- `test_pvt.cpp` 中 5-6 处断言硬编码了 `va / kPageSize`——需改为使用 `result.pa_page_id`
- `PvtEntry.expected_va` 仍存储原始 VA，PVT 一致性检查语义不变

#### 改动 2：Demo 重命名

- `demos/cross_process/` → `demos/malicious_mapping/`
- `demo_cross_process.cpp` → `demo_malicious_mapping.cpp`
- CMakeLists.txt target `demo_cross_process` → `demo_malicious_mapping`
- 代码内异常输出字符串同步更新
- 详细重写 `demos/malicious_mapping/README.md`

#### 改动 3：新建 same-user cross-process demo

- 新建 `demos/cross_process/demo_cross_process.cpp`
- 场景：同一 user_id（1001）、不同 context_handle、不同 key_id、不同 base_va、不同 window_id
- Case A：进程 A 正常执行到 HALT
- Case B：context switch 到进程 B，B 的程序包含跳转到 A 代码地址的指令，被 EWC 拦截（`EWC_ILLEGAL_PC`）
- 构造模式参考 `demo_cross_user.cpp`：给 B 组一段含 `J` 跳转到 A base_va 的程序
- demo 输出中显式打印 alice_handle / bob_handle 值，因为 same-user 下 user_id 相同，区分度靠 context_handle
- 详细重写 `demos/cross_process/README.md`
- CMakeLists.txt 新增 `demo_cross_process` target

#### Claim 边界

本 demo 证明：**same-user 下 EWC per-handle 代码执行窗口隔离**（per-process execution window）。不 claim 完整的 same-user cross-process data isolation（SPE bounds 尚未实现完整 data bounds）。

#### 文件范围

| 文件 | 操作 | 关联改动项 |
|------|------|-----------|
| `src/security/pvt.cpp` | 修改（替换 allocator 实现） | 1 |
| `tests/test_pvt.cpp` | 修改（断言改用 result.pa_page_id） | 1 |
| `demos/cross_process/demo_cross_process.cpp` | 移动→ `demos/malicious_mapping/demo_malicious_mapping.cpp` | 2 |
| `demos/malicious_mapping/README.md` | 新建（详细重写） | 2 |
| `demos/cross_process/demo_cross_process.cpp` | 新建（same-user demo） | 3 |
| `demos/cross_process/README.md` | 新建（详细重写） | 3 |
| `CMakeLists.txt` | 修改（target 重命名 + 新增） | 2, 3 |

#### 不变量

1. MonotonicPageAllocator 保证每次 AllocatePageId 返回全局唯一值——同一 VA 不同进程不再冲突
2. PvtEntry.expected_va 仍存储原始 VA，PVT 一致性检查语义不变
3. demo_malicious_mapping 行为与原 demo_cross_process 完全一致（仅重命名，不改逻辑）
4. 新 demo_cross_process 中两个进程 user_id 相同，EWC 隔离仍生效（per-handle 语义）
5. 所有现有测试继续通过

#### 测试目标

1. `demo_malicious_mapping` exit code = 0，行为与原 demo_cross_process 完全一致
2. 新 `demo_cross_process`：Case A 到 HALT，Case B 触发 `EWC_ILLEGAL_PC`，审计事件包含正确 user_id 和 context_handle
3. `test_pvt.cpp` 全部通过（断言适配 monotonic allocator）
4. 全部 CTest 继续通过

#### 设计决策记录

**D-1：PageAllocator 从 identity mapping 改为 monotonic counter**
- 决策：替换默认 `IdentityMappedPageAllocator` 为 `MonotonicPageAllocator`
- 原因：identity mapping 导致同 VA 不同进程的 pa_page_id 冲突。same-user cross-process 是第一个自然暴露此问题的场景。`PageAllocator` 抽象接口本就是为此替换预留的扩展点。生产代码已使用返回值而非直接计算，改动影响面极小。

**D-2：新 demo 使用 cross_user 的跳转构造模式**
- 决策：给进程 B 组一段含 `J` 跳转到 A base_va 的程序，而非使用现有 demo_cross_process Case C 的"恶意 OS 写代码"模式
- 原因：Issue 14A 只需证明 EWC per-handle 隔离，跳转构造最干净直接。恶意 OS 注入模式是 ablation demo（Issue 14B）的范畴。

**D-3：demo 输出显式打印 handle 值**
- 决策：stdout 中打印 alice_handle / bob_handle
- 原因：same-user 场景下 user_id 相同，区分度靠 context_handle。显式打印增强可读性和 claim 论证清晰度。

#### Codex 可行性评估摘要
- session_id: 019d2d60-6e8f-7230-a7d8-df6c17ef483e
- 总体判断：方案可行，不需要改任何核心安全模块（EWC/SPE/Gateway/Executor）
- 6 项检查全部通过：SecureIrBuilderConfig 支持 same user_id / KernelProcessTable 无 user_id 唯一性约束 / EWC per-handle 隔离已成立 / CMake 改动无冲突 / cross_user 跳转构造可直接复用 / 审计 user_id 由 executor 直接写入，不依赖 resolver
- PageAllocator 冲突问题由用户在 Step 3 讨论中提出，确认纳入 Issue 14A 范围

### Phase B：实现复盘
**状态：** 已实现（未推送）
**提交：** TBD
**远端：** 未推送

#### 1. 变更文件清单
- 修改 `src/security/pvt.cpp`：将默认 `PageAllocator` 从 `IdentityMappedPageAllocator` 替换为 `MonotonicPageAllocator`，按 1, 2, 3... 递增分配 `pa_page_id`，避免 same-VA 跨进程注册冲突。
- 修改 `tests/test_pvt.cpp`：将依赖 `va / kPageSize` 的断言改为基于 `RegisterPage` 返回的 `result.pa_page_id`；`LoadProcess` 场景改为从 `ProcessContext.pvt_page_ids` 取回已注册页号。
- 新增 `demos/cross_process/demo_cross_process.cpp`：实现 same-user、different-context_handle 的跨进程执行隔离 demo；Case A 正常到 `HALT`，Case B 由 `EWC` 拦截跨窗口跳转并校验审计中的 `user_id`、`context_handle` 和目标 `pc`。
- 新增 `demos/cross_process/README.md`：说明 same-user per-process execution window 隔离的 demo 场景、预期结果和 claim 边界。
- 修改 `demos/malicious_mapping/demo_malicious_mapping.cpp`：由原 `demos/cross_process/demo_cross_process.cpp` 移动并重命名而来，保持三段式演示逻辑不变，仅同步目标名称和异常输出字符串。
- 新增 `demos/malicious_mapping/README.md`：补充 cross-user malicious page mapping 三个 case 的详细说明，并在后续代码审查修正 PVT 查询机制措辞。
- 修改 `CMakeLists.txt`：将原 `demo_cross_process` target 重命名为 `demo_malicious_mapping`，并新增新的 `demo_cross_process` target 指向 same-user demo。

#### 2. 测试与运行
- 已执行命令：
  - `cmake --build build`
  - `ctest --test-dir build`
  - `./build/demo_normal`
  - `./build/demo_cross_process`
  - `./build/demo_malicious_mapping`
- 结果：
  - `cmake --build build` 成功；复查阶段输出 `ninja: no work to do.`，说明构建产物与当前源码一致。
  - `ctest --test-dir build` 结果为 `100% tests passed, 0 tests failed out of 8`。
  - 8 个 CTest 套件全部通过：`test_sanity`、`test_isa_assembler`、`test_executor`、`test_gateway`、`test_kernel_process`、`test_pvt`、`test_spe`、`test_securir_builder`。
  - `demo_cross_process` 运行通过：Case A `FINAL_REASON=HALT`，Case B `FINAL_REASON=EWC_ILLEGAL_PC`，进程 B 的审计事件带有正确的 `user_id=1001` 和 B 自身的 `context_handle`，进程退出码为 0。
  - `demo_malicious_mapping` 运行通过：行为与旧 `demo_cross_process` 保持一致，进程退出码为 0。
  - `demo_normal` 运行通过，进程退出码为 0。

#### 3. 与 Phase A 方案的偏差
- 运行时代码实现与 Phase A 方案一致：`PageAllocator` 保持接口不变，旧 demo 仅做重命名迁移，新 same-user demo 采用预定的 `J` 跳转构造来证明 EWC 的 per-handle 隔离。
- 唯一偏差来自代码审查后的 README 返工（Step 6a）：`demos/malicious_mapping/README.md` 最初写成 “PVT queries Bob's active EWC windows”，而实际实现是 `PvtTable::RegisterPage()` 调用 `ewc_.Query(va, handle)`，按传入的 `context_handle` 查询其已注册 EWC 窗口。该措辞已在后续 coding session 中修正，不影响任何运行时代码或测试结果。

#### 4. 已知限制 & 下一步建议
- `MonotonicPageAllocator` 通过 `const` 方法里的 `mutable` 计数器递增生成页号；对当前单核 toy simulator 是可接受的，但它不是线程安全实现。若未来引入并发装载或多核仿真，需要改为显式同步或将 allocator 生命周期下沉到受控调度层。
- 当前 same-user demo 证明的是 per-process execution window 隔离，而不是完整的数据隔离或 same-user cross-process memory confidentiality；后续若要扩大 claim，仍需先补齐更完整的 `SPE bounds` / data path enforcement。
- `demo_malicious_mapping` 仍保留”恶意 OS 可覆写 Bob code region，再由 EWC 在取指阶段拦截”的防御纵深叙事；如果后续拆分成更纯粹的 ablation demo，可考虑在 Issue 14B 中进一步细分说明文档与输出标签。

### Push 状态
**已推送：** 2026-03-27
**分支：** issue-14a-demo-cross-process
**提交：** issue 14A: demo rename + same-user cross-process + MonotonicPageAllocator
