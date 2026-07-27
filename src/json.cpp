#include "bang/json.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace bang {
namespace {

std::string Escape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 2);
  constexpr char kHex[] = "0123456789abcdef";
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20U) {
          out += "\\u00";
          out.push_back(kHex[ch >> 4U]);
          out.push_back(kHex[ch & 0x0fU]);
        } else {
          out.push_back(static_cast<char>(ch));
        }
    }
  }
  return out;
}

int HexValue(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

void AppendUtf8(std::string& out, std::uint32_t code_point) {
  if (code_point <= 0x7FU) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }
}

void DumpInto(std::ostringstream& out, const JsonValue& value) {
  const auto& storage = value.storage();
  if (std::holds_alternative<std::nullptr_t>(storage)) {
    out << "null";
  } else if (const auto* b = std::get_if<bool>(&storage)) {
    out << (*b ? "true" : "false");
  } else if (const auto* number = std::get_if<double>(&storage)) {
    // Numbers must survive a dump/parse round trip: session journals store
    // 31-bit seeds, and the default 6-significant-digit ostream precision
    // silently corrupts them (1587783211 -> 1.58778e+09).
    const double value = *number;
    if (std::isfinite(value) && value == std::floor(value) &&
        std::abs(value) <= 9007199254740992.0) {
      out << static_cast<long long>(value);
    } else {
      out << std::setprecision(std::numeric_limits<double>::max_digits10)
          << value;
    }
  } else if (const auto* string = std::get_if<std::string>(&storage)) {
    out << '"' << Escape(*string) << '"';
  } else if (const auto* array = std::get_if<JsonValue::Array>(&storage)) {
    out << '[';
    for (std::size_t i = 0; i < array->size(); ++i) {
      if (i != 0) out << ',';
      DumpInto(out, (*array)[i]);
    }
    out << ']';
  } else if (const auto* object = std::get_if<JsonValue::Object>(&storage)) {
    out << '{';
    bool first = true;
    for (const auto& [key, object_value] : *object) {
      if (!first) out << ',';
      first = false;
      out << '"' << Escape(key) << "\":";
      DumpInto(out, object_value);
    }
    out << '}';
  }
}

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  std::optional<JsonValue> Parse() {
    SkipWhitespace();
    auto value = ParseValue();
    if (!value.has_value()) {
      return std::nullopt;
    }
    SkipWhitespace();
    if (position_ != text_.size()) {
      return std::nullopt;
    }
    return value;
  }

 private:
  void SkipWhitespace() {
    while (position_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
      ++position_;
    }
  }

  bool Consume(char expected) {
    SkipWhitespace();
    if (position_ >= text_.size() || text_[position_] != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  bool ConsumeLiteral(std::string_view literal) {
    SkipWhitespace();
    if (text_.substr(position_, literal.size()) != literal) {
      return false;
    }
    position_ += literal.size();
    return true;
  }

  std::optional<JsonValue> ParseValue() {
    SkipWhitespace();
    if (position_ >= text_.size()) {
      return std::nullopt;
    }
    const char ch = text_[position_];
    if (ch == '{') return ParseObject();
    if (ch == '[') return ParseArray();
    if (ch == '"') return ParseString();
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) return ParseNumber();
    if (ConsumeLiteral("true")) return JsonValue(true);
    if (ConsumeLiteral("false")) return JsonValue(false);
    if (ConsumeLiteral("null")) return JsonValue(nullptr);
    return std::nullopt;
  }

  std::optional<JsonValue> ParseObject() {
    if (!Consume('{')) {
      return std::nullopt;
    }
    JsonValue::Object object;
    SkipWhitespace();
    if (Consume('}')) {
      return JsonValue(std::move(object));
    }
    while (true) {
      auto key = ParseStringValue();
      if (!key.has_value() || !Consume(':')) {
        return std::nullopt;
      }
      auto value = ParseValue();
      if (!value.has_value()) {
        return std::nullopt;
      }
      object.emplace(std::move(*key), std::move(*value));
      if (Consume('}')) {
        return JsonValue(std::move(object));
      }
      if (!Consume(',')) {
        return std::nullopt;
      }
    }
  }

  std::optional<JsonValue> ParseArray() {
    if (!Consume('[')) {
      return std::nullopt;
    }
    JsonValue::Array array;
    SkipWhitespace();
    if (Consume(']')) {
      return JsonValue(std::move(array));
    }
    while (true) {
      auto value = ParseValue();
      if (!value.has_value()) {
        return std::nullopt;
      }
      array.push_back(std::move(*value));
      if (Consume(']')) {
        return JsonValue(std::move(array));
      }
      if (!Consume(',')) {
        return std::nullopt;
      }
    }
  }

  std::optional<std::string> ParseStringValue() {
    SkipWhitespace();
    if (position_ >= text_.size() || text_[position_] != '"') {
      return std::nullopt;
    }
    ++position_;
    std::string value;
    while (position_ < text_.size()) {
      const char ch = text_[position_++];
      if (ch == '"') {
        return value;
      }
      if (ch != '\\') {
        value.push_back(ch);
        continue;
      }
      if (position_ >= text_.size()) {
        return std::nullopt;
      }
      const char escaped = text_[position_++];
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
        case 'u': {
          auto code_point = ParseHex4();
          if (!code_point.has_value()) {
            return std::nullopt;
          }
          if (*code_point >= 0xD800U && *code_point <= 0xDBFFU) {
            if (position_ + 2 > text_.size() || text_[position_] != '\\' ||
                text_[position_ + 1] != 'u') {
              return std::nullopt;
            }
            position_ += 2;
            auto low = ParseHex4();
            if (!low.has_value() || *low < 0xDC00U || *low > 0xDFFFU) {
              return std::nullopt;
            }
            code_point =
                0x10000U + ((*code_point - 0xD800U) << 10U) + (*low - 0xDC00U);
          } else if (*code_point >= 0xDC00U && *code_point <= 0xDFFFU) {
            return std::nullopt;
          }
          AppendUtf8(value, *code_point);
          break;
        }
        default:
          return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<JsonValue> ParseString() {
    auto value = ParseStringValue();
    if (!value.has_value()) {
      return std::nullopt;
    }
    return JsonValue(std::move(*value));
  }

  std::optional<std::uint32_t> ParseHex4() {
    if (position_ + 4 > text_.size()) {
      return std::nullopt;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const int digit = HexValue(text_[position_++]);
      if (digit < 0) {
        return std::nullopt;
      }
      value = (value << 4U) | static_cast<std::uint32_t>(digit);
    }
    return value;
  }

  std::optional<JsonValue> ParseNumber() {
    SkipWhitespace();
    const std::size_t start = position_;
    if (position_ < text_.size() && text_[position_] == '-') {
      ++position_;
    }
    while (position_ < text_.size() &&
           std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
      ++position_;
    }
    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
        ++position_;
      }
    }
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) {
        ++position_;
      }
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
        ++position_;
      }
    }
    double value = 0.0;
    const std::string token(text_.substr(start, position_ - start));
    char* end = nullptr;
    value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size()) {
      return std::nullopt;
    }
    return JsonValue(value);
  }

  std::string_view text_;
  std::size_t position_ = 0;
};

}  // namespace

JsonValue::JsonValue() : storage_(nullptr) {}
JsonValue::JsonValue(std::nullptr_t) : storage_(nullptr) {}
JsonValue::JsonValue(bool value) : storage_(value) {}
JsonValue::JsonValue(double value) : storage_(value) {}
JsonValue::JsonValue(int value) : storage_(static_cast<double>(value)) {}
JsonValue::JsonValue(const char* value) : storage_(std::string(value)) {}
JsonValue::JsonValue(std::string value) : storage_(std::move(value)) {}
JsonValue::JsonValue(Array value) : storage_(std::move(value)) {}
JsonValue::JsonValue(Object value) : storage_(std::move(value)) {}

const JsonValue::Storage& JsonValue::storage() const {
  return storage_;
}

std::string JsonValue::Dump() const {
  std::ostringstream out;
  DumpInto(out, *this);
  return out.str();
}

std::optional<JsonValue> ParseJson(const std::string& text) {
  return Parser(text).Parse();
}

}  // namespace bang
