#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "isa/instr.hpp"

namespace sim::isa {

// Supported assembly (minimal):
//   [label:] OPCODE operands [# comment]
// Notes:
// 1) Opcode parsing is case-insensitive; labels and registers are case-sensitive.
// 2) Registers must be x0..x31.
// 3) CALL/J/BEQ target rules:
//    - label target: imm = target_va - (current_va + 4)  (next-PC relative)
//    - numeric target: number is used directly as the final pc-relative imm
// 4) Memory operand syntax for LD/ST:
//    [rs1], [rs1+imm], [rs1-imm], with optional spaces; omitted imm means 0.
struct AsmProgram {
  std::uint64_t base_va = 0;
  std::vector<Instr> code;
};

struct LocatedInstr {
  std::uint64_t va = 0;
  Instr instr;
};

AsmProgram AssembleText(std::string_view text, std::uint64_t base_va);
std::uint64_t InstrVa(const AsmProgram& program, std::size_t index);
std::vector<LocatedInstr> ToLocated(const AsmProgram& program);

}  // namespace sim::isa
