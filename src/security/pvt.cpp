#include "security/pvt.hpp"

#include <sstream>
#include <utility>

namespace sim::security {
namespace {

class IdentityMappedPageAllocator final : public PageAllocator {
 public:
  std::uint64_t AllocatePageId(std::uint64_t va) const override { return va / kPageSize; }
};

bool PermissionsMatchPageType(PvtPageType page_type, MemoryPermissions permissions) {
  switch (page_type) {
    case PvtPageType::CODE:
      return permissions == MemoryPermissions::RX;
    case PvtPageType::DATA:
      return permissions == MemoryPermissions::RW || permissions == MemoryPermissions::RO;
  }
  return false;
}

std::string MakeMismatchDetail(ContextHandle handle, std::uint64_t va, PvtPageType page_type,
                               const EwcQueryResult& query_result, const char* reason) {
  std::ostringstream oss;
  oss << "reason=" << reason << " context_handle=" << handle << " va=" << va
      << " page_type=" << PvtPageTypeToString(page_type) << " permissions="
      << MemoryPermissionsToString(query_result.permissions) << " window_id=";
  if (query_result.matched_window) {
    oss << query_result.window_id;
  } else {
    oss << "none";
  }
  return oss.str();
}

}  // namespace

const PageAllocator& DefaultPageAllocator() {
  static const IdentityMappedPageAllocator allocator;
  return allocator;
}

PvtTable::PvtTable(const EwcTable& ewc, AuditCollector& audit, const PageAllocator& page_allocator)
    : ewc_(ewc), audit_(audit), page_allocator_(page_allocator) {}

PvtRegisterResult PvtTable::RegisterPage(ContextHandle handle, std::uint64_t va, PvtPageType page_type) {
  const std::uint64_t pa_page_id = page_allocator_.AllocatePageId(va);
  const EwcQueryResult query_result = ewc_.Query(va, handle);

  if (!query_result.matched_window) {
    const std::string detail = MakeMismatchDetail(handle, va, page_type, query_result, "missing_window");
    audit_.LogEvent("PVT_MISMATCH", query_result.owner_user_id, handle, va, detail);
    return PvtRegisterResult{false, pa_page_id, detail};
  }

  if (!PermissionsMatchPageType(page_type, query_result.permissions)) {
    const std::string detail = MakeMismatchDetail(handle, va, page_type, query_result, "page_type_permissions");
    audit_.LogEvent("PVT_MISMATCH", query_result.owner_user_id, handle, va, detail);
    return PvtRegisterResult{false, pa_page_id, detail};
  }

  PvtEntry entry;
  entry.pa_page_id = pa_page_id;
  entry.owner_user_id = query_result.owner_user_id;
  entry.expected_va = va;
  entry.permissions = query_result.permissions;
  entry.page_type = page_type;
  entry.state = PvtEntryState::ALLOCATED;
  entries_[pa_page_id] = entry;

  return PvtRegisterResult{true, pa_page_id, {}};
}

void PvtTable::RemovePage(std::uint64_t pa_page_id) { entries_.erase(pa_page_id); }

PvtEntry* PvtTable::LookupPage(std::uint64_t pa_page_id) {
  const auto it = entries_.find(pa_page_id);
  if (it == entries_.end()) {
    return nullptr;
  }
  return &it->second;
}

const PvtEntry* PvtTable::LookupPage(std::uint64_t pa_page_id) const {
  const auto it = entries_.find(pa_page_id);
  if (it == entries_.end()) {
    return nullptr;
  }
  return &it->second;
}

const char* MemoryPermissionsToString(MemoryPermissions permissions) {
  switch (permissions) {
    case MemoryPermissions::NONE:
      return "NONE";
    case MemoryPermissions::RX:
      return "RX";
    case MemoryPermissions::RW:
      return "RW";
    case MemoryPermissions::RO:
      return "RO";
  }
  return "NONE";
}

const char* PvtPageTypeToString(PvtPageType page_type) {
  switch (page_type) {
    case PvtPageType::CODE:
      return "CODE";
    case PvtPageType::DATA:
      return "DATA";
  }
  return "DATA";
}

}  // namespace sim::security
