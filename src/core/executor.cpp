#include "core/executor.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "isa/instr.hpp"
#include "isa/opcode.hpp"
#include "security/code_codec.hpp"

namespace sim::core {
namespace {

struct FetchPacket {
  std::uint64_t pc = 0;
  std::uint64_t next_pc = 0;
  std::size_t index = 0;
  std::uint32_t owner_user_id = 0;
  std::uint32_t key_id = 0;
  bool has_cipher = false;
  sim::isa::Instr instr;
  sim::security::CipherInstrUnit cipher;
};

std::string MakeInvalidPcMsg(std::uint64_t pc, std::uint64_t base_va, std::string_view index,
                             std::size_t code_size, const char* reason) {
  std::ostringstream oss;
  oss << "invalid_pc reason=" << reason << " pc=" << pc << " base_va=" << base_va
      << " index=" << index << " code_size=" << code_size;
  return oss.str();
}

std::string MakeEwcDenyMsg(std::uint64_t pc, sim::security::ContextHandle context_handle,
                           const sim::security::EwcQueryResult& query_result) {
  std::ostringstream oss;
  oss << "ewc_illegal_pc"
      << " pc=" << pc << " context_handle=" << context_handle << " window_id=";
  if (query_result.matched_window) {
    oss << query_result.window_id;
  } else {
    oss << "none";
  }
  return oss.str();
}

std::string MakeEwcDenyDetail(const sim::security::EwcQueryResult& query_result) {
  std::ostringstream oss;
  oss << "window_id=";
  if (query_result.matched_window) {
    oss << query_result.window_id;
  } else {
    oss << "none";
  }
  return oss.str();
}

std::string MakeDecryptFailMsg(std::uint64_t pc, sim::security::ContextHandle context_handle,
                               std::uint32_t key_id, std::string_view detail) {
  std::ostringstream oss;
  oss << "decrypt_decode_fail"
      << " pc=" << pc << " context_handle=" << context_handle << " key_id=" << key_id
      << " detail=" << detail;
  return oss.str();
}

std::string MakeDecryptFailDetail(std::uint32_t key_id, std::string_view detail) {
  std::ostringstream oss;
  oss << "key_id=" << key_id << " reason=" << detail;
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

bool FetchStage(const sim::isa::AsmProgram& program, std::uint64_t pc,
                const sim::security::EwcTable& ewc, sim::security::ContextHandle context_handle,
                const sim::security::CipherProgram* ciphertext, FetchPacket* packet, Trap* trap,
                std::string* deny_detail) {
  const std::uint64_t base_va = program.base_va;
  const std::size_t code_size = program.code.size();

  if (pc < base_va) {
    *trap = Trap{TrapReason::INVALID_PC, pc,
                 MakeInvalidPcMsg(pc, base_va, "-1", code_size, "underflow")};
    return false;
  }

  const std::uint64_t delta = pc - base_va;
  const std::uint64_t index = delta / sim::isa::kInstrBytes;
  // Alignment is defined relative to program image base, not absolute address.
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

  const sim::security::EwcQueryResult query_result = ewc.Query(pc, context_handle);
  if (!query_result.allow) {
    *trap = Trap{TrapReason::EWC_ILLEGAL_PC, pc, MakeEwcDenyMsg(pc, context_handle, query_result)};
    *deny_detail = MakeEwcDenyDetail(query_result);
    return false;
  }

  packet->pc = pc;
  packet->next_pc = pc + sim::isa::kInstrBytes;
  packet->index = static_cast<std::size_t>(index);
  packet->owner_user_id = query_result.owner_user_id;
  packet->key_id = query_result.key_id;
  packet->has_cipher = (ciphertext != nullptr);
  if (packet->has_cipher) {
    packet->cipher = (*ciphertext)[packet->index];
  } else {
    packet->instr = program.code[packet->index];
  }

  return true;
}

bool DecodeStage(const FetchPacket& packet, sim::security::ContextHandle context_handle, sim::isa::Instr* instr,
                 Trap* trap, std::string* decrypt_detail) {
  if (!packet.has_cipher) {
    *instr = packet.instr;
    return true;
  }

  const sim::security::DecryptResult decrypt_result =
      sim::security::DecryptInstr(packet.cipher, packet.key_id, packet.pc);
  if (!decrypt_result.ok) {
    *trap = Trap{TrapReason::DECRYPT_DECODE_FAIL, packet.pc,
                 MakeDecryptFailMsg(packet.pc, context_handle, packet.key_id, decrypt_result.detail)};
    *decrypt_detail = MakeDecryptFailDetail(packet.key_id, decrypt_result.detail);
    return false;
  }

  *instr = decrypt_result.instr;
  return true;
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

void LogAudit(sim::security::AuditCollector* audit, std::string type, std::uint32_t user_id,
              sim::security::ContextHandle context_handle, std::uint64_t pc, std::string detail) {
  if (audit != nullptr) {
    audit->LogEvent(std::move(type), user_id, context_handle, pc, std::move(detail));
  }
}

ExecResult ExecuteProgram(const sim::isa::AsmProgram& program, std::uint64_t entry_pc,
                          const ExecuteOptions& options) {
  if (options.ciphertext != nullptr && options.ciphertext->size() != program.code.size()) {
    std::ostringstream oss;
    oss << "ciphertext_size_mismatch code_size=" << program.code.size()
        << " ciphertext_size=" << options.ciphertext->size();
    throw std::runtime_error(oss.str());
  }

  ExecResult result;
  result.state.pc = entry_pc;
  result.state.regs.fill(0);
  result.state.mem.assign(options.mem_size, 0);
  {
    std::ostringstream oss;
    oss << "context_handle=" << options.context_handle;
    result.context_trace.push_back(oss.str());
  }

  if (options.ewc == nullptr) {
    std::ostringstream oss;
    oss << "ewc_not_configured"
        << " pc=" << entry_pc << " context_handle=" << options.context_handle;
    result.trap = Trap{TrapReason::EWC_ILLEGAL_PC, entry_pc, oss.str()};
    return result;
  }

  std::size_t steps = 0;
  while (true) {
    if (steps >= options.max_steps) {
      std::ostringstream oss;
      oss << "step_limit_exceeded pc=" << result.state.pc << " steps=" << steps
          << " max_steps=" << options.max_steps;
      result.trap = Trap{TrapReason::STEP_LIMIT, result.state.pc, oss.str()};
      break;
    }

    FetchPacket fetched;
    Trap fetch_trap;
    std::string deny_detail;
    if (!FetchStage(program, result.state.pc, *options.ewc, options.context_handle, options.ciphertext,
                    &fetched, &fetch_trap, &deny_detail)) {
      if (!deny_detail.empty()) {
        LogAudit(options.audit, "EWC_ILLEGAL_PC", 0, options.context_handle, fetch_trap.pc, std::move(deny_detail));
      }
      result.trap = std::move(fetch_trap);
      break;
    }

    sim::isa::Instr instr;
    Trap decode_trap;
    std::string decrypt_detail;
    if (!DecodeStage(fetched, options.context_handle, &instr, &decode_trap, &decrypt_detail)) {
      if (!decrypt_detail.empty()) {
        LogAudit(options.audit, "DECRYPT_DECODE_FAIL", fetched.owner_user_id, options.context_handle,
                 decode_trap.pc, std::move(decrypt_detail));
      }
      result.trap = std::move(decode_trap);
      break;
    }
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
    result.state.regs[0] = 0;
    ++steps;
  }

  return result;
}

ExecResult ExecuteProgram(const sim::isa::AsmProgram& program, std::uint64_t entry_pc, std::size_t mem_size,
                          std::size_t max_steps) {
  sim::security::EwcTable temp_ewc;
  if (!program.code.empty()) {
    sim::security::ExecWindow allow_all;
    allow_all.window_id = 1;
    allow_all.start_va = program.base_va;
    allow_all.owner_user_id = 0;
    allow_all.key_id = 0;
    allow_all.type = sim::security::ExecWindowType::CODE;
    allow_all.code_policy_id = 0;

    const unsigned __int128 span =
        static_cast<unsigned __int128>(program.code.size()) * sim::isa::kInstrBytes;
    const unsigned __int128 end128 = static_cast<unsigned __int128>(program.base_va) + span;
    const unsigned __int128 max_u64 = static_cast<unsigned __int128>(std::numeric_limits<std::uint64_t>::max());
    if (end128 > max_u64) {
      ExecResult error;
      error.state.pc = entry_pc;
      error.state.regs.fill(0);
      error.state.mem.assign(mem_size, 0);
      error.trap = Trap{TrapReason::INVALID_PC, entry_pc, "invalid_pc reason=allow_all_window_overflow"};
      return error;
    }
    // end_va is exclusive: [base_va, base_va + code_size * kInstrBytes)
    allow_all.end_va = static_cast<std::uint64_t>(end128);
    temp_ewc.SetWindows(0, std::vector<sim::security::ExecWindow>{allow_all});
  }

  ExecuteOptions options;
  options.mem_size = mem_size;
  options.max_steps = max_steps;
  options.context_handle = 0;
  options.ewc = &temp_ewc;
  return ExecuteProgram(program, entry_pc, options);
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
    case TrapReason::EWC_ILLEGAL_PC:
      return "EWC_ILLEGAL_PC";
    case TrapReason::DECRYPT_DECODE_FAIL:
      return "DECRYPT_DECODE_FAIL";
  }
  return "UNKNOWN_OPCODE";
}

void PrintRunSummary(const ExecResult& result, std::ostream& os) {
  os << "FINAL_REASON=" << TrapReasonToString(result.trap.reason) << '\n';
  os << "FINAL_PC=" << result.trap.pc << '\n';
  os << "SYSCALL_COUNT=" << result.syscall_log.size() << '\n';
  os << "CTX_TRACE_COUNT=" << result.context_trace.size() << '\n';
}

}  // namespace sim::core
