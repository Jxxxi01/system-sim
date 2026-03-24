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

namespace {

std::string MakeSecureIrJson(const std::string& program_name, std::uint32_t user_id, std::uint64_t base_va,
                             std::uint64_t end_va, std::uint32_t key_id, std::uint32_t window_id,
                             const std::string& signature) {
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
      << "\"pages\":[],"
      << "\"cfi_level\":0,"
      << "\"call_targets\":[],"
      << "\"jmp_targets\":[]"
      << "}";
  return oss.str();
}

void PrintArtifacts(const sim::core::ExecResult& result, const sim::security::AuditCollector& audit) {
  for (const auto& event : audit.GetEvents()) {
    std::cout << "AUDIT " << sim::security::FormatAuditEvent(event) << '\n';
  }
  for (const auto& trace : result.context_trace) {
    std::cout << "CTX " << trace << '\n';
  }
}

std::uint64_t ProgramEndVa(const sim::isa::AsmProgram& program) {
  return program.base_va + program.code.size() * sim::isa::kInstrBytes;
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

    std::ostringstream bob_source;
    bob_source << "LI x1, 1\n";
    bob_source << "J " << (static_cast<std::int64_t>(alice_base_va) -
                            static_cast<std::int64_t>(bob_base_va + 2 * sim::isa::kInstrBytes))
               << '\n';

    const sim::isa::AsmProgram alice_program = sim::isa::AssembleText(alice_source, alice_base_va);
    const sim::isa::AsmProgram bob_program = sim::isa::AssembleText(bob_source.str(), bob_base_va);

    const std::vector<std::uint8_t> alice_code_memory =
        sim::security::BuildCodeMemory(sim::security::EncryptProgram(alice_program, 11));
    const std::vector<std::uint8_t> bob_code_memory =
        sim::security::BuildCodeMemory(sim::security::EncryptProgram(bob_program, 22));

    sim::security::SecurityHardware hardware;
    sim::security::Gateway gateway(hardware);
    sim::security::AuditCollector& audit = hardware.GetAuditCollector();
    sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

    const sim::security::ContextHandle alice_handle =
        process_table.LoadProcess(MakeSecureIrJson("alice_demo", 1001, alice_base_va, ProgramEndVa(alice_program), 11,
                                                   1, "stub-valid"),
                                  alice_code_memory);
    const sim::security::ContextHandle bob_handle =
        process_table.LoadProcess(MakeSecureIrJson("bob_demo", 1002, bob_base_va, ProgramEndVa(bob_program), 22, 2,
                                                   "stub-valid"),
                                  bob_code_memory);

    auto run_case = [&](const char* label, sim::security::ContextHandle handle,
                        std::uint64_t entry_pc) -> sim::core::ExecResult {
      audit.Clear();
      process_table.ContextSwitch(handle);

      sim::core::ExecuteOptions options;
      options.hardware = &hardware;
      const sim::core::ExecResult result = sim::core::ExecuteProgram(entry_pc, options);

      std::cout << '[' << label << "]\n";
      sim::core::PrintRunSummary(result, std::cout);
      PrintArtifacts(result, audit);
      return result;
    };

    const sim::core::ExecResult case_a = run_case("CASE_A_ALICE_NORMAL", alice_handle, alice_base_va);
    const sim::core::ExecResult case_b = run_case("CASE_B_BOB_USER_ATTACK", bob_handle, bob_base_va);
    const sim::core::ExecResult case_c = run_case("CASE_C_BOB_MALICIOUS_OS", bob_handle, alice_base_va);

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
