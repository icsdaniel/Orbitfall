#pragma once
// mini_json: a compact, header-only, exception-free JSON (RFC 8259) reader.
// Written for Orbitfall's content loader; no dependencies beyond the C++17
// standard library, so it builds unchanged inside the GDExtension (godot-cpp
// compiles with -fno-exceptions) and on web/Android.
//
//   mini_json::Value v; std::string err;
//   if (!mini_json::parse(text, v, err)) { /* err = "line 3 col 7: ..." */ }
//
// Objects keep their key order. Numbers are doubles. Read-only: no writer.
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace mini_json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value {
  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Value> array;
  std::vector<std::pair<std::string, Value>> object;

  bool is_null() const { return type == Type::Null; }
  bool is_bool() const { return type == Type::Bool; }
  bool is_number() const { return type == Type::Number; }
  bool is_string() const { return type == Type::String; }
  bool is_array() const { return type == Type::Array; }
  bool is_object() const { return type == Type::Object; }

  // Object member lookup; nullptr when absent or when this is not an object.
  const Value* find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : object)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
};

inline const char* type_name(Type t) {
  switch (t) {
    case Type::Null: return "null";
    case Type::Bool: return "bool";
    case Type::Number: return "number";
    case Type::String: return "string";
    case Type::Array: return "array";
    case Type::Object: return "object";
  }
  return "?";
}

namespace detail {

struct Parser {
  const char* p;
  const char* end;
  const char* begin;
  std::string err;
  static constexpr int kMaxDepth = 64;

  bool fail(const char* msg) {
    if (err.empty()) {
      int line = 1, col = 1;
      for (const char* c = begin; c < p && c < end; ++c) {
        if (*c == '\n') { ++line; col = 1; } else { ++col; }
      }
      err = "line " + std::to_string(line) + " col " + std::to_string(col) + ": " + msg;
    }
    return false;
  }

  void skip_ws() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }

  bool literal(const char* word) {
    const char* q = p;
    for (const char* w = word; *w; ++w, ++q)
      if (q >= end || *q != *w) return fail("invalid literal");
    p = q;
    return true;
  }

  static void append_utf8(std::string& s, unsigned cp) {
    if (cp < 0x80) {
      s += static_cast<char>(cp);
    } else if (cp < 0x800) {
      s += static_cast<char>(0xC0 | (cp >> 6));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      s += static_cast<char>(0xE0 | (cp >> 12));
      s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      s += static_cast<char>(0xF0 | (cp >> 18));
      s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool hex4(unsigned& out) {
    out = 0;
    for (int i = 0; i < 4; ++i) {
      if (p >= end) return fail("truncated \\u escape");
      const char c = *p++;
      out <<= 4;
      if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
      else return fail("bad hex digit in \\u escape");
    }
    return true;
  }

  bool parse_string(std::string& out) {
    ++p;  // opening quote
    while (true) {
      if (p >= end) return fail("unterminated string");
      const char c = *p++;
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
      if (c != '\\') { out += c; continue; }
      if (p >= end) return fail("unterminated escape");
      const char e = *p++;
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp = 0;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate pair
            if (end - p < 2 || p[0] != '\\' || p[1] != 'u') return fail("lone surrogate");
            p += 2;
            unsigned lo = 0;
            if (!hex4(lo)) return false;
            if (lo < 0xDC00 || lo > 0xDFFF) return fail("bad low surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return fail("lone surrogate");
          }
          append_utf8(out, cp);
          break;
        }
        default: return fail("unknown escape");
      }
    }
  }

  static bool digit(char c) { return c >= '0' && c <= '9'; }

  bool parse_number(double& out) {
    const char* s = p;
    if (p < end && *p == '-') ++p;
    if (p >= end || !digit(*p)) return fail("invalid number");
    if (*p == '0') { ++p; } else { while (p < end && digit(*p)) ++p; }
    if (p < end && *p == '.') {
      ++p;
      if (p >= end || !digit(*p)) return fail("digits expected after '.'");
      while (p < end && digit(*p)) ++p;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
      ++p;
      if (p < end && (*p == '+' || *p == '-')) ++p;
      if (p >= end || !digit(*p)) return fail("digits expected in exponent");
      while (p < end && digit(*p)) ++p;
    }
    out = std::strtod(std::string(s, p).c_str(), nullptr);
    return true;
  }

  bool parse_value(Value& v, int depth) {
    if (depth > kMaxDepth) return fail("nesting too deep");
    skip_ws();
    if (p >= end) return fail("unexpected end of input");
    switch (*p) {
      case '{': {
        v.type = Type::Object;
        ++p;
        skip_ws();
        if (p < end && *p == '}') { ++p; return true; }
        while (true) {
          skip_ws();
          if (p >= end || *p != '"') return fail("object key expected");
          std::string key;
          if (!parse_string(key)) return false;
          if (v.find(key)) return fail("duplicate object key");
          skip_ws();
          if (p >= end || *p != ':') return fail("':' expected");
          ++p;
          v.object.emplace_back(std::move(key), Value());
          if (!parse_value(v.object.back().second, depth + 1)) return false;
          skip_ws();
          if (p < end && *p == ',') { ++p; continue; }
          if (p < end && *p == '}') { ++p; return true; }
          return fail("',' or '}' expected");
        }
      }
      case '[': {
        v.type = Type::Array;
        ++p;
        skip_ws();
        if (p < end && *p == ']') { ++p; return true; }
        while (true) {
          v.array.emplace_back();
          if (!parse_value(v.array.back(), depth + 1)) return false;
          skip_ws();
          if (p < end && *p == ',') { ++p; continue; }
          if (p < end && *p == ']') { ++p; return true; }
          return fail("',' or ']' expected");
        }
      }
      case '"':
        v.type = Type::String;
        return parse_string(v.string);
      case 't':
        v.type = Type::Bool;
        v.boolean = true;
        return literal("true");
      case 'f':
        v.type = Type::Bool;
        v.boolean = false;
        return literal("false");
      case 'n':
        v.type = Type::Null;
        return literal("null");
      default:
        v.type = Type::Number;
        return parse_number(v.number);
    }
  }
};

}  // namespace detail

// Parses a complete JSON document. Returns false and fills `err` on failure.
inline bool parse(const std::string& text, Value& out, std::string& err) {
  detail::Parser ps{text.data(), text.data() + text.size(), text.data(), std::string()};
  // Tolerate a UTF-8 byte-order mark.
  if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) ps.p += 3;
  out = Value();
  if (!ps.parse_value(out, 0)) { err = ps.err; return false; }
  ps.skip_ws();
  if (ps.p != ps.end) { ps.fail("trailing characters after document"); err = ps.err; return false; }
  return true;
}

}  // namespace mini_json
