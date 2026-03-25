#include "security/gateway.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "security/ewc.hpp"

namespace sim::security {
namespace {

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
    oss << "gateway_json_parse_error pos=" << pos_ << " " << message;
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

struct SecureIrWindow {
  std::uint32_t window_id = 0;
  std::uint64_t start_va = 0;
  std::uint64_t end_va = 0;
  std::uint32_t key_id = 0;
  std::string type;
  std::uint32_t code_policy_id = 0;
};

struct SecureIr {
  std::string program_name;
  std::uint32_t user_id = 0;
  std::string signature;
  std::uint64_t base_va = 0;
  std::vector<SecureIrWindow> windows;
  std::uint32_t cfi_level = 0;
  std::vector<std::uint64_t> call_targets;
  std::vector<std::uint64_t> jmp_targets;
};

const JsonValue::Object& RequireObject(const JsonValue& value, const char* name) {
  if (value.kind != JsonValue::Kind::OBJECT) {
    std::ostringstream oss;
    oss << "gateway_invalid_field field=" << name << " expected=object";
    throw std::runtime_error(oss.str());
  }
  return value.object_value;
}

const JsonValue::Array& RequireArray(const JsonValue& value, const char* name) {
  if (value.kind != JsonValue::Kind::ARRAY) {
    std::ostringstream oss;
    oss << "gateway_invalid_field field=" << name << " expected=array";
    throw std::runtime_error(oss.str());
  }
  return value.array_value;
}

const std::string& RequireString(const JsonValue& value, const char* name) {
  if (value.kind != JsonValue::Kind::STRING) {
    std::ostringstream oss;
    oss << "gateway_invalid_field field=" << name << " expected=string";
    throw std::runtime_error(oss.str());
  }
  return value.string_value;
}

std::uint64_t RequireNumber(const JsonValue& value, const char* name) {
  if (value.kind != JsonValue::Kind::NUMBER) {
    std::ostringstream oss;
    oss << "gateway_invalid_field field=" << name << " expected=number";
    throw std::runtime_error(oss.str());
  }
  return value.number_value;
}

const JsonValue& RequireField(const JsonValue::Object& object, const char* name) {
  auto it = object.find(name);
  if (it == object.end()) {
    std::ostringstream oss;
    oss << "gateway_missing_field field=" << name;
    throw std::runtime_error(oss.str());
  }
  return it->second;
}

std::uint32_t RequireU32Field(const JsonValue::Object& object, const char* name) {
  const std::uint64_t value = RequireNumber(RequireField(object, name), name);
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    std::ostringstream oss;
    oss << "gateway_invalid_field field=" << name << " expected=u32";
    throw std::runtime_error(oss.str());
  }
  return static_cast<std::uint32_t>(value);
}

std::uint64_t RequireU64Field(const JsonValue::Object& object, const char* name) {
  return RequireNumber(RequireField(object, name), name);
}

std::string RequireStringField(const JsonValue::Object& object, const char* name) {
  return RequireString(RequireField(object, name), name);
}

std::vector<std::uint64_t> ParseNumberArray(const JsonValue::Array& array, const char* name) {
  std::vector<std::uint64_t> values;
  values.reserve(array.size());
  for (const JsonValue& element : array) {
    values.push_back(RequireNumber(element, name));
  }
  return values;
}

SecureIr ParseSecureIr(const std::string& json) {
  const JsonValue root = JsonParser(json).Parse();
  const JsonValue::Object& object = RequireObject(root, "root");

  SecureIr secure_ir;
  secure_ir.program_name = RequireStringField(object, "program_name");
  secure_ir.user_id = RequireU32Field(object, "user_id");
  secure_ir.signature = RequireStringField(object, "signature");
  secure_ir.base_va = RequireU64Field(object, "base_va");
  secure_ir.cfi_level = RequireU32Field(object, "cfi_level");

  const JsonValue::Array& windows = RequireArray(RequireField(object, "windows"), "windows");
  secure_ir.windows.reserve(windows.size());
  for (const JsonValue& window_value : windows) {
    const JsonValue::Object& window_object = RequireObject(window_value, "windows[]");
    SecureIrWindow window;
    window.window_id = RequireU32Field(window_object, "window_id");
    window.start_va = RequireU64Field(window_object, "start_va");
    window.end_va = RequireU64Field(window_object, "end_va");
    window.key_id = RequireU32Field(window_object, "key_id");
    window.type = RequireStringField(window_object, "type");
    window.code_policy_id = RequireU32Field(window_object, "code_policy_id");
    secure_ir.windows.push_back(std::move(window));
  }

  static_cast<void>(RequireArray(RequireField(object, "pages"), "pages"));
  secure_ir.call_targets =
      ParseNumberArray(RequireArray(RequireField(object, "call_targets"), "call_targets"), "call_targets");
  secure_ir.jmp_targets =
      ParseNumberArray(RequireArray(RequireField(object, "jmp_targets"), "jmp_targets"), "jmp_targets");

  return secure_ir;
}

std::string MakeLoadOkDetail(const SecureIr& secure_ir) {
  std::ostringstream oss;
  oss << "program_name=" << secure_ir.program_name << " window_count=" << secure_ir.windows.size();
  return oss.str();
}

std::string MakeLoadFailDetail(const std::string& error) {
  std::ostringstream oss;
  oss << "error=" << error;
  return oss.str();
}

std::string MakeReleaseDetail(bool released) {
  std::ostringstream oss;
  oss << "status=" << (released ? "cleared" : "missing");
  return oss.str();
}

}  // namespace

Gateway::Gateway(SecurityHardware& hardware) : hardware_(hardware) {}

ContextHandle Gateway::Load(const std::string& json) {
  const ContextHandle handle = next_handle_++;

  try {
    if (handle_to_user_.size() >= kMaxContextHandles) {
      std::ostringstream oss;
      oss << "gateway_capacity_exceeded active_handles=" << handle_to_user_.size()
          << " max_handles=" << kMaxContextHandles;
      throw std::runtime_error(oss.str());
    }

    const SecureIr secure_ir = ParseSecureIr(json);

    if (secure_ir.signature.empty()) {
      throw std::runtime_error("gateway_invalid_signature reason=empty");
    }
    if (secure_ir.windows.empty()) {
      throw std::runtime_error("gateway_invalid_windows reason=empty");
    }

    std::vector<ExecWindow> windows;
    windows.reserve(secure_ir.windows.size());
    for (const SecureIrWindow& ir_window : secure_ir.windows) {
      if (ir_window.type != "CODE") {
        std::ostringstream oss;
        oss << "gateway_invalid_window_type window_id=" << ir_window.window_id
            << " type=" << ir_window.type;
        throw std::runtime_error(oss.str());
      }

      ExecWindow window;
      window.window_id = ir_window.window_id;
      window.start_va = ir_window.start_va;
      window.end_va = ir_window.end_va;
      window.owner_user_id = secure_ir.user_id;
      window.key_id = ir_window.key_id;
      window.type = ExecWindowType::CODE;
      window.permissions = MemoryPermissions::RX;
      window.code_policy_id = ir_window.code_policy_id;
      windows.push_back(window);
    }

    hardware_.GetEwcTable().SetWindows(handle, std::move(windows));
    hardware_.GetSpeTable().ConfigurePolicy(handle, secure_ir.user_id, secure_ir.cfi_level,
                                            secure_ir.call_targets, secure_ir.jmp_targets);
    handle_to_user_[handle] = secure_ir.user_id;
    hardware_.GetAuditCollector().LogEvent("GATEWAY_LOAD_OK", secure_ir.user_id, handle, secure_ir.base_va,
                                           MakeLoadOkDetail(secure_ir));
    return handle;
  } catch (const std::exception& ex) {
    handle_to_user_.erase(handle);
    hardware_.GetSpeTable().ClearPolicy(handle);
    hardware_.GetEwcTable().ClearWindows(handle);
    hardware_.GetAuditCollector().LogEvent("GATEWAY_LOAD_FAIL", 0, handle, 0, MakeLoadFailDetail(ex.what()));
    throw;
  }
}

void Gateway::Release(ContextHandle handle) {
  const auto it = handle_to_user_.find(handle);
  const bool released = (it != handle_to_user_.end());
  const std::uint32_t user_id = released ? it->second : 0;
  if (released) {
    handle_to_user_.erase(it);
  }
  hardware_.GetSpeTable().ClearPolicy(handle);
  hardware_.GetEwcTable().ClearWindows(handle);
  hardware_.GetAuditCollector().LogEvent("GATEWAY_RELEASE", user_id, handle, 0, MakeReleaseDetail(released));
}

std::optional<std::uint32_t> Gateway::GetUserIdForHandle(ContextHandle handle) const {
  auto it = handle_to_user_.find(handle);
  if (it == handle_to_user_.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace sim::security
