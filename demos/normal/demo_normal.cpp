#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "security/code_codec.hpp"
#include "security/ewc.hpp"

namespace {

void PrintArtifacts(const sim::core::ExecResult& result) {
  for (const auto& event : result.audit_log) {
    std::cout << "AUDIT " << event << '\n';
  }
  for (const auto& trace : result.context_trace) {
    std::cout << "CTX " << trace << '\n';
  }
}

}  // namespace

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
    const sim::security::CipherProgram ciphertext = sim::security::EncryptProgram(program, 11);

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
    allow_options.ciphertext = &ciphertext;
    const sim::core::ExecResult allow_result = sim::core::ExecuteProgram(program, base_va, allow_options);

    std::cout << "[CASE_A_ALLOW]\n";
    sim::core::PrintRunSummary(allow_result, std::cout);
    PrintArtifacts(allow_result);

    sim::security::EwcTable ewc_wrong_key;
    if (!program.code.empty()) {
      sim::security::ExecWindow wrong_key;
      wrong_key.window_id = 2;
      wrong_key.start_va = base_va;
      wrong_key.end_va = base_va + program.code.size() * sim::isa::kInstrBytes;
      wrong_key.owner_user_id = 1;
      wrong_key.key_id = 99;
      wrong_key.type = sim::security::ExecWindowType::CODE;
      wrong_key.code_policy_id = 1;
      ewc_wrong_key.SetWindows(2, std::vector<sim::security::ExecWindow>{wrong_key});
    }

    sim::core::ExecuteOptions wrong_key_options;
    wrong_key_options.context_handle = 2;
    wrong_key_options.ewc = &ewc_wrong_key;
    wrong_key_options.ciphertext = &ciphertext;
    const sim::core::ExecResult wrong_key_result =
        sim::core::ExecuteProgram(program, base_va, wrong_key_options);
    std::cout << "[CASE_B_WRONG_KEY]\n";
    sim::core::PrintRunSummary(wrong_key_result, std::cout);
    PrintArtifacts(wrong_key_result);

    return (allow_result.trap.reason == sim::core::TrapReason::HALT &&
            wrong_key_result.trap.reason == sim::core::TrapReason::DECRYPT_DECODE_FAIL)
               ? 0
               : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_normal failed: " << ex.what() << '\n';
    return 1;
  }
}
