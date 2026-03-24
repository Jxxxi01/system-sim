#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "security/audit.hpp"
#include "security/ewc.hpp"

namespace sim::core {

enum class TrapReason {
  HALT,
  INVALID_PC,
  INVALID_MEMORY,
  SYSCALL_FAIL,  // Reserved for later issues; not triggered in issue 2.
  UNKNOWN_OPCODE,
  STEP_LIMIT,
  EWC_ILLEGAL_PC,
  DECRYPT_DECODE_FAIL
};

struct Trap {
  TrapReason reason = TrapReason::UNKNOWN_OPCODE;
  std::uint64_t pc = 0;
  std::string msg;
};

struct CpuState {
  std::uint64_t pc = 0;
  std::array<std::uint64_t, 32> regs{};
  std::vector<std::uint8_t> mem;
};

struct ExecResult {
  Trap trap;
  CpuState state;
  std::vector<std::string> context_trace;
  std::vector<std::string> syscall_log;
};

struct ExecuteOptions {
  std::size_t mem_size = 64 * 1024;
  std::size_t max_steps = 1000000;
  sim::security::ContextHandle context_handle = 0;
  const sim::security::EwcTable* ewc = nullptr;
  sim::security::AuditCollector* audit = nullptr;
  std::uint64_t region_base_va = 0;
  const std::uint8_t* code_memory = nullptr;
  std::size_t code_memory_size = 0;
};

ExecResult ExecuteProgram(std::uint64_t entry_pc, const ExecuteOptions& options);

const char* TrapReasonToString(TrapReason reason);
void PrintRunSummary(const ExecResult& result, std::ostream& os);

}  // namespace sim::core
