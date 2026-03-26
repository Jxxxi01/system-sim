#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "kernel/process.hpp"
#include "security/audit.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "security/securir_builder.hpp"

namespace {

std::uint64_t ProgramEndVa(const sim::isa::AsmProgram& program) {
  return program.base_va + program.code.size() * sim::isa::kInstrBytes;
}

void PrintArtifacts(const sim::core::ExecResult& result, const sim::security::AuditCollector& audit) {
  for (const auto& event : audit.GetEvents()) {
    std::cout << "AUDIT " << sim::security::FormatAuditEvent(event) << '\n';
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

    sim::security::SecurityHardware hardware;
    sim::security::Gateway gateway(hardware);
    sim::kernel::KernelProcessTable process_table(gateway, hardware, hardware.GetAuditCollector());

    sim::security::SecureIrBuilderConfig allow_config;
    allow_config.program_name = "demo_normal_allow";
    allow_config.user_id = 1;
    allow_config.key_id = 11;
    allow_config.window_id = 1;

    hardware.GetAuditCollector().Clear();
    const sim::security::ContextHandle allow_handle =
        process_table.LoadProcess(sim::security::SecureIrBuilder::Build(program, allow_config));
    process_table.ContextSwitch(allow_handle);

    sim::core::ExecuteOptions allow_options;
    allow_options.hardware = &hardware;
    const sim::core::ExecResult allow_result = sim::core::ExecuteProgram(base_va, allow_options);

    std::cout << "[CASE_A_ALLOW]\n";
    sim::core::PrintRunSummary(allow_result, std::cout);
    PrintArtifacts(allow_result, hardware.GetAuditCollector());

    sim::security::SecureIrBuilderConfig wrong_key_config;
    wrong_key_config.program_name = "demo_normal_wrong_key";
    wrong_key_config.user_id = 1;
    wrong_key_config.key_id = 99;
    wrong_key_config.window_id = 2;

    sim::security::SecureIrPackage wrong_key_package = sim::security::SecureIrBuilder::Build(program, wrong_key_config);
    wrong_key_package.code_memory = sim::security::SecureIrBuilder::Build(program, allow_config).code_memory;

    hardware.GetAuditCollector().Clear();
    const sim::security::ContextHandle wrong_key_handle =
        process_table.LoadProcess(std::move(wrong_key_package));
    process_table.ContextSwitch(wrong_key_handle);

    sim::core::ExecuteOptions wrong_key_options;
    wrong_key_options.hardware = &hardware;
    const sim::core::ExecResult wrong_key_result = sim::core::ExecuteProgram(base_va, wrong_key_options);
    std::cout << "[CASE_B_WRONG_KEY]\n";
    sim::core::PrintRunSummary(wrong_key_result, std::cout);
    PrintArtifacts(wrong_key_result, hardware.GetAuditCollector());

    return (allow_result.trap.reason == sim::core::TrapReason::HALT &&
            wrong_key_result.trap.reason == sim::core::TrapReason::DECRYPT_DECODE_FAIL)
               ? 0
               : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_normal failed: " << ex.what() << '\n';
    return 1;
  }
}
