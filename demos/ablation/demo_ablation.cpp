#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "isa/instr.hpp"
#include "kernel/process.hpp"
#include "security/audit.hpp"
#include "security/ewc.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "security/securir_builder.hpp"

namespace {

constexpr std::uint64_t kAliceBaseVa = 0x1000;
constexpr std::uint64_t kBobLayer1BaseVa = 0x2000;
constexpr std::uint32_t kAliceUserId = 1001;
constexpr std::uint32_t kBobUserId = 1002;
constexpr std::uint32_t kAliceKeyId = 11;
constexpr std::uint32_t kBobKeyId = 22;
constexpr std::uint64_t kExpectedAliceX3 = 42;

struct LayerOutcome {
  sim::core::ExecResult result;
  bool has_expected_audit = false;
  bool has_bob_ctx_switch = false;
};

void PrintArtifacts(const sim::core::ExecResult& result, const sim::security::AuditCollector& audit) {
  for (const auto& event : audit.GetEvents()) {
    std::cout << "AUDIT " << sim::security::FormatAuditEvent(event) << '\n';
  }
  for (const auto& trace : result.context_trace) {
    std::cout << "CTX " << trace << '\n';
  }
}

std::uint64_t ProgramEndVa(const sim::isa::AsmProgram& program) {
  return program.base_va + static_cast<std::uint64_t>(program.code.size()) * sim::isa::kInstrBytes;
}

bool HasAuditEvent(const sim::security::AuditCollector& audit, const std::string& type, std::uint32_t user_id) {
  for (const auto& event : audit.GetEvents()) {
    if (event.type == type && event.user_id == user_id) {
      return true;
    }
  }
  return false;
}

bool HasCtxSwitchForBob(const sim::security::AuditCollector& audit) {
  return HasAuditEvent(audit, "CTX_SWITCH", kBobUserId);
}

std::optional<sim::security::ContextHandle> FindCtxSwitchHandle(const sim::security::AuditCollector& audit,
                                                                std::uint32_t user_id) {
  for (const auto& event : audit.GetEvents()) {
    if (event.type == "CTX_SWITCH" && event.user_id == user_id) {
      return event.context_handle;
    }
  }
  return std::nullopt;
}

sim::core::ExecResult RunProgram(sim::security::SecurityHardware& hardware) {
  sim::core::ExecuteOptions options;
  options.hardware = &hardware;
  return sim::core::ExecuteProgram(options);
}

sim::core::ExecResult RunHandle(sim::kernel::KernelProcessTable& process_table, sim::security::SecurityHardware& hardware,
                                sim::security::ContextHandle handle) {
  hardware.GetAuditCollector().Clear();
  process_table.ContextSwitch(handle);
  return RunProgram(hardware);
}

sim::security::SecureIrPackage BuildPackage(const sim::isa::AsmProgram& program, const std::string& program_name,
                                            std::uint32_t user_id, std::uint32_t key_id, std::uint32_t window_id,
                                            std::uint32_t cfi_level = 0) {
  sim::security::SecureIrBuilderConfig config;
  config.program_name = program_name;
  config.user_id = user_id;
  config.key_id = key_id;
  config.window_id = window_id;
  config.cfi_level = cfi_level;
  return sim::security::SecureIrBuilder::Build(program, config);
}

void StoreAliceCiphertextIntoBob(sim::security::SecurityHardware& hardware, sim::security::ContextHandle alice_handle,
                                 sim::security::ContextHandle bob_handle, std::uint64_t bob_base_va) {
  const sim::security::CodeRegion* alice_region = hardware.GetCodeRegion(alice_handle);
  if (alice_region == nullptr) {
    throw std::runtime_error("ablation_missing_alice_code_region");
  }
  hardware.StoreCodeRegion(bob_handle, bob_base_va, alice_region->code_memory);
}

void SetWildcardWindow(sim::security::SecurityHardware& hardware, sim::security::ContextHandle handle,
                       std::uint64_t start_va, std::uint64_t end_va, std::uint32_t owner_user_id,
                       std::uint32_t key_id, std::uint32_t window_id) {
  sim::security::ExecWindow window;
  window.window_id = window_id;
  window.start_va = start_va;
  window.end_va = end_va;
  window.owner_user_id = owner_user_id;
  window.key_id = key_id;
  window.type = sim::security::ExecWindowType::CODE;
  window.permissions = sim::security::MemoryPermissions::RX;
  window.code_policy_id = 1;
  hardware.GetEwcTable().SetWindows(handle, std::vector<sim::security::ExecWindow>{window});
}

sim::isa::AsmProgram BuildAliceProgram() {
  const std::string source = R"(
  LI x1, 10
  LI x2, 32
  ADD x3, x1, x2
  HALT
)";
  return sim::isa::AssembleText(source, kAliceBaseVa);
}

sim::isa::AsmProgram BuildBobLayer1Program() {
  std::ostringstream source;
  source << "LI x4, 1\n";
  source << "J "
         << (static_cast<std::int64_t>(kAliceBaseVa) -
             static_cast<std::int64_t>(kBobLayer1BaseVa + 2 * sim::isa::kInstrBytes))
         << '\n';
  return sim::isa::AssembleText(source.str(), kBobLayer1BaseVa);
}

sim::isa::AsmProgram BuildBobDummyProgram(std::uint64_t base_va) {
  const std::string source = R"(
  LI x3, 1
  HALT
)";
  return sim::isa::AssembleText(source, base_va);
}

LayerOutcome RunLayer1() {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  const sim::isa::AsmProgram alice_program = BuildAliceProgram();
  const sim::isa::AsmProgram bob_program = BuildBobLayer1Program();

  const sim::security::ContextHandle alice_handle =
      process_table.LoadProcess(BuildPackage(alice_program, "ablation_layer1_alice", kAliceUserId, kAliceKeyId, 1));
  const sim::security::ContextHandle bob_handle =
      process_table.LoadProcess(BuildPackage(bob_program, "ablation_layer1_bob", kBobUserId, kBobKeyId, 2));
  static_cast<void>(alice_handle);

  const sim::core::ExecResult result = RunHandle(process_table, hardware, bob_handle);

  std::cout << "[LAYER_1_FULL_SECURITY]\n";
  std::cout << "SUMMARY=Full EWC isolation, distinct base_va, distinct key_id.\n";
  std::cout << "ATTACK=Bob jumps from 0x2000 into Alice window at 0x1000.\n";
  sim::core::PrintRunSummary(result, std::cout);
  PrintArtifacts(result, audit);

  return LayerOutcome{result, HasAuditEvent(audit, "EWC_ILLEGAL_PC", kBobUserId), HasCtxSwitchForBob(audit)};
}

LayerOutcome RunLayer2() {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  const sim::isa::AsmProgram alice_program = BuildAliceProgram();
  const sim::isa::AsmProgram bob_dummy_program = BuildBobDummyProgram(kAliceBaseVa);
  const std::uint64_t wildcard_end_va = ProgramEndVa(alice_program);

  const sim::security::ContextHandle alice_handle =
      process_table.LoadProcess(BuildPackage(alice_program, "ablation_layer2_alice", kAliceUserId, kAliceKeyId, 1));
  const sim::security::ContextHandle bob_handle =
      process_table.LoadProcess(BuildPackage(bob_dummy_program, "ablation_layer2_bob_dummy", kBobUserId, kBobKeyId, 2));

  StoreAliceCiphertextIntoBob(hardware, alice_handle, bob_handle, kAliceBaseVa);
  SetWildcardWindow(hardware, bob_handle, kAliceBaseVa, wildcard_end_va, kBobUserId, kBobKeyId, 200);

  const sim::core::ExecResult result = RunHandle(process_table, hardware, bob_handle);

  std::cout << "[LAYER_2_EWC_BYPASSED_KEY_BARRIER]\n";
  std::cout << "SUMMARY=Bob reuses Alice base_va and gets a wildcard EWC window, but keeps Bob key_id=22.\n";
  std::cout << "ATTACK=Malicious OS overwrites Bob code_region with Alice ciphertext after LoadProcess.\n";
  sim::core::PrintRunSummary(result, std::cout);
  PrintArtifacts(result, audit);

  return LayerOutcome{result, HasAuditEvent(audit, "DECRYPT_DECODE_FAIL", kBobUserId), HasCtxSwitchForBob(audit)};
}

LayerOutcome RunLayer3() {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::security::AuditCollector& audit = hardware.GetAuditCollector();
  sim::kernel::KernelProcessTable process_table(gateway, hardware, audit);

  const sim::isa::AsmProgram alice_program = BuildAliceProgram();
  const sim::isa::AsmProgram bob_dummy_program = BuildBobDummyProgram(kAliceBaseVa);
  const std::uint64_t wildcard_end_va = ProgramEndVa(alice_program);

  const sim::security::ContextHandle alice_handle =
      process_table.LoadProcess(BuildPackage(alice_program, "ablation_layer3_alice", kAliceUserId, kAliceKeyId, 1));
  const sim::security::ContextHandle bob_handle = process_table.LoadProcess(
      BuildPackage(bob_dummy_program, "ablation_layer3_bob_dummy", kBobUserId, kAliceKeyId, 2, 0));

  StoreAliceCiphertextIntoBob(hardware, alice_handle, bob_handle, kAliceBaseVa);
  SetWildcardWindow(hardware, bob_handle, kAliceBaseVa, wildcard_end_va, kBobUserId, kAliceKeyId, 300);

  const sim::core::ExecResult result = RunHandle(process_table, hardware, bob_handle);
  const std::optional<sim::security::ContextHandle> confirmed_bob_handle = FindCtxSwitchHandle(audit, kBobUserId);
  const bool attack_success_confirmed = confirmed_bob_handle.has_value() && *confirmed_bob_handle == bob_handle;

  std::cout << "[LAYER_3_ALL_REMOVED_ATTACK_SUCCEEDS]\n";
  std::cout << "SUMMARY=Bob shares Alice base_va and key_id, wildcard EWC stays open, CFI is disabled.\n";
  std::cout << "ATTACK=The same injected Alice ciphertext now executes under Bob context_handle.\n";
  sim::core::PrintRunSummary(result, std::cout);
  std::cout << "FINAL_X3=" << result.state.regs[3] << '\n';
  PrintArtifacts(result, audit);
  if (attack_success_confirmed) {
    std::cout << "ATTACK_SUCCESS=Alice code executed under Bob context_handle=" << *confirmed_bob_handle
              << " user_id=" << kBobUserId << '\n';
  } else {
    std::cout << "ATTACK_SUCCESS=UNVERIFIED missing_or_mismatched_bob_ctx_switch_audit\n";
  }

  return LayerOutcome{result, attack_success_confirmed, HasCtxSwitchForBob(audit)};
}

}  // namespace

int main() {
  try {
    const LayerOutcome layer1 = RunLayer1();
    const LayerOutcome layer2 = RunLayer2();
    const LayerOutcome layer3 = RunLayer3();

    const bool ok = layer1.result.trap.reason == sim::core::TrapReason::EWC_ILLEGAL_PC &&
                    layer1.has_expected_audit && layer1.has_bob_ctx_switch &&
                    layer2.result.trap.reason == sim::core::TrapReason::DECRYPT_DECODE_FAIL &&
                    layer2.has_expected_audit && layer2.has_bob_ctx_switch &&
                    layer3.result.trap.reason == sim::core::TrapReason::HALT &&
                    layer3.has_expected_audit && layer3.has_bob_ctx_switch &&
                    layer3.result.state.regs[3] == kExpectedAliceX3;

    std::cout << "[ABLATION_SUMMARY]\n";
    std::cout << "LAYER_1_EXPECTED=EWC_ILLEGAL_PC\n";
    std::cout << "LAYER_1_OBSERVED=" << sim::core::TrapReasonToString(layer1.result.trap.reason) << '\n';
    std::cout << "LAYER_2_EXPECTED=DECRYPT_DECODE_FAIL\n";
    std::cout << "LAYER_2_OBSERVED=" << sim::core::TrapReasonToString(layer2.result.trap.reason) << '\n';
    std::cout << "LAYER_3_EXPECTED=HALT\n";
    std::cout << "LAYER_3_OBSERVED=" << sim::core::TrapReasonToString(layer3.result.trap.reason) << '\n';
    std::cout << "LAYER_3_X3_EXPECTED=" << kExpectedAliceX3 << '\n';
    std::cout << "LAYER_3_X3_OBSERVED=" << layer3.result.state.regs[3] << '\n';

    return ok ? 0 : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_ablation failed: " << ex.what() << '\n';
    return 1;
  }
}
