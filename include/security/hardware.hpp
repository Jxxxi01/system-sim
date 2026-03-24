#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "security/audit.hpp"
#include "security/ewc.hpp"

namespace sim::security {

struct CodeRegion {
  std::uint64_t base_va = 0;
  std::vector<std::uint8_t> code_memory;
};

class SecurityHardware {
 public:
  EwcTable& GetEwcTable();
  const EwcTable& GetEwcTable() const;

  AuditCollector& GetAuditCollector();
  const AuditCollector& GetAuditCollector() const;

  void StoreCodeRegion(ContextHandle handle, std::uint64_t base_va, std::vector<std::uint8_t> code_memory);
  CodeRegion* GetCodeRegion(ContextHandle handle);
  const CodeRegion* GetCodeRegion(ContextHandle handle) const;
  void RemoveCodeRegion(ContextHandle handle);

 private:
  EwcTable ewc_table_;
  AuditCollector audit_collector_;
  std::unordered_map<ContextHandle, CodeRegion> code_regions_;
};

inline EwcTable& SecurityHardware::GetEwcTable() { return ewc_table_; }

inline const EwcTable& SecurityHardware::GetEwcTable() const { return ewc_table_; }

inline AuditCollector& SecurityHardware::GetAuditCollector() { return audit_collector_; }

inline const AuditCollector& SecurityHardware::GetAuditCollector() const { return audit_collector_; }

inline void SecurityHardware::StoreCodeRegion(ContextHandle handle, std::uint64_t base_va,
                                              std::vector<std::uint8_t> code_memory) {
  CodeRegion region;
  region.base_va = base_va;
  region.code_memory = std::move(code_memory);
  code_regions_[handle] = std::move(region);
}

inline CodeRegion* SecurityHardware::GetCodeRegion(ContextHandle handle) {
  auto it = code_regions_.find(handle);
  if (it == code_regions_.end()) {
    return nullptr;
  }
  return &it->second;
}

inline const CodeRegion* SecurityHardware::GetCodeRegion(ContextHandle handle) const {
  auto it = code_regions_.find(handle);
  if (it == code_regions_.end()) {
    return nullptr;
  }
  return &it->second;
}

inline void SecurityHardware::RemoveCodeRegion(ContextHandle handle) { code_regions_.erase(handle); }

}  // namespace sim::security
