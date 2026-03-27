#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "isa/opcode.hpp"
#include "security/audit.hpp"
#include "security/scaffold.hpp"

namespace sim::security {

struct SpeCheckResult {
  bool allow = true;
  std::string detail;
};

class SpeTable {
 public:
  explicit SpeTable(AuditCollector& audit_collector);

  void ConfigurePolicy(ContextHandle handle, std::uint32_t user_id, std::uint32_t cfi_level,
                       std::vector<std::uint64_t> call_targets, std::vector<std::uint64_t> jmp_targets);
  void ClearPolicy(ContextHandle handle);
  SpeCheckResult CheckInstruction(ContextHandle handle, sim::isa::Op op, std::uint64_t pc,
                                  std::uint64_t committed_pc, std::uint64_t next_pc);

 private:
  struct Policy {
    std::uint32_t user_id = 0;
    std::uint32_t cfi_level = 0;
    std::vector<std::uint64_t> bounds;  // Reserved for future policy-bound metadata.
    std::vector<std::uint64_t> call_targets;
    std::vector<std::uint64_t> jmp_targets;
    std::vector<std::uint64_t> shadow_stack;
  };

  AuditCollector& audit_collector_;
  std::unordered_map<ContextHandle, Policy> policies_;
};

}  // namespace sim::security
