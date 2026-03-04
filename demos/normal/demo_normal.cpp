#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "core/executor.hpp"
#include "isa/assembler.hpp"

int main() {
  const std::string source = R"(
start:
  LI x1, 10
  LI x2, 32
  ADD x3, x1, x2
  SYSCALL 1
  HALT
)";

  try {
    const std::uint64_t base_va = 0x1000;
    const sim::isa::AsmProgram program = sim::isa::AssembleText(source, base_va);
    const sim::core::ExecResult result = sim::core::ExecuteProgram(program, base_va);
    sim::core::PrintRunSummary(result, std::cout);
    return (result.trap.reason == sim::core::TrapReason::HALT) ? 0 : 1;
  } catch (const std::exception& ex) {
    std::cerr << "demo_normal failed: " << ex.what() << '\n';
    return 1;
  }
}
