#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <stdexcept>

#include "security/audit.hpp"
#include "security/ewc.hpp"
#include "security/pvt.hpp"
#include "security/spe.hpp"

namespace sim::security {

class Gateway;

struct CodeRegion {
  std::uint64_t base_va = 0;
  std::vector<std::uint8_t> code_memory;
};

struct HandleMetadata {
  std::uint64_t saved_pc = 0;
  std::uint32_t user_id = 0;
};

class SecurityHardware {
 public:
  SecurityHardware();

  EwcTable& GetEwcTable();
  const EwcTable& GetEwcTable() const;

  SpeTable& GetSpeTable();
  const SpeTable& GetSpeTable() const;

  PvtTable& GetPvtTable();
  const PvtTable& GetPvtTable() const;

  AuditCollector& GetAuditCollector();
  const AuditCollector& GetAuditCollector() const;

  void StoreCodeRegion(ContextHandle handle, std::uint64_t base_va, std::vector<std::uint8_t> code_memory);
  CodeRegion* GetCodeRegion(ContextHandle handle);
  const CodeRegion* GetCodeRegion(ContextHandle handle) const;
  const HandleMetadata* GetHandleMetadata(ContextHandle handle) const;
  std::optional<std::uint64_t> GetSavedPcForHandle(ContextHandle handle) const;
  std::optional<std::uint32_t> GetUserIdForHandle(ContextHandle handle) const;
  std::size_t GetLoadedHandleCount() const;
  void RemoveCodeRegion(ContextHandle handle);
  void SetActiveHandle(ContextHandle handle);
  std::optional<ContextHandle> GetActiveHandle() const;
  void ClearActiveHandle();

 private:
  friend class Gateway;

  void StoreHandleMetadata(ContextHandle handle, std::uint64_t saved_pc, std::uint32_t user_id);
  void RemoveHandleMetadata(ContextHandle handle);

  EwcTable ewc_table_;
  AuditCollector audit_collector_;
  SpeTable spe_table_;
  PvtTable pvt_table_;
  std::unordered_map<ContextHandle, CodeRegion> code_regions_;
  std::unordered_map<ContextHandle, HandleMetadata> handle_metadata_;
  std::optional<ContextHandle> active_handle_;
};

inline SecurityHardware::SecurityHardware() : spe_table_(audit_collector_), pvt_table_(ewc_table_, audit_collector_) {
  audit_collector_.SetHandleUserIdResolver(
      [this](ContextHandle handle) -> std::optional<std::uint32_t> { return GetUserIdForHandle(handle); });
}

inline EwcTable& SecurityHardware::GetEwcTable() { return ewc_table_; }

inline const EwcTable& SecurityHardware::GetEwcTable() const { return ewc_table_; }

inline SpeTable& SecurityHardware::GetSpeTable() { return spe_table_; }

inline const SpeTable& SecurityHardware::GetSpeTable() const { return spe_table_; }

inline PvtTable& SecurityHardware::GetPvtTable() { return pvt_table_; }

inline const PvtTable& SecurityHardware::GetPvtTable() const { return pvt_table_; }

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

inline const HandleMetadata* SecurityHardware::GetHandleMetadata(ContextHandle handle) const {
  auto it = handle_metadata_.find(handle);
  if (it == handle_metadata_.end()) {
    return nullptr;
  }
  return &it->second;
}

inline std::optional<std::uint64_t> SecurityHardware::GetSavedPcForHandle(ContextHandle handle) const {
  const HandleMetadata* metadata = GetHandleMetadata(handle);
  if (metadata == nullptr) {
    return std::nullopt;
  }
  return metadata->saved_pc;
}

inline std::optional<std::uint32_t> SecurityHardware::GetUserIdForHandle(ContextHandle handle) const {
  const HandleMetadata* metadata = GetHandleMetadata(handle);
  if (metadata == nullptr) {
    return std::nullopt;
  }
  return metadata->user_id;
}

inline std::size_t SecurityHardware::GetLoadedHandleCount() const { return handle_metadata_.size(); }

inline void SecurityHardware::RemoveCodeRegion(ContextHandle handle) { code_regions_.erase(handle); }

inline void SecurityHardware::StoreHandleMetadata(ContextHandle handle, std::uint64_t saved_pc, std::uint32_t user_id) {
  handle_metadata_[handle] = HandleMetadata{saved_pc, user_id};
}

inline void SecurityHardware::RemoveHandleMetadata(ContextHandle handle) { handle_metadata_.erase(handle); }

inline void SecurityHardware::SetActiveHandle(ContextHandle handle) {
  if (GetCodeRegion(handle) == nullptr || GetHandleMetadata(handle) == nullptr) {
    throw std::runtime_error("security_hardware_invalid_active_handle");
  }
  active_handle_ = handle;
}

inline std::optional<ContextHandle> SecurityHardware::GetActiveHandle() const { return active_handle_; }

inline void SecurityHardware::ClearActiveHandle() { active_handle_.reset(); }

}  // namespace sim::security
