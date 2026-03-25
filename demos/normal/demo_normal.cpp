#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
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
    const std::vector<std::uint8_t> code_memory = sim::security::BuildCodeMemory(ciphertext);
    const std::uint64_t end_va = base_va + program.code.size() * sim::isa::kInstrBytes;

    sim::security::SecurityHardware hardware;
    sim::security::Gateway gateway(hardware);

    hardware.GetAuditCollector().Clear();
    const sim::security::ContextHandle allow_handle =
        gateway.Load(MakeSecureIrJson("demo_normal_allow", 1, base_va, end_va, 11, 1, "stub-valid"));
    hardware.StoreCodeRegion(allow_handle, base_va, code_memory);
    hardware.SetActiveHandle(allow_handle);

    sim::core::ExecuteOptions allow_options;
    allow_options.hardware = &hardware;
    const sim::core::ExecResult allow_result = sim::core::ExecuteProgram(base_va, allow_options);

    std::cout << "[CASE_A_ALLOW]\n";
    sim::core::PrintRunSummary(allow_result, std::cout);
    PrintArtifacts(allow_result, hardware.GetAuditCollector());

    hardware.GetAuditCollector().Clear();
    const sim::security::ContextHandle wrong_key_handle =
        gateway.Load(MakeSecureIrJson("demo_normal_wrong_key", 1, base_va, end_va, 99, 2, "stub-valid"));
    hardware.StoreCodeRegion(wrong_key_handle, base_va, code_memory);
    hardware.SetActiveHandle(wrong_key_handle);

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
