#include "security/audit.hpp"

#include <sstream>
#include <utility>

namespace sim::security {

void AuditCollector::LogEvent(std::string type, std::uint32_t user_id, std::uint32_t context_handle,
                              std::uint64_t pc, std::string detail) {
  AuditEvent event;
  event.type = std::move(type);
  event.user_id = user_id;
  event.context_handle = context_handle;
  event.pc = pc;
  event.detail = std::move(detail);
  LogEvent(std::move(event));
}

void AuditCollector::LogEvent(AuditEvent event) {
  event.seq_no = next_seq_no_++;
  events_.push_back(std::move(event));
}

const std::vector<AuditEvent>& AuditCollector::GetEvents() const { return events_; }

void AuditCollector::Clear() { events_.clear(); }

std::string FormatAuditEvent(const AuditEvent& event) {
  std::ostringstream oss;
  oss << "seq_no=" << event.seq_no << " type=" << event.type << " user_id=" << event.user_id
      << " context_handle=" << event.context_handle << " pc=" << event.pc << " detail=" << event.detail;
  return oss.str();
}

}  // namespace sim::security
