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
  std::uint32_t owner_user_id = 0;
  std::uint32_t key_id = 0;
  sim::security::CipherInstrUnit cipher;
};

std::string MakeInvalidPcMsg(std::uint64_t pc, std::uint64_t base_va, std::size_t code_memory_size,
                             const char* reason) {
  std::ostringstream oss;
  oss << "invalid_pc reason=" << reason << " pc=" << pc << " base_va=" << base_va
      << " code_memory_size=" << code_memory_size;
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

bool FetchStage(std::uint64_t pc, const sim::security::EwcTable& ewc,
                sim::security::ContextHandle context_handle, const std::uint8_t* code_memory,
                std::size_t code_memory_size, std::uint64_t region_base_va, FetchPacket* packet,
                Trap* trap, std::string* deny_detail) {
  if (pc < region_base_va) {
    *trap = Trap{TrapReason::INVALID_PC, pc,
                 MakeInvalidPcMsg(pc, region_base_va, code_memory_size, "underflow")};
    return false;
  }

  const std::uint64_t delta = pc - region_base_va;
  if ((delta % sim::isa::kInstrBytes) != 0) {
    *trap = Trap{TrapReason::INVALID_PC, pc,
                 MakeInvalidPcMsg(pc, region_base_va, code_memory_size, "misaligned")};
    return false;
  }

  const unsigned __int128 instr_index = static_cast<unsigned __int128>(delta / sim::isa::kInstrBytes);
  const unsigned __int128 byte_offset =
      instr_index * static_cast<unsigned __int128>(sim::security::kCipherUnitBytes);
  const unsigned __int128 byte_end =
      byte_offset + static_cast<unsigned __int128>(sim::security::kCipherUnitBytes);
  const unsigned __int128 max_size = static_cast<unsigned __int128>(std::numeric_limits<std::size_t>::max());
  if (byte_end > max_size) {
    *trap = Trap{TrapReason::INVALID_PC, pc,
                 MakeInvalidPcMsg(pc, region_base_va, code_memory_size, "byte_offset_overflow")};
    return false;
  }

  if (byte_end > static_cast<unsigned __int128>(code_memory_size)) {
    *trap = Trap{TrapReason::INVALID_PC, pc,
                 MakeInvalidPcMsg(pc, region_base_va, code_memory_size, "oob")};
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
  packet->owner_user_id = query_result.owner_user_id;
  packet->key_id = query_result.key_id;
  const std::size_t byte_offset_size_t = static_cast<std::size_t>(byte_offset);
  packet->cipher = sim::security::DeserializeCipherUnit(code_memory + byte_offset_size_t,
                                                        sim::security::kCipherUnitBytes);

  return true;
}

bool DecodeStage(const FetchPacket& packet, sim::security::ContextHandle context_handle, sim::isa::Instr* instr,
                 Trap* trap, std::string* decrypt_detail) {
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

ExecResult ExecuteProgram(std::uint64_t entry_pc, const ExecuteOptions& options) {
  if (options.code_memory == nullptr) {
    throw std::runtime_error("code_memory_not_configured");
  }
  if (options.code_memory_size == 0) {
    throw std::runtime_error("code_memory_empty");
  }
  if ((options.code_memory_size % sim::security::kCipherUnitBytes) != 0) {
    throw std::runtime_error("code_memory_size_not_aligned");
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
    if (!FetchStage(result.state.pc, *options.ewc, options.context_handle, options.code_memory,
                    options.code_memory_size, options.region_base_va, &fetched, &fetch_trap,
                    &deny_detail)) {
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
              << " base_va=" << options.region_base_va << " fault_pc=" << fetched.pc;
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
                << " base_va=" << options.region_base_va << " fault_pc=" << fetched.pc;
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
              << " base_va=" << options.region_base_va << " fault_pc=" << fetched.pc;
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
