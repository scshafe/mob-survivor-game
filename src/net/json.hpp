#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace mob_survivor::net {

// A parsed JSON value. Small and strict: enough for client messages, which
// are short objects of strings, numbers and booleans.
class JsonValue {
 public:
  using Array = std::vector<JsonValue>;
  using Object = std::vector<std::pair<std::string, JsonValue>>;

  JsonValue() = default;
  explicit JsonValue(bool value) : data_(value) {}
  explicit JsonValue(double value) : data_(value) {}
  explicit JsonValue(std::string value) : data_(std::move(value)) {}
  explicit JsonValue(Array value) : data_(std::move(value)) {}
  explicit JsonValue(Object value) : data_(std::move(value)) {}

  [[nodiscard]] bool is_null() const { return std::holds_alternative<std::monostate>(data_); }
  [[nodiscard]] bool is_object() const { return std::holds_alternative<Object>(data_); }

  // Object member lookup; nullptr if absent or not an object.
  [[nodiscard]] const JsonValue* find(std::string_view key) const;

  [[nodiscard]] std::optional<std::string_view> string() const;
  [[nodiscard]] std::optional<double> number() const;
  [[nodiscard]] std::optional<bool> boolean() const;

  // Conveniences for members: absent or mistyped gives the fallback.
  [[nodiscard]] std::string_view get_string(std::string_view key, std::string_view fallback = {}) const;
  [[nodiscard]] double get_number(std::string_view key, double fallback = 0.0) const;
  [[nodiscard]] bool get_bool(std::string_view key, bool fallback = false) const;

 private:
  std::variant<std::monostate, bool, double, std::string, Array, Object> data_;
};

// Parses a whole document; nullopt on any syntax error, trailing bytes or
// nesting deeper than 16.
[[nodiscard]] std::optional<JsonValue> parse_json(std::string_view text);

// Builds JSON text. Commas are handled for you:
//   JsonWriter w; w.begin_object(); w.key("t"); w.value("room"); w.end_object();
class JsonWriter {
 public:
  JsonWriter& begin_object();
  JsonWriter& end_object();
  JsonWriter& begin_array();
  JsonWriter& end_array();
  JsonWriter& key(std::string_view name);
  JsonWriter& value(std::string_view text);
  JsonWriter& value(const char* text) { return value(std::string_view(text)); }
  JsonWriter& value(double number);
  JsonWriter& value(int number);
  JsonWriter& value(std::int64_t number);
  JsonWriter& value(std::uint32_t number) { return value(static_cast<std::int64_t>(number)); }
  JsonWriter& value(bool flag);
  JsonWriter& null();

  template <typename T>
  JsonWriter& field(std::string_view name, T&& v) {
    key(name);
    return value(std::forward<T>(v));
  }

  [[nodiscard]] const std::string& str() const { return out_; }
  [[nodiscard]] std::string take() { return std::move(out_); }

 private:
  void separate();
  std::string out_;
  bool need_comma_ = false;
};

[[nodiscard]] std::string json_quote(std::string_view text);

}  // namespace mob_survivor::net
