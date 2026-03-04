#include "core/executor.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "isa/instr.hpp"
#include "isa/opcode.hpp"

namespace sim::core {
namespace {

struct FetchPacket {
  std::uint64_t pc = 0;
  std::uint64_t next_pc = 0;
  std::size_t index = 0;
  sim::isa::Instr instr;
};

std::string MakeInvalidPcMsg(std::uint64_t pc, std::uint64_t base_va, std::string_view index,
                             std::size_t code_size, const char* reason) {
  std::ostringstream oss;
  oss << "invalid_pc reason=" << reason << " pc=" << pc << " base_va=" << base_va
      << " index=" << index << " code_size=" << code_size;
  return oss.str();
}

const char* OpToString(sim::isa::Op op) {
  switch (op) {
    case sim::isa::Op::NOP:
      return "NOP";
    case sim::isa::Op::LI:
      return "LI";
    case sim::isa::Op::ADD:
      return "ADD";
    case sim::isa::Op::XOR:
      return "XOR";
    case sim::isa::Op::LD:
      return "LD";
    case sim::isa::Op::ST:
      return "ST";
    case sim::isa::Op::J:
      return "J";
    case sim::isa::Op::BEQ:
      return "BEQ";
    case sim::isa::Op::CALL:
      return "CALL";
    case sim::isa::Op::RET:
      return "RET";
    case sim::isa::Op::HALT:
      return "HALT";
    case sim::isa::Op::SYSCALL:
      return "SYSCALL";
  }
  return "UNKNOWN";
}

bool IsValidRegIndex(int reg) {
  return reg >= 0 && reg < 32;
}

Trap MakeBadRegTrap(std::uint64_t pc, sim::isa::Op op, const char* field, int reg) {
  std::ostringstream oss;
  oss << "bad_reg op=" << OpToString(op) << " field=" << field << " reg=" << reg << " pc=" << pc;
  return Trap{TrapReason::UNKNOWN_OPCODE, pc, oss.str()};
}

bool AddPcRelative(std::uint64_t next_pc, std::int64_t imm, std::uint64_t* out_pc) {
  const __int128 value = static_cast<__int128>(next_pc) + static_cast<__int128>(imm);
  if (value < 0 || value > static_cast<__int128>(std::numeric_limits<std::uint64_t>::max())) {
    return false;
  }
  *out_pc = static_cast<std::uint64_t>(value);
  return true;
}

bool FetchStage(const sim::isa::AsmProgram& program, std::uint64_t pc, FetchPacket* packet, Trap* trap) {
  const std::uint64_t base_va = program.base_va;
  const std::size_t code_size = program.code.size();

  if (pc < base_va) {
    *trap = Trap{TrapReason::INVALID_PC, pc,
                 MakeInvalidPcMsg(pc, base_va, "-1", code_size, "underflow")};
    return false;
  }

  const std::uint64_t delta = pc - base_va;
  const std::uint64_t index = delta / sim::isa::kInstrBytes;

  if ((delta % sim::isa::kInstrBytes) != 0) {
    *trap = Trap{TrapReason::INVALID_PC, pc,
                 MakeInvalidPcMsg(pc, base_va, std::to_string(index), code_size, "misaligned")};
    return false;
  }

  if (index >= code_size) {
    *trap = Trap{TrapReason::INVALID_PC, pc,
                 MakeInvalidPcMsg(pc, base_va, std::to_string(index), code_size, "oob")};
    return false;
  }

  packet->pc = pc;
  packet->next_pc = pc + sim::isa::kInstrBytes;
  packet->index = static_cast<std::size_t>(index);
  packet->instr = program.code[packet->index];

  // Issue 3 hook: EWC gate check belongs here (before decode/execute).
  return true;
}

sim::isa::Instr DecodeStage(const FetchPacket& packet) {
  // Issue 3 hook: decrypt/decode failure path can be injected here.
  return packet.instr;
}

bool ResolveMemAddress(std::uint64_t base, std::int64_t imm, std::size_t mem_size,
                       std::uint64_t* out_addr) {
  const __int128 start = static_cast<__int128>(base) + static_cast<__int128>(imm);
  if (start < 0) {
    return false;
  }
  const __int128 end = start + 7;
  if (end >= static_cast<__int128>(mem_size)) {
    return false;
  }
  if (start > static_cast<__int128>(std::numeric_limits<std::uint64_t>::max())) {
    return false;
  }
  *out_addr = static_cast<std::uint64_t>(start);
  return true;
}

std::uint64_t Load64LE(const std::vector<std::uint8_t>& mem, std::uint64_t addr) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(mem[static_cast<std::size_t>(addr) + i]) << (8 * i);
  }
  return value;
}

void Store64LE(std::vector<std::uint8_t>* mem, std::uint64_t addr, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    (*mem)[static_cast<std::size_t>(addr) + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
  }
}

}  // namespace

ExecResult ExecuteProgram(const sim::isa::AsmProgram& program, std::uint64_t entry_pc, std::size_t mem_size,
                          std::size_t max_steps) {
  ExecResult result;
  result.state.pc = entry_pc;
  result.state.regs.fill(0);
  result.state.mem.assign(mem_size, 0);

  std::size_t steps = 0;
  while (true) {
    if (steps >= max_steps) {
      std::ostringstream oss;
      oss << "step_limit_exceeded pc=" << result.state.pc << " steps=" << steps
          << " max_steps=" << max_steps;
      result.trap = Trap{TrapReason::STEP_LIMIT, result.state.pc, oss.str()};
      break;
    }

    FetchPacket fetched;
    Trap fetch_trap;
    if (!FetchStage(program, result.state.pc, &fetched, &fetch_trap)) {
      result.trap = std::move(fetch_trap);
      break;
    }

    const sim::isa::Instr instr = DecodeStage(fetched);
    std::uint64_t committed_pc = fetched.next_pc;
    Trap exec_trap;
    bool has_trap = false;

    auto require_reg = [&](int reg, const char* field) -> bool {
      if (!IsValidRegIndex(reg)) {
        exec_trap = MakeBadRegTrap(fetched.pc, instr.op, field, reg);
        has_trap = true;
        return false;
      }
      return true;
    };

    switch (instr.op) {
      case sim::isa::Op::NOP: {
        break;
      }
      case sim::isa::Op::LI: {
        if (!require_reg(instr.rd, "rd")) break;
        // Explicit cast preserves the two's-complement bit pattern for negative immediates.
        result.state.regs[static_cast<std::size_t>(instr.rd)] = static_cast<std::uint64_t>(instr.imm);
        break;
      }
      case sim::isa::Op::ADD: {
        if (!require_reg(instr.rd, "rd") || !require_reg(instr.rs1, "rs1") ||
            !require_reg(instr.rs2, "rs2")) {
          break;
        }
        result.state.regs[static_cast<std::size_t>(instr.rd)] =
            result.state.regs[static_cast<std::size_t>(instr.rs1)] +
            result.state.regs[static_cast<std::size_t>(instr.rs2)];
        break;
      }
      case sim::isa::Op::XOR: {
        if (!require_reg(instr.rd, "rd") || !require_reg(instr.rs1, "rs1") ||
            !require_reg(instr.rs2, "rs2")) {
          break;
        }
        result.state.regs[static_cast<std::size_t>(instr.rd)] =
            result.state.regs[static_cast<std::size_t>(instr.rs1)] ^
            result.state.regs[static_cast<std::size_t>(instr.rs2)];
        break;
      }
      case sim::isa::Op::LD: {
        if (!require_reg(instr.rd, "rd") || !require_reg(instr.rs1, "rs1")) {
          break;
        }
        std::uint64_t addr = 0;
        const std::uint64_t base = result.state.regs[static_cast<std::size_t>(instr.rs1)];
        if (!ResolveMemAddress(base, instr.imm, result.state.mem.size(), &addr)) {
          std::ostringstream oss;
          oss << "invalid_memory op=LD pc=" << fetched.pc << " addr_expr=" << base << "+"
              << instr.imm << " mem_size=" << result.state.mem.size();
          exec_trap = Trap{TrapReason::INVALID_MEMORY, fetched.pc, oss.str()};
          has_trap = true;
          break;
        }
        result.state.regs[static_cast<std::size_t>(instr.rd)] = Load64LE(result.state.mem, addr);
        break;
      }
      case sim::isa::Op::ST: {
        if (!require_reg(instr.rs2, "rs2") || !require_reg(instr.rs1, "rs1")) {
          break;
        }
        std::uint64_t addr = 0;
        const std::uint64_t base = result.state.regs[static_cast<std::size_t>(instr.rs1)];
        if (!ResolveMemAddress(base, instr.imm, result.state.mem.size(), &addr)) {
          std::ostringstream oss;
          oss << "invalid_memory op=ST pc=" << fetched.pc << " addr_expr=" << base << "+"
              << instr.imm << " mem_size=" << result.state.mem.size();
          exec_trap = Trap{TrapReason::INVALID_MEMORY, fetched.pc, oss.str()};
          has_trap = true;
          break;
        }
        Store64LE(&result.state.mem, addr, result.state.regs[static_cast<std::size_t>(instr.rs2)]);
        break;
      }
      case sim::isa::Op::J: {
        if (!AddPcRelative(fetched.next_pc, instr.imm, &committed_pc)) {
          std::ostringstream oss;
          oss << "invalid_pc reason=pc_relative_overflow pc=" << fetched.pc
              << " base_va=" << program.base_va << " index=" << fetched.index
              << " code_size=" << program.code.size();
          exec_trap = Trap{TrapReason::INVALID_PC, fetched.pc, oss.str()};
          has_trap = true;
        }
        break;
      }
      case sim::isa::Op::BEQ: {
        if (!require_reg(instr.rs1, "rs1") || !require_reg(instr.rs2, "rs2")) {
          break;
        }
        if (result.state.regs[static_cast<std::size_t>(instr.rs1)] ==
            result.state.regs[static_cast<std::size_t>(instr.rs2)]) {
          if (!AddPcRelative(fetched.next_pc, instr.imm, &committed_pc)) {
            std::ostringstream oss;
            oss << "invalid_pc reason=pc_relative_overflow pc=" << fetched.pc
                << " base_va=" << program.base_va << " index=" << fetched.index
                << " code_size=" << program.code.size();
            exec_trap = Trap{TrapReason::INVALID_PC, fetched.pc, oss.str()};
            has_trap = true;
          }
        }
        break;
      }
      case sim::isa::Op::CALL: {
        result.state.regs[1] = fetched.next_pc;
        if (!AddPcRelative(fetched.next_pc, instr.imm, &committed_pc)) {
          std::ostringstream oss;
          oss << "invalid_pc reason=pc_relative_overflow pc=" << fetched.pc
              << " base_va=" << program.base_va << " index=" << fetched.index
              << " code_size=" << program.code.size();
          exec_trap = Trap{TrapReason::INVALID_PC, fetched.pc, oss.str()};
          has_trap = true;
        }
        break;
      }
      case sim::isa::Op::RET: {
        committed_pc = result.state.regs[1];
        break;
      }
      case sim::isa::Op::HALT: {
        // Keep HALT stop point at the HALT instruction PC, not next_pc.
        result.state.regs[0] = 0;
        result.trap = Trap{TrapReason::HALT, fetched.pc, "halt"};
        return result;
      }
      case sim::isa::Op::SYSCALL: {
        std::ostringstream oss;
        oss << "SYSCALL imm=" << instr.imm << " pc=" << fetched.pc;
        result.syscall_log.push_back(oss.str());
        break;
      }
      default: {
        std::ostringstream oss;
        oss << "unknown_opcode pc=" << fetched.pc;
        exec_trap = Trap{TrapReason::UNKNOWN_OPCODE, fetched.pc, oss.str()};
        has_trap = true;
        break;
      }
    }

    if (has_trap) {
      result.trap = std::move(exec_trap);
      break;
    }

    result.state.pc = committed_pc;
    // x0 is hardwired to 0 and must be re-enforced every committed instruction.
    result.state.regs[0] = 0;
    ++steps;
  }

  return result;
}

const char* TrapReasonToString(TrapReason reason) {
  switch (reason) {
    case TrapReason::HALT:
      return "HALT";
    case TrapReason::INVALID_PC:
      return "INVALID_PC";
    case TrapReason::INVALID_MEMORY:
      return "INVALID_MEMORY";
    case TrapReason::SYSCALL_FAIL:
      return "SYSCALL_FAIL";
    case TrapReason::UNKNOWN_OPCODE:
      return "UNKNOWN_OPCODE";
    case TrapReason::STEP_LIMIT:
      return "STEP_LIMIT";
  }
  return "UNKNOWN_OPCODE";
}

void PrintRunSummary(const ExecResult& result, std::ostream& os) {
  os << "FINAL_REASON=" << TrapReasonToString(result.trap.reason) << '\n';
  // Always report stop-point PC (HALT instruction PC or trap PC).
  os << "FINAL_PC=" << result.trap.pc << '\n';
  os << "SYSCALL_COUNT=" << result.syscall_log.size() << '\n';
  os << "AUDIT_COUNT=" << result.audit_log.size() << '\n';
  os << "CTX_TRACE_COUNT=" << result.context_trace.size() << '\n';
}

}  // namespace sim::core
