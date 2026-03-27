#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "security/code_codec.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct RunArtifacts {
  sim::core::ExecResult result;
  std::vector<sim::security::AuditEvent> audit_events;
};

std::string NumberArrayJson(std::initializer_list<std::uint64_t> values) {
  std::ostringstream oss;
  oss << '[';
  bool first = true;
  for (std::uint64_t value : values) {
    if (!first) {
      oss << ',';
    }
    first = false;
    oss << value;
  }
  oss << ']';
  return oss.str();
}

std::string MakeSecureIrJson(const std::string& program_name, std::uint32_t user_id, const std::string& signature,
                             std::uint64_t base_va, const std::string& windows_json, std::uint32_t cfi_level,
                             const std::string& call_targets_json, const std::string& jmp_targets_json) {
  std::ostringstream oss;
  oss << "{"
      << "\"program_name\":\"" << program_name << "\"," << "\"user_id\":" << user_id << ','
      << "\"signature\":\"" << signature << "\"," << "\"base_va\":" << base_va << ','
      << "\"windows\":" << windows_json << ',' << "\"pages\":[]," << "\"cfi_level\":" << cfi_level
      << ',' << "\"call_targets\":" << call_targets_json << ',' << "\"jmp_targets\":" << jmp_targets_json
      << '}';
  return oss.str();
}

std::string MakeCodeWindowJson(std::uint32_t window_id, std::uint64_t start_va, std::uint64_t end_va,
                               std::uint32_t key_id) {
  std::ostringstream oss;
  oss << "[{"
      << "\"window_id\":" << window_id << ',' << "\"start_va\":" << start_va << ','
      << "\"end_va\":" << end_va << ',' << "\"key_id\":" << key_id << ','
      << "\"type\":\"CODE\"," << "\"code_policy_id\":1" << "}]";
  return oss.str();
}

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

sim::security::SecureIrPackage MakePackage(const std::string& metadata, std::vector<std::uint8_t> code_memory = {}) {
  return sim::security::SecureIrPackage{metadata, code_memory};
}

RunArtifacts RunProgramWithPolicy(const std::string& source, std::uint64_t base_va, std::uint32_t user_id,
                                  std::uint32_t key_id, std::uint32_t cfi_level,
                                  const std::string& call_targets_json, const std::string& jmp_targets_json,
                                  bool clear_audit_after_load = true) {
  const sim::isa::AsmProgram program = sim::isa::AssembleText(source, base_va);
  const std::vector<std::uint8_t> code_memory =
      sim::security::BuildCodeMemory(sim::security::EncryptProgram(program, key_id));
  const std::uint64_t end_va = base_va + program.code.size() * sim::isa::kInstrBytes;

  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const sim::security::ContextHandle handle =
      gateway
          .Load(MakePackage(MakeSecureIrJson("spe_case", user_id, "stub-valid", base_va,
                                            MakeCodeWindowJson(1, base_va, end_va, key_id), cfi_level,
                                            call_targets_json, jmp_targets_json),
                            code_memory))
          .handle;
  hardware.SetActiveHandle(handle);
  if (clear_audit_after_load) {
    hardware.GetAuditCollector().Clear();
  }

  sim::core::ExecuteOptions options;
  options.hardware = &hardware;

  RunArtifacts artifacts;
  artifacts.result = sim::core::ExecuteProgram(options);
  artifacts.audit_events = hardware.GetAuditCollector().GetEvents();
  return artifacts;
}

}  // namespace

SIM_TEST(SPE_L3_CallIllegalTarget_TriggersTrap) {
  const std::uint64_t base = 0x2000;
  const std::string source = R"(
  CALL func
  HALT
func:
  RET
)";

  const RunArtifacts artifacts =
      RunProgramWithPolicy(source, base, 11, 7, 3, NumberArrayJson({base + 16}), NumberArrayJson({}));

  SIM_EXPECT_EQ(artifacts.result.trap.reason, sim::core::TrapReason::SPE_VIOLATION);
  SIM_EXPECT_EQ(artifacts.result.trap.pc, base);
  SIM_EXPECT_EQ(artifacts.audit_events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(artifacts.audit_events[0].type, std::string("SPE_VIOLATION"));
  SIM_EXPECT_TRUE(Contains(artifacts.audit_events[0].detail, "stage=execute"));
  SIM_EXPECT_TRUE(Contains(artifacts.audit_events[0].detail, "op=CALL"));
}

SIM_TEST(SPE_L3_JumpIllegalTarget_TriggersTrap) {
  const std::uint64_t base = 0x2100;
  const std::string source = R"(
  J target
  NOP
target:
  HALT
)";

  const RunArtifacts artifacts =
      RunProgramWithPolicy(source, base, 12, 9, 3, NumberArrayJson({}), NumberArrayJson({base + 32}));

  SIM_EXPECT_EQ(artifacts.result.trap.reason, sim::core::TrapReason::SPE_VIOLATION);
  SIM_EXPECT_EQ(artifacts.result.trap.pc, base);
  SIM_EXPECT_EQ(artifacts.audit_events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_TRUE(Contains(artifacts.audit_events[0].detail, "stage=execute"));
  SIM_EXPECT_TRUE(Contains(artifacts.audit_events[0].detail, "op=J"));
}

SIM_TEST(SPE_L3_LegalTargets_RunNormally) {
  const std::uint64_t base = 0x2200;
  const std::string source = R"(
  CALL func
  HALT
func:
  RET
)";

  const RunArtifacts artifacts =
      RunProgramWithPolicy(source, base, 13, 11, 3, NumberArrayJson({base + 8}), NumberArrayJson({}));

  SIM_EXPECT_EQ(artifacts.result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(artifacts.result.trap.pc, base + 4);
  SIM_EXPECT_EQ(artifacts.audit_events.size(), static_cast<std::size_t>(0));
}

SIM_TEST(SPE_L2_TamperedReturn_TriggersTrap) {
  const std::uint64_t base = 0x2300;
  const std::string source = R"(
  CALL func
  HALT
func:
  LI x1, 4660
  RET
  HALT
)";

  const RunArtifacts artifacts =
      RunProgramWithPolicy(source, base, 14, 13, 2, NumberArrayJson({}), NumberArrayJson({}));

  SIM_EXPECT_EQ(artifacts.result.trap.reason, sim::core::TrapReason::SPE_VIOLATION);
  SIM_EXPECT_EQ(artifacts.result.trap.pc, base + 12);
  SIM_EXPECT_EQ(artifacts.audit_events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_TRUE(Contains(artifacts.audit_events[0].detail, "stage=execute"));
  SIM_EXPECT_TRUE(Contains(artifacts.audit_events[0].detail, "reason=shadow_stack_mismatch"));
}

SIM_TEST(SPE_L2_NormalCallReturn_Passes) {
  const std::uint64_t base = 0x2400;
  const std::string source = R"(
  CALL func
  HALT
func:
  RET
)";

  const RunArtifacts artifacts =
      RunProgramWithPolicy(source, base, 15, 17, 2, NumberArrayJson({}), NumberArrayJson({}));

  SIM_EXPECT_EQ(artifacts.result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(artifacts.audit_events.size(), static_cast<std::size_t>(0));
}

SIM_TEST(SPE_L1_IllegalJump_DoesNotTrap) {
  const std::uint64_t base = 0x2500;
  const std::string source = R"(
  J target
  NOP
target:
  HALT
)";

  const RunArtifacts artifacts =
      RunProgramWithPolicy(source, base, 16, 19, 1, NumberArrayJson({}), NumberArrayJson({}));

  SIM_EXPECT_EQ(artifacts.result.trap.reason, sim::core::TrapReason::HALT);
  SIM_EXPECT_EQ(artifacts.audit_events.size(), static_cast<std::size_t>(0));
}

SIM_TEST(Gateway_PropagatesPolicyIntoSpeTable) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::uint64_t base = 0x2600;
  const sim::security::ContextHandle handle =
      gateway
          .Load(MakePackage(MakeSecureIrJson("policy", 21, "sig", base,
                                            MakeCodeWindowJson(1, base, base + 16, 23), 3,
                                            NumberArrayJson({base + 12}), NumberArrayJson({base + 20}))))
          .handle;

  hardware.GetAuditCollector().Clear();
  const sim::security::SpeCheckResult call_ok =
      hardware.GetSpeTable().CheckInstruction(handle, sim::isa::Op::CALL, base, base + 12, base + 4);
  const sim::security::SpeCheckResult j_ok =
      hardware.GetSpeTable().CheckInstruction(handle, sim::isa::Op::J, base + 4, base + 20, base + 8);
  const sim::security::SpeCheckResult j_bad =
      hardware.GetSpeTable().CheckInstruction(handle, sim::isa::Op::J, base + 8, base + 24, base + 12);

  SIM_EXPECT_TRUE(call_ok.allow);
  SIM_EXPECT_TRUE(j_ok.allow);
  SIM_EXPECT_TRUE(!j_bad.allow);
  SIM_EXPECT_EQ(hardware.GetAuditCollector().GetEvents().size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(hardware.GetAuditCollector().GetEvents()[0].user_id, 21u);
}

SIM_TEST(ExecuteProgram_UsesHardwareOnlyPath) {
  const std::uint64_t base = 0x2700;
  const std::string source = R"(
  HALT
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(source, base);
  const std::vector<std::uint8_t> code_memory =
      sim::security::BuildCodeMemory(sim::security::EncryptProgram(program, 29));

  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const sim::security::ContextHandle handle =
      gateway
          .Load(MakePackage(MakeSecureIrJson("hardware_only", 22, "sig", base,
                                            MakeCodeWindowJson(1, base, base + 4, 29), 0,
                                            NumberArrayJson({}), NumberArrayJson({})),
                            code_memory))
          .handle;
  hardware.SetActiveHandle(handle);

  sim::core::ExecuteOptions options;
  options.hardware = &hardware;
  const sim::core::ExecResult result = sim::core::ExecuteProgram(options);

  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::HALT);
}

SIM_TEST(SPE_AuditDetails_ContainStageMarkers) {
  sim::security::SecurityHardware hardware;

  hardware.GetSpeTable().ConfigurePolicy(1, 33, 3, std::vector<std::uint64_t>{}, std::vector<std::uint64_t>{});
  const sim::security::SpeCheckResult decode_violation =
      hardware.GetSpeTable().CheckInstruction(1, sim::isa::Op::CALL, 0x2800, 0x2810, 0x2804);
  SIM_EXPECT_TRUE(!decode_violation.allow);
  SIM_EXPECT_EQ(hardware.GetAuditCollector().GetEvents().size(), static_cast<std::size_t>(1));
  SIM_EXPECT_TRUE(Contains(hardware.GetAuditCollector().GetEvents()[0].detail, "stage=execute"));

  hardware.GetAuditCollector().Clear();
  hardware.GetSpeTable().ConfigurePolicy(1, 33, 2, std::vector<std::uint64_t>{}, std::vector<std::uint64_t>{});
  const sim::security::SpeCheckResult execute_violation =
      hardware.GetSpeTable().CheckInstruction(1, sim::isa::Op::RET, 0x2808, 0x2804, 0x280C);
  SIM_EXPECT_TRUE(!execute_violation.allow);
  SIM_EXPECT_EQ(hardware.GetAuditCollector().GetEvents().size(), static_cast<std::size_t>(1));
  SIM_EXPECT_TRUE(Contains(hardware.GetAuditCollector().GetEvents()[0].detail, "stage=execute"));
}

SIM_TEST(Gateway_Rollback_ClearsEwcAndSpeState) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::uint64_t base = 0x2900;

  try {
    static_cast<void>(gateway.Load(MakePackage(MakeSecureIrJson("rollback", 44, "sig", base,
                                                                MakeCodeWindowJson(1, base, base + 8, 31), 99,
                                                                NumberArrayJson({}), NumberArrayJson({})))));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "spe_invalid_cfi_level"));
  }

  const sim::security::EwcQueryResult query = hardware.GetEwcTable().Query(base, 1);
  SIM_EXPECT_TRUE(!query.allow);
  SIM_EXPECT_TRUE(!gateway.GetUserIdForHandle(1).has_value());

  const sim::security::SpeCheckResult spe_result =
      hardware.GetSpeTable().CheckInstruction(1, sim::isa::Op::RET, base, base, base + 4);
  SIM_EXPECT_TRUE(spe_result.allow);

  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_FAIL"));
  SIM_EXPECT_TRUE(Contains(events[0].detail, "spe_invalid_cfi_level"));
}

SIM_TEST(ExecuteProgram_NullHardware_Throws) {
  sim::core::ExecuteOptions options;
  try {
    static_cast<void>(sim::core::ExecuteProgram(options));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_EQ(std::string(ex.what()), std::string("hardware_not_configured"));
  }
}

int main() {
  return sim::test::RunAll();
}
