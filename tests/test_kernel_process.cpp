#include "kernel/process.hpp"

#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "test_harness.hpp"

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
                             const std::string& jmp_targets_json = "[]") {
  std::ostringstream oss;
  oss << "{"
      << "\"program_name\":\"" << program_name << "\","
      << "\"user_id\":" << user_id << ","
      << "\"signature\":\"" << signature << "\","
      << "\"base_va\":" << base_va << ","
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

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

sim::kernel::KernelProcessTable MakeProcessTable(sim::security::Gateway& gateway,
                                                 sim::security::SecurityHardware& hardware) {
  return sim::kernel::KernelProcessTable(gateway, hardware, hardware.GetAuditCollector());
}

}  // namespace

SIM_TEST(LoadProcess_Success_ProcessQueryable) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x2000;

  const sim::security::ContextHandle handle = processes.LoadProcess(
      MakeSecureIrJson("proc", 11, "sig", base_va, MakeCodeWindowJson(1, base_va, base_va + 8, 7)), {1, 2, 3, 4});

  const sim::kernel::ProcessContext* process = processes.GetProcess(handle);
  SIM_EXPECT_TRUE(process != nullptr);
  SIM_EXPECT_EQ(handle, static_cast<sim::security::ContextHandle>(1));
  SIM_EXPECT_EQ(process->context_handle, handle);
  SIM_EXPECT_EQ(process->user_id, 11u);
  SIM_EXPECT_EQ(process->base_va, base_va);
  SIM_EXPECT_TRUE(processes.GetActiveProcess() == nullptr);
}

SIM_TEST(LoadProcess_Success_CodeMemoryInHardware) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x2400;
  const std::vector<std::uint8_t> code_memory{9, 8, 7, 6};

  const sim::security::ContextHandle handle = processes.LoadProcess(
      MakeSecureIrJson("proc", 12, "sig", base_va, MakeCodeWindowJson(1, base_va, base_va + 8, 13)), code_memory);

  const sim::security::CodeRegion* region = hardware.GetCodeRegion(handle);
  SIM_EXPECT_TRUE(region != nullptr);
  SIM_EXPECT_EQ(region->base_va, base_va);
  SIM_EXPECT_EQ(region->code_memory, code_memory);
}

SIM_TEST(LoadProcess_MultiUser_HandleIsolation) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_a = 0x3000;
  const std::uint64_t base_b = 0x4000;

  const sim::security::ContextHandle handle_a = processes.LoadProcess(
      MakeSecureIrJson("proc_a", 21, "sig-a", base_a, MakeCodeWindowJson(1, base_a, base_a + 8, 3)), {1, 1, 1, 1});
  const sim::security::ContextHandle handle_b = processes.LoadProcess(
      MakeSecureIrJson("proc_b", 22, "sig-b", base_b, MakeCodeWindowJson(2, base_b, base_b + 8, 4)), {2, 2, 2, 2});

  SIM_EXPECT_TRUE(handle_a != handle_b);
  SIM_EXPECT_EQ(processes.GetProcess(handle_a)->user_id, 21u);
  SIM_EXPECT_EQ(processes.GetProcess(handle_b)->user_id, 22u);
  SIM_EXPECT_EQ(hardware.GetCodeRegion(handle_a)->base_va, base_a);
  SIM_EXPECT_EQ(hardware.GetCodeRegion(handle_b)->base_va, base_b);

  const sim::security::EwcQueryResult query_a = hardware.GetEwcTable().Query(base_a, handle_a);
  const sim::security::EwcQueryResult cross_query = hardware.GetEwcTable().Query(base_a, handle_b);
  SIM_EXPECT_TRUE(query_a.allow);
  SIM_EXPECT_EQ(query_a.owner_user_id, 21u);
  SIM_EXPECT_TRUE(!cross_query.allow);
  SIM_EXPECT_TRUE(!cross_query.matched_window);
}

SIM_TEST(ContextSwitch_UpdatesActive_EmitsAudit) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x5000;
  const sim::security::ContextHandle handle = processes.LoadProcess(
      MakeSecureIrJson("proc", 31, "sig", base_va, MakeCodeWindowJson(1, base_va, base_va + 8, 6)), {1, 2, 3, 4});

  hardware.GetAuditCollector().Clear();
  processes.ContextSwitch(handle);

  const sim::kernel::ProcessContext* active = processes.GetActiveProcess();
  SIM_EXPECT_TRUE(active != nullptr);
  SIM_EXPECT_EQ(active->context_handle, handle);
  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(events[0].type, std::string("CTX_SWITCH"));
  SIM_EXPECT_EQ(events[0].user_id, 31u);
  SIM_EXPECT_EQ(events[0].context_handle, handle);
  SIM_EXPECT_EQ(events[0].pc, base_va);
  SIM_EXPECT_TRUE(Contains(events[0].detail, "base_va=20480"));
}

SIM_TEST(ContextSwitch_InvalidHandle_Throws) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);

  try {
    processes.ContextSwitch(99);
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "kernel_process_invalid_handle"));
    SIM_EXPECT_TRUE(Contains(ex.what(), "context_handle=99"));
  }
}

SIM_TEST(ContextSwitch_Idempotent) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x5400;
  const sim::security::ContextHandle handle = processes.LoadProcess(
      MakeSecureIrJson("proc", 32, "sig", base_va, MakeCodeWindowJson(1, base_va, base_va + 8, 9)), {5, 6, 7, 8});

  hardware.GetAuditCollector().Clear();
  processes.ContextSwitch(handle);
  processes.ContextSwitch(handle);

  const sim::kernel::ProcessContext* active = processes.GetActiveProcess();
  SIM_EXPECT_TRUE(active != nullptr);
  SIM_EXPECT_EQ(active->context_handle, handle);
  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(2));
  SIM_EXPECT_EQ(events[0].type, std::string("CTX_SWITCH"));
  SIM_EXPECT_EQ(events[1].type, std::string("CTX_SWITCH"));
  SIM_EXPECT_EQ(events[0].context_handle, handle);
  SIM_EXPECT_EQ(events[1].context_handle, handle);
}

SIM_TEST(ReleaseProcess_RemovesFromTable) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x5800;
  const sim::security::ContextHandle handle = processes.LoadProcess(
      MakeSecureIrJson("proc", 41, "sig", base_va, MakeCodeWindowJson(1, base_va, base_va + 8, 2)), {1, 3, 5, 7});

  processes.ReleaseProcess(handle);

  SIM_EXPECT_TRUE(processes.GetProcess(handle) == nullptr);
  SIM_EXPECT_TRUE(!gateway.GetUserIdForHandle(handle).has_value());
}

SIM_TEST(ReleaseProcess_ActiveHandle_ResetsActive) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x5c00;
  const sim::security::ContextHandle handle = processes.LoadProcess(
      MakeSecureIrJson("proc", 42, "sig", base_va, MakeCodeWindowJson(1, base_va, base_va + 8, 2)), {2, 4, 6, 8});

  processes.ContextSwitch(handle);
  processes.ReleaseProcess(handle);

  SIM_EXPECT_TRUE(processes.GetActiveProcess() == nullptr);
}

SIM_TEST(ReleaseProcess_CodeMemoryRemovedFromHardware) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x6000;
  const sim::security::ContextHandle handle = processes.LoadProcess(
      MakeSecureIrJson("proc", 43, "sig", base_va, MakeCodeWindowJson(1, base_va, base_va + 8, 2)), {9, 9, 9, 9});

  processes.ReleaseProcess(handle);

  SIM_EXPECT_TRUE(hardware.GetCodeRegion(handle) == nullptr);
}

SIM_TEST(ReleaseProcess_ThenContextSwitch_Throws) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x6400;
  const sim::security::ContextHandle handle = processes.LoadProcess(
      MakeSecureIrJson("proc", 44, "sig", base_va, MakeCodeWindowJson(1, base_va, base_va + 8, 2)), {7, 7, 7, 7});

  processes.ReleaseProcess(handle);

  try {
    processes.ContextSwitch(handle);
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "kernel_process_invalid_handle"));
  }
}

SIM_TEST(LoadProcess_GatewayError_NoDirtyState) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x6800;

  try {
    static_cast<void>(processes.LoadProcess(
        MakeSecureIrJson("broken", 51, "", base_va, MakeCodeWindowJson(1, base_va, base_va + 8, 2)), {1, 2, 3, 4}));
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    SIM_EXPECT_TRUE(Contains(ex.what(), "gateway_invalid_signature"));
  }

  SIM_EXPECT_TRUE(processes.GetProcess(1) == nullptr);
  SIM_EXPECT_TRUE(processes.GetActiveProcess() == nullptr);
  SIM_EXPECT_TRUE(hardware.GetCodeRegion(1) == nullptr);
  SIM_EXPECT_TRUE(!gateway.GetUserIdForHandle(1).has_value());
  const sim::security::EwcQueryResult query = hardware.GetEwcTable().Query(base_va, 1);
  SIM_EXPECT_TRUE(!query.allow);
  SIM_EXPECT_TRUE(!query.matched_window);
  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(events[0].type, std::string("GATEWAY_LOAD_FAIL"));
}

int main() {
  return sim::test::RunAll();
}
