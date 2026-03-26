#include "kernel/process.hpp"

#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "security/pvt.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <sstream>
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
                               std::uint32_t key_id) {
  std::ostringstream oss;
  oss << "[{"
      << "\"window_id\":" << window_id << ","
      << "\"start_va\":" << start_va << ","
      << "\"end_va\":" << end_va << ","
      << "\"key_id\":" << key_id << ","
      << "\"type\":\"CODE\","
      << "\"code_policy_id\":1"
      << "}]";
  return oss.str();
}

std::string MakePagesJson(std::uint64_t va, const char* page_type) {
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

sim::kernel::KernelProcessTable MakeProcessTable(sim::security::Gateway& gateway,
                                                 sim::security::SecurityHardware& hardware) {
  return sim::kernel::KernelProcessTable(gateway, hardware, hardware.GetAuditCollector());
}

sim::security::SecureIrPackage MakePackage(const std::string& metadata, std::vector<std::uint8_t> code_memory = {}) {
  return sim::security::SecureIrPackage{metadata, code_memory};
}

sim::security::ExecWindow MakeWindow(std::uint64_t start_va, std::uint64_t end_va, std::uint32_t owner_user_id,
                                     sim::security::MemoryPermissions permissions) {
  sim::security::ExecWindow window;
  window.window_id = 1;
  window.start_va = start_va;
  window.end_va = end_va;
  window.owner_user_id = owner_user_id;
  window.key_id = 9;
  window.type = sim::security::ExecWindowType::CODE;
  window.permissions = permissions;
  window.code_policy_id = 1;
  return window;
}

}  // namespace

SIM_TEST(RegisterPage_CodePage_ValidWindow_Succeeds) {
  sim::security::SecurityHardware hardware;
  const sim::security::ContextHandle handle = 1;
  const std::uint64_t va = 0x2000;

  hardware.GetEwcTable().SetWindows(handle, {MakeWindow(va, va + sim::security::kPageSize, 41,
                                                        sim::security::MemoryPermissions::RX)});

  const sim::security::PvtRegisterResult result =
      hardware.GetPvtTable().RegisterPage(handle, va, sim::security::PvtPageType::CODE);

  SIM_EXPECT_TRUE(result.ok);
  SIM_EXPECT_EQ(result.pa_page_id, va / sim::security::kPageSize);
  const sim::security::PvtEntry* entry = hardware.GetPvtTable().LookupPage(result.pa_page_id);
  SIM_EXPECT_TRUE(entry != nullptr);
  SIM_EXPECT_EQ(entry->owner_user_id, 41u);
  SIM_EXPECT_EQ(entry->expected_va, va);
  SIM_EXPECT_EQ(entry->permissions, sim::security::MemoryPermissions::RX);
  SIM_EXPECT_EQ(entry->page_type, sim::security::PvtPageType::CODE);
  SIM_EXPECT_EQ(entry->state, sim::security::PvtEntryState::ALLOCATED);
}

SIM_TEST(RegisterPage_OwnerComesFromEwc) {
  sim::security::SecurityHardware hardware;
  const sim::security::ContextHandle handle = 3;
  const std::uint64_t va = 0x3000;

  hardware.GetEwcTable().SetWindows(handle, {MakeWindow(va, va + sim::security::kPageSize, 77,
                                                        sim::security::MemoryPermissions::RX)});

  const sim::security::PvtRegisterResult result =
      hardware.GetPvtTable().RegisterPage(handle, va, sim::security::PvtPageType::CODE);

  SIM_EXPECT_TRUE(result.ok);
  const sim::security::PvtEntry* entry = hardware.GetPvtTable().LookupPage(result.pa_page_id);
  SIM_EXPECT_TRUE(entry != nullptr);
  SIM_EXPECT_EQ(entry->owner_user_id, 77u);
}

SIM_TEST(RegisterPage_CodePage_NoWindow_Fails) {
  sim::security::SecurityHardware hardware;
  const sim::security::PvtRegisterResult result =
      hardware.GetPvtTable().RegisterPage(5, 0x4000, sim::security::PvtPageType::CODE);

  SIM_EXPECT_TRUE(!result.ok);
  SIM_EXPECT_TRUE(hardware.GetPvtTable().LookupPage(0x4000 / sim::security::kPageSize) == nullptr);
  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(events[0].type, std::string("PVT_MISMATCH"));
  SIM_EXPECT_TRUE(Contains(events[0].detail, "reason=missing_window"));
}

SIM_TEST(RegisterPage_CodePage_OwnerMismatch_Fails) {
  sim::security::SecurityHardware hardware;
  const sim::security::ContextHandle alice_handle = 1;
  const sim::security::ContextHandle bob_handle = 2;
  const std::uint64_t alice_va = 0x1000;
  const std::uint64_t bob_va = 0x2000;

  hardware.GetEwcTable().SetWindows(alice_handle, {MakeWindow(alice_va, alice_va + sim::security::kPageSize, 1,
                                                              sim::security::MemoryPermissions::RX)});
  hardware.GetEwcTable().SetWindows(bob_handle, {MakeWindow(bob_va, bob_va + sim::security::kPageSize, 2,
                                                            sim::security::MemoryPermissions::RX)});

  const sim::security::PvtRegisterResult result =
      hardware.GetPvtTable().RegisterPage(bob_handle, alice_va, sim::security::PvtPageType::CODE);

  SIM_EXPECT_TRUE(!result.ok);
  SIM_EXPECT_TRUE(hardware.GetPvtTable().LookupPage(alice_va / sim::security::kPageSize) == nullptr);
  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(events[0].type, std::string("PVT_MISMATCH"));
  SIM_EXPECT_EQ(events[0].context_handle, bob_handle);
  SIM_EXPECT_TRUE(Contains(events[0].detail, "reason=missing_window"));
}

SIM_TEST(RegisterPage_PageTypePermissionsMismatch_Fails) {
  sim::security::SecurityHardware hardware;
  const sim::security::ContextHandle handle = 6;
  const std::uint64_t va = 0x5000;

  hardware.GetEwcTable().SetWindows(handle, {MakeWindow(va, va + sim::security::kPageSize, 88,
                                                        sim::security::MemoryPermissions::RO)});

  const sim::security::PvtRegisterResult result =
      hardware.GetPvtTable().RegisterPage(handle, va, sim::security::PvtPageType::CODE);

  SIM_EXPECT_TRUE(!result.ok);
  SIM_EXPECT_TRUE(hardware.GetPvtTable().LookupPage(va / sim::security::kPageSize) == nullptr);
  const auto& events = hardware.GetAuditCollector().GetEvents();
  SIM_EXPECT_EQ(events.size(), static_cast<std::size_t>(1));
  SIM_EXPECT_EQ(events[0].type, std::string("PVT_MISMATCH"));
  SIM_EXPECT_TRUE(Contains(events[0].detail, "reason=page_type_permissions"));
}

SIM_TEST(RemovePage_Works) {
  sim::security::SecurityHardware hardware;
  const sim::security::ContextHandle handle = 7;
  const std::uint64_t va = 0x6000;

  hardware.GetEwcTable().SetWindows(handle, {MakeWindow(va, va + sim::security::kPageSize, 91,
                                                        sim::security::MemoryPermissions::RX)});
  const sim::security::PvtRegisterResult result =
      hardware.GetPvtTable().RegisterPage(handle, va, sim::security::PvtPageType::CODE);
  SIM_EXPECT_TRUE(result.ok);

  hardware.GetPvtTable().RemovePage(result.pa_page_id);

  SIM_EXPECT_TRUE(hardware.GetPvtTable().LookupPage(result.pa_page_id) == nullptr);
}

SIM_TEST(LoadProcess_WithPages_RegistersPvt) {
  sim::security::SecurityHardware hardware;
  sim::security::Gateway gateway(hardware);
  sim::kernel::KernelProcessTable processes = MakeProcessTable(gateway, hardware);
  const std::uint64_t base_va = 0x7000;

  const sim::security::ContextHandle handle = processes.LoadProcess(MakePackage(
      MakeSecureIrJson("proc", 55, "sig", base_va,
                       MakeCodeWindowJson(1, base_va, base_va + sim::security::kPageSize, 11),
                       MakePagesJson(base_va, "CODE")),
      {1, 2, 3, 4}));

  SIM_EXPECT_EQ(handle, static_cast<sim::security::ContextHandle>(1));
  const sim::security::PvtEntry* entry = hardware.GetPvtTable().LookupPage(base_va / sim::security::kPageSize);
  SIM_EXPECT_TRUE(entry != nullptr);
  SIM_EXPECT_EQ(entry->owner_user_id, 55u);
  SIM_EXPECT_EQ(entry->expected_va, base_va);
  SIM_EXPECT_EQ(entry->permissions, sim::security::MemoryPermissions::RX);
  SIM_EXPECT_EQ(entry->page_type, sim::security::PvtPageType::CODE);
}

int main() {
  return sim::test::RunAll();
}
