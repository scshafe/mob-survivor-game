#include "net/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mob_survivor::net {

const JsonValue* JsonValue::find(std::string_view key) const {
  const auto* object = std::get_if<Object>(&data_);
  if (object == nullptr) return nullptr;
  for (const auto& [name, member] : *object) {
    if (name == key) return &member;
  }
  return nullptr;
}

std::optional<std::string_view> JsonValue::string() const {
  if (const auto* text = std::get_if<std::string>(&data_)) return std::string_view(*text);
  return std::nullopt;
}

std::optional<double> JsonValue::number() const {
  if (const auto* number = std::get_if<double>(&data_)) return *number;
  return std::nullopt;
}

std::optional<bool> JsonValue::boolean() const {
  if (const auto* flag = std::get_if<bool>(&data_)) return *flag;
  return std::nullopt;
}

std::string_view JsonValue::get_string(std::string_view key, std::string_view fallback) const {
  const JsonValue* member = find(key);
  return member != nullptr ? member->string().value_or(fallback) : fallback;
}

double JsonValue::get_number(std::string_view key, double fallback) const {
  const JsonValue* member = find(key);
  return member != nullptr ? member->number().value_or(fallback) : fallback;
}

bool JsonValue::get_bool(std::string_view key, bool fallback) const {
  const JsonValue* member = find(key);
  return member != nullptr ? member->boolean().value_or(fallback) : fallback;
}

namespace {

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  std::optional<JsonValue> document() {
    auto value = parse_value(0);
    skip_blanks();
    if (!value || at_ != text_.size()) return std::nullopt;
    return value;
  }

 private:
  static constexpr int kMaxDepth = 16;

  void skip_blanks() {
    while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
      ++at_;
    }
  }

  bool literal(std::string_view word) {
    if (text_.substr(at_, word.size()) != word) return false;
    at_ += word.size();
    return true;
  }

  std::optional<JsonValue> parse_value(int depth) {
    if (depth > kMaxDepth) return std::nullopt;
    skip_blanks();
    if (at_ >= text_.size()) return std::nullopt;
    const char c = text_[at_];
    if (c == '{') return parse_object(depth);
    if (c == '[') return parse_array(depth);
    if (c == '"') {
      auto text = parse_string();
      if (!text) return std::nullopt;
      return JsonValue(std::move(*text));
    }
    if (literal("true")) return JsonValue(true);
    if (literal("false")) return JsonValue(false);
    if (literal("null")) return JsonValue();
    return parse_number();
  }

  std::optional<JsonValue> parse_number() {
    const std::size_t start = at_;
    if (at_ < text_.size() && text_[at_] == '-') ++at_;
    bool digits = false;
    while (at_ < text_.size() && ((text_[at_] >= '0' && text_[at_] <= '9') || text_[at_] == '.' ||
                                  text_[at_] == 'e' || text_[at_] == 'E' || text_[at_] == '+' || text_[at_] == '-')) {
      digits = digits || (text_[at_] >= '0' && text_[at_] <= '9');
      ++at_;
    }
    if (!digits) return std::nullopt;
    const std::string number(text_.substr(start, at_ - start));
    char* end = nullptr;
    const double value = std::strtod(number.c_str(), &end);
    if (end != number.c_str() + number.size() || !std::isfinite(value)) return std::nullopt;
    return JsonValue(value);
  }

  static void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80U) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800U) {
      out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
      out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else if (code < 0x10000U) {
      out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
      out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xF0U | (code >> 18U)));
      out.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    }
  }

  std::optional<std::uint32_t> hex4() {
    if (at_ + 4 > text_.size()) return std::nullopt;
    std::uint32_t code = 0;
    for (int i = 0; i < 4; ++i) {
      const char h = text_[at_++];
      code <<= 4U;
      if (h >= '0' && h <= '9') {
        code |= static_cast<std::uint32_t>(h - '0');
      } else if (h >= 'a' && h <= 'f') {
        code |= static_cast<std::uint32_t>(h - 'a' + 10);
      } else if (h >= 'A' && h <= 'F') {
        code |= static_cast<std::uint32_t>(h - 'A' + 10);
      } else {
        return std::nullopt;
      }
    }
    return code;
  }

  std::optional<std::string> parse_string() {
    ++at_;  // opening quote
    std::string out;
    while (at_ < text_.size()) {
      const char c = text_[at_++];
      if (c == '"') return out;
      if (static_cast<unsigned char>(c) < 0x20U) return std::nullopt;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (at_ >= text_.size()) return std::nullopt;
      const char escape = text_[at_++];
      switch (escape) {
        case '"':
        case '\\':
        case '/':
          out.push_back(escape);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          auto code = hex4();
          if (!code) return std::nullopt;
          if (*code >= 0xD800U && *code <= 0xDBFFU) {
            if (!literal("\\u")) return std::nullopt;
            auto low = hex4();
            if (!low || *low < 0xDC00U || *low > 0xDFFFU) return std::nullopt;
            *code = 0x10000U + ((*code - 0xD800U) << 10U) + (*low - 0xDC00U);
          } else if (*code >= 0xDC00U && *code <= 0xDFFFU) {
            return std::nullopt;
          }
          append_utf8(out, *code);
          break;
        }
        default:
          return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<JsonValue> parse_array(int depth) {
    ++at_;
    JsonValue::Array items;
    skip_blanks();
    if (at_ < text_.size() && text_[at_] == ']') {
      ++at_;
      return JsonValue(std::move(items));
    }
    while (true) {
      auto item = parse_value(depth + 1);
      if (!item) return std::nullopt;
      items.push_back(std::move(*item));
      skip_blanks();
      if (at_ >= text_.size()) return std::nullopt;
      if (text_[at_] == ',') {
        ++at_;
        continue;
      }
      if (text_[at_] == ']') {
        ++at_;
        return JsonValue(std::move(items));
      }
      return std::nullopt;
    }
  }

  std::optional<JsonValue> parse_object(int depth) {
    ++at_;
    JsonValue::Object members;
    skip_blanks();
    if (at_ < text_.size() && text_[at_] == '}') {
      ++at_;
      return JsonValue(std::move(members));
    }
    while (true) {
      skip_blanks();
      if (at_ >= text_.size() || text_[at_] != '"') return std::nullopt;
      auto name = parse_string();
      if (!name) return std::nullopt;
      skip_blanks();
      if (at_ >= text_.size() || text_[at_] != ':') return std::nullopt;
      ++at_;
      auto member = parse_value(depth + 1);
      if (!member) return std::nullopt;
      members.emplace_back(std::move(*name), std::move(*member));
      skip_blanks();
      if (at_ >= text_.size()) return std::nullopt;
      if (text_[at_] == ',') {
        ++at_;
        continue;
      }
      if (text_[at_] == '}') {
        ++at_;
        return JsonValue(std::move(members));
      }
      return std::nullopt;
    }
  }

  std::string_view text_;
  std::size_t at_ = 0;
};

}  // namespace

std::optional<JsonValue> parse_json(std::string_view text) { return Parser(text).document(); }

std::string json_quote(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20U) {
          char escaped[8];
          std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += escaped;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
  return out;
}

void JsonWriter::separate() {
  if (need_comma_) out_.push_back(',');
  need_comma_ = true;
}

JsonWriter& JsonWriter::begin_object() {
  separate();
  out_.push_back('{');
  need_comma_ = false;
  return *this;
}

JsonWriter& JsonWriter::end_object() {
  out_.push_back('}');
  need_comma_ = true;
  return *this;
}

JsonWriter& JsonWriter::begin_array() {
  separate();
  out_.push_back('[');
  need_comma_ = false;
  return *this;
}

JsonWriter& JsonWriter::end_array() {
  out_.push_back(']');
  need_comma_ = true;
  return *this;
}

JsonWriter& JsonWriter::key(std::string_view name) {
  separate();
  out_ += json_quote(name);
  out_.push_back(':');
  need_comma_ = false;
  return *this;
}

JsonWriter& JsonWriter::value(std::string_view text) {
  separate();
  out_ += json_quote(text);
  return *this;
}

JsonWriter& JsonWriter::value(double number) {
  separate();
  if (!std::isfinite(number)) {
    out_ += "null";
    return *this;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.10g", number);
  out_ += buffer;
  return *this;
}

JsonWriter& JsonWriter::value(int number) { return value(static_cast<std::int64_t>(number)); }

JsonWriter& JsonWriter::value(std::int64_t number) {
  separate();
  out_ += std::to_string(number);
  return *this;
}

JsonWriter& JsonWriter::value(bool flag) {
  separate();
  out_ += flag ? "true" : "false";
  return *this;
}

JsonWriter& JsonWriter::null() {
  separate();
  out_ += "null";
  return *this;
}

}  // namespace mob_survivor::net
