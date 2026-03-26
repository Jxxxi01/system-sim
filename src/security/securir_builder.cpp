#include "security/securir_builder.hpp"

#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "isa/instr.hpp"
#include "security/code_codec.hpp"

namespace sim::security {
namespace {

std::uint64_t ProgramEndVa(const sim::isa::AsmProgram& program) {
  const unsigned __int128 end_va = static_cast<unsigned __int128>(program.base_va) +
                                   static_cast<unsigned __int128>(program.code.size()) * sim::isa::kInstrBytes;
  const unsigned __int128 max_u64 = static_cast<unsigned __int128>(std::numeric_limits<std::uint64_t>::max());
  if (end_va > max_u64) {
    throw std::runtime_error("securir_builder_end_va_overflow");
  }
  return static_cast<std::uint64_t>(end_va);
}

std::string EscapeJsonString(const std::string& value) {
  std::ostringstream oss;
  for (char ch : value) {
    switch (ch) {
      case '"':
        oss << "\\\"";
        break;
      case '\\':
        oss << "\\\\";
        break;
      case '\b':
        oss << "\\b";
        break;
      case '\f':
        oss << "\\f";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        oss << ch;
        break;
    }
  }
  return oss.str();
}

const char* PageTypeToString(PvtPageType page_type) {
  switch (page_type) {
    case PvtPageType::CODE:
      return "CODE";
    case PvtPageType::DATA:
      return "DATA";
  }
  return "DATA";
}

std::vector<SecureIrWindowSpec> ResolveWindows(const sim::isa::AsmProgram& program,
                                               const SecureIrBuilderConfig& config) {
  if (!config.windows.empty()) {
    return config.windows;
  }

  SecureIrWindowSpec window;
  window.window_id = config.window_id;
  window.start_va = program.base_va;
  window.end_va = ProgramEndVa(program);
  window.key_id = config.key_id;
  window.type = "CODE";
  window.code_policy_id = config.code_policy_id;
  return std::vector<SecureIrWindowSpec>{window};
}

std::uint32_t ResolveEncryptionKeyId(const SecureIrBuilderConfig& config) {
  if (config.windows.empty()) {
    return config.key_id;
  }

  const std::uint32_t key_id = config.windows.front().key_id;
  for (const SecureIrWindowSpec& window : config.windows) {
    if (window.key_id != key_id) {
      throw std::runtime_error("securir_builder_inconsistent_window_key_ids");
    }
  }
  return key_id;
}

void AppendWindowArray(std::ostringstream* oss, const std::vector<SecureIrWindowSpec>& windows) {
  *oss << '[';
  for (std::size_t i = 0; i < windows.size(); ++i) {
    if (i != 0) {
      *oss << ',';
    }
    const SecureIrWindowSpec& window = windows[i];
    *oss << '{'
         << "\"window_id\":" << window.window_id << ','
         << "\"start_va\":" << window.start_va << ','
         << "\"end_va\":" << window.end_va << ','
         << "\"key_id\":" << window.key_id << ','
         << "\"type\":\"" << EscapeJsonString(window.type) << "\"," 
         << "\"code_policy_id\":" << window.code_policy_id
         << '}';
  }
  *oss << ']';
}

void AppendPageArray(std::ostringstream* oss, const std::vector<SecureIrPageSpec>& pages) {
  *oss << '[';
  for (std::size_t i = 0; i < pages.size(); ++i) {
    if (i != 0) {
      *oss << ',';
    }
    const SecureIrPageSpec& page = pages[i];
    *oss << '{'
         << "\"va\":" << page.va << ','
         << "\"page_type\":\"" << PageTypeToString(page.page_type) << "\""
         << '}';
  }
  *oss << ']';
}

void AppendNumberArray(std::ostringstream* oss, const std::vector<std::uint64_t>& values) {
  *oss << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      *oss << ',';
    }
    *oss << values[i];
  }
  *oss << ']';
}

std::string BuildMetadataJson(const sim::isa::AsmProgram& program, const SecureIrBuilderConfig& config,
                              const std::vector<SecureIrWindowSpec>& windows) {
  std::ostringstream oss;
  oss << '{'
      << "\"program_name\":\"" << EscapeJsonString(config.program_name) << "\","
      << "\"user_id\":" << config.user_id << ','
      << "\"signature\":\"" << EscapeJsonString(config.signature) << "\","
      << "\"base_va\":" << program.base_va << ','
      << "\"entry_offset\":" << config.entry_offset << ','
      << "\"windows\":";
  AppendWindowArray(&oss, windows);
  oss << ',' << "\"pages\":";
  AppendPageArray(&oss, config.pages);
  oss << ',' << "\"cfi_level\":" << config.cfi_level << ',' << "\"call_targets\":";
  AppendNumberArray(&oss, config.call_targets);
  oss << ',' << "\"jmp_targets\":";
  AppendNumberArray(&oss, config.jmp_targets);
  oss << '}';
  return oss.str();
}

}  // namespace

SecureIrPackage SecureIrBuilder::Build(const sim::isa::AsmProgram& program, const SecureIrBuilderConfig& config) {
  const std::uint32_t encryption_key_id = ResolveEncryptionKeyId(config);
  const std::vector<SecureIrWindowSpec> windows = ResolveWindows(program, config);
  const CipherProgram ciphertext = EncryptProgram(program, encryption_key_id);

  SecureIrPackage package;
  package.metadata = BuildMetadataJson(program, config, windows);
  package.code_memory = BuildCodeMemory(ciphertext);
  return package;
}

}  // namespace sim::security
