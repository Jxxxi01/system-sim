#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "security/pvt.hpp"

namespace sim::security {

struct SecureIrPackage {
  std::string metadata;
  std::vector<std::uint8_t> code_memory;
};

struct GatewayPageLayout {
  std::uint64_t va = 0;
  PvtPageType page_type = PvtPageType::DATA;
};

struct GatewayLoadResult {
  ContextHandle handle = 0;
  std::uint32_t user_id = 0;
  std::uint64_t base_va = 0;
  std::vector<GatewayPageLayout> pages;
};

}  // namespace sim::security
