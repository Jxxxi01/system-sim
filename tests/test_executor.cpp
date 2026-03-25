#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "security/audit.hpp"
#include "security/code_codec.hpp"
#include "security/ewc.hpp"
#include "security/hardware.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct RunOutcome {
  sim::core::ExecResult result;
  std::vector<sim::security::AuditEvent> audit_events;
};

sim::security::ExecWindow MakeCodeWindow(std::uint32_t window_id, std::uint64_t start_va, std::uint64_t end_va,
                                         std::uint32_t owner_user_id, std::uint32_t key_id,
                                         std::uint32_t code_policy_id = 1) {
  sim::security::ExecWindow window;
  window.window_id = window_id;
  window.start_va = start_va;
  window.end_va = end_va;
  window.owner_user_id = owner_user_id;
  window.key_id = key_id;
  window.type = sim::security::ExecWindowType::CODE;
  window.permissions = sim::security::MemoryPermissions::RX;
  window.code_policy_id = code_policy_id;
  return window;
}

sim::core::ExecResult RunAllowAll(const sim::isa::AsmProgram& program, std::uint64_t entry_pc,
                                  std::size_t mem_size = 64 * 1024, std::size_t max_steps = 1000000) {
  std::vector<std::uint8_t> code_memory;
  sim::security::SecurityHardware hardware;
  constexpr sim::security::ContextHandle kContextHandle = 1;
  if (!program.code.empty()) {
    const unsigned __int128 span =
        static_cast<unsigned __int128>(program.code.size()) * sim::isa::kInstrBytes;
    const unsigned __int128 end128 = static_cast<unsigned __int128>(program.base_va) + span;
    const unsigned __int128 max_u64 = static_cast<unsigned __int128>(std::numeric_limits<std::uint64_t>::max());
    if (end128 > max_u64) {
      sim::core::ExecResult error;
      error.state.pc = entry_pc;
      error.state.regs.fill(0);
      error.state.mem.assign(mem_size, 0);
      error.trap = sim::core::Trap{sim::core::TrapReason::INVALID_PC, entry_pc,
                                   "invalid_pc reason=allow_all_window_overflow"};
      return error;
    }

    hardware.GetEwcTable().SetWindows(
        kContextHandle,
        std::vector<sim::security::ExecWindow>{MakeCodeWindow(1, program.base_va, static_cast<std::uint64_t>(end128),
                                                              0, 0, 0)});
    code_memory = sim::security::BuildCodeMemory(sim::security::EncryptProgram(program, 0));
    hardware.StoreCodeRegion(kContextHandle, program.base_va, code_memory);
    hardware.SetActiveHandle(kContextHandle);
  }

  sim::core::ExecuteOptions options;
  options.mem_size = mem_size;
  options.max_steps = max_steps;
  options.hardware = &hardware;
  return sim::core::ExecuteProgram(entry_pc, options);
}

sim::core::ExecResult Run(const std::string& text, std::uint64_t base_va, std::size_t mem_size = 64 * 1024,
                          std::size_t max_steps = 1000000) {
  const sim::isa::AsmProgram program = sim::isa::AssembleText(text, base_va);
  return RunAllowAll(program, base_va, mem_size, max_steps);
}

RunOutcome RunWithEwc(const std::string& text, std::uint64_t base_va, const sim::security::EwcTable& ewc,
                      sim::security::ContextHandle context_handle,
                      const std::vector<std::uint8_t>* code_memory = nullptr,
                      std::size_t mem_size = 64 * 1024, std::size_t max_steps = 1000000) {
  const sim::isa::AsmProgram program = sim::isa::AssembleText(text, base_va);
  std::vector<std::uint8_t> local_code_memory;
  sim::security::SecurityHardware hardware;
  hardware.GetEwcTable() = ewc;
  if (code_memory == nullptr) {
    std::uint32_t key_id = 0;
    const sim::security::EwcQueryResult query_result = hardware.GetEwcTable().Query(base_va, context_handle);
    if (query_result.allow) {
      key_id = query_result.key_id;
    }
    local_code_memory = sim::security::BuildCodeMemory(sim::security::EncryptProgram(program, key_id));
    code_memory = &local_code_memory;
  }
  hardware.StoreCodeRegion(context_handle, base_va, *code_memory);
  hardware.SetActiveHandle(context_handle);

  sim::core::ExecuteOptions options;
  options.mem_size = mem_size;
  options.max_steps = max_steps;
  options.hardware = &hardware;
  RunOutcome outcome;
  outcome.result = sim::core::ExecuteProgram(base_va, options);
  outcome.audit_events = hardware.GetAuditCollector().GetEvents();
  return outcome;
}

sim::core::ExecResult RunWithHardware(const std::string& text, std::uint64_t base_va,
                                      sim::security::ContextHandle context_handle,
                                      std::uint32_t user_id = 1, std::uint32_t key_id = 0) {
  const sim::isa::AsmProgram program = sim::isa::AssembleText(text, base_va);
  const std::vector<std::uint8_t> code_memory =
      sim::security::BuildCodeMemory(sim::security::EncryptProgram(program, key_id));

  sim::security::SecurityHardware hardware;
  sim::security::ExecWindow window;
  window.window_id = 1;
  window.start_va = base_va;
  window.end_va = base_va + program.code.size() * sim::isa::kInstrBytes;
  window.owner_user_id = user_id;
  window.key_id = key_id;
  window.type = sim::security::ExecWindowType::CODE;
  window.permissions = sim::security::MemoryPermissions::RX;
  window.code_policy_id = 1;
  hardware.GetEwcTable().SetWindows(context_handle, std::vector<sim::security::ExecWindow>{window});
  hardware.StoreCodeRegion(context_handle, base_va, code_memory);
  hardware.SetActiveHandle(context_handle);

  sim::core::ExecuteOptions options;
  options.hardware = &hardware;
  return sim::core::ExecuteProgram(base_va, options);
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

  const auto underflow = RunAllowAll(program, 0x3ffc);
  SIM_EXPECT_EQ(underflow.trap.reason, sim::core::TrapReason::EWC_ILLEGAL_PC);
  SIM_EXPECT_TRUE(Contains(underflow.trap.msg, "pc="));
  SIM_EXPECT_TRUE(Contains(underflow.trap.msg, "window_id=none"));

  const auto misaligned = RunAllowAll(program, 0x4002);
  SIM_EXPECT_EQ(misaligned.trap.reason, sim::core::TrapReason::INVALID_PC);
  SIM_EXPECT_TRUE(Contains(misaligned.trap.msg, "reason=misaligned"));

  const auto oob = RunAllowAll(program, 0x4004);
  SIM_EXPECT_EQ(oob.trap.reason, sim::core::TrapReason::EWC_ILLEGAL_PC);
  SIM_EXPECT_TRUE(Contains(oob.trap.msg, "window_id=none"));
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

  const auto result = RunAllowAll(program, 0x7000);
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

SIM_TEST(Execute_EwcAllows_ProgramRunsToHalt) {
  const std::string src = R"(
  NOP
  HALT
)";
  sim::security::EwcTable ewc;
  const std::uint64_t base = 0x9000;
  sim::security::ExecWindow w;
  w.window_id = 1;
  w.start_va = base;
  w.end_va = base + 8;
  w.owner_user_id = 1;
  w.key_id = 11;
  w.type = sim::security::ExecWindowType::CODE;
  w.code_policy_id = 1;
  ewc.SetWindows(7, std::vector<sim::security::ExecWindow>{w});

  const RunOutcome outcome = RunWithEwc(src, base, ewc, 7);
  SIM_EXPECT_EQ(outcome.result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(outcome.audit_events.size(), static_cast<std::size_t>(0));
}

SIM_TEST(Executor_HardwarePath_NormalExecution) {
  const std::string src = R"(
  LI x1, 7
  LI x2, 9
  ADD x3, x1, x2
  HALT
)";

  const auto result = RunWithHardware(src, 0x9800, 17, 8, 41);
  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(result.state.regs[3], 16u);
  SIM_EXPECT_EQ(result.context_trace.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_TRUE(Contains(result.context_trace[0], "context_handle=17"));
}

SIM_TEST(Executor_HardwarePath_NoActiveHandle_Error) {
  sim::security::SecurityHardware hardware;

  sim::core::ExecuteOptions options;
  options.hardware = &hardware;
  const auto result = sim::core::ExecuteProgram(0x9900, options);

  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::INVALID_PC);
  SIM_EXPECT_EQ(result.trap.pc, 0x9900u);
  SIM_EXPECT_TRUE(Contains(result.trap.msg, "reason=no_active_context"));
}

SIM_TEST(Execute_EwcDenies_AtEntry_TrapsEwcIllegalPc) {
  const std::string src = R"(
  NOP
  HALT
)";
  sim::security::EwcTable ewc;
  const std::uint64_t base = 0xA000;
  sim::security::ExecWindow w;
  w.window_id = 2;
  w.start_va = base + 4;
  w.end_va = base + 8;
  w.owner_user_id = 1;
  w.key_id = 22;
  w.type = sim::security::ExecWindowType::CODE;
  w.code_policy_id = 1;
  ewc.SetWindows(9, std::vector<sim::security::ExecWindow>{w});

  const RunOutcome outcome = RunWithEwc(src, base, ewc, 9);
  SIM_EXPECT_EQ(outcome.result.trap.reason, sim::core::TrapReason::EWC_ILLEGAL_PC);
  SIM_EXPECT_EQ(outcome.audit_events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(outcome.audit_events[0].type, std::string("EWC_ILLEGAL_PC"));
  SIM_EXPECT_EQ(outcome.audit_events[0].context_handle, 9u);
  SIM_EXPECT_EQ(outcome.audit_events[0].pc, base);
  SIM_EXPECT_TRUE(Contains(outcome.audit_events[0].detail, "window_id=none"));
  SIM_EXPECT_TRUE(Contains(outcome.result.trap.msg, "pc="));
  SIM_EXPECT_TRUE(Contains(outcome.result.trap.msg, "context_handle=9"));
}

SIM_TEST(Execute_EwcSubsetWindow_JumpOut_TrapsEwcIllegalPc) {
  const std::string src = R"(
  J 8
  NOP
  NOP
  HALT
)";
  sim::security::EwcTable ewc;
  const std::uint64_t base = 0xB000;
  sim::security::ExecWindow w;
  w.window_id = 3;
  w.start_va = base;
  w.end_va = base + 8;  // Allow first 2 instructions only.
  w.owner_user_id = 2;
  w.key_id = 33;
  w.type = sim::security::ExecWindowType::CODE;
  w.code_policy_id = 1;
  ewc.SetWindows(10, std::vector<sim::security::ExecWindow>{w});

  const RunOutcome outcome = RunWithEwc(src, base, ewc, 10);
  SIM_EXPECT_EQ(outcome.result.trap.reason, sim::core::TrapReason::EWC_ILLEGAL_PC);
  SIM_EXPECT_EQ(outcome.result.trap.pc, base + 12);  // Jump target still inside AsmProgram range.
  SIM_EXPECT_EQ(outcome.audit_events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(outcome.audit_events[0].type, std::string("EWC_ILLEGAL_PC"));
  SIM_EXPECT_EQ(outcome.audit_events[0].context_handle, 10u);
  SIM_EXPECT_EQ(outcome.audit_events[0].pc, base + 12);
  SIM_EXPECT_TRUE(Contains(outcome.audit_events[0].detail, "window_id=none"));
  SIM_EXPECT_TRUE(Contains(outcome.result.trap.msg, "context_handle=10"));
}

SIM_TEST(Execute_PseudoDecrypt_CorrectKey_RunsToHalt) {
  const std::string src = R"(
  LI x1, 7
  LI x2, 9
  ADD x3, x1, x2
  HALT
)";
  const std::uint64_t base = 0xC000;
  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, base);
  const sim::security::CipherProgram ciphertext = sim::security::EncryptProgram(program, 41);
  const std::vector<std::uint8_t> code_memory = sim::security::BuildCodeMemory(ciphertext);

  sim::security::EwcTable ewc;
  sim::security::ExecWindow w;
  w.window_id = 4;
  w.start_va = base;
  w.end_va = base + program.code.size() * sim::isa::kInstrBytes;
  w.owner_user_id = 3;
  w.key_id = 41;
  w.type = sim::security::ExecWindowType::CODE;
  w.code_policy_id = 1;
  ewc.SetWindows(11, std::vector<sim::security::ExecWindow>{w});

  const RunOutcome outcome = RunWithEwc(src, base, ewc, 11, &code_memory);

  SIM_EXPECT_EQ(outcome.result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(outcome.result.state.regs[3], 16u);
  SIM_EXPECT_EQ(outcome.audit_events.size(), static_cast<std::size_t>(0));
}

SIM_TEST(Execute_PseudoDecrypt_WrongKey_TrapsDecryptDecodeFail) {
  const std::string src = R"(
  LI x1, 123
  HALT
)";
  const std::uint64_t base = 0xD000;
  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, base);
  const sim::security::CipherProgram ciphertext = sim::security::EncryptProgram(program, 55);
  const std::vector<std::uint8_t> code_memory = sim::security::BuildCodeMemory(ciphertext);

  sim::security::EwcTable ewc;
  sim::security::ExecWindow w;
  w.window_id = 5;
  w.start_va = base;
  w.end_va = base + program.code.size() * sim::isa::kInstrBytes;
  w.owner_user_id = 4;
  w.key_id = 77;
  w.type = sim::security::ExecWindowType::CODE;
  w.code_policy_id = 1;
  ewc.SetWindows(12, std::vector<sim::security::ExecWindow>{w});

  const RunOutcome outcome = RunWithEwc(src, base, ewc, 12, &code_memory);

  SIM_EXPECT_EQ(outcome.result.trap.reason, sim::core::TrapReason::DECRYPT_DECODE_FAIL);
  SIM_EXPECT_EQ(outcome.result.trap.pc, base);
  SIM_EXPECT_EQ(outcome.audit_events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(outcome.audit_events[0].type, std::string("DECRYPT_DECODE_FAIL"));
  SIM_EXPECT_EQ(outcome.audit_events[0].user_id, 4u);
  SIM_EXPECT_EQ(outcome.audit_events[0].context_handle, 12u);
  SIM_EXPECT_EQ(outcome.audit_events[0].pc, base);
  SIM_EXPECT_TRUE(Contains(outcome.audit_events[0].detail, "key_id=77"));
  SIM_EXPECT_TRUE(Contains(outcome.audit_events[0].detail, "reason=key_check_mismatch"));
  SIM_EXPECT_TRUE(Contains(outcome.result.trap.msg, "detail=key_check_mismatch"));
}

SIM_TEST(Execute_PseudoDecrypt_TamperedCiphertext_TrapsDecryptDecodeFail) {
  const std::string src = R"(
  LI x1, 5
  HALT
)";
  const std::uint64_t base = 0xE000;
  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, base);
  sim::security::CipherProgram ciphertext = sim::security::EncryptProgram(program, 91);
  ciphertext[0].payload[0] ^= 0x01u;
  const std::vector<std::uint8_t> code_memory = sim::security::BuildCodeMemory(ciphertext);

  sim::security::EwcTable ewc;
  sim::security::ExecWindow w;
  w.window_id = 6;
  w.start_va = base;
  w.end_va = base + program.code.size() * sim::isa::kInstrBytes;
  w.owner_user_id = 5;
  w.key_id = 91;
  w.type = sim::security::ExecWindowType::CODE;
  w.code_policy_id = 1;
  ewc.SetWindows(13, std::vector<sim::security::ExecWindow>{w});

  const RunOutcome outcome = RunWithEwc(src, base, ewc, 13, &code_memory);

  SIM_EXPECT_EQ(outcome.result.trap.reason, sim::core::TrapReason::DECRYPT_DECODE_FAIL);
  SIM_EXPECT_EQ(outcome.audit_events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(outcome.audit_events[0].type, std::string("DECRYPT_DECODE_FAIL"));
  SIM_EXPECT_EQ(outcome.audit_events[0].user_id, 5u);
  SIM_EXPECT_EQ(outcome.audit_events[0].context_handle, 13u);
  SIM_EXPECT_EQ(outcome.audit_events[0].pc, base);
  SIM_EXPECT_TRUE(Contains(outcome.audit_events[0].detail, "key_id=91"));
  SIM_EXPECT_TRUE(Contains(outcome.audit_events[0].detail, "reason=tag_mismatch"));
  SIM_EXPECT_TRUE(Contains(outcome.result.trap.msg, "detail=tag_mismatch"));
}

SIM_TEST(Ewc_SetWindows_OverlapRejected) {
  sim::security::EwcTable ewc;
  sim::security::ExecWindow a;
  a.window_id = 1;
  a.start_va = 0x1000;
  a.end_va = 0x1010;
  a.type = sim::security::ExecWindowType::CODE;
  sim::security::ExecWindow b;
  b.window_id = 2;
  b.start_va = 0x1008;
  b.end_va = 0x1020;
  b.type = sim::security::ExecWindowType::CODE;

  try {
    ewc.SetWindows(1, std::vector<sim::security::ExecWindow>{a, b});
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    const std::string msg = ex.what();
    SIM_EXPECT_TRUE(Contains(msg, "overlap"));
    SIM_EXPECT_TRUE(Contains(msg, "context_handle=1"));
  }
}

int main() {
  return sim::test::RunAll();
}
