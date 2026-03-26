#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "isa/instr.hpp"
#include "kernel/process.hpp"
#include "security/audit.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "security/securir_builder.hpp"

namespace {

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
  try {
    const std::uint64_t alice_base_va = 0x1000;
    const std::uint64_t bob_case_b_base_va = 0x2000;
    const std::uint64_t bob_case_c_base_va = 0x3000;

    const std::string alice_source = R"(
  LI x1, 10
  LI x2, 32
  ADD x3, x1, x2
  HALT
)";

    std::ostringstream bob_source;
    bob_source << "LI x1, 1\n";
    bob_source << "J "
               << (static_cast<std::int64_t>(alice_base_va) -
                   static_cast<std::int64_t>(bob_case_b_base_va + 2 * sim::isa::kInstrBytes))
               << '\n';

    std::ostringstream bob_hidden_entry_source;
    bob_hidden_entry_source << "LI x7, 99\n";
    bob_hidden_entry_source << "J "
                            << (static_cast<std::int64_t>(alice_base_va) -
                                static_cast<std::int64_t>(bob_case_c_base_va + 2 * sim::isa::kInstrBytes))
                            << '\n';

    const sim::isa::AsmProgram alice_program = sim::isa::AssembleText(alice_source, alice_base_va);
    const sim::isa::AsmProgram bob_program = sim::isa::AssembleText(bob_source.str(), bob_case_b_base_va);
    const sim::isa::AsmProgram bob_hidden_entry_program =
        sim::isa::AssembleText(bob_hidden_entry_source.str(), bob_case_c_base_va);

    sim::security::SecurityHardware hardware;
    sim::security::Gateway gateway(hardware);
    sim::security::AuditCollector& audit = hardware.GetAuditCollector();
    sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

    sim::security::SecureIrBuilderConfig alice_config;
    alice_config.program_name = "alice_demo";
    alice_config.user_id = 1001;
    alice_config.key_id = 11;
    alice_config.window_id = 1;

    sim::security::SecureIrBuilderConfig bob_config;
    bob_config.program_name = "bob_demo";
    bob_config.user_id = 1002;
    bob_config.key_id = 22;
    bob_config.window_id = 2;

    sim::security::SecureIrBuilderConfig bob_hidden_entry_config;
    bob_hidden_entry_config.program_name = "bob_hidden_entry_demo";
    bob_hidden_entry_config.user_id = 1002;
    bob_hidden_entry_config.key_id = 22;
    bob_hidden_entry_config.window_id = 3;
    bob_hidden_entry_config.entry_offset = sim::isa::kInstrBytes;

    const sim::security::ContextHandle alice_handle =
        process_table.LoadProcess(sim::security::SecureIrBuilder::Build(alice_program, alice_config));
    const sim::security::ContextHandle bob_handle =
        process_table.LoadProcess(sim::security::SecureIrBuilder::Build(bob_program, bob_config));
    const sim::security::ContextHandle bob_hidden_entry_handle =
        process_table.LoadProcess(sim::security::SecureIrBuilder::Build(bob_hidden_entry_program,
                                                                        bob_hidden_entry_config));

    auto run_case = [&](const char* label, sim::security::ContextHandle handle) -> sim::core::ExecResult {
      audit.Clear();
      process_table.ContextSwitch(handle);

      sim::core::ExecuteOptions options;
      options.hardware = &hardware;
      const sim::core::ExecResult result = sim::core::ExecuteProgram(options);

      std::cout << '[' << label << "]\n";
      sim::core::PrintRunSummary(result, std::cout);
      PrintArtifacts(result, audit);
      return result;
    };

    const sim::core::ExecResult case_a = run_case("CASE_A_ALICE_NORMAL", alice_handle);
    const sim::core::ExecResult case_b = run_case("CASE_B_BOB_USER_ATTACK", bob_handle);
    const sim::core::ExecResult case_c = run_case("CASE_C_BOB_HIDDEN_ENTRY_ATTACK", bob_hidden_entry_handle);

    return (case_a.trap.reason == sim::core::TrapReason::HALT &&
            case_b.trap.reason == sim::core::TrapReason::EWC_ILLEGAL_PC &&
            case_c.trap.reason == sim::core::TrapReason::EWC_ILLEGAL_PC)
               ? 0
               : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_cross_user failed: " << ex.what() << '\n';
    return 1;
  }
}
