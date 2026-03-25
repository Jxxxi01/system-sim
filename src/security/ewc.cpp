#include "security/ewc.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace sim::security {

void EwcTable::SetWindows(ContextHandle context_handle, std::vector<ExecWindow> windows) {
  std::sort(windows.begin(), windows.end(),
            [](const ExecWindow& lhs, const ExecWindow& rhs) { return lhs.start_va < rhs.start_va; });

  for (std::size_t i = 0; i < windows.size(); ++i) {
    const ExecWindow& current = windows[i];
    if (current.start_va >= current.end_va) {
      std::ostringstream oss;
      oss << "ewc invalid_range context_handle=" << context_handle << " window_id=" << current.window_id
          << " range=[" << current.start_va << "," << current.end_va << ")";
      throw std::runtime_error(oss.str());
    }

    if (i == 0) {
      continue;
    }
    const ExecWindow& prev = windows[i - 1];
    if (prev.end_va > current.start_va) {
      std::ostringstream oss;
      oss << "ewc overlap context_handle=" << context_handle << " prev_window_id=" << prev.window_id
          << " prev_range=[" << prev.start_va << "," << prev.end_va << ")"
          << " cur_window_id=" << current.window_id << " cur_range=[" << current.start_va << ","
          << current.end_va << ")";
      throw std::runtime_error(oss.str());
    }
  }

  windows_by_context_[context_handle] = std::move(windows);
}

void EwcTable::ClearWindows(ContextHandle context_handle) { windows_by_context_.erase(context_handle); }

EwcQueryResult EwcTable::Query(std::uint64_t pc, ContextHandle context_handle) const {
  EwcQueryResult result;

  auto it = windows_by_context_.find(context_handle);
  if (it == windows_by_context_.end()) {
    return result;
  }

  for (const ExecWindow& window : it->second) {
    if (window.start_va <= pc && pc < window.end_va) {
      result.allow = true;
      result.matched_window = true;
      result.key_id = window.key_id;
      result.window_id = window.window_id;
      result.owner_user_id = window.owner_user_id;
      result.permissions = window.permissions;
      result.code_policy_id = window.code_policy_id;
      return result;
    }
  }
  return result;
}

}  // namespace sim::security
