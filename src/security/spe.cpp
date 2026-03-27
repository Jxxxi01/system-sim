#include "security/spe.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace sim::security {
namespace {

bool ContainsTarget(const std::vector<std::uint64_t>& targets, std::uint64_t target) {
  for (std::uint64_t allowed_target : targets) {
    if (allowed_target == target) {
      return true;
    }
  }
  return false;
}

SpeCheckResult MakeViolationDetail(const char* stage, sim::isa::Op op, const char* reason, std::uint32_t cfi_level,
                                   std::uint64_t pc, std::uint64_t committed_pc, std::uint64_t next_pc,
                                   std::uint64_t expected_pc = 0, bool has_expected_pc = false,
                                   std::size_t shadow_depth = 0) {
  std::ostringstream oss;
  oss << "stage=" << stage << " op=" << sim::isa::OpToString(op) << " reason=" << reason
      << " cfi_level=" << cfi_level << " pc=" << pc << " committed_pc=" << committed_pc
      << " next_pc=" << next_pc;
  if (has_expected_pc) {
    oss << " expected_pc=" << expected_pc;
  }
  oss << " shadow_depth=" << shadow_depth;
  return SpeCheckResult{false, oss.str()};
}

}  // namespace

SpeTable::SpeTable(AuditCollector& audit_collector) : audit_collector_(audit_collector) {}

void SpeTable::ConfigurePolicy(ContextHandle handle, std::uint32_t user_id, std::uint32_t cfi_level,
                               std::vector<std::uint64_t> call_targets, std::vector<std::uint64_t> jmp_targets) {
  if (cfi_level > 3) {
    std::ostringstream oss;
    oss << "spe_invalid_cfi_level cfi_level=" << cfi_level;
    throw std::runtime_error(oss.str());
  }

  Policy policy;
  policy.user_id = user_id;
  policy.cfi_level = cfi_level;
  policy.call_targets = std::move(call_targets);
  policy.jmp_targets = std::move(jmp_targets);
  policies_[handle] = std::move(policy);
}

void SpeTable::ClearPolicy(ContextHandle handle) { policies_.erase(handle); }

SpeCheckResult SpeTable::CheckInstruction(ContextHandle handle, sim::isa::Op op, std::uint64_t pc,
                                          std::uint64_t committed_pc, std::uint64_t next_pc) {
  auto it = policies_.find(handle);
  if (it == policies_.end()) {
    return SpeCheckResult{};
  }

  Policy& policy = it->second;
  if (policy.cfi_level <= 1) {
    return SpeCheckResult{};
  }

  SpeCheckResult result;
  switch (op) {
    case sim::isa::Op::CALL: {
      if (policy.cfi_level >= 3 && !ContainsTarget(policy.call_targets, committed_pc)) {
        result = MakeViolationDetail("execute", op, "call_target_not_allowed", policy.cfi_level, pc, committed_pc,
                                     next_pc, 0, false, policy.shadow_stack.size());
        break;
      }
      policy.shadow_stack.push_back(next_pc);
      return SpeCheckResult{};
    }
    case sim::isa::Op::J: {
      if (policy.cfi_level >= 3 && !ContainsTarget(policy.jmp_targets, committed_pc)) {
        result = MakeViolationDetail("execute", op, "jump_target_not_allowed", policy.cfi_level, pc, committed_pc,
                                     next_pc, 0, false, policy.shadow_stack.size());
      }
      break;
    }
    case sim::isa::Op::BEQ: {
      if (committed_pc == next_pc) {
        return SpeCheckResult{};
      }
      if (policy.cfi_level >= 3 && !ContainsTarget(policy.jmp_targets, committed_pc)) {
        result = MakeViolationDetail("execute", op, "jump_target_not_allowed", policy.cfi_level, pc, committed_pc,
                                     next_pc, 0, false, policy.shadow_stack.size());
      }
      break;
    }
    case sim::isa::Op::RET: {
      if (policy.shadow_stack.empty()) {
        result = MakeViolationDetail("execute", op, "shadow_stack_underflow", policy.cfi_level, pc, committed_pc,
                                     next_pc, 0, false, 0);
        break;
      }
      const std::uint64_t expected_pc = policy.shadow_stack.back();
      policy.shadow_stack.pop_back();
      if (expected_pc != committed_pc) {
        result = MakeViolationDetail("execute", op, "shadow_stack_mismatch", policy.cfi_level, pc, committed_pc,
                                     next_pc, expected_pc, true, policy.shadow_stack.size());
      }
      break;
    }
    default:
      return SpeCheckResult{};
  }

  if (!result.allow) {
    audit_collector_.LogEvent("SPE_VIOLATION", policy.user_id, handle, pc, result.detail);
  }
  return result;
}

}  // namespace sim::security
