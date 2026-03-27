#include "isa/opcode.hpp"

namespace sim::isa {

const char* OpToString(Op op) {
  switch (op) {
    case Op::NOP:
      return "NOP";
    case Op::LI:
      return "LI";
    case Op::ADD:
      return "ADD";
    case Op::XOR:
      return "XOR";
    case Op::LD:
      return "LD";
    case Op::ST:
      return "ST";
    case Op::J:
      return "J";
    case Op::BEQ:
      return "BEQ";
    case Op::CALL:
      return "CALL";
    case Op::RET:
      return "RET";
    case Op::HALT:
      return "HALT";
    case Op::SYSCALL:
      return "SYSCALL";
  }
  return "UNKNOWN";
}

}  // namespace sim::isa
