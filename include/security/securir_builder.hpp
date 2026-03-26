#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "isa/assembler.hpp"
#include "security/pvt.hpp"
#include "security/securir_package.hpp"

namespace sim::security {

struct SecureIrPageSpec {
  std::uint64_t va = 0;
  PvtPageType page_type = PvtPageType::DATA;
};

struct SecureIrWindowSpec {
  std::uint32_t window_id = 0;
  std::uint64_t start_va = 0;
  std::uint64_t end_va = 0;
  std::uint32_t key_id = 0;
  std::string type = "CODE";
  std::uint32_t code_policy_id = 1;
};

struct SecureIrBuilderConfig {
  std::string program_name;
  std::uint32_t user_id = 0;
  std::uint32_t key_id = 0;
  std::uint64_t entry_offset = 0;
  std::uint32_t window_id = 1;
  std::uint32_t code_policy_id = 1;
  std::string signature = "stub-valid";
  std::uint32_t cfi_level = 0;
  std::vector<std::uint64_t> call_targets;
  std::vector<std::uint64_t> jmp_targets;
  std::vector<SecureIrPageSpec> pages;
  std::vector<SecureIrWindowSpec> windows;
};

class SecureIrBuilder {
 public:
  static SecureIrPackage Build(const sim::isa::AsmProgram& program, const SecureIrBuilderConfig& config);
};

}  // namespace sim::security
