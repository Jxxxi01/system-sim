#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "security/pvt.hpp"
#include "security/audit.hpp"
#include "security/gateway.hpp"
#include "security/hardware.hpp"
#include "security/securir_package.hpp"

namespace sim::kernel {

struct ProcessContext {
  sim::security::ContextHandle context_handle = 0;
  std::uint32_t user_id = 0;
  std::uint64_t base_va = 0;
  std::vector<std::uint64_t> pvt_page_ids;
};

class KernelProcessTable {
 public:
  KernelProcessTable(sim::security::Gateway& gateway, sim::security::SecurityHardware& hardware,
                     sim::security::AuditCollector& audit);

  sim::security::ContextHandle LoadProcess(sim::security::SecureIrPackage package);
  void ReleaseProcess(sim::security::ContextHandle handle);
  void ContextSwitch(sim::security::ContextHandle handle);

  const ProcessContext* GetActiveProcess() const;
  const ProcessContext* GetProcess(sim::security::ContextHandle handle) const;

 private:
  sim::security::Gateway& gateway_;
  sim::security::SecurityHardware& hardware_;
  sim::security::AuditCollector& audit_;
  std::unordered_map<sim::security::ContextHandle, ProcessContext> processes_;
  std::optional<sim::security::ContextHandle> active_handle_;
};

}  // namespace sim::kernel
