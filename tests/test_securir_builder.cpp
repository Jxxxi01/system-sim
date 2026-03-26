#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "security/pvt.hpp"
#include "security/securir_builder.hpp"
#include "test_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

SIM_TEST(SecureIrBuilder_BasicBuild_LoadSucceeds) {
  const std::uint64_t base_va = 4096;
  const sim::isa::AsmProgram program = sim::isa::AssembleText("HALT\n", base_va);

  sim::security::SecureIrBuilderConfig config;
  config.program_name = "builder_basic";
  config.user_id = 7;
  config.key_id = 11;
  config.window_id = 3;

  const sim::security::SecureIrPackage package = sim::security::SecureIrBuilder::Build(program, config);

  SIM_EXPECT_TRUE(Contains(package.metadata, "\"program_name\":\"builder_basic\""));
  SIM_EXPECT_TRUE(Contains(package.metadata, "\"base_va\":4096"));

  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const sim::security::GatewayLoadResult load_result = gateway.Load(package);

  SIM_EXPECT_EQ(load_result.handle, static_cast<sim::security::ContextHandle>(1));
  SIM_EXPECT_EQ(load_result.user_id, 7u);
  SIM_EXPECT_EQ(load_result.base_va, base_va);
  SIM_EXPECT_EQ(load_result.pages.size(), static_cast<std::size_t>(0));

  const sim::security::EwcQueryResult query = hardware.GetEwcTable().Query(base_va, load_result.handle);
  SIM_EXPECT_TRUE(query.allow);
  SIM_EXPECT_EQ(query.window_id, 3u);
  SIM_EXPECT_EQ(query.key_id, 11u);
}

SIM_TEST(SecureIrBuilder_CfiFields_ConfigureSpePolicy) {
  const std::uint64_t base_va = 8192;
  const std::string source = R"(
  CALL func
  HALT
func:
  RET
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(source, base_va);

  sim::security::SecureIrBuilderConfig config;
  config.program_name = "builder_cfi";
  config.user_id = 9;
  config.key_id = 13;
  config.window_id = 2;
  config.cfi_level = 3;
  config.call_targets = {base_va + 8};
  config.jmp_targets = {base_va + 16};

  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const sim::security::ContextHandle handle =
      gateway.Load(sim::security::SecureIrBuilder::Build(program, config)).handle;

  hardware.GetAuditCollector().Clear();
  const sim::security::SpeCheckResult call_ok =
      hardware.GetSpeTable().CheckInstruction(handle, sim::isa::Op::CALL, base_va, base_va + 8, base_va + 4);
  const sim::security::SpeCheckResult jump_bad =
      hardware.GetSpeTable().CheckInstruction(handle, sim::isa::Op::J, base_va + 4, base_va + 20, base_va + 8);

  SIM_EXPECT_TRUE(call_ok.allow);
  SIM_EXPECT_TRUE(!jump_bad.allow);
  SIM_EXPECT_EQ(hardware.GetAuditCollector().GetEvents().size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(hardware.GetAuditCollector().GetEvents()[0].type, std::string("SPE_VIOLATION"));
}

SIM_TEST(SecureIrBuilder_PagesField_PropagatesThroughLoad) {
  const std::uint64_t base_va = 12288;
  const sim::isa::AsmProgram program = sim::isa::AssembleText("HALT\n", base_va);

  sim::security::SecureIrBuilderConfig config;
  config.program_name = "builder_pages";
  config.user_id = 5;
  config.key_id = 17;
  config.window_id = 1;
  config.pages = {{base_va, sim::security::PvtPageType::CODE},
                  {base_va + sim::security::kPageSize, sim::security::PvtPageType::DATA}};

  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const sim::security::GatewayLoadResult load_result =
      gateway.Load(sim::security::SecureIrBuilder::Build(program, config));

  SIM_EXPECT_EQ(load_result.pages.size(), static_cast<std::size_t>(2));
  SIM_EXPECT_EQ(load_result.pages[0].va, base_va);
  SIM_EXPECT_EQ(load_result.pages[0].page_type, sim::security::PvtPageType::CODE);
  SIM_EXPECT_EQ(load_result.pages[1].va, base_va + sim::security::kPageSize);
  SIM_EXPECT_EQ(load_result.pages[1].page_type, sim::security::PvtPageType::DATA);
}

SIM_TEST(SecureIrBuilder_RoundTrip_ExecuteHalts) {
  const std::uint64_t base_va = 16384;
  const std::string source = R"(
  LI x1, 10
  LI x2, 32
  ADD x3, x1, x2
  HALT
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(source, base_va);

  sim::security::SecureIrBuilderConfig config;
  config.program_name = "builder_round_trip";
  config.user_id = 12;
  config.key_id = 19;
  config.window_id = 6;

  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const sim::security::GatewayLoadResult load_result =
      gateway.Load(sim::security::SecureIrBuilder::Build(program, config));
  hardware.SetActiveHandle(load_result.handle);

  sim::core::ExecuteOptions options;
  options.hardware = &hardware;
  const sim::core::ExecResult result = sim::core::ExecuteProgram(base_va, options);

  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::HALT);
}

SIM_TEST(SecureIrBuilder_MultiWindow_NonOverlappingLoadSucceeds) {
  const std::uint64_t base_va = 20480;
  const std::string source = R"(
  NOP
  NOP
  HALT
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(source, base_va);

  sim::security::SecureIrBuilderConfig config;
  config.program_name = "builder_multi_window";
  config.user_id = 15;
  config.key_id = 23;
  config.windows = {
      {1, base_va, base_va + 4, 23, "CODE", 1},
      {2, base_va + 4, base_va + 12, 23, "CODE", 2},
  };

  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const sim::security::GatewayLoadResult load_result =
      gateway.Load(sim::security::SecureIrBuilder::Build(program, config));

  const sim::security::EwcQueryResult first = hardware.GetEwcTable().Query(base_va, load_result.handle);
  const sim::security::EwcQueryResult second = hardware.GetEwcTable().Query(base_va + 8, load_result.handle);

  SIM_EXPECT_TRUE(first.allow);
  SIM_EXPECT_EQ(first.window_id, 1u);
  SIM_EXPECT_TRUE(second.allow);
  SIM_EXPECT_EQ(second.window_id, 2u);
}

SIM_TEST(SecureIrBuilder_MultiWindow_InconsistentKeyId_Throws) {
  const std::uint64_t base_va = 24576;
  const sim::isa::AsmProgram program = sim::isa::AssembleText("HALT\n", base_va);

  sim::security::SecureIrBuilderConfig config;
  config.program_name = "builder_multi_window_bad_keys";
  config.user_id = 21;
  config.key_id = 99;
  config.windows = {
      {1, base_va, base_va + 4, 23, "CODE", 1},
      {2, base_va + 4, base_va + 8, 24, "CODE", 1},
  };

  try {
    static_cast<void>(sim::security::SecureIrBuilder::Build(program, config));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "securir_builder_inconsistent_window_key_ids"));
  }
}

SIM_TEST(SecureIrBuilder_MultiWindow_RoundTrip_ExecuteHalts) {
  const std::uint64_t base_va = 28672;
  const std::string source = R"(
  NOP
  NOP
  HALT
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(source, base_va);

  sim::security::SecureIrBuilderConfig config;
  config.program_name = "builder_multi_window_round_trip";
  config.user_id = 22;
  config.key_id = 99;
  config.windows = {
      {1, base_va, base_va + 8, 23, "CODE", 1},
      {2, base_va + 8, base_va + 12, 23, "CODE", 1},
  };

  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const sim::security::GatewayLoadResult load_result =
      gateway.Load(sim::security::SecureIrBuilder::Build(program, config));
  hardware.SetActiveHandle(load_result.handle);

  sim::core::ExecuteOptions options;
  options.hardware = &hardware;
  const sim::core::ExecResult result = sim::core::ExecuteProgram(base_va, options);

  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::HALT);
}

}  // namespace

int main() {
  return sim::test::RunAll();
}
