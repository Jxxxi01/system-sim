#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "kernel/process.hpp"
#include "security/audit.hpp"
#include "security/code_codec.hpp"
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

sim::core::ExecResult RunProgram(sim::security::SecurityHardware* hardware) {
  sim::core::ExecuteOptions options;
  options.hardware = hardware;
  return sim::core::ExecuteProgram(options);
}

void XorWholeCiphertext(sim::security::SecurityHardware* hardware, sim::security::ContextHandle handle,
                        std::uint8_t mask) {
  sim::security::CodeRegion* region = hardware->GetCodeRegion(handle);
  if (region == nullptr) {
    throw std::runtime_error("missing_code_region_for_full_tamper");
  }

  for (std::uint8_t& byte : region->code_memory) {
    byte ^= mask;
  }
}

void XorPayloadBytes(sim::security::SecurityHardware* hardware, sim::security::ContextHandle handle,
                     std::size_t instr_index, std::uint8_t mask) {
  sim::security::CodeRegion* region = hardware->GetCodeRegion(handle);
  if (region == nullptr) {
    throw std::runtime_error("missing_code_region_for_partial_tamper");
  }

  const std::size_t unit_offset = instr_index * sim::security::kCipherUnitBytes;
  const std::size_t payload_end = unit_offset + 24;
  if (payload_end > region->code_memory.size()) {
    throw std::runtime_error("partial_tamper_out_of_range");
  }

  for (std::size_t i = unit_offset; i < payload_end; ++i) {
    region->code_memory[i] ^= mask;
  }
}

}  // namespace

int main() {
  const std::string source = R"(
start:
  LI x1, 7
  LI x2, 5
  ADD x3, x1, x2
  XOR x4, x3, x1
  HALT
)";

  try {
    const std::uint64_t base_va = 0x1000;
    const sim::isa::AsmProgram program = sim::isa::AssembleText(source, base_va);

    sim::security::SecurityHardware hardware;
    sim::security::Gateway gateway(hardware);
    sim::kernel::KernelProcessTable process_table(gateway, hardware, hardware.GetAuditCollector());

    auto build_package = [&](const std::string& program_name, std::uint32_t window_id) {
      sim::security::SecureIrBuilderConfig config;
      config.program_name = program_name;
      config.user_id = 1;
      config.key_id = 11;
      config.window_id = window_id;
      return sim::security::SecureIrBuilder::Build(program, config);
    };

    hardware.GetAuditCollector().Clear();
    const sim::security::ContextHandle baseline_handle =
        process_table.LoadProcess(build_package("demo_injection_case_a", 1));
    process_table.ContextSwitch(baseline_handle);
    const sim::core::ExecResult baseline_result = RunProgram(&hardware);
    std::cout << "[CASE_A_BASELINE]\n";
    sim::core::PrintRunSummary(baseline_result, std::cout);
    PrintArtifacts(baseline_result, hardware.GetAuditCollector());

    hardware.GetAuditCollector().Clear();
    const sim::security::ContextHandle full_tamper_handle =
        process_table.LoadProcess(build_package("demo_injection_case_b", 2));
    process_table.ContextSwitch(full_tamper_handle);
    XorWholeCiphertext(&hardware, full_tamper_handle, 0xFFu);
    const sim::core::ExecResult full_tamper_result = RunProgram(&hardware);
    std::cout << "[CASE_B_FULL_TAMPER]\n";
    sim::core::PrintRunSummary(full_tamper_result, std::cout);
    PrintArtifacts(full_tamper_result, hardware.GetAuditCollector());

    hardware.GetAuditCollector().Clear();
    const sim::security::ContextHandle partial_tamper_handle =
        process_table.LoadProcess(build_package("demo_injection_case_c", 3));
    process_table.ContextSwitch(partial_tamper_handle);
    XorPayloadBytes(&hardware, partial_tamper_handle, 3, 0xAAu);
    const sim::core::ExecResult partial_tamper_result = RunProgram(&hardware);
    std::cout << "[CASE_C_PARTIAL_TAMPER]\n";
    sim::core::PrintRunSummary(partial_tamper_result, std::cout);
    PrintArtifacts(partial_tamper_result, hardware.GetAuditCollector());

    return (baseline_result.trap.reason == sim::core::TrapReason::HALT &&
            full_tamper_result.trap.reason == sim::core::TrapReason::DECRYPT_DECODE_FAIL &&
            partial_tamper_result.trap.reason == sim::core::TrapReason::DECRYPT_DECODE_FAIL)
               ? 0
               : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_injection failed: " << ex.what() << '\n';
    return 1;
  }
}
