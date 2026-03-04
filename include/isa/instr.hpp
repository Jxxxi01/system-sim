#pragma once

#include <cstdint>

#include "isa/opcode.hpp"

namespace sim::isa {

inline constexpr int kNoReg = -1;
inline constexpr std::uint64_t kInstrBytes = 4;

struct Instr {
  Op op = Op::NOP;
  int rd = kNoReg;
  int rs1 = kNoReg;
  int rs2 = kNoReg;
  std::int64_t imm = 0;
};

}  // namespace sim::isa
