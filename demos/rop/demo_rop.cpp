#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "kernel/process.hpp"
#include "security/audit.hpp"
#include "security/code_codec.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "security/securir_package.hpp"

namespace {

std::string NumberArrayJson(const std::vector<std::uint64_t>& values) {
  std::ostringstream oss;
  oss << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      oss << ',';
    }
    oss << values[i];
  }
  oss << ']';
  return oss.str();
}

std::string MakeSecureIrJson(const std::string& program_name, std::uint32_t user_id, std::uint64_t base_va,
                             std::uint64_t end_va, std::uint32_t key_id, std::uint32_t window_id,
                             const std::string& signature, std::uint32_t cfi_level,
                             const std::vector<std::uint64_t>& call_targets,
                             const std::vector<std::uint64_t>& jmp_targets) {
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
      << "\"cfi_level\":" << cfi_level << ','
      << "\"call_targets\":" << NumberArrayJson(call_targets) << ','
      << "\"jmp_targets\":" << NumberArrayJson(jmp_targets)
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

std::uint64_t InstructionVa(std::uint64_t base_va, std::size_t index) {
  return base_va + index * sim::isa::kInstrBytes;
}

std::string MakeRopSource(std::uint64_t bad_addr) {
  std::ostringstream oss;
  oss << "main:\n"
      << "  CALL func\n"
      << "  HALT\n"
      << "func:\n"
      << "  LI x1, " << bad_addr << '\n'
      << "  RET\n"
      << "landing:\n"
      << "  HALT\n";
  return oss.str();
}

sim::security::ContextHandle LoadProgram(sim::kernel::KernelProcessTable* process_table,
                                         const sim::isa::AsmProgram& program, const std::string& program_name,
                                         std::uint32_t user_id, std::uint32_t key_id, std::uint32_t window_id,
                                         std::uint32_t cfi_level, const std::vector<std::uint64_t>& call_targets,
                                         const std::vector<std::uint64_t>& jmp_targets) {
  const sim::security::CipherProgram ciphertext = sim::security::EncryptProgram(program, key_id);
  const std::vector<std::uint8_t> code_memory = sim::security::BuildCodeMemory(ciphertext);
  const std::uint64_t end_va = program.base_va + program.code.size() * sim::isa::kInstrBytes;

  const sim::security::ContextHandle handle = process_table->LoadProcess(
      {MakeSecureIrJson(program_name, user_id, program.base_va, end_va, key_id, window_id, "stub-valid", cfi_level,
                        call_targets, jmp_targets),
       code_memory});
  process_table->ContextSwitch(handle);
  return handle;
}

sim::core::ExecResult RunProgram(sim::security::SecurityHardware* hardware, std::uint64_t base_va) {
  sim::core::ExecuteOptions options;
  options.hardware = hardware;
  return sim::core::ExecuteProgram(base_va, options);
}

sim::core::ExecResult RunCase(const char* label, sim::kernel::KernelProcessTable* process_table,
                              sim::security::SecurityHardware* hardware, const std::string& program_name,
                              const sim::isa::AsmProgram& program, std::uint32_t user_id, std::uint32_t key_id,
                              std::uint32_t window_id, std::uint32_t cfi_level,
                              const std::vector<std::uint64_t>& call_targets,
                              const std::vector<std::uint64_t>& jmp_targets) {
  sim::security::AuditCollector& audit = hardware->GetAuditCollector();
  audit.Clear();
  LoadProgram(process_table, program, program_name, user_id, key_id, window_id, cfi_level, call_targets, jmp_targets);

  const sim::core::ExecResult result = RunProgram(hardware, program.base_va);
  std::cout << '[' << label << "]\n";
  sim::core::PrintRunSummary(result, std::cout);
  PrintArtifacts(result, audit);
  return result;
}

}  // namespace

int main() {
  try {
    sim::security::SecurityHardware hardware;
    sim::security::Gateway gateway(hardware);
    sim::kernel::KernelProcessTable process_table(gateway, hardware, hardware.GetAuditCollector());

    const std::uint64_t case_a_base = 0x3000;
    const std::string case_a_source = R"(
main:
  CALL func
  HALT
func:
  RET
)";
    const sim::isa::AsmProgram case_a_program = sim::isa::AssembleText(case_a_source, case_a_base);
    const std::vector<std::uint64_t> case_a_call_targets = {InstructionVa(case_a_base, 2)};

    const std::uint64_t case_b_base = 0x4000;
    const std::uint64_t case_b_bad_addr = InstructionVa(case_b_base, 4);
    const sim::isa::AsmProgram case_b_program = sim::isa::AssembleText(MakeRopSource(case_b_bad_addr), case_b_base);
    const std::vector<std::uint64_t> case_b_call_targets = {InstructionVa(case_b_base, 2)};

    const std::uint64_t case_c1_base = 0x5000;
    const std::uint64_t case_c1_bad_addr = InstructionVa(case_c1_base, 4);
    const sim::isa::AsmProgram case_c1_program =
        sim::isa::AssembleText(MakeRopSource(case_c1_bad_addr), case_c1_base);
    const std::vector<std::uint64_t> case_c1_call_targets = {InstructionVa(case_c1_base, 2)};

    const std::uint64_t case_c2_base = 0x6000;
    const std::uint64_t case_c2_bad_addr = 3203334144ULL;
    const sim::isa::AsmProgram case_c2_program =
        sim::isa::AssembleText(MakeRopSource(case_c2_bad_addr), case_c2_base);
    const std::vector<std::uint64_t> case_c2_call_targets = {InstructionVa(case_c2_base, 2)};

    const sim::core::ExecResult case_a = RunCase("CASE_A_L3_NORMAL", &process_table, &hardware, "demo_rop_case_a",
                                                 case_a_program, 1, 11, 1, 3, case_a_call_targets, {});
    const sim::core::ExecResult case_b = RunCase("CASE_B_L3_ROP", &process_table, &hardware, "demo_rop_case_b",
                                                 case_b_program, 1, 11, 2, 3, case_b_call_targets, {});
    const sim::core::ExecResult case_c1 = RunCase("CASE_C1_L1_ROP_WINDOW_OK", &process_table, &hardware,
                                                  "demo_rop_case_c1", case_c1_program, 1, 11, 3, 1,
                                                  case_c1_call_targets, {});
    const sim::core::ExecResult case_c2 = RunCase("CASE_C2_L1_ROP_WINDOW_OOB", &process_table, &hardware,
                                                  "demo_rop_case_c2", case_c2_program, 1, 11, 4, 1,
                                                  case_c2_call_targets, {});

    return (case_a.trap.reason == sim::core::TrapReason::HALT &&
            case_b.trap.reason == sim::core::TrapReason::SPE_VIOLATION &&
            case_c1.trap.reason == sim::core::TrapReason::HALT &&
            case_c2.trap.reason == sim::core::TrapReason::EWC_ILLEGAL_PC)
               ? 0
               : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_rop failed: " << ex.what() << '\n';
    return 1;
  }
}
