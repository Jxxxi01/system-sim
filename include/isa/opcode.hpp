#pragma once

namespace sim::isa {

enum class Op {
  NOP,
  LI,
  ADD,
  XOR,
  LD,
  ST,
  J,
  BEQ,
  CALL,
  RET,
  HALT,
  SYSCALL
};

}  // namespace sim::isa
