#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "security/ewc.hpp"

int main() {
  const std::string source = R"(
start:
  LI x1, 10
  LI x2, 32
  ADD x3, x1, x2
  SYSCALL 1
  HALT
)";

  try {
    const std::uint64_t base_va = 0x1000;
    const sim::isa::AsmProgram program = sim::isa::AssembleText(source, base_va);

    sim::security::EwcTable ewc_allow;
    if (!program.code.empty()) {
      sim::security::ExecWindow allow_all;
      allow_all.window_id = 1;
      allow_all.start_va = base_va;
      allow_all.end_va = base_va + program.code.size() * sim::isa::kInstrBytes;
      allow_all.owner_user_id = 1;
      allow_all.key_id = 11;
      allow_all.type = sim::security::ExecWindowType::CODE;
      allow_all.code_policy_id = 1;
      ewc_allow.SetWindows(1, std::vector<sim::security::ExecWindow>{allow_all});
    }

    sim::core::ExecuteOptions allow_options;
    allow_options.context_handle = 1;
    allow_options.ewc = &ewc_allow;
    const sim::core::ExecResult allow_result = sim::core::ExecuteProgram(program, base_va, allow_options);

    std::cout << "[CASE_A_ALLOW]\n";
    sim::core::PrintRunSummary(allow_result, std::cout);

    sim::security::EwcTable ewc_deny;
    if (!program.code.empty()) {
      sim::security::ExecWindow subset;
      subset.window_id = 2;
      subset.start_va = base_va;
      subset.end_va = base_va + sim::isa::kInstrBytes;
      subset.owner_user_id = 1;
      subset.key_id = 11;
      subset.type = sim::security::ExecWindowType::CODE;
      subset.code_policy_id = 1;
      ewc_deny.SetWindows(2, std::vector<sim::security::ExecWindow>{subset});
    }

    sim::core::ExecuteOptions deny_options;
    deny_options.context_handle = 2;
    deny_options.ewc = &ewc_deny;
    const sim::core::ExecResult deny_result = sim::core::ExecuteProgram(program, base_va, deny_options);
    std::cout << "[CASE_B_DENY]\n";
    sim::core::PrintRunSummary(deny_result, std::cout);
    for (const auto& event : deny_result.audit_log) {
      std::cout << "AUDIT " << event << '\n';
    }

    return (allow_result.trap.reason == sim::core::TrapReason::HALT &&
            deny_result.trap.reason == sim::core::TrapReason::EWC_ILLEGAL_PC)
               ? 0
               : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_normal failed: " << ex.what() << '\n';
    return 1;
  }
}
