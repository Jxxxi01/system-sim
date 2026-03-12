#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim::security {

struct AuditEvent {
  std::uint64_t seq_no = 0;      // Monotonic sequence assigned by the collector.
  std::string type;              // Event type string.
  std::uint32_t user_id = 0;
  std::uint32_t context_handle = 0;
  std::uint64_t pc = 0;
  std::string detail;  // Space-delimited key=value pairs.
};

class AuditCollector {
 public:
  void LogEvent(std::string type, std::uint32_t user_id, std::uint32_t context_handle, std::uint64_t pc,
                std::string detail);
  void LogEvent(AuditEvent event);
  const std::vector<AuditEvent>& GetEvents() const;
  void Clear();

 private:
  std::uint64_t next_seq_no_ = 1;
  std::vector<AuditEvent> events_;
};

std::string FormatAuditEvent(const AuditEvent& event);

}  // namespace sim::security
