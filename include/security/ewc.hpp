#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "security/scaffold.hpp"

namespace sim::security {

enum class ExecWindowType {
  CODE
};

struct ExecWindow {
  std::uint32_t window_id = 0;
  std::uint64_t start_va = 0;  // inclusive
  std::uint64_t end_va = 0;    // exclusive
  std::uint32_t owner_user_id = 0;
  std::uint32_t key_id = 0;
  ExecWindowType type = ExecWindowType::CODE;
  std::uint32_t code_policy_id = 0;
};

struct EwcQueryResult {
  bool allow = false;
  bool matched_window = false;
  std::uint32_t key_id = 0;
  std::uint32_t window_id = 0;
  std::uint32_t owner_user_id = 0;
  std::uint32_t code_policy_id = 0;
};

class EwcTable {
 public:
  void SetWindows(ContextHandle context_handle, std::vector<ExecWindow> windows);
  EwcQueryResult Query(std::uint64_t pc, ContextHandle context_handle) const;

 private:
  std::unordered_map<ContextHandle, std::vector<ExecWindow>> windows_by_context_;
};

}  // namespace sim::security
