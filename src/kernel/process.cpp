#include "kernel/process.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace sim::kernel {
namespace {

struct ProcessPageSpec {
  std::uint64_t va = 0;
  sim::security::PvtPageType page_type = sim::security::PvtPageType::DATA;
};

struct ProcessLoadSpec {
  ProcessContext process;
  std::vector<ProcessPageSpec> pages;
};

struct JsonValue {
  enum class Kind {
    OBJECT,
    ARRAY,
    STRING,
    NUMBER
  };

  using Object = std::map<std::string, JsonValue>;
  using Array = std::vector<JsonValue>;

  Kind kind = Kind::OBJECT;
  Object object_value;
  Array array_value;
  std::string string_value;
  std::uint64_t number_value = 0;
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& text) : text_(text) {}

  JsonValue Parse() {
    JsonValue value = ParseValue();
    SkipWhitespace();
    if (!IsEnd()) {
      Fail("unexpected trailing characters");
    }
    return value;
  }

 private:
  JsonValue ParseValue() {
    SkipWhitespace();
    if (IsEnd()) {
      Fail("unexpected end of input");
    }

    switch (Peek()) {
      case '{':
        return ParseObject();
      case '[':
        return ParseArray();
      case '"':
        return MakeString(ParseString());
      default:
        if (std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
          return MakeNumber(ParseNumber());
        }
        Fail("unsupported JSON token");
    }
  }

  JsonValue ParseObject() {
    Expect('{');
    JsonValue value;
    value.kind = JsonValue::Kind::OBJECT;

    SkipWhitespace();
    if (Consume('}')) {
      return value;
    }

    while (true) {
      SkipWhitespace();
      if (Peek() != '"') {
        Fail("expected object key");
      }
      std::string key = ParseString();
      SkipWhitespace();
      Expect(':');
      JsonValue field_value = ParseValue();
      value.object_value.emplace(std::move(key), std::move(field_value));

      SkipWhitespace();
      if (Consume('}')) {
        return value;
      }
      Expect(',');
    }
  }

  JsonValue ParseArray() {
    Expect('[');
    JsonValue value;
    value.kind = JsonValue::Kind::ARRAY;

    SkipWhitespace();
    if (Consume(']')) {
      return value;
    }

    while (true) {
      value.array_value.push_back(ParseValue());
      SkipWhitespace();
      if (Consume(']')) {
        return value;
      }
      Expect(',');
    }
  }

  std::string ParseString() {
    Expect('"');
    std::string value;

    while (!IsEnd()) {
      const char ch = text_[pos_++];
      if (ch == '"') {
        return value;
      }
      if (ch == '\\') {
        if (IsEnd()) {
          Fail("unterminated escape");
        }
        const char escaped = text_[pos_++];
        switch (escaped) {
          case '"':
          case '\\':
          case '/':
            value.push_back(escaped);
            break;
          case 'b':
            value.push_back('\b');
            break;
          case 'f':
            value.push_back('\f');
            break;
          case 'n':
            value.push_back('\n');
            break;
          case 'r':
            value.push_back('\r');
            break;
          case 't':
            value.push_back('\t');
            break;
          default:
            Fail("unsupported escape");
        }
        continue;
      }
      value.push_back(ch);
    }

    Fail("unterminated string");
  }

  std::uint64_t ParseNumber() {
    const std::size_t start = pos_;
    while (!IsEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
      ++pos_;
    }
    const std::string token = text_.substr(start, pos_ - start);
    if (token.empty()) {
      Fail("expected number");
    }
    try {
      return std::stoull(token);
    } catch (const std::exception&) {
      Fail("invalid number");
    }
  }

  void SkipWhitespace() {
    while (!IsEnd() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
      ++pos_;
    }
  }

  bool Consume(char ch) {
    if (!IsEnd() && text_[pos_] == ch) {
      ++pos_;
      return true;
    }
    return false;
  }

  void Expect(char ch) {
    SkipWhitespace();
    if (IsEnd() || text_[pos_] != ch) {
      std::ostringstream oss;
      oss << "expected '" << ch << "'";
      Fail(oss.str());
    }
    ++pos_;
  }

  char Peek() const { return text_[pos_]; }

  bool IsEnd() const { return pos_ >= text_.size(); }

  [[noreturn]] void Fail(const std::string& message) const {
    std::ostringstream oss;
    oss << "kernel_process_json_parse_error pos=" << pos_ << " " << message;
    throw std::runtime_error(oss.str());
  }

  static JsonValue MakeString(std::string value) {
    JsonValue json;
    json.kind = JsonValue::Kind::STRING;
    json.string_value = std::move(value);
    return json;
  }

  static JsonValue MakeNumber(std::uint64_t value) {
    JsonValue json;
    json.kind = JsonValue::Kind::NUMBER;
    json.number_value = value;
    return json;
  }

  const std::string& text_;
  std::size_t pos_ = 0;
};

const JsonValue::Object& RequireObject(const JsonValue& value, const char* name) {
  if (value.kind != JsonValue::Kind::OBJECT) {
    std::ostringstream oss;
    oss << "kernel_process_invalid_field field=" << name << " expected=object";
    throw std::runtime_error(oss.str());
  }
  return value.object_value;
}

const JsonValue& RequireField(const JsonValue::Object& object, const char* name) {
  const auto it = object.find(name);
  if (it == object.end()) {
    std::ostringstream oss;
    oss << "kernel_process_missing_field field=" << name;
    throw std::runtime_error(oss.str());
  }
  return it->second;
}

std::uint64_t RequireNumber(const JsonValue& value, const char* name) {
  if (value.kind != JsonValue::Kind::NUMBER) {
    std::ostringstream oss;
    oss << "kernel_process_invalid_field field=" << name << " expected=number";
    throw std::runtime_error(oss.str());
  }
  return value.number_value;
}

std::uint32_t RequireU32Field(const JsonValue::Object& object, const char* name) {
  const std::uint64_t value = RequireNumber(RequireField(object, name), name);
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    std::ostringstream oss;
    oss << "kernel_process_invalid_field field=" << name << " expected=u32";
    throw std::runtime_error(oss.str());
  }
  return static_cast<std::uint32_t>(value);
}

std::uint64_t RequireU64Field(const JsonValue::Object& object, const char* name) {
  return RequireNumber(RequireField(object, name), name);
}

const JsonValue::Array& RequireArray(const JsonValue& value, const char* name) {
  if (value.kind != JsonValue::Kind::ARRAY) {
    std::ostringstream oss;
    oss << "kernel_process_invalid_field field=" << name << " expected=array";
    throw std::runtime_error(oss.str());
  }
  return value.array_value;
}

std::string RequireStringField(const JsonValue::Object& object, const char* name) {
  const JsonValue& value = RequireField(object, name);
  if (value.kind != JsonValue::Kind::STRING) {
    std::ostringstream oss;
    oss << "kernel_process_invalid_field field=" << name << " expected=string";
    throw std::runtime_error(oss.str());
  }
  return value.string_value;
}

sim::security::PvtPageType ParsePageType(const std::string& value) {
  if (value == "CODE") {
    return sim::security::PvtPageType::CODE;
  }
  if (value == "DATA") {
    return sim::security::PvtPageType::DATA;
  }

  std::ostringstream oss;
  oss << "kernel_process_invalid_page_type type=" << value;
  throw std::runtime_error(oss.str());
}

ProcessLoadSpec ParseProcessLoadSpec(const std::string& secureir_json) {
  const JsonValue root = JsonParser(secureir_json).Parse();
  const JsonValue::Object& object = RequireObject(root, "root");

  ProcessLoadSpec spec;
  spec.process.user_id = RequireU32Field(object, "user_id");
  spec.process.base_va = RequireU64Field(object, "base_va");

  const JsonValue::Array& pages = RequireArray(RequireField(object, "pages"), "pages");
  spec.pages.reserve(pages.size());
  for (const JsonValue& page_value : pages) {
    const JsonValue::Object& page_object = RequireObject(page_value, "pages[]");
    ProcessPageSpec page;
    page.va = RequireU64Field(page_object, "va");
    page.page_type = ParsePageType(RequireStringField(page_object, "page_type"));
    spec.pages.push_back(page);
  }

  return spec;
}

std::string MakeContextSwitchDetail(const ProcessContext& process) {
  std::ostringstream oss;
  oss << "base_va=" << process.base_va;
  return oss.str();
}

}  // namespace

KernelProcessTable::KernelProcessTable(sim::security::Gateway& gateway, sim::security::SecurityHardware& hardware,
                                       sim::security::AuditCollector& audit)
    : gateway_(gateway), hardware_(hardware), audit_(audit) {}

sim::security::ContextHandle KernelProcessTable::LoadProcess(const std::string& secureir_json,
                                                             std::vector<std::uint8_t> code_memory) {
  ProcessLoadSpec load_spec = ParseProcessLoadSpec(secureir_json);
  ProcessContext& process = load_spec.process;
  bool gateway_loaded = false;
  std::vector<std::uint64_t> registered_page_ids;

  try {
    process.context_handle = gateway_.Load(secureir_json);
    gateway_loaded = true;

    hardware_.StoreCodeRegion(process.context_handle, process.base_va, std::move(code_memory));

    for (const ProcessPageSpec& page : load_spec.pages) {
      const sim::security::PvtRegisterResult register_result =
          hardware_.GetPvtTable().RegisterPage(process.context_handle, page.va, page.page_type);
      if (!register_result.ok) {
        throw std::runtime_error(register_result.error);
      }
      registered_page_ids.push_back(register_result.pa_page_id);
    }
    process.pvt_page_ids = registered_page_ids;

    const auto inserted = processes_.emplace(process.context_handle, process);
    if (!inserted.second) {
      std::ostringstream oss;
      oss << "kernel_process_duplicate_handle context_handle=" << process.context_handle;
      throw std::runtime_error(oss.str());
    }

    return process.context_handle;
  } catch (...) {
    for (std::uint64_t pa_page_id : registered_page_ids) {
      hardware_.GetPvtTable().RemovePage(pa_page_id);
    }
    if (process.context_handle != 0) {
      hardware_.RemoveCodeRegion(process.context_handle);
      processes_.erase(process.context_handle);
      if (active_handle_.has_value() && *active_handle_ == process.context_handle) {
        active_handle_.reset();
      }
      if (gateway_loaded) {
        gateway_.Release(process.context_handle);
      }
    }
    throw;
  }
}

void KernelProcessTable::ReleaseProcess(sim::security::ContextHandle handle) {
  const bool was_active = active_handle_.has_value() && *active_handle_ == handle;
  const ProcessContext* process = GetProcess(handle);
  if (process != nullptr) {
    for (std::uint64_t pa_page_id : process->pvt_page_ids) {
      hardware_.GetPvtTable().RemovePage(pa_page_id);
    }
  }
  gateway_.Release(handle);
  hardware_.RemoveCodeRegion(handle);
  processes_.erase(handle);
  if (was_active) {
    active_handle_.reset();
    hardware_.ClearActiveHandle();
  }
}

void KernelProcessTable::ContextSwitch(sim::security::ContextHandle handle) {
  const ProcessContext* process = GetProcess(handle);
  if (process == nullptr) {
    std::ostringstream oss;
    oss << "kernel_process_invalid_handle context_handle=" << handle;
    throw std::runtime_error(oss.str());
  }

  hardware_.SetActiveHandle(handle);
  active_handle_ = handle;
  audit_.LogEvent("CTX_SWITCH", process->user_id, process->context_handle, process->base_va,
                  MakeContextSwitchDetail(*process));
}

const ProcessContext* KernelProcessTable::GetActiveProcess() const {
  if (!active_handle_.has_value()) {
    return nullptr;
  }
  return GetProcess(*active_handle_);
}

const ProcessContext* KernelProcessTable::GetProcess(sim::security::ContextHandle handle) const {
  const auto it = processes_.find(handle);
  if (it == processes_.end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace sim::kernel
