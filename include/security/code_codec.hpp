#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "isa/assembler.hpp"
#include "isa/instr.hpp"

namespace sim::security {

struct CipherInstrUnit {
  std::array<std::uint8_t, 24> payload{};
  std::uint32_t key_check = 0;
  std::uint32_t tag = 0;
};

using CipherProgram = std::vector<CipherInstrUnit>;

struct DecryptResult {
  bool ok = false;
  sim::isa::Instr instr;
  std::string detail;
};

// Toolchain-side helper that simulates signing-time code encryption while sharing
// the exact on-wire format with the hardware-side EWC decrypt path.
CipherProgram EncryptProgram(const sim::isa::AsmProgram& program, std::uint32_t key_id);

// Hardware-side helper that simulates EWC decrypting one fetched ciphertext
// instruction on every Fetch before the Decode stage.
DecryptResult DecryptInstr(const CipherInstrUnit& unit, std::uint32_t key_id, std::uint64_t pc);

}  // namespace sim::security
