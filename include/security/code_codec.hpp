#pragma once

#include <array>
#include <cstddef>
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
constexpr std::size_t kCipherUnitBytes = 32;

struct DecryptResult {
  bool ok = false;
  sim::isa::Instr instr;
  std::string detail;
};

// Toolchain-side helper that simulates signing-time code encryption while sharing
// the exact on-wire format with the hardware-side EWC decrypt path.
CipherProgram EncryptProgram(const sim::isa::AsmProgram& program, std::uint32_t key_id);
std::array<std::uint8_t, kCipherUnitBytes> SerializeCipherUnit(const CipherInstrUnit& unit);
CipherInstrUnit DeserializeCipherUnit(const std::uint8_t* data, std::size_t len);
std::vector<std::uint8_t> BuildCodeMemory(const CipherProgram& program);

// Hardware-side helper that simulates EWC decrypting one fetched ciphertext
// instruction on every Fetch before the Decode stage.
DecryptResult DecryptInstr(const CipherInstrUnit& unit, std::uint32_t key_id, std::uint64_t pc);

}  // namespace sim::security
