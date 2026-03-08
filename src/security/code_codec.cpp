#include "security/code_codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace sim::security {
namespace {

constexpr std::size_t kPayloadBytes = 24;
constexpr std::size_t kOpOffset = 0;
constexpr std::size_t kRdOffset = 1;
constexpr std::size_t kRs1Offset = 5;
constexpr std::size_t kRs2Offset = 9;
constexpr std::size_t kImmOffset = 13;
constexpr std::size_t kMagicOffset = 21;
constexpr std::array<std::uint8_t, 3> kMagic = {'S', 'I', 'M'};

std::uint64_t Rotl64(std::uint64_t value, unsigned shift) {
  return (value << shift) | (value >> (64 - shift));
}

std::uint64_t Mix64(std::uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  value ^= value >> 31;
  return value;
}

std::uint64_t PcSeed(std::uint64_t pc) {
  return Mix64(pc ^ 0x9e3779b97f4a7c15ULL);
}

std::uint64_t KeySeed(std::uint32_t key_id) {
  return Mix64(static_cast<std::uint64_t>(key_id) ^ 0xd1b54a32d192ed03ULL);
}

std::uint8_t ByteMask(std::uint32_t key_id, std::uint64_t pc, std::size_t index) {
  const std::uint64_t lane =
      Mix64(PcSeed(pc) + Rotl64(KeySeed(key_id), static_cast<unsigned>((index % 7) + 1)) +
            static_cast<std::uint64_t>(index) * 0x9e3779b185ebca87ULL);
  return static_cast<std::uint8_t>((lane >> ((index % 8) * 8)) & 0xffu);
}

std::uint32_t MakeKeyCheck(std::uint32_t key_id, std::uint64_t pc) {
  const std::uint64_t mixed = Mix64(static_cast<std::uint64_t>(key_id) ^ PcSeed(pc));
  return static_cast<std::uint32_t>(mixed ^ (mixed >> 32));
}

std::uint32_t MakeTag(const std::array<std::uint8_t, kPayloadBytes>& payload, std::uint32_t key_id,
                      std::uint64_t pc) {
  std::uint64_t acc = Mix64(PcSeed(pc) ^ Rotl64(KeySeed(key_id), 9));
  for (std::size_t i = 0; i < payload.size(); ++i) {
    const std::uint64_t folded = static_cast<std::uint64_t>(payload[i]) |
                                 (static_cast<std::uint64_t>(i) << 8) |
                                 (static_cast<std::uint64_t>(payload.size() - i) << 16);
    acc = Mix64(acc ^ folded ^ Rotl64(acc, static_cast<unsigned>((i % 13) + 1)));
  }
  return static_cast<std::uint32_t>(acc ^ (acc >> 32));
}

void WriteU32LE(std::array<std::uint8_t, kPayloadBytes>* out, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    (*out)[offset + i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu);
  }
}

void WriteU64LE(std::array<std::uint8_t, kPayloadBytes>* out, std::size_t offset, std::uint64_t value) {
  for (std::size_t i = 0; i < 8; ++i) {
    (*out)[offset + i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu);
  }
}

std::uint32_t ReadU32LE(const std::array<std::uint8_t, kPayloadBytes>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * 8);
  }
  return value;
}

std::uint64_t ReadU64LE(const std::array<std::uint8_t, kPayloadBytes>& bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8);
  }
  return value;
}

std::array<std::uint8_t, kPayloadBytes> SerializeInstr(const sim::isa::Instr& instr) {
  std::array<std::uint8_t, kPayloadBytes> bytes{};
  bytes[kOpOffset] = static_cast<std::uint8_t>(instr.op);
  WriteU32LE(&bytes, kRdOffset, static_cast<std::uint32_t>(static_cast<std::int32_t>(instr.rd)));
  WriteU32LE(&bytes, kRs1Offset, static_cast<std::uint32_t>(static_cast<std::int32_t>(instr.rs1)));
  WriteU32LE(&bytes, kRs2Offset, static_cast<std::uint32_t>(static_cast<std::int32_t>(instr.rs2)));
  WriteU64LE(&bytes, kImmOffset, static_cast<std::uint64_t>(instr.imm));
  bytes[kMagicOffset + 0] = kMagic[0];
  bytes[kMagicOffset + 1] = kMagic[1];
  bytes[kMagicOffset + 2] = kMagic[2];
  return bytes;
}

bool DeserializeInstr(const std::array<std::uint8_t, kPayloadBytes>& bytes, sim::isa::Instr* instr) {
  if (bytes[kMagicOffset + 0] != kMagic[0] || bytes[kMagicOffset + 1] != kMagic[1] ||
      bytes[kMagicOffset + 2] != kMagic[2]) {
    return false;
  }

  if (bytes[kOpOffset] > static_cast<std::uint8_t>(sim::isa::Op::SYSCALL)) {
    return false;
  }

  instr->op = static_cast<sim::isa::Op>(bytes[kOpOffset]);
  instr->rd = static_cast<std::int32_t>(ReadU32LE(bytes, kRdOffset));
  instr->rs1 = static_cast<std::int32_t>(ReadU32LE(bytes, kRs1Offset));
  instr->rs2 = static_cast<std::int32_t>(ReadU32LE(bytes, kRs2Offset));
  instr->imm = static_cast<std::int64_t>(ReadU64LE(bytes, kImmOffset));
  return true;
}

std::uint64_t InstrPc(const sim::isa::AsmProgram& program, std::size_t index) {
  const unsigned __int128 pc128 = static_cast<unsigned __int128>(program.base_va) +
                                  static_cast<unsigned __int128>(index) * sim::isa::kInstrBytes;
  const unsigned __int128 max_u64 = static_cast<unsigned __int128>(std::numeric_limits<std::uint64_t>::max());
  if (pc128 > max_u64) {
    throw std::runtime_error("ciphertext_pc_overflow");
  }
  return static_cast<std::uint64_t>(pc128);
}

}  // namespace

CipherProgram EncryptProgram(const sim::isa::AsmProgram& program, std::uint32_t key_id) {
  CipherProgram output;
  output.reserve(program.code.size());

  for (std::size_t i = 0; i < program.code.size(); ++i) {
    const std::uint64_t pc = InstrPc(program, i);
    const std::array<std::uint8_t, kPayloadBytes> plain = SerializeInstr(program.code[i]);

    CipherInstrUnit unit;
    unit.payload = plain;
    for (std::size_t j = 0; j < unit.payload.size(); ++j) {
      unit.payload[j] ^= ByteMask(key_id, pc, j);
    }
    unit.key_check = MakeKeyCheck(key_id, pc);
    unit.tag = MakeTag(unit.payload, key_id, pc);
    output.push_back(unit);
  }

  return output;
}

DecryptResult DecryptInstr(const CipherInstrUnit& unit, std::uint32_t key_id, std::uint64_t pc) {
  DecryptResult result;

  if (unit.key_check != MakeKeyCheck(key_id, pc)) {
    result.detail = "key_check_mismatch";
    return result;
  }

  if (unit.tag != MakeTag(unit.payload, key_id, pc)) {
    result.detail = "tag_mismatch";
    return result;
  }

  std::array<std::uint8_t, kPayloadBytes> plain = unit.payload;
  for (std::size_t i = 0; i < plain.size(); ++i) {
    plain[i] ^= ByteMask(key_id, pc, i);
  }

  if (!DeserializeInstr(plain, &result.instr)) {
    result.detail = "decode_mismatch";
    return result;
  }

  result.ok = true;
  return result;
}

}  // namespace sim::security
