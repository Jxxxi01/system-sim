#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "security/hardware.hpp"
#include "security/securir_package.hpp"

namespace sim::security {

inline constexpr std::size_t kMaxContextHandles = 256;  // Simulation limit for concurrent context_handle slots.

class Gateway {
 public:
  explicit Gateway(SecurityHardware& hardware);

  GatewayLoadResult Load(SecureIrPackage package);
  void Release(ContextHandle handle);
  std::optional<std::uint32_t> GetUserIdForHandle(ContextHandle handle) const;

 private:
  SecurityHardware& hardware_;
  ContextHandle next_handle_ = 1;
  std::unordered_map<ContextHandle, std::uint32_t> handle_to_user_;
};

}  // namespace sim::security
