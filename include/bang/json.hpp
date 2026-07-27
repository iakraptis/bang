#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bang {

class JsonValue {
 public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;
  using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

  JsonValue();
  JsonValue(std::nullptr_t);
  JsonValue(bool value);
  JsonValue(double value);
  JsonValue(int value);
  JsonValue(const char* value);
  JsonValue(std::string value);
  JsonValue(Array value);
  JsonValue(Object value);

  const Storage& storage() const;
  std::string Dump() const;

 private:
  Storage storage_;
};

std::optional<JsonValue> ParseJson(const std::string& text);

}  // namespace bang
