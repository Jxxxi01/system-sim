#include "core/executor.hpp"
#include "isa/assembler.hpp"
#include "security/code_codec.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "test_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string MakeSecureIrJson(const std::string& program_name, std::uint32_t user_id, const std::string& signature,
                             std::uint64_t base_va, const std::string& windows_json,
                             const std::string& pages_json = "[]", std::uint32_t cfi_level = 0,
                             const std::string& call_targets_json = "[]",
                             const std::string& jmp_targets_json = "[]", std::uint64_t entry_offset = 0) {
  std::ostringstream oss;
  oss << "{"
      << "\"program_name\":\"" << program_name << "\","
      << "\"user_id\":" << user_id << ","
      << "\"signature\":\"" << signature << "\","
      << "\"base_va\":" << base_va << ","
      << "\"entry_offset\":" << entry_offset << ","
      << "\"windows\":" << windows_json << ","
      << "\"pages\":" << pages_json << ","
      << "\"cfi_level\":" << cfi_level << ","
      << "\"call_targets\":" << call_targets_json << ","
      << "\"jmp_targets\":" << jmp_targets_json
      << "}";
  return oss.str();
}

std::string MakeCodeWindowJson(std::uint32_t window_id, std::uint64_t start_va, std::uint64_t end_va,
                               std::uint32_t key_id, const std::string& type = "CODE",
                               std::uint32_t code_policy_id = 1) {
  std::ostringstream oss;
  oss << "[{"
      << "\"window_id\":" << window_id << ","
      << "\"start_va\":" << start_va << ","
      << "\"end_va\":" << end_va << ","
      << "\"key_id\":" << key_id << ","
      << "\"type\":\"" << type << "\","
      << "\"code_policy_id\":" << code_policy_id
      << "}]";
  return oss.str();
}

std::string MakePagesJson(std::uint64_t va, const std::string& page_type) {
  std::ostringstream oss;
  oss << "[{"
      << "\"va\":" << va << ","
      << "\"page_type\":\"" << page_type << "\""
      << "}]";
  return oss.str();
}

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

SIM_TEST(Gateway_Load_ValidSecureIR_ConfiguresEwcAndReturnsHandle) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::string json = MakeSecureIrJson(
      "demo", 7, "stub-valid", 4096, MakeCodeWindowJson(1, 4096, 4104, 11), "[{\"va\":4096,\"page_type\":\"CODE\"}]");

  const sim::security::GatewayLoadResult load_result = gateway.Load({json, {}});
  const sim::security::ContextHandle handle = load_result.handle;

  SIM_EXPECT_EQ(handle, static_cast<sim::security::ContextHandle>(1));
  SIM_EXPECT_EQ(load_result.user_id, 7u);
  SIM_EXPECT_EQ(load_result.base_va, 4096u);
  SIM_EXPECT_EQ(load_result.pages.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(load_result.pages[0].va, 4096u);
  SIM_EXPECT_EQ(load_result.pages[0].page_type, sim::security::PvtPageType::CODE);
  const auto user_id = gateway.GetUserIdForHandle(handle);
  SIM_EXPECT_TRUE(user_id.has_value());
  SIM_EXPECT_EQ(user_id.value(), 7u);
  const auto saved_pc = hardware.GetSavedPcForHandle(handle);
  SIM_EXPECT_TRUE(saved_pc.has_value());
  SIM_EXPECT_EQ(saved_pc.value(), 4096u);

  const sim::security::EwcQueryResult query = hardware.GetEwcTable().Query(4096, handle);
  SIM_EXPECT_TRUE(query.allow);
  SIM_EXPECT_TRUE(query.matched_window);
  SIM_EXPECT_EQ(query.key_id, 11u);
  SIM_EXPECT_EQ(query.owner_user_id, 7u);
  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(events[0].seq_no, 1u);
  SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_OK"));
  SIM_EXPECT_EQ(events[0].user_id, 7u);
  SIM_EXPECT_EQ(events[0].context_handle, handle);
  SIM_EXPECT_EQ(events[0].pc, 4096u);
  SIM_EXPECT_TRUE(Contains(events[0].detail, "program_name=demo"));
  SIM_EXPECT_TRUE(Contains(events[0].detail, "window_count=1"));
}

SIM_TEST(Gateway_Load_EntryOffset_PopulatesHiddenSavedPc) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::uint64_t base_va = 0x1800;

  const sim::security::GatewayLoadResult load_result =
      gateway.Load({MakeSecureIrJson("entry_offset_demo", 8, "stub-valid", base_va,
                                     MakeCodeWindowJson(1, base_va, base_va + 12, 7), "[]", 0, "[]", "[]", 4),
                    {}});

  SIM_EXPECT_EQ(load_result.base_va, base_va);
  const auto saved_pc = hardware.GetSavedPcForHandle(load_result.handle);
  SIM_EXPECT_TRUE(saved_pc.has_value());
  SIM_EXPECT_EQ(saved_pc.value(), base_va + 4);
}

SIM_TEST(Gateway_Load_MisalignedEntryOffset_Fails) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::uint64_t base_va = 0x1A00;

  try {
    static_cast<void>(gateway.Load({MakeSecureIrJson("misaligned_entry", 8, "stub-valid", base_va,
                                                    MakeCodeWindowJson(1, base_va, base_va + 12, 7),
                                                    "[]", 0, "[]", "[]", 2),
                                    {}}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "gateway_invalid_entry_offset"));
    SIM_EXPECT_TRUE(Contains(ex.what(), "reason=misaligned"));
    const auto& events = hardware.GetAuditCollector().GetEvents();
    SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
    SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_FAIL"));
    SIM_EXPECT_TRUE(Contains(events[0].detail, "reason=misaligned"));
  }
}

SIM_TEST(Gateway_Load_OutOfRangeEntryOffset_Fails) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::uint64_t base_va = 0x1C00;

  try {
    static_cast<void>(gateway.Load({MakeSecureIrJson("oob_entry", 9, "stub-valid", base_va,
                                                    MakeCodeWindowJson(1, base_va, base_va + 8, 7),
                                                    "[]", 0, "[]", "[]", 8),
                                    {}}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "gateway_invalid_entry_offset"));
    SIM_EXPECT_TRUE(Contains(ex.what(), "reason=out_of_range"));
    const auto& events = hardware.GetAuditCollector().GetEvents();
    SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
    SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_FAIL"));
    SIM_EXPECT_TRUE(Contains(events[0].detail, "reason=out_of_range"));
  }
}

SIM_TEST(Gateway_Load_MultipleCalls_UniqueHandles) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);

  const auto handle_a =
      gateway.Load({MakeSecureIrJson("demo_a", 1, "sig-a", 4096, MakeCodeWindowJson(1, 4096, 4104, 11)), {}}).handle;
  const auto handle_b =
      gateway.Load({MakeSecureIrJson("demo_b", 2, "sig-b", 8192, MakeCodeWindowJson(2, 8192, 8200, 12)), {}}).handle;

  SIM_EXPECT_EQ(handle_a, static_cast<sim::security::ContextHandle>(1));
  SIM_EXPECT_EQ(handle_b, static_cast<sim::security::ContextHandle>(2));
  SIM_EXPECT_TRUE(handle_b > handle_a);
}

SIM_TEST(Gateway_Load_EmptySignature_Fails) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);

  try {
    static_cast<void>(
        gateway.Load({MakeSecureIrJson("demo", 3, "", 4096, MakeCodeWindowJson(1, 4096, 4104, 11)), {}}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "gateway_invalid_signature"));
    const auto& events = hardware.GetAuditCollector().GetEvents();
    SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
    SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_FAIL"));
    SIM_EXPECT_EQ(events[0].context_handle, static_cast<sim::security::ContextHandle>(1));
    SIM_EXPECT_TRUE(Contains(events[0].detail, "gateway_invalid_signature"));
  }
}

SIM_TEST(Gateway_Load_MissingWindows_Fails) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::string json =
      "{\"program_name\":\"demo\",\"user_id\":1,\"signature\":\"sig\",\"base_va\":4096,"
      "\"pages\":[],\"cfi_level\":0,\"call_targets\":[],\"jmp_targets\":[]}";

  try {
    static_cast<void>(gateway.Load({json, {}}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "gateway_missing_field field=windows"));
    const auto& event = hardware.GetAuditCollector().GetEvents()[0];
    SIM_EXPECT_EQ(event.type, std::string("GATEWAY_LOAD_FAIL"));
    SIM_EXPECT_TRUE(Contains(event.detail, "gateway_missing_field"));
  }
}

SIM_TEST(Gateway_Load_EmptyWindows_Fails) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);

  try {
    static_cast<void>(gateway.Load({MakeSecureIrJson("demo", 1, "sig", 4096, "[]"), {}}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "gateway_invalid_windows"));
    const auto& event = hardware.GetAuditCollector().GetEvents()[0];
    SIM_EXPECT_EQ(event.type, std::string("GATEWAY_LOAD_FAIL"));
    SIM_EXPECT_TRUE(Contains(event.detail, "gateway_invalid_windows"));
  }
}

SIM_TEST(Gateway_HandleToUserIdMapping_Correct) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);

  const auto handle = gateway
                          .Load({MakeSecureIrJson("demo", 99, "sig", 12288, MakeCodeWindowJson(1, 12288, 12296, 5)),
                                 {}})
                          .handle;
  const std::optional<std::uint32_t> user_id = gateway.GetUserIdForHandle(handle);

  SIM_EXPECT_TRUE(user_id.has_value());
  SIM_EXPECT_EQ(user_id.value(), 99u);
  SIM_EXPECT_TRUE(!gateway.GetUserIdForHandle(handle + 1).has_value());
}

SIM_TEST(Gateway_Release_ClearsMappingWindowsAndDoesNotReuseHandle) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::uint64_t base_a = 0x3200;
  const sim::security::ContextHandle handle_a =
      gateway.Load({MakeSecureIrJson("demo_a", 41, "sig-a", base_a, MakeCodeWindowJson(1, base_a, base_a + 8, 21)),
                    {}})
          .handle;

  gateway.Release(handle_a);

  SIM_EXPECT_TRUE(!gateway.GetUserIdForHandle(handle_a).has_value());
  SIM_EXPECT_TRUE(!hardware.GetSavedPcForHandle(handle_a).has_value());
  SIM_EXPECT_TRUE(hardware.GetCodeRegion(handle_a) == nullptr);
  const sim::security::EwcQueryResult released_query = hardware.GetEwcTable().Query(base_a, handle_a);
  SIM_EXPECT_TRUE(!released_query.allow);
  SIM_EXPECT_TRUE(!released_query.matched_window);

  const sim::security::ContextHandle handle_b =
      gateway
          .Load({MakeSecureIrJson("demo_b", 42, "sig-b", 0x4200, MakeCodeWindowJson(2, 0x4200, 0x4208, 22)), {}})
          .handle;
  SIM_EXPECT_EQ(handle_a, static_cast<sim::security::ContextHandle>(1));
  SIM_EXPECT_EQ(handle_b, static_cast<sim::security::ContextHandle>(2));
  SIM_EXPECT_TRUE(handle_b != handle_a);

  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(3));
  SIM_EXPECT_EQ(events[1].type, std::string("GATEWAY_RELEASE"));
  SIM_EXPECT_EQ(events[1].user_id, 41u);
  SIM_EXPECT_EQ(events[1].context_handle, handle_a);
  SIM_EXPECT_TRUE(Contains(events[1].detail, "status=cleared"));
}

SIM_TEST(Gateway_Load_OverlappingWindows_Fails) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::string windows =
      "[{\"window_id\":1,\"start_va\":4096,\"end_va\":4112,\"key_id\":11,\"type\":\"CODE\",\"code_policy_id\":1},"
      "{\"window_id\":2,\"start_va\":4104,\"end_va\":4120,\"key_id\":11,\"type\":\"CODE\",\"code_policy_id\":1}]";

  try {
    static_cast<void>(gateway.Load({MakeSecureIrJson("demo", 1, "sig", 4096, windows), {}}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "overlap"));
    const auto& events = hardware.GetAuditCollector().GetEvents();
    SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
    SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_FAIL"));
    SIM_EXPECT_TRUE(Contains(events[0].detail, "overlap"));
  }
}

SIM_TEST(Gateway_Load_InvalidType_Fails) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);

  try {
    static_cast<void>(gateway.Load(
        {MakeSecureIrJson("demo", 1, "sig", 4096, MakeCodeWindowJson(1, 4096, 4104, 11, "DATA")), {}}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "gateway_invalid_window_type"));
    const auto& event = hardware.GetAuditCollector().GetEvents()[0];
    SIM_EXPECT_EQ(event.type, std::string("GATEWAY_LOAD_FAIL"));
    SIM_EXPECT_TRUE(Contains(event.detail, "gateway_invalid_window_type"));
  }
}

SIM_TEST(Gateway_Load_InvalidPageType_Fails) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::uint64_t base = 0x4600;

  try {
    static_cast<void>(gateway.Load({MakeSecureIrJson("demo", 1, "sig", base, MakeCodeWindowJson(1, base, base + 8, 11),
                                                    MakePagesJson(base, "EXECUTE")),
                                    {1, 2, 3, 4}}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "gateway_invalid_page_type"));
  }

  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_FAIL"));
  SIM_EXPECT_TRUE(Contains(events[0].detail, "gateway_invalid_page_type"));
  SIM_EXPECT_TRUE(hardware.GetCodeRegion(1) == nullptr);
  const sim::security::EwcQueryResult query = hardware.GetEwcTable().Query(base, 1);
  SIM_EXPECT_TRUE(!query.allow);
  SIM_EXPECT_TRUE(!query.matched_window);
  const sim::security::SpeCheckResult spe_result =
      hardware.GetSpeTable().CheckInstruction(1, sim::isa::Op::RET, base, base, base + 4);
  SIM_EXPECT_TRUE(spe_result.allow);
}

SIM_TEST(Gateway_Load_CapacityOverflow_Fails) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);

  for (std::size_t i = 0; i < sim::security::kMaxContextHandles; ++i) {
    const std::uint64_t base = 0x5000 + static_cast<std::uint64_t>(i) * 0x100;
    const auto handle = gateway
                            .Load({MakeSecureIrJson("demo", static_cast<std::uint32_t>(i + 1), "sig", base,
                                                    MakeCodeWindowJson(1, base, base + 8, 10)),
                                   {}})
                            .handle;
    SIM_EXPECT_EQ(handle, static_cast<sim::security::ContextHandle>(i + 1));
  }

  hardware.GetAuditCollector().Clear();

  try {
    static_cast<void>(gateway.Load(
        {MakeSecureIrJson("overflow", 999, "sig", 0xF000, MakeCodeWindowJson(1, 0xF000, 0xF008, 99)), {}}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "gateway_capacity_exceeded"));
    const auto& events = hardware.GetAuditCollector().GetEvents();
    SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
    SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_FAIL"));
    SIM_EXPECT_EQ(events[0].context_handle, static_cast<sim::security::ContextHandle>(257));
    SIM_EXPECT_TRUE(Contains(events[0].detail, "gateway_capacity_exceeded"));
  }
}

SIM_TEST(AuditCollector_UnifiedEvents) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  const std::string src = R"(
  LI x1, 123
  HALT
)";
  const std::uint64_t base = 0x4000;
  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, base);
  const sim::security::CipherProgram ciphertext = sim::security::EncryptProgram(program, 55);
  const std::vector<std::uint8_t> code_memory = sim::security::BuildCodeMemory(ciphertext);
  const sim::security::ContextHandle handle =
      gateway.Load({MakeSecureIrJson("demo", 5, "sig", base, MakeCodeWindowJson(1, base, base + 8, 77)),
                    code_memory})
          .handle;
  hardware.SetActiveHandle(handle);

  sim::core::ExecuteOptions options;
  options.hardware = &hardware;
  const sim::core::ExecResult result = sim::core::ExecuteProgram(options);

  SIM_EXPECT_EQ(result.trap.reason, sim::core::TrapReason::DECRYPT_DECODE_FAIL);
  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(2));
  SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_OK"));
  SIM_EXPECT_EQ(events[0].user_id, 5u);
  SIM_EXPECT_EQ(events[0].context_handle, handle);
  SIM_EXPECT_EQ(events[1].seq_no, 2u);
  SIM_EXPECT_EQ(events[1].type, std::string("DECRYPT_DECODE_FAIL"));
  SIM_EXPECT_EQ(events[1].user_id, 5u);
  SIM_EXPECT_EQ(events[1].context_handle, handle);
  SIM_EXPECT_EQ(events[1].pc, base);
  SIM_EXPECT_TRUE(Contains(events[1].detail, "key_id=77"));
  SIM_EXPECT_TRUE(Contains(events[1].detail, "reason=key_check_mismatch"));
}

int main() {
  return sim::test::RunAll();
}
