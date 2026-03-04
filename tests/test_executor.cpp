#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace {

sim::core::ExecResult Run(const std::string& text, std::uint64_t base_va, std::size_t mem_size = 64 * 1024,
                          std::size_t max_steps = 1000000) {
  const sim::isa::AsmProgram program = sim::isa::AssembleText(text, base_va);
  return sim::core::ExecuteProgram(program, base_va, mem_size, max_steps);
}

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

SIM_TEST(Execute_SimpleArithmetic_ToHalt) {
  const std::string src = R"(
  LI x1, 5
  LI x2, -1
  ADD x3, x1, x1
  XOR x4, x3, x2
  HALT
)";
  const auto result = Run(src, 0x1000);
  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(result.trap.pc, 0x1010u);  // HALT instruction PC, not next_pc.
  SIM_EXPECT_EQ(result.state.regs[1], 5u);
  SIM_EXPECT_EQ(result.state.regs[2], std::numeric_limits<std::uint64_t>::max());
  SIM_EXPECT_EQ(result.state.regs[3], 10u);
  SIM_EXPECT_EQ(result.state.regs[0], 0u);
}

SIM_TEST(Execute_JumpAndBranch_PcRelativeCorrect) {
  const std::string src = R"(
  J 8
  LI x1, 1
  LI x1, 2
  LI x1, 3
  BEQ x1, x1, 4
  LI x2, 9
  HALT
)";
  const auto result = Run(src, 0x1200);
  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(result.state.regs[1], 3u);
  SIM_EXPECT_EQ(result.state.regs[2], 0u);
  SIM_EXPECT_EQ(result.trap.pc, 0x1218u);
}

SIM_TEST(Execute_CallRet_Works) {
  const std::string src = R"(
  LI x5, 0
  CALL func
  HALT
func:
  LI x5, 42
  RET
)";
  const auto result = Run(src, 0x2000);
  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(result.state.regs[5], 42u);
  SIM_EXPECT_EQ(result.state.regs[1], 0x2008u);  // next_pc of CALL
  SIM_EXPECT_EQ(result.trap.pc, 0x2008u);
}

SIM_TEST(Execute_LoadStore_InvalidAddr_Traps) {
  const std::string src = R"(
  LI x1, 252
  LI x2, 1234
  ST x2, [x1]
  HALT
)";
  const auto result = Run(src, 0x3000, 256);
  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::INVALID_MEMORY);
  SIM_EXPECT_EQ(result.trap.pc, 0x3008u);
  SIM_EXPECT_TRUE(Contains(result.trap.msg, "invalid_memory"));
  SIM_EXPECT_TRUE(Contains(result.trap.msg, "op=ST"));
  SIM_EXPECT_TRUE(Contains(result.trap.msg, "mem_size=256"));
}

SIM_TEST(Execute_InvalidPc_Cases_TrapWithDiagnostics) {
  const std::string src = R"(
  HALT
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, 0x4000);

  const auto underflow = sim::core::ExecuteProgram(program, 0x3ffc);
  SIM_EXPECT_EQ(underflow.trap.reason, sim::core::TrapReason::INVALID_PC);
  SIM_EXPECT_TRUE(Contains(underflow.trap.msg, "pc="));
  SIM_EXPECT_TRUE(Contains(underflow.trap.msg, "base_va="));
  SIM_EXPECT_TRUE(Contains(underflow.trap.msg, "index="));
  SIM_EXPECT_TRUE(Contains(underflow.trap.msg, "code_size="));
  SIM_EXPECT_TRUE(Contains(underflow.trap.msg, "reason=underflow"));

  const auto misaligned = sim::core::ExecuteProgram(program, 0x4002);
  SIM_EXPECT_EQ(misaligned.trap.reason, sim::core::TrapReason::INVALID_PC);
  SIM_EXPECT_TRUE(Contains(misaligned.trap.msg, "reason=misaligned"));

  const auto oob = sim::core::ExecuteProgram(program, 0x4004);
  SIM_EXPECT_EQ(oob.trap.reason, sim::core::TrapReason::INVALID_PC);
  SIM_EXPECT_TRUE(Contains(oob.trap.msg, "reason=oob"));
}

SIM_TEST(Execute_Syscall_LogsAndContinues) {
  const std::string src = R"(
  SYSCALL 7
  SYSCALL -1
  HALT
)";
  const auto result = Run(src, 0x5000);
  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(result.syscall_log.size(), static_cast<std::size_t>(2));
  SIM_EXPECT_TRUE(Contains(result.syscall_log[0], "SYSCALL imm=7 pc=20480"));
  SIM_EXPECT_TRUE(Contains(result.syscall_log[1], "SYSCALL imm=-1 pc=20484"));
}

SIM_TEST(Execute_StepLimit_TriggersTrap) {
  const std::string src = R"(
  J -4
)";
  const auto result = Run(src, 0x6000, 64 * 1024, 5);
  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::STEP_LIMIT);
  SIM_EXPECT_TRUE(Contains(result.trap.msg, "step_limit_exceeded"));
}

SIM_TEST(Execute_BadRegister_InInstr_Traps) {
  sim::isa::AsmProgram program;
  program.base_va = 0x7000;
  sim::isa::Instr broken;
  broken.op = sim::isa::Op::LI;
  broken.rd = -1;  // Invalid on purpose.
  broken.imm = 7;
  program.code.push_back(broken);

  const auto result = sim::core::ExecuteProgram(program, 0x7000);
  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::UNKNOWN_OPCODE);
  SIM_EXPECT_TRUE(Contains(result.trap.msg, "bad_reg"));
}

SIM_TEST(PrintRunSummary_UsesTrapPcAsFinalPc) {
  const std::string src = R"(
  HALT
)";
  const auto result = Run(src, 0x8000);
  std::ostringstream oss;
  sim::core::PrintRunSummary(result, oss);
  const std::string out = oss.str();
  SIM_EXPECT_TRUE(Contains(out, "FINAL_REASON=HALT"));
  SIM_EXPECT_TRUE(Contains(out, "FINAL_PC=32768"));
}

int main() {
  return sim::test::RunAll();
}
