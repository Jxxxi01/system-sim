#pragma once

#include "security/audit.hpp"
#include "security/ewc.hpp"

namespace sim::security {

class SecurityHardware {
 public:
  EwcTable& GetEwcTable();
  const EwcTable& GetEwcTable() const;

  AuditCollector& GetAuditCollector();
  const AuditCollector& GetAuditCollector() const;

 private:
  EwcTable ewc_table_;
  AuditCollector audit_collector_;
};

inline EwcTable& SecurityHardware::GetEwcTable() { return ewc_table_; }

inline const EwcTable& SecurityHardware::GetEwcTable() const { return ewc_table_; }

inline AuditCollector& SecurityHardware::GetAuditCollector() { return audit_collector_; }

inline const AuditCollector& SecurityHardware::GetAuditCollector() const { return audit_collector_; }

}  // namespace sim::security
