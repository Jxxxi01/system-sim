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

struct CaseOutcome {
  sim::core::ExecResult result;
  bool has_expected_ewc_audit = false;
};

void PrintArtifacts(const sim::core::ExecResult& result, const sim::security::AuditCollector& audit) {
  for (const auto& event : audit.GetEvents()) {
    std::cout << "AUDIT " << sim::security::FormatAuditEvent(event) << '\n';
  }
  for (const auto& trace : result.context_trace) {
    std::cout << "CTX " << trace << '\n';
  }
}

const sim::security::AuditEvent* FindAuditEvent(const sim::security::AuditCollector& audit, const std::string& type,
                                                std::uint32_t user_id, sim::security::ContextHandle handle,
                                                std::uint64_t pc) {
  for (const auto& event : audit.GetEvents()) {
    if (event.type == type && event.user_id == user_id && event.context_handle == handle && event.pc == pc) {
      return &event;
    }
  }
  return nullptr;
}

sim::core::ExecResult RunProgram(sim::security::SecurityHardware& hardware) {
  sim::core::ExecuteOptions options;
  options.hardware = &hardware;
  return sim::core::ExecuteProgram(options);
}

sim::security::SecureIrBuilderConfig MakeConfig(const std::string& program_name, std::uint32_t user_id,
                                                std::uint32_t key_id, std::uint32_t window_id,
                                                std::uint64_t base_va) {
  sim::security::SecureIrBuilderConfig config;
  config.program_name = program_name;
  config.user_id = user_id;
  config.key_id = key_id;
  config.window_id = window_id;
  config.pages = {{base_va, sim::security::PvtPageType::CODE}};
  return config;
}

CaseOutcome RunCaseA(const sim::isa::AsmProgram& program_a, std::uint64_t base_va_a) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  const sim::security::ContextHandle handle_a =
      process_table.LoadProcess(sim::security::SecureIrBuilder::Build(
          program_a, MakeConfig("same_user_process_a", 1001, 11, 1, base_va_a)));
  process_table.ContextSwitch(handle_a);
  const sim::core::ExecResult result = RunProgram(hardware);

  std::cout << "[CASE_A_NORMAL_EXECUTION]\n";
  std::cout << "HANDLE_A=" << handle_a << '\n';
  sim::core::PrintRunSummary(result, std::cout);
  PrintArtifacts(result, audit);

  return CaseOutcome{result, false};
}

CaseOutcome RunCaseB(const sim::isa::AsmProgram& program_a, std::uint64_t base_va_a,
                     const sim::isa::AsmProgram& program_b, std::uint64_t base_va_b) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  const sim::security::ContextHandle handle_a =
      process_table.LoadProcess(sim::security::SecureIrBuilder::Build(
          program_a, MakeConfig("same_user_process_a", 1001, 11, 1, base_va_a)));
  const sim::security::ContextHandle handle_b =
      process_table.LoadProcess(sim::security::SecureIrBuilder::Build(
          program_b, MakeConfig("same_user_process_b", 1001, 12, 2, base_va_b)));

  process_table.ContextSwitch(handle_b);
  const sim::core::ExecResult result = RunProgram(hardware);
  const sim::security::AuditEvent* ewc_event =
      FindAuditEvent(audit, "EWC_ILLEGAL_PC", 1001, handle_b, base_va_a);

  std::cout << "[CASE_B_SAME_USER_CROSS_PROCESS]\n";
  std::cout << "HANDLE_A=" << handle_a << '\n';
  std::cout << "HANDLE_B=" << handle_b << '\n';
  sim::core::PrintRunSummary(result, std::cout);
  std::cout << "EXPECTED_EWC_AUDIT=" << (ewc_event != nullptr ? "true" : "false") << '\n';
  if (ewc_event != nullptr) {
    std::cout << "MATCHED_AUDIT " << sim::security::FormatAuditEvent(*ewc_event) << '\n';
  } else {
    std::cout << "MATCHED_AUDIT <missing>\n";
  }
  PrintArtifacts(result, audit);

  return CaseOutcome{result, ewc_event != nullptr};
}

}  // namespace

int main() {
  try {
    const std::uint64_t base_va_a = 0x1000;
    const std::uint64_t base_va_b = 0x2000;

    const std::string process_a_source = R"(
  LI x1, 10
  LI x2, 32
  ADD x3, x1, x2
  HALT
)";

    std::ostringstream process_b_source;
    process_b_source << "LI x4, 7\n";
    process_b_source << "J "
                     << (static_cast<std::int64_t>(base_va_a) -
                         static_cast<std::int64_t>(base_va_b + 2 * sim::isa::kInstrBytes))
                     << '\n';
    process_b_source << "HALT\n";

    const sim::isa::AsmProgram program_a = sim::isa::AssembleText(process_a_source, base_va_a);
    const sim::isa::AsmProgram program_b = sim::isa::AssembleText(process_b_source.str(), base_va_b);

    const CaseOutcome case_a = RunCaseA(program_a, base_va_a);
    const CaseOutcome case_b = RunCaseB(program_a, base_va_a, program_b, base_va_b);

    return (case_a.result.trap.reason == sim::core::TrapReason::HALT &&
            case_b.result.trap.reason == sim::core::TrapReason::EWC_ILLEGAL_PC &&
            case_b.has_expected_ewc_audit)
               ? 0
               : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_cross_process failed: " << ex.what() << '\n';
    return 1;
  }
}
