#include "isa/assembler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sim::isa {
namespace {

struct PendingInstrLine {
  std::size_t line_no = 0;
  std::string raw_line;
  std::uint64_t va = 0;
  std::string opcode;
  std::vector<std::string> operands;
};

std::string Trim(std::string_view input) {
  std::size_t begin = 0;
  while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
    ++begin;
  }
  std::size_t end = input.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
    --end;
  }
  return std::string(input.substr(begin, end - begin));
}

std::string StripComment(std::string_view line) {
  const std::size_t pos = line.find('#');
  if (pos == std::string_view::npos) {
    return std::string(line);
  }
  return std::string(line.substr(0, pos));
}

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      lines.emplace_back(text.substr(start));
      break;
    }
    lines.emplace_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

bool IsLabelStart(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool IsLabelBody(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

std::string TruncateLine(std::string_view line) {
  std::string out = Trim(line);
  constexpr std::size_t kMax = 120;
  if (out.size() > kMax) {
    out.resize(kMax);
    out += "...";
  }
  return out;
}

[[noreturn]] void ThrowAsmError(std::size_t line_no, std::string_view raw_line, std::string_view kind,
                                std::string_view detail) {
  std::ostringstream oss;
  oss << "asm:" << line_no << ": " << kind << ": " << detail << "; line=\""
      << TruncateLine(raw_line) << "\"";
  throw std::runtime_error(oss.str());
}

std::string ToUpper(std::string input) {
  std::transform(input.begin(), input.end(), input.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return input;
}

std::vector<std::string> SplitOperands(std::string_view input) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start < input.size()) {
    const std::size_t comma = input.find(',', start);
    const std::size_t end = (comma == std::string_view::npos) ? input.size() : comma;
    std::string token = Trim(input.substr(start, end - start));
    if (!token.empty()) {
      out.push_back(token);
    } else {
      out.push_back("");
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return out;
}

bool TryParseInt64(std::string_view token, std::int64_t* out) {
  std::string s = Trim(token);
  if (s.empty()) {
    return false;
  }
  std::size_t pos = 0;
  try {
    const long long value = std::stoll(s, &pos, 10);
    if (pos != s.size()) {
      return false;
    }
    *out = static_cast<std::int64_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

int ParseRegisterOrThrow(std::string_view token, std::size_t line_no, std::string_view raw_line) {
  std::string s = Trim(token);
  if (s.size() < 2 || s[0] != 'x') {
    ThrowAsmError(line_no, raw_line, "bad register", s);
  }
  for (std::size_t i = 1; i < s.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(s[i])) == 0) {
      ThrowAsmError(line_no, raw_line, "bad register", s);
    }
  }
  const int idx = std::stoi(s.substr(1));
  if (idx < 0 || idx > 31) {
    ThrowAsmError(line_no, raw_line, "bad register", s);
  }
  return idx;
}

std::pair<int, std::int64_t> ParseMemOperandOrThrow(std::string_view token, std::size_t line_no,
                                                     std::string_view raw_line) {
  std::string s = Trim(token);
  if (s.size() < 3 || s.front() != '[' || s.back() != ']') {
    ThrowAsmError(line_no, raw_line, "bad mem operand", s);
  }

  std::string inside = s.substr(1, s.size() - 2);
  inside.erase(std::remove_if(inside.begin(), inside.end(),
                              [](unsigned char ch) { return std::isspace(ch) != 0; }),
               inside.end());
  if (inside.empty()) {
    ThrowAsmError(line_no, raw_line, "bad mem operand", s);
  }

  std::size_t reg_end = 0;
  if (inside[0] != 'x') {
    ThrowAsmError(line_no, raw_line, "bad mem operand", s);
  }
  reg_end = 1;
  while (reg_end < inside.size() && std::isdigit(static_cast<unsigned char>(inside[reg_end])) != 0) {
    ++reg_end;
  }
  const std::string reg_token = inside.substr(0, reg_end);
  const int rs1 = ParseRegisterOrThrow(reg_token, line_no, raw_line);
  if (reg_end == inside.size()) {
    return {rs1, 0};
  }

  const char sign = inside[reg_end];
  if (sign != '+' && sign != '-') {
    ThrowAsmError(line_no, raw_line, "bad mem operand", s);
  }
  if (reg_end + 1 >= inside.size()) {
    ThrowAsmError(line_no, raw_line, "bad mem operand", s);
  }
  const std::string imm_token = inside.substr(reg_end + 1);
  if (imm_token.empty() || !std::all_of(imm_token.begin(), imm_token.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    ThrowAsmError(line_no, raw_line, "bad mem operand", s);
  }

  std::int64_t imm_abs = 0;
  if (!TryParseInt64(imm_token, &imm_abs)) {
    ThrowAsmError(line_no, raw_line, "bad mem operand", s);
  }
  if (sign == '-') {
    imm_abs = -imm_abs;
  }
  return {rs1, imm_abs};
}

Op ParseOpcodeOrThrow(std::string_view opcode_token, std::size_t line_no, std::string_view raw_line) {
  const std::string opcode = ToUpper(Trim(opcode_token));
  if (opcode == "NOP") return Op::NOP;
  if (opcode == "LI") return Op::LI;
  if (opcode == "ADD") return Op::ADD;
  if (opcode == "XOR") return Op::XOR;
  if (opcode == "LD") return Op::LD;
  if (opcode == "ST") return Op::ST;
  if (opcode == "J") return Op::J;
  if (opcode == "BEQ") return Op::BEQ;
  if (opcode == "CALL") return Op::CALL;
  if (opcode == "RET") return Op::RET;
  if (opcode == "HALT") return Op::HALT;
  if (opcode == "SYSCALL") return Op::SYSCALL;
  ThrowAsmError(line_no, raw_line, "unknown opcode", Trim(opcode_token));
}

std::int64_t ResolveTargetOrThrow(std::string_view token, std::uint64_t current_va,
                                  const std::unordered_map<std::string, std::uint64_t>& labels,
                                  std::size_t line_no, std::string_view raw_line) {
  std::int64_t numeric_imm = 0;
  if (TryParseInt64(token, &numeric_imm)) {
    return numeric_imm;
  }

  const std::string label = Trim(token);
  auto it = labels.find(label);
  if (it == labels.end()) {
    ThrowAsmError(line_no, raw_line, "undefined label", label);
  }
  const std::uint64_t target_va = it->second;
  const std::uint64_t next_va = current_va + kInstrBytes;
  const std::int64_t delta =
      static_cast<std::int64_t>(target_va) - static_cast<std::int64_t>(next_va);
  return delta;
}

void ExpectOperandCountOrThrow(const PendingInstrLine& pending, std::size_t expected) {
  if (pending.operands.size() != expected) {
    std::ostringstream oss;
    oss << "expected " << expected << " operand(s), got " << pending.operands.size();
    ThrowAsmError(pending.line_no, pending.raw_line, "operand count mismatch", oss.str());
  }
}

void ParseLabelPrefixOrThrow(std::string* line, std::size_t line_no, std::string_view raw_line,
                             std::unordered_map<std::string, std::uint64_t>* labels,
                             std::uint64_t current_va) {
  std::string& s = *line;
  if (s.empty()) {
    return;
  }

  const std::size_t colon_pos = s.find(':');
  if (colon_pos == std::string::npos) {
    return;
  }
  const std::string left = Trim(s.substr(0, colon_pos));
  if (left.empty()) {
    ThrowAsmError(line_no, raw_line, "bad label", "empty label");
  }
  if (!IsLabelStart(left[0])) {
    return;
  }
  if (!std::all_of(left.begin() + 1, left.end(), IsLabelBody)) {
    ThrowAsmError(line_no, raw_line, "bad label", left);
  }
  if (labels->find(left) != labels->end()) {
    ThrowAsmError(line_no, raw_line, "duplicate label", left);
  }
  labels->emplace(left, current_va);
  s = Trim(s.substr(colon_pos + 1));
}

}  // namespace

AsmProgram AssembleText(std::string_view text, std::uint64_t base_va) {
  if (base_va % kInstrBytes != 0) {
    ThrowAsmError(0, "", "unaligned base_va", std::to_string(base_va));
  }

  std::unordered_map<std::string, std::uint64_t> labels;
  std::vector<PendingInstrLine> pending;
  std::uint64_t current_va = base_va;
  const std::vector<std::string> lines = SplitLines(text);

  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::size_t line_no = i + 1;
    std::string raw = lines[i];
    if (!raw.empty() && raw.back() == '\r') {
      raw.pop_back();
    }

    std::string code = Trim(StripComment(raw));
    if (code.empty()) {
      continue;
    }

    ParseLabelPrefixOrThrow(&code, line_no, raw, &labels, current_va);
    if (code.empty()) {
      continue;
    }

    std::size_t split = 0;
    while (split < code.size() && std::isspace(static_cast<unsigned char>(code[split])) == 0) {
      ++split;
    }
    const std::string opcode = code.substr(0, split);
    const std::string operands_text = (split < code.size()) ? code.substr(split + 1) : "";
    std::vector<std::string> operands = SplitOperands(operands_text);

    PendingInstrLine item;
    item.line_no = line_no;
    item.raw_line = raw;
    item.va = current_va;
    item.opcode = opcode;
    item.operands = std::move(operands);
    pending.push_back(std::move(item));
    current_va += kInstrBytes;
  }

  AsmProgram program;
  program.base_va = base_va;
  program.code.reserve(pending.size());

  for (const PendingInstrLine& p : pending) {
    Instr instr;
    instr.op = ParseOpcodeOrThrow(p.opcode, p.line_no, p.raw_line);

    switch (instr.op) {
      case Op::NOP:
      case Op::RET:
      case Op::HALT: {
        ExpectOperandCountOrThrow(p, 0);
        break;
      }
      case Op::LI: {
        ExpectOperandCountOrThrow(p, 2);
        instr.rd = ParseRegisterOrThrow(p.operands[0], p.line_no, p.raw_line);
        if (!TryParseInt64(p.operands[1], &instr.imm)) {
          ThrowAsmError(p.line_no, p.raw_line, "bad immediate", Trim(p.operands[1]));
        }
        break;
      }
      case Op::ADD:
      case Op::XOR: {
        ExpectOperandCountOrThrow(p, 3);
        instr.rd = ParseRegisterOrThrow(p.operands[0], p.line_no, p.raw_line);
        instr.rs1 = ParseRegisterOrThrow(p.operands[1], p.line_no, p.raw_line);
        instr.rs2 = ParseRegisterOrThrow(p.operands[2], p.line_no, p.raw_line);
        break;
      }
      case Op::LD: {
        ExpectOperandCountOrThrow(p, 2);
        instr.rd = ParseRegisterOrThrow(p.operands[0], p.line_no, p.raw_line);
        const auto [rs1, imm] = ParseMemOperandOrThrow(p.operands[1], p.line_no, p.raw_line);
        instr.rs1 = rs1;
        instr.imm = imm;
        break;
      }
      case Op::ST: {
        ExpectOperandCountOrThrow(p, 2);
        instr.rs2 = ParseRegisterOrThrow(p.operands[0], p.line_no, p.raw_line);
        const auto [rs1, imm] = ParseMemOperandOrThrow(p.operands[1], p.line_no, p.raw_line);
        instr.rs1 = rs1;
        instr.imm = imm;
        break;
      }
      case Op::J:
      case Op::CALL: {
        ExpectOperandCountOrThrow(p, 1);
        instr.imm = ResolveTargetOrThrow(p.operands[0], p.va, labels, p.line_no, p.raw_line);
        break;
      }
      case Op::BEQ: {
        ExpectOperandCountOrThrow(p, 3);
        instr.rs1 = ParseRegisterOrThrow(p.operands[0], p.line_no, p.raw_line);
        instr.rs2 = ParseRegisterOrThrow(p.operands[1], p.line_no, p.raw_line);
        instr.imm = ResolveTargetOrThrow(p.operands[2], p.va, labels, p.line_no, p.raw_line);
        break;
      }
      case Op::SYSCALL: {
        ExpectOperandCountOrThrow(p, 1);
        if (!TryParseInt64(p.operands[0], &instr.imm)) {
          ThrowAsmError(p.line_no, p.raw_line, "bad immediate", Trim(p.operands[0]));
        }
        break;
      }
    }

    program.code.push_back(instr);
  }

  return program;
}

std::uint64_t InstrVa(const AsmProgram& program, std::size_t index) {
  if (index >= program.code.size()) {
    throw std::out_of_range("InstrVa index out of range");
  }
  return program.base_va + index * kInstrBytes;
}

std::vector<LocatedInstr> ToLocated(const AsmProgram& program) {
  std::vector<LocatedInstr> out;
  out.reserve(program.code.size());
  for (std::size_t i = 0; i < program.code.size(); ++i) {
    out.push_back(LocatedInstr{InstrVa(program, i), program.code[i]});
  }
  return out;
}

}  // namespace sim::isa
