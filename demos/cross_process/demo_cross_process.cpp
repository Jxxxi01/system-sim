#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "isa/instr.hpp"
#include "kernel/process.hpp"
#include "security/audit.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "security/pvt.hpp"
#include "security/securir_builder.hpp"

namespace {

struct CaseAOutcome {
  sim::core::ExecResult result;
  bool has_gateway_load_ok = false;
};

struct CaseBOutcome {
  sim::security::PvtRegisterResult register_result;
  bool has_missing_window_audit = false;
};

void PrintAuditEvents(const sim::security::AuditCollector& audit) {
  for (const auto& event : audit.GetEvents()) {
    std::cout << "AUDIT " << sim::security::FormatAuditEvent(event) << '\n';
  }
}

void PrintArtifacts(const sim::core::ExecResult& result, const sim::security::AuditCollector& audit) {
  PrintAuditEvents(audit);
  for (const auto& trace : result.context_trace) {
    std::cout << "CTX " << trace << '\n';
  }
}

bool HasAuditType(const sim::security::AuditCollector& audit, const std::string& type) {
  for (const auto& event : audit.GetEvents()) {
    if (event.type == type) {
      return true;
    }
  }
  return false;
}

const sim::security::AuditEvent* FindAuditEvent(const sim::security::AuditCollector& audit, const std::string& type,
                                                const std::string& detail_substr) {
  for (const auto& event : audit.GetEvents()) {
    if (event.type == type && event.detail.find(detail_substr) != std::string::npos) {
      return &event;
    }
  }
  return nullptr;
}

sim::core::ExecResult RunProgram(sim::security::SecurityHardware& hardware, std::uint64_t entry_pc) {
  sim::core::ExecuteOptions options;
  options.hardware = &hardware;
  return sim::core::ExecuteProgram(entry_pc, options);
}

CaseAOutcome RunCaseA(const sim::isa::AsmProgram& alice_program, std::uint64_t alice_base_va) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  audit.Clear();

  sim::security::SecureIrBuilderConfig alice_config;
  alice_config.program_name = "alice_case_a";
  alice_config.user_id = 1001;
  alice_config.key_id = 11;
  alice_config.window_id = 1;
  alice_config.pages = {{alice_base_va, sim::security::PvtPageType::CODE}};

  const sim::security::ContextHandle alice_handle =
      process_table.LoadProcess(sim::security::SecureIrBuilder::Build(alice_program, alice_config));
  process_table.ContextSwitch(alice_handle);
  const sim::core::ExecResult result = RunProgram(hardware, alice_base_va);

  std::cout << "[CASE_A_NORMAL]\n";
  sim::core::PrintRunSummary(result, std::cout);
  PrintArtifacts(result, audit);
  std::cout << "CASE_A_GATEWAY_LOAD_OK=" << (HasAuditType(audit, "GATEWAY_LOAD_OK") ? "true" : "false") << '\n';

  return CaseAOutcome{result, HasAuditType(audit, "GATEWAY_LOAD_OK")};
}

CaseBOutcome RunCaseB(const sim::isa::AsmProgram& alice_program, std::uint64_t alice_base_va,
                      const sim::isa::AsmProgram& bob_program, std::uint64_t bob_base_va) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  audit.Clear();

  sim::security::SecureIrBuilderConfig alice_config;
  alice_config.program_name = "alice_case_b";
  alice_config.user_id = 1001;
  alice_config.key_id = 11;
  alice_config.window_id = 1;
  alice_config.pages = {{alice_base_va, sim::security::PvtPageType::CODE}};

  sim::security::SecureIrBuilderConfig bob_config;
  bob_config.program_name = "bob_case_b";
  bob_config.user_id = 1002;
  bob_config.key_id = 22;
  bob_config.window_id = 2;
  bob_config.pages = {{bob_base_va, sim::security::PvtPageType::CODE}};

  static_cast<void>(process_table.LoadProcess(sim::security::SecureIrBuilder::Build(alice_program, alice_config)));
  const sim::security::ContextHandle bob_handle =
      process_table.LoadProcess(sim::security::SecureIrBuilder::Build(bob_program, bob_config));

  const sim::security::PvtRegisterResult register_result =
      hardware.GetPvtTable().RegisterPage(bob_handle, alice_base_va, sim::security::PvtPageType::CODE);
  const sim::security::AuditEvent* mismatch_event =
      FindAuditEvent(audit, "PVT_MISMATCH", "reason=missing_window");

  std::cout << "[CASE_B_MALICIOUS_MAPPING]\n";
  std::cout << "REGISTER_PAGE_OK=" << (register_result.ok ? "true" : "false") << '\n';
  std::cout << "REGISTER_PAGE_ERROR=" << (register_result.error.empty() ? "<empty>" : register_result.error)
            << '\n';
  if (mismatch_event != nullptr) {
    std::cout << "CASE_B_MATCHED_AUDIT " << sim::security::FormatAuditEvent(*mismatch_event) << '\n';
  } else {
    std::cout << "CASE_B_MATCHED_AUDIT <missing>\n";
  }
  PrintAuditEvents(audit);

  return CaseBOutcome{register_result, mismatch_event != nullptr};
}

sim::core::ExecResult RunCaseC(const sim::isa::AsmProgram& alice_program, std::uint64_t alice_base_va,
                               const sim::isa::AsmProgram& bob_program, std::uint64_t bob_base_va) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  audit.Clear();

  sim::security::SecureIrBuilderConfig alice_config;
  alice_config.program_name = "alice_payload";
  alice_config.user_id = 1001;
  alice_config.key_id = 11;
  alice_config.window_id = 1;
  const std::vector<std::uint8_t> alice_code_memory =
      sim::security::SecureIrBuilder::Build(alice_program, alice_config).code_memory;

  sim::security::SecureIrBuilderConfig bob_config;
  bob_config.program_name = "bob_case_c";
  bob_config.user_id = 1002;
  bob_config.key_id = 22;
  bob_config.window_id = 2;

  const sim::security::ContextHandle bob_handle =
      process_table.LoadProcess(sim::security::SecureIrBuilder::Build(bob_program, bob_config));

  hardware.StoreCodeRegion(bob_handle, alice_base_va, alice_code_memory);
  process_table.ContextSwitch(bob_handle);
  const sim::core::ExecResult result = RunProgram(hardware, alice_base_va);

  std::cout << "[CASE_C_DEFENSE_IN_DEPTH]\n";
  sim::core::PrintRunSummary(result, std::cout);
  PrintArtifacts(result, audit);
  std::cout << "CASE_C_NOTE=PVT can be bypassed by a malicious OS write, but EWC still denies fetch at alice_base_va."
            << '\n';
  std::cout << "CASE_C_NOTE_2=PVT and EWC are complementary independent enforcement layers.\n";

  return result;
}

}  // namespace

int main() {
  try {
    const std::uint64_t alice_base_va = 0x1000;
    const std::uint64_t bob_base_va = 0x2000;

    const std::string alice_source = R"(
  LI x1, 10
  LI x2, 32
  ADD x3, x1, x2
  HALT
)";
    const std::string bob_source = R"(
  LI x4, 7
  HALT
)";

    const sim::isa::AsmProgram alice_program = sim::isa::AssembleText(alice_source, alice_base_va);
    const sim::isa::AsmProgram bob_program = sim::isa::AssembleText(bob_source, bob_base_va);

    const CaseAOutcome case_a = RunCaseA(alice_program, alice_base_va);
    const CaseBOutcome case_b = RunCaseB(alice_program, alice_base_va, bob_program, bob_base_va);
    const sim::core::ExecResult case_c = RunCaseC(alice_program, alice_base_va, bob_program, bob_base_va);

    return (case_a.result.trap.reason == sim::core::TrapReason::HALT && case_a.has_gateway_load_ok &&
            !case_b.register_result.ok && case_b.register_result.error.find("missing_window") != std::string::npos &&
            case_b.has_missing_window_audit && case_c.trap.reason == sim::core::TrapReason::EWC_ILLEGAL_PC)
               ? 0
               : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_cross_process failed: " << ex.what() << '\n';
    return 1;
  }
}
