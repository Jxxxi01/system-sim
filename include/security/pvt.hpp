#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "security/audit.hpp"
#include "security/ewc.hpp"

namespace sim::security {

inline constexpr std::uint64_t kPageSize = 4096;

enum class PvtPageType {
  CODE,
  DATA
};

enum class PvtEntryState {
  FREE,
  ALLOCATED
};

struct PvtEntry {
  std::uint64_t pa_page_id = 0;
  std::uint32_t owner_user_id = 0;
  std::uint64_t expected_va = 0;
  MemoryPermissions permissions = MemoryPermissions::NONE;
  PvtPageType page_type = PvtPageType::DATA;
  PvtEntryState state = PvtEntryState::FREE;
};

struct PvtRegisterResult {
  bool ok = false;
  std::uint64_t pa_page_id = 0;
  std::string error;
};

class PageAllocator {
 public:
  virtual ~PageAllocator() = default;
  virtual std::uint64_t AllocatePageId(std::uint64_t va) const = 0;
};

const PageAllocator& DefaultPageAllocator();

class PvtTable {
 public:
  PvtTable(const EwcTable& ewc, AuditCollector& audit,
           const PageAllocator& page_allocator = DefaultPageAllocator());

  PvtRegisterResult RegisterPage(ContextHandle handle, std::uint64_t va, PvtPageType page_type);
  void RemovePage(std::uint64_t pa_page_id);
  PvtEntry* LookupPage(std::uint64_t pa_page_id);
  const PvtEntry* LookupPage(std::uint64_t pa_page_id) const;

 private:
  const EwcTable& ewc_;
  AuditCollector& audit_;
  const PageAllocator& page_allocator_;
  std::unordered_map<std::uint64_t, PvtEntry> entries_;
};

const char* MemoryPermissionsToString(MemoryPermissions permissions);
const char* PvtPageTypeToString(PvtPageType page_type);

}  // namespace sim::security
