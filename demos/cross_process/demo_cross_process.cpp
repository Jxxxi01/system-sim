#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "isa/instr.hpp"
#include "kernel/process.hpp"
#include "security/audit.hpp"
#include "security/code_codec.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "security/pvt.hpp"

namespace {

struct SecureIrPageSpec {
  std::uint64_t va = 0;
  const char* page_type = "DATA";
};

struct CaseAOutcome {
  sim::core::ExecResult result;
  bool has_gateway_load_ok = false;
};

struct CaseBOutcome {
  sim::security::PvtRegisterResult register_result;
  bool has_missing_window_audit = false;
};

std::string PagesJson(const std::vector<SecureIrPageSpec>& pages) {
  std::ostringstream oss;
  oss << '[';
  for (std::size_t i = 0; i < pages.size(); ++i) {
    if (i != 0) {
      oss << ',';
    }
    oss << "{"
        << "\"va\":" << pages[i].va << ','
        << "\"page_type\":\"" << pages[i].page_type << "\""
        << "}";
  }
  oss << ']';
  return oss.str();
}

std::string MakeSecureIrJson(const std::string& program_name, std::uint32_t user_id, std::uint64_t base_va,
                             std::uint64_t end_va, std::uint32_t key_id, std::uint32_t window_id,
                             const std::string& signature, const std::vector<SecureIrPageSpec>& pages) {
  std::ostringstream oss;
  oss << "{"
      << "\"program_name\":\"" << program_name << "\","
      << "\"user_id\":" << user_id << ","
      << "\"signature\":\"" << signature << "\","
      << "\"base_va\":" << base_va << ","
      << "\"windows\":[{"
      << "\"window_id\":" << window_id << ","
      << "\"start_va\":" << base_va << ","
      << "\"end_va\":" << end_va << ","
      << "\"key_id\":" << key_id << ","
      << "\"type\":\"CODE\","
      << "\"code_policy_id\":1"
      << "}],"
      << "\"pages\":" << PagesJson(pages) << ','
      << "\"cfi_level\":0,"
      << "\"call_targets\":[],"
      << "\"jmp_targets\":[]"
      << "}";
  return oss.str();
}

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

std::uint64_t ProgramEndVa(const sim::isa::AsmProgram& program) {
  return program.base_va + program.code.size() * sim::isa::kInstrBytes;
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

CaseAOutcome RunCaseA(const sim::isa::AsmProgram& alice_program, const std::vector<std::uint8_t>& alice_code_memory,
                      std::uint64_t alice_base_va) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  audit.Clear();

  const sim::security::ContextHandle alice_handle =
      process_table.LoadProcess(MakeSecureIrJson("alice_case_a", 1001, alice_base_va, ProgramEndVa(alice_program), 11,
                                                 1, "stub-valid", {{alice_base_va, "CODE"}}),
                                alice_code_memory);
  process_table.ContextSwitch(alice_handle);
  const sim::core::ExecResult result = RunProgram(hardware, alice_base_va);

  std::cout << "[CASE_A_NORMAL]\n";
  sim::core::PrintRunSummary(result, std::cout);
  PrintArtifacts(result, audit);
  std::cout << "CASE_A_GATEWAY_LOAD_OK=" << (HasAuditType(audit, "GATEWAY_LOAD_OK") ? "true" : "false") << '\n';

  return CaseAOutcome{result, HasAuditType(audit, "GATEWAY_LOAD_OK")};
}

CaseBOutcome RunCaseB(const sim::isa::AsmProgram& alice_program, const std::vector<std::uint8_t>& alice_code_memory,
                      std::uint64_t alice_base_va, const sim::isa::AsmProgram& bob_program,
                      const std::vector<std::uint8_t>& bob_code_memory, std::uint64_t bob_base_va) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  audit.Clear();

  static_cast<void>(process_table.LoadProcess(
      MakeSecureIrJson("alice_case_b", 1001, alice_base_va, ProgramEndVa(alice_program), 11, 1, "stub-valid",
                       {{alice_base_va, "CODE"}}),
      alice_code_memory));
  const sim::security::ContextHandle bob_handle =
      process_table.LoadProcess(MakeSecureIrJson("bob_case_b", 1002, bob_base_va, ProgramEndVa(bob_program), 22, 2,
                                                 "stub-valid", {{bob_base_va, "CODE"}}),
                                bob_code_memory);

  const sim::security::PvtRegisterResult register_result =
      hardware.GetPvtTable().RegisterPage(bob_handle, alice_base_va, sim::security::PvtPageType::CODE);
  const sim::security::AuditEvent* mismatch_event =
      FindAuditEvent(audit, "PVT_MISMATCH", "reason=missing_window");

  std::cout << "[CASE_B_MALICIOUS_MAPPING]\n";
  std::cout << "REGISTER_PAGE_OK=" << (register_result.ok ? "true" : "false") << '\n';
  std::cout << "REGISTER_PAGE_ERROR=" << (register_result.error.empty() ? "<empty>" : register_result.error) << '\n';
  if (mismatch_event != nullptr) {
    std::cout << "CASE_B_MATCHED_AUDIT " << sim::security::FormatAuditEvent(*mismatch_event) << '\n';
  } else {
    std::cout << "CASE_B_MATCHED_AUDIT <missing>\n";
  }
  PrintAuditEvents(audit);

  return CaseBOutcome{register_result, mismatch_event != nullptr};
}

sim::core::ExecResult RunCaseC(const sim::isa::AsmProgram& alice_program, const std::vector<std::uint8_t>& alice_code_memory,
                               std::uint64_t alice_base_va, const sim::isa::AsmProgram& bob_program,
                               const std::vector<std::uint8_t>& bob_code_memory, std::uint64_t bob_base_va) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  audit.Clear();

  const sim::security::ContextHandle bob_handle =
      process_table.LoadProcess(MakeSecureIrJson("bob_case_c", 1002, bob_base_va, ProgramEndVa(bob_program), 22, 2,
                                                 "stub-valid", {}),
                                bob_code_memory);

  hardware.StoreCodeRegion(bob_handle, alice_base_va, alice_code_memory);
  process_table.ContextSwitch(bob_handle);
  const sim::core::ExecResult result = RunProgram(hardware, alice_base_va);

  std::cout << "[CASE_C_DEFENSE_IN_DEPTH]\n";
  sim::core::PrintRunSummary(result, std::cout);
  PrintArtifacts(result, audit);
  std::cout << "CASE_C_NOTE=PVT can be bypassed by a malicious OS write, but EWC still denies fetch at alice_base_va."
            << '\n';
  std::cout << "CASE_C_NOTE_2=PVT and EWC are complementary independent enforcement layers." << '\n';

  static_cast<void>(alice_program);
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

    const std::vector<std::uint8_t> alice_code_memory =
        sim::security::BuildCodeMemory(sim::security::EncryptProgram(alice_program, 11));
    const std::vector<std::uint8_t> bob_code_memory =
        sim::security::BuildCodeMemory(sim::security::EncryptProgram(bob_program, 22));

    const CaseAOutcome case_a = RunCaseA(alice_program, alice_code_memory, alice_base_va);
    const CaseBOutcome case_b =
        RunCaseB(alice_program, alice_code_memory, alice_base_va, bob_program, bob_code_memory, bob_base_va);
    const sim::core::ExecResult case_c =
        RunCaseC(alice_program, alice_code_memory, alice_base_va, bob_program, bob_code_memory, bob_base_va);

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
