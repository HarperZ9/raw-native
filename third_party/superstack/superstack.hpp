// SPDX-License-Identifier: FSL-1.1-MIT
// Copyright 2026 Zain Dana Harper
//
// Licensed under the Functional Source License, Version 1.1, MIT Future
// License (FSL-1.1-MIT). The full text is LICENSE in the superstack
// repository and https://fsl.software/FSL-1.1-MIT.template.md. Each release
// becomes available under the MIT licence below on the second anniversary of
// the date it was made available. From v0.2.0 this file is FSL-1.1-MIT;
// releases up to and including v0.1.0 remain under the MIT licence.
//
// Algorithms by others, each under its own terms, which the FSL does not change:
//   mulberry32: Tommy Ettinger, 2017, CC0 1.0 public domain dedication.
//   xmur3: bryc (github.com/bryc/code), public domain, MIT fallback,
//     Copyright (c) 2024 bryc.
//   OKLab matrices: Bjorn Ottosson, 2020, public domain, MIT fallback.
//   K-weighting constants for rates other than 48 kHz: as published in
//     libebur128 (MIT); the 48 kHz table is ITU-R BS.1770-4's.
// Sources and dates: docs/LICENSING.md in the superstack repository.
//
// MIT licence text. It applies to the parts above that use an MIT fallback,
// and to this file once its FSL period ends:
//
// Copyright 2026 Zain Dana Harper
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// superstack contract v0, C++23 implementation. Header-only, standard library only.
// Same rules as superstack.py and superstack.mjs; all three must pass every file in
// vectors/. Bytes are std::vector<std::uint8_t>; JSON is superstack::Value.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace superstack {

inline constexpr std::string_view CONTRACT = "superstack/0";
inline constexpr std::string_view VERSION = "0.2.0";
inline constexpr std::string_view RECEIPT_SCHEMA = "superstack.receipt/1";
inline constexpr std::int64_t MAX_SAFE_INTEGER = 9007199254740991LL;
using Bytes = std::vector<std::uint8_t>;

struct error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------- JSON value

struct Value;
struct Member;
using Array = std::vector<Value>;
using Object = std::vector<Member>;  // insertion order; canonical() sorts

struct Value {
  std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> v;
  // The constructors are declared here and defined after Member, and v has no
  // default member initializer, so nothing builds the variant while Member is
  // still incomplete. libc++ (Emscripten, Clang) rejects that; libstdc++ and
  // MSVC accept it.
  Value();
  Value(std::nullptr_t);
  Value(bool b);
  Value(int i);
  Value(long i);
  Value(long long i);
  Value(unsigned u);
  Value(double d);
  Value(const char* s);
  Value(std::string s);
  Value(std::string_view s);
  Value(Array a);
  Value(Object o);

  bool is_null() const { return std::holds_alternative<std::nullptr_t>(v); }
  bool is_bool() const { return std::holds_alternative<bool>(v); }
  bool is_string() const { return std::holds_alternative<std::string>(v); }
  bool is_array() const { return std::holds_alternative<Array>(v); }
  bool is_object() const { return std::holds_alternative<Object>(v); }
  bool is_number() const { return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v); }
  const std::string& str() const { return std::get<std::string>(v); }
  const Array& arr() const { return std::get<Array>(v); }
  Array& arr() { return std::get<Array>(v); }
  const Object& obj() const { return std::get<Object>(v); }
  Object& obj() { return std::get<Object>(v); }
  double num() const {
    if (auto p = std::get_if<std::int64_t>(&v)) return static_cast<double>(*p);
    return std::get<double>(v);
  }
  const Value* find(std::string_view key) const;
  Value* find(std::string_view key);
  const Value& at(std::string_view key) const;
  Value& set(std::string_view key, Value val);
  bool erase(std::string_view key);
};

struct Member {
  std::string key;
  Value value;
};

inline Value::Value() : v(nullptr) {}
inline Value::Value(std::nullptr_t) : v(nullptr) {}
inline Value::Value(bool b) : v(b) {}
inline Value::Value(int i) : v(std::int64_t{i}) {}
inline Value::Value(long i) : v(static_cast<std::int64_t>(i)) {}
inline Value::Value(long long i) : v(static_cast<std::int64_t>(i)) {}
inline Value::Value(unsigned u) : v(std::int64_t{u}) {}
inline Value::Value(double d) : v(d) {}
inline Value::Value(const char* s) : v(std::string(s)) {}
inline Value::Value(std::string s) : v(std::move(s)) {}
inline Value::Value(std::string_view s) : v(std::string(s)) {}
inline Value::Value(Array a) : v(std::move(a)) {}
inline Value::Value(Object o) : v(std::move(o)) {}
inline const Value* Value::find(std::string_view key) const {
  if (!is_object()) return nullptr;
  for (const auto& m : obj())
    if (m.key == key) return &m.value;
  return nullptr;
}
inline Value* Value::find(std::string_view key) {
  if (!is_object()) return nullptr;
  for (auto& m : obj())
    if (m.key == key) return &m.value;
  return nullptr;
}
inline const Value& Value::at(std::string_view key) const {
  static const Value null_value;
  const Value* p = find(key);
  return p ? *p : null_value;
}
inline Value& Value::set(std::string_view key, Value val) {
  if (!is_object()) v = Object{};
  if (Value* p = find(key)) return *p = std::move(val);
  obj().push_back(Member{std::string(key), std::move(val)});
  return obj().back().value;
}
inline bool Value::erase(std::string_view key) {
  if (!is_object()) return false;
  auto& o = obj();
  auto it = std::find_if(o.begin(), o.end(), [&](const Member& m) { return m.key == key; });
  if (it == o.end()) return false;
  o.erase(it);
  return true;
}

// Value equality: numbers compare by value, so 2 and 2.0 are equal.
inline bool operator==(const Value& a, const Value& b) {
  if (a.is_number() && b.is_number()) return a.num() == b.num();
  if (a.v.index() != b.v.index()) return false;
  if (a.is_object()) {
    if (a.obj().size() != b.obj().size()) return false;
    for (const auto& m : a.obj()) {
      const Value* o = b.find(m.key);
      if (!o || !(*o == m.value)) return false;
    }
    return true;
  }
  if (a.is_array()) {
    if (a.arr().size() != b.arr().size()) return false;
    for (std::size_t i = 0; i < a.arr().size(); ++i)
      if (!(a.arr()[i] == b.arr()[i])) return false;
    return true;
  }
  if (a.is_string()) return a.str() == b.str();
  if (a.is_bool()) return std::get<bool>(a.v) == std::get<bool>(b.v);
  return true;  // both null
}

// ---------------------------------------------------------------- UTF-8

namespace detail {
// Decode UTF-8 into code points; throws on invalid input or encoded surrogates.
inline std::vector<std::uint32_t> code_points(std::string_view s) {
  std::vector<std::uint32_t> out;
  for (std::size_t i = 0; i < s.size();) {
    auto c = static_cast<std::uint8_t>(s[i]);
    std::uint32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
    else throw error("invalid UTF-8");
    if (i + static_cast<std::size_t>(n) >= s.size() && n > 0) throw error("invalid UTF-8");
    for (int k = 1; k <= n; ++k) {
      auto d = static_cast<std::uint8_t>(s[i + k]);
      if ((d & 0xC0) != 0x80) throw error("invalid UTF-8");
      cp = (cp << 6) | (d & 0x3F);
    }
    static constexpr std::uint32_t min_cp[4] = {0, 0x80, 0x800, 0x10000};
    if (cp < min_cp[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) throw error("invalid UTF-8 or lone surrogate");
    out.push_back(cp);
    i += 1 + n;
  }
  return out;
}

inline void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}
}  // namespace detail

inline std::vector<std::uint16_t> utf16_units(std::string_view s) {
  std::vector<std::uint16_t> out;
  for (std::uint32_t cp : detail::code_points(s)) {
    if (cp < 0x10000) out.push_back(static_cast<std::uint16_t>(cp));
    else {
      cp -= 0x10000;
      out.push_back(static_cast<std::uint16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<std::uint16_t>(0xDC00 + (cp & 0x3FF)));
    }
  }
  return out;
}

// ---------------------------------------------------------------- JSON parser

namespace detail {
class Parser {
 public:
  explicit Parser(std::string_view s) : s_(s) {}
  Value document() {
    Value v = value();
    ws();
    if (i_ != s_.size()) fail("trailing characters");
    return v;
  }

 private:
  std::string_view s_;
  std::size_t i_ = 0;
  [[noreturn]] void fail(const char* what) { throw error(std::string("JSON: ") + what); }
  void ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_; }
  char peek() { if (i_ >= s_.size()) fail("unexpected end"); return s_[i_]; }
  void expect(std::string_view w) {
    if (s_.substr(i_, w.size()) != w) fail("unexpected token");
    i_ += w.size();
  }
  Value value() {
    ws();
    char c = peek();
    if (c == '{') return object();
    if (c == '[') return array();
    if (c == '"') return Value(string());
    if (c == 't') { expect("true"); return Value(true); }
    if (c == 'f') { expect("false"); return Value(false); }
    if (c == 'n') { expect("null"); return Value(nullptr); }
    return number();
  }
  Value object() {
    ++i_;
    Object o;
    ws();
    if (peek() == '}') { ++i_; return Value(std::move(o)); }
    for (;;) {
      ws();
      if (peek() != '"') fail("expected key");
      std::string k = string();
      for (const auto& m : o) if (m.key == k) fail("duplicate key");
      ws();
      expect(":");
      Value v = value();
      o.push_back(Member{std::move(k), std::move(v)});
      ws();
      if (peek() == ',') { ++i_; continue; }
      expect("}");
      return Value(std::move(o));
    }
  }
  Value array() {
    ++i_;
    Array a;
    ws();
    if (peek() == ']') { ++i_; return Value(std::move(a)); }
    for (;;) {
      a.push_back(value());
      ws();
      if (peek() == ',') { ++i_; continue; }
      expect("]");
      return Value(std::move(a));
    }
  }
  std::uint32_t hex4() {
    if (i_ + 4 > s_.size()) fail("short \\u escape");
    std::uint32_t v = 0;
    auto r = std::from_chars(s_.data() + i_, s_.data() + i_ + 4, v, 16);
    if (r.ptr != s_.data() + i_ + 4) fail("bad \\u escape");
    i_ += 4;
    return v;
  }
  std::string string() {
    ++i_;
    std::string out;
    for (;;) {
      char c = peek();
      if (c == '"') { ++i_; break; }
      if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
      if (c != '\\') { out += c; ++i_; continue; }
      ++i_;
      char e = peek();
      ++i_;
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
          std::uint32_t cp = hex4();
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (s_.substr(i_, 2) != "\\u") fail("lone surrogate");
            i_ += 2;
            std::uint32_t lo = hex4();
            if (lo < 0xDC00 || lo > 0xDFFF) fail("lone surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            fail("lone surrogate");
          }
          append_utf8(out, cp);
          break;
        }
        default: fail("bad escape");
      }
    }
    code_points(out);  // validates UTF-8 of raw bytes
    return out;
  }
  Value number() {
    std::size_t start = i_;
    bool real = false;
    if (peek() == '-') ++i_;
    if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) fail("bad number");
    if (s_[i_] == '0') ++i_;
    else while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
    if (i_ < s_.size() && s_[i_] == '.') {
      real = true;
      ++i_;
      if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) fail("bad fraction");
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      real = true;
      ++i_;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
      if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) fail("bad exponent");
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    const char* b = s_.data() + start;
    const char* e = s_.data() + i_;
    if (!real) {
      std::int64_t n = 0;
      auto r = std::from_chars(b, e, n);
      if (r.ec == std::errc() && r.ptr == e && n <= MAX_SAFE_INTEGER && n >= -MAX_SAFE_INTEGER) return Value(static_cast<long long>(n));
      // Outside the safe range: read as a double, as JavaScript does.
    }
    double d = 0;
    auto r = std::from_chars(b, e, d);
    if (r.ec != std::errc() || r.ptr != e) fail("number out of range");
    return Value(d);
  }
};
}  // namespace detail

inline Value parse(std::string_view text) { return detail::Parser(text).document(); }

// ---------------------------------------------------------------- canonical JSON v2

inline std::string positional(std::string_view sign, std::string_view int_part, std::string_view frac, long exp) {
  std::string digits = std::string(int_part) + std::string(frac);
  long point = static_cast<long>(int_part.size()) + exp;
  std::size_t lead = digits.find_first_not_of('0');
  if (lead == std::string::npos) return "0";
  point -= static_cast<long>(lead);
  digits = digits.substr(lead);
  digits.erase(digits.find_last_not_of('0') + 1);
  std::string body;
  long n = static_cast<long>(digits.size());
  if (point <= 0) body = "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
  else if (point >= n) body = digits + std::string(static_cast<std::size_t>(point - n), '0');
  else body = digits.substr(0, static_cast<std::size_t>(point)) + "." + digits.substr(static_cast<std::size_t>(point));
  return std::string(sign) + body;
}

inline std::string canonical_number(double x) {
  if (!std::isfinite(x)) throw error("non-finite number");
  if (x == 0) return "0";
  char buf[64];
  auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);  // shortest round trip
  std::string_view t(buf, static_cast<std::size_t>(r.ptr - buf));
  std::string_view sign = t.front() == '-' ? "-" : "";
  t.remove_prefix(sign.size());
  std::size_t epos = t.find('e');
  std::string_view mant = t.substr(0, epos);
  long exp = std::stol(std::string(t.substr(epos + 1)));
  std::size_t dot = mant.find('.');
  std::string_view ip = mant.substr(0, dot);
  std::string_view fp = dot == std::string_view::npos ? std::string_view{} : mant.substr(dot + 1);
  return positional(sign, ip, fp, exp);
}

inline std::string canonical_integer(std::int64_t n) {
  if (n > MAX_SAFE_INTEGER || n < -MAX_SAFE_INTEGER) throw error("integer outside the safe range");
  return std::to_string(n);
}

inline std::string canonical_string(std::string_view s) {
  detail::code_points(s);  // rejects invalid UTF-8 and surrogates
  std::string out = "\"";
  for (char ch : s) {
    auto c = static_cast<unsigned char>(ch);
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          static constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out += hex[c >> 4];
          out += hex[c & 15];
        } else {
          out += ch;
        }
    }
  }
  return out + "\"";
}

inline std::string canonical(const Value& v) {
  return std::visit([](const auto& x) -> std::string {
    using T = std::decay_t<decltype(x)>;
    if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
    else if constexpr (std::is_same_v<T, bool>) return x ? "true" : "false";
    else if constexpr (std::is_same_v<T, std::int64_t>) return canonical_integer(x);
    else if constexpr (std::is_same_v<T, double>) return canonical_number(x);
    else if constexpr (std::is_same_v<T, std::string>) return canonical_string(x);
    else if constexpr (std::is_same_v<T, Array>) {
      std::string out = "[";
      for (std::size_t i = 0; i < x.size(); ++i) out += (i ? "," : "") + canonical(x[i]);
      return out + "]";
    } else {
      std::vector<const Member*> ms;
      for (const auto& m : x) ms.push_back(&m);
      // Byte order of valid UTF-8 equals Unicode code point order.
      std::sort(ms.begin(), ms.end(), [](const Member* a, const Member* b) { return a->key < b->key; });
      for (std::size_t i = 1; i < ms.size(); ++i)
        if (ms[i]->key == ms[i - 1]->key) throw error("duplicate key");
      std::string out = "{";
      for (std::size_t i = 0; i < ms.size(); ++i) out += (i ? "," : "") + canonical_string(ms[i]->key) + ":" + canonical(ms[i]->value);
      return out + "}";
    }
  }, v.v);
}

// ---------------------------------------------------------------- SHA-256

inline std::string sha256_hex(std::span<const std::uint8_t> data) {
  static constexpr std::uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::uint32_t H[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  Bytes m(data.begin(), data.end());
  std::uint64_t bits = static_cast<std::uint64_t>(m.size()) * 8;
  m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 7; i >= 0; --i) m.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
  for (std::size_t off = 0; off < m.size(); off += 64) {
    std::uint32_t W[64];
    for (int i = 0; i < 16; ++i)
      W[i] = (std::uint32_t{m[off + 4 * i]} << 24) | (std::uint32_t{m[off + 4 * i + 1]} << 16) |
             (std::uint32_t{m[off + 4 * i + 2]} << 8) | std::uint32_t{m[off + 4 * i + 3]};
    for (int i = 16; i < 64; ++i) {
      std::uint32_t s0 = std::rotr(W[i - 15], 7) ^ std::rotr(W[i - 15], 18) ^ (W[i - 15] >> 3);
      std::uint32_t s1 = std::rotr(W[i - 2], 17) ^ std::rotr(W[i - 2], 19) ^ (W[i - 2] >> 10);
      W[i] = W[i - 16] + s0 + W[i - 7] + s1;
    }
    std::uint32_t a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
    for (int i = 0; i < 64; ++i) {
      std::uint32_t t1 = h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + W[i];
      std::uint32_t t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    H[0] += a; H[1] += b; H[2] += c; H[3] += d; H[4] += e; H[5] += f; H[6] += g; H[7] += h;
  }
  static constexpr char hex[] = "0123456789abcdef";
  std::string out;
  for (std::uint32_t x : H)
    for (int s = 28; s >= 0; s -= 4) out += hex[(x >> s) & 15];
  return out;
}

inline std::string sha256_hex(std::string_view s) {
  return sha256_hex(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}
inline std::string canonical_sha256(const Value& v) { return sha256_hex(std::string_view(canonical(v))); }

// ---------------------------------------------------------------- seed rule

inline constexpr std::string_view SEED_RULE = "xmur3-mulberry32/1";
inline constexpr std::array<std::string_view, 5> SEED_RULES = {
    SEED_RULE, "raw-pixel-hash/1", "fnv1a32-mulberry32", "studio-engine-lcg/1", "splitmix-fnv/1"};

// bryc's xmur3 over UTF-16 code units; the first output of its return function.
inline std::uint32_t xmur3(std::string_view s) {
  auto units = utf16_units(s);
  std::uint32_t h = 1779033703u ^ static_cast<std::uint32_t>(units.size());
  for (std::uint16_t u : units) {
    h = (h ^ u) * 3432918353u;
    h = std::rotl(h, 13);
  }
  h = (h ^ (h >> 16)) * 2246822507u;
  h = (h ^ (h >> 13)) * 3266489909u;
  return h ^ (h >> 16);
}

// Tommy Ettinger's mulberry32 (CC0).
struct Mulberry32 {
  std::uint32_t a;
  explicit Mulberry32(std::uint32_t seed) : a(seed) {}
  std::uint32_t next_u32() {
    a += 0x6D2B79F5u;
    std::uint32_t t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
  }
  double next_float() { return next_u32() / 4294967296.0; }
};

inline Mulberry32 rng(std::string_view seed) { return Mulberry32(xmur3(seed)); }
inline std::string substream(std::string_view seed, std::string_view tag) { return std::string(seed) + "/" + std::string(tag); }

// raw-pixel-hash/1: raw-native's stateless (x, y, sample) integer hash.
inline std::uint32_t pixel_hash(std::uint32_t x, std::uint32_t y, std::uint32_t s) {
  std::uint32_t h = x * 374761393u + y * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
inline double pixel_hash01(std::uint32_t x, std::uint32_t y, std::uint32_t s) {
  return (pixel_hash(x, y, s) & 0xFFFFFFu) / 16777216.0;
}

// ---------------------------------------------------------------- flick clock

inline constexpr std::int64_t FLICKS_PER_SECOND = 705600000;
inline constexpr std::array<std::int64_t, 11> SAMPLE_RATES = {8000, 11025, 16000, 22050, 32000, 44100,
                                                              48000, 88200, 96000, 176400, 192000};
inline bool is_sample_rate(std::int64_t r) { return std::find(SAMPLE_RATES.begin(), SAMPLE_RATES.end(), r) != SAMPLE_RATES.end(); }

inline std::int64_t flicks_per_frame(std::int64_t num, std::int64_t den = 1) {
  if (num <= 0 || den <= 0) throw error("frame rate must be positive");
  if ((FLICKS_PER_SECOND * den) % num) throw error("frame rate does not divide the flick clock");
  return FLICKS_PER_SECOND * den / num;
}
inline std::int64_t flicks_per_sample(std::int64_t rate) {
  if (rate <= 0 || FLICKS_PER_SECOND % rate) throw error("rate does not divide the flick clock");
  return FLICKS_PER_SECOND / rate;
}

inline double round6(double x) { return std::floor(x * 1e6 + 0.5) / 1e6; }

// ---------------------------------------------------------------- colour

inline double srgb_to_linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
inline double linear_to_srgb(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }

// Ottosson's OKLab, published matrices (public domain, MIT fallback).
inline std::array<double, 3> linear_srgb_to_oklab(double r, double g, double b) {
  double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
          1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
          0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}
inline std::array<double, 3> oklab_to_linear_srgb(double L, double a, double b) {
  double l_ = L + 0.3963377774 * a + 0.2158037573 * b;
  double m_ = L - 0.1055613458 * a - 0.0638541728 * b;
  double s_ = L - 0.0894841775 * a - 1.2914855480 * b;
  double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
  return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
          -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
          -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}
inline std::array<int, 3> hex_to_srgb8(std::string_view hex) {
  if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1);
  if (hex.size() != 6) throw error("expected #rrggbb");
  std::array<int, 3> out{};
  for (int i = 0; i < 3; ++i) {
    auto r = std::from_chars(hex.data() + 2 * i, hex.data() + 2 * i + 2, out[i], 16);
    if (r.ptr != hex.data() + 2 * i + 2) throw error("expected #rrggbb");
  }
  return out;
}
inline std::array<double, 3> hex_to_oklab(std::string_view hex) {
  auto c = hex_to_srgb8(hex);
  return linear_srgb_to_oklab(srgb_to_linear(c[0] / 255.0), srgb_to_linear(c[1] / 255.0), srgb_to_linear(c[2] / 255.0));
}
inline double contrast_ratio(std::string_view a, std::string_view b) {
  auto lum = [](std::string_view h) {
    auto c = hex_to_srgb8(h);
    return 0.2126 * srgb_to_linear(c[0] / 255.0) + 0.7152 * srgb_to_linear(c[1] / 255.0) + 0.0722 * srgb_to_linear(c[2] / 255.0);
  };
  double x = lum(a), y = lum(b);
  return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

inline constexpr std::array<std::string_view, 4> RISK_LEVELS = {"low", "moderate", "elevated", "high"};
struct RiskPole { std::string_view low, moderate, elevated, high, ink, quiet; };
inline constexpr RiskPole RISK_LIGHT{"#186844", "#5c5a66", "#8f5200", "#b3261e", "#15130f", "#5d584e"};
inline constexpr RiskPole RISK_DARK{"#8fdc8a", "#a9a6b4", "#f0a848", "#ff7a6b", "#ebe5d8", "#9d978a"};
inline constexpr std::array<std::string_view, 3> RISK_GROUNDS_LIGHT = {"#f4f3ef", "#ebe5d8", "#f2efe6"};
inline constexpr std::array<std::string_view, 3> RISK_GROUNDS_DARK = {"#060608", "#070406", "#1d1622"};

inline std::string risk_of(std::string_view verdict) {
  for (auto lv : RISK_LEVELS) if (verdict == lv) return std::string(lv);
  std::string w(verdict);
  auto b = w.find_first_not_of(" \t\n\r\f\v");
  auto e = w.find_last_not_of(" \t\n\r\f\v");
  w = b == std::string::npos ? "" : w.substr(b, e - b + 1);
  for (auto& ch : w) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  static constexpr std::pair<std::string_view, std::string_view> table[] = {
      {"MATCH", "low"}, {"VERIFIED", "low"}, {"PASS", "low"}, {"OK", "low"},
      {"UNVERIFIABLE", "moderate"}, {"UNKNOWN", "moderate"}, {"PENDING", "moderate"},
      {"DRIFT", "elevated"}, {"WARN", "elevated"}, {"STALE", "elevated"}, {"REFUTED", "high"},
      {"FAIL", "high"}, {"FAILED", "high"}, {"REFUSED", "high"}, {"ERROR", "high"}, {"BROKEN", "high"}};
  for (auto& [k, v] : table) if (w == k) return std::string(v);
  return "moderate";
}
inline int hot_mark(const std::vector<std::string>& levels) {
  int best = -1, rank = -1;
  for (std::size_t i = 0; i < levels.size(); ++i) {
    std::string lv = risk_of(levels[i]);
    int r = static_cast<int>(std::find(RISK_LEVELS.begin(), RISK_LEVELS.end(), lv) - RISK_LEVELS.begin());
    if (r > rank) { rank = r; best = static_cast<int>(i); }
  }
  return best;
}

// ---------------------------------------------------------------- sound forms and exports

inline Bytes quantize_s16(std::span<const double> samples) {
  Bytes out;
  out.reserve(samples.size() * 2);
  for (double x : samples) {
    double v = std::max(-1.0, std::min(1.0, x));
    auto q = static_cast<std::int16_t>(std::floor(v * 32767.0 + 0.5));
    auto u = static_cast<std::uint16_t>(q);
    out.push_back(static_cast<std::uint8_t>(u & 0xFF));
    out.push_back(static_cast<std::uint8_t>(u >> 8));
  }
  return out;
}

inline std::int16_t s16_at(std::span<const std::uint8_t> pcm, std::size_t i) {
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(pcm[2 * i] | (pcm[2 * i + 1] << 8)));
}

namespace detail {
inline void put_le(Bytes& b, std::uint32_t v, int n) { for (int i = 0; i < n; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
inline void put_tag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }
inline Bytes pnm(const char* magic, std::span<const std::uint8_t> body, int w, int h) {
  std::string hdr = std::string(magic) + "\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
  Bytes out(hdr.begin(), hdr.end());
  out.insert(out.end(), body.begin(), body.end());
  return out;
}
}  // namespace detail

inline Bytes wav_s16(std::span<const std::uint8_t> pcm, std::uint32_t rate, std::uint32_t channels) {
  Bytes b;
  auto n = static_cast<std::uint32_t>(pcm.size());
  detail::put_tag(b, "RIFF"); detail::put_le(b, 36 + n, 4); detail::put_tag(b, "WAVE"); detail::put_tag(b, "fmt ");
  detail::put_le(b, 16, 4); detail::put_le(b, 1, 2); detail::put_le(b, channels, 2); detail::put_le(b, rate, 4);
  detail::put_le(b, rate * channels * 2, 4); detail::put_le(b, channels * 2, 2); detail::put_le(b, 16, 2);
  detail::put_tag(b, "data"); detail::put_le(b, n, 4);
  b.insert(b.end(), pcm.begin(), pcm.end());
  return b;
}
inline Bytes ppm_rgb8(std::span<const std::uint8_t> rgb, int w, int h) { return detail::pnm("P6", rgb, w, h); }
inline Bytes pgm_u8(std::span<const std::uint8_t> gray, int w, int h) { return detail::pnm("P5", gray, w, h); }

inline constexpr std::string_view METER = "superstack-bs1770/1";
using Biquad = std::array<double, 5>;  // b0, b1, b2, a1, a2

// ITU-R BS.1770-4 K-weighting: the standard's 48 kHz table as printed; other rates
// derive the same filters (the derivation libebur128 uses).
inline std::array<Biquad, 2> k_weighting(std::int64_t rate) {
  if (rate == 48000)
    return {Biquad{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585},
            Biquad{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621}};
  const double fs = static_cast<double>(rate);
  double k = std::tan(std::numbers::pi * 1681.974450955533 / fs);
  double q = 0.7071752369554196;
  double vh = std::pow(10.0, 3.999843853973347 / 20.0);
  double vb = std::pow(vh, 0.4996667741545416);
  double a0 = 1.0 + k / q + k * k;
  Biquad shelf{(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
               2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
  k = std::tan(std::numbers::pi * 38.13547087602444 / fs);
  q = 0.5003270373238773;
  a0 = 1.0 + k / q + k * k;
  Biquad high{1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
  return {shelf, high};
}

namespace detail {
inline std::vector<double> biquad(const std::vector<double>& x, const Biquad& c) {
  std::vector<double> y(x.size());
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    double v = x[i];
    double o = c[0] * v + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2;
    x2 = x1; x1 = v; y2 = y1; y1 = o; y[i] = o;
  }
  return y;
}
inline double loud(double p) { return p > 0 ? -0.691 + 10.0 * std::log10(p) : -INFINITY; }
}  // namespace detail

// BS.1770 integrated loudness, 400 ms blocks at 100 ms hops, gates -70 LUFS and -10 LU.
// Interleaved samples; nullopt when shorter than a block, silent, or the rate is unsupported.
inline std::optional<double> integrated_lufs(std::span<const double> samples, std::int64_t rate, int channels = 1) {
  if ((channels != 1 && channels != 2) || !is_sample_rate(rate) || rate % 10) return std::nullopt;
  auto filt = k_weighting(rate);
  std::vector<std::vector<double>> chans;
  for (int c = 0; c < channels; ++c) {
    std::vector<double> xs;
    for (std::size_t i = static_cast<std::size_t>(c); i < samples.size(); i += static_cast<std::size_t>(channels)) xs.push_back(samples[i]);
    chans.push_back(detail::biquad(detail::biquad(xs, filt[0]), filt[1]));
  }
  std::size_t n = chans[0].size(), block = static_cast<std::size_t>(rate * 4 / 10), hop = static_cast<std::size_t>(rate / 10);
  std::vector<double> powers;
  for (std::size_t i = 0; i + block <= n; i += hop) {
    double p = 0;
    for (const auto& z : chans) {
      double acc = 0;
      for (std::size_t j = i; j < i + block; ++j) acc += z[j] * z[j];
      p += acc / static_cast<double>(block);
    }
    powers.push_back(p);
  }
  std::vector<double> gated;
  for (double p : powers) if (detail::loud(p) > -70.0) gated.push_back(p);
  if (gated.empty()) return std::nullopt;
  double acc = 0;
  for (double p : gated) acc += p;
  double rel = detail::loud(acc / static_cast<double>(gated.size())) - 10.0;
  acc = 0;
  std::size_t count = 0;
  for (double p : gated) if (detail::loud(p) > rel) { acc += p; ++count; }
  return detail::loud(acc / static_cast<double>(count));
}

inline std::optional<double> peak_dbfs(std::span<const double> samples) {
  double peak = 0;
  for (double v : samples) peak = std::max(peak, std::abs(v));
  if (peak > 0) return 20.0 * std::log10(peak);
  return std::nullopt;
}

struct LoudnessTarget { double integrated_lufs, tolerance_lu, peak_max; bool ceiling_only; };
inline std::optional<LoudnessTarget> loudness_target(std::string_view cls) {
  if (cls == "speech") return LoudnessTarget{-16.0, 1.0, -1.5, false};
  if (cls == "music") return LoudnessTarget{-14.0, 1.0, -1.0, false};
  if (cls == "interactive") return LoudnessTarget{-18.0, 0.0, -1.0, true};
  return std::nullopt;
}
// v0 reads sample peak, a lower bound on true peak, so a pass is necessary only.
inline std::string loudness_check(std::string_view cls, std::optional<double> lufs, std::optional<double> peak) {
  auto t = loudness_target(cls);
  if (!t || !lufs || !peak) return "unverifiable";
  bool ok = t->ceiling_only ? (*lufs <= t->integrated_lufs && *peak <= t->peak_max)
                            : (std::abs(*lufs - t->integrated_lufs) <= t->tolerance_lu && *peak <= t->peak_max);
  return ok ? "verified" : "refuted";
}

// ---------------------------------------------------------------- reconcile

inline std::string identity(std::string_view ref_sha, std::string_view cand_sha) { return ref_sha == cand_sha ? "MATCH" : "DRIFT"; }

namespace detail {
inline Value block(const std::string& rs, const std::string& cs, std::string verdict, Value metrics, Value bounds,
                   std::string reason = {}) {
  Value tol;
  tol.set("verdict", std::move(verdict));
  tol.set("metrics", std::move(metrics));
  tol.set("bounds", std::move(bounds));
  if (!reason.empty()) tol.set("reason", std::move(reason));
  Value out;
  out.set("identity", identity(rs, cs));
  out.set("tolerance", std::move(tol));
  return out;
}
}  // namespace detail

struct PcmTolerance { std::int64_t max_abs_lsb = 2; double min_snr_db = 60.0; };
struct Rgb8Tolerance { double mean_abs_max = 1.0; };

inline Value reconcile_pcm_s16(std::span<const std::uint8_t> ref, std::span<const std::uint8_t> cand, PcmTolerance tol = {}) {
  std::string rs = sha256_hex(ref), cs = sha256_hex(cand);
  Value bounds;
  bounds.set("max_abs_lsb", static_cast<long long>(tol.max_abs_lsb));
  bounds.set("min_snr_db", tol.min_snr_db);
  if (ref.size() != cand.size() || ref.size() % 2) return detail::block(rs, cs, "unverifiable", Value(Object{}), bounds, "length differs from the reference");
  std::size_t n = ref.size() / 2;
  double sig = 0, err = 0;
  long long max_d = 0, exact = 0;
  for (std::size_t i = 0; i < n; ++i) {
    long long r = s16_at(ref, i), d = s16_at(cand, i) - r;
    sig += static_cast<double>(r * r);
    err += static_cast<double>(d * d);
    max_d = std::max(max_d, d < 0 ? -d : d);
    exact += d == 0;
  }
  std::optional<double> snr;
  if (err != 0) snr = sig > 0 ? 10.0 * std::log10(sig / err) : -INFINITY;
  Value metrics;
  metrics.set("max_abs_lsb", max_d);
  metrics.set("exact_frac", n ? static_cast<double>(exact) / static_cast<double>(n) : 1.0);
  metrics.set("snr_db", snr && std::isfinite(*snr) ? Value(round6(*snr)) : Value(nullptr));
  bool ok = max_d <= tol.max_abs_lsb && (!snr || *snr >= tol.min_snr_db);
  return detail::block(rs, cs, ok ? "verified" : "refuted", std::move(metrics), std::move(bounds));
}

inline Value reconcile_rgb8(std::span<const std::uint8_t> ref, std::span<const std::uint8_t> cand, Rgb8Tolerance tol = {}) {
  std::string rs = sha256_hex(ref), cs = sha256_hex(cand);
  Value bounds;
  bounds.set("mean_abs_max", tol.mean_abs_max);
  if (ref.size() != cand.size() || ref.size() % 3 || ref.empty()) return detail::block(rs, cs, "unverifiable", Value(Object{}), bounds, "size differs from the reference");
  long long total = 0, max_d = 0, exact_px = 0;
  std::size_t px = ref.size() / 3;
  for (std::size_t p = 0; p < px; ++p) {
    long long worst = 0;
    for (int c = 0; c < 3; ++c) {
      long long d = std::abs(static_cast<int>(cand[3 * p + c]) - static_cast<int>(ref[3 * p + c]));
      total += d;
      worst = std::max(worst, d);
    }
    max_d = std::max(max_d, worst);
    exact_px += worst == 0;
  }
  double mean = static_cast<double>(total) / static_cast<double>(ref.size());
  Value metrics;
  metrics.set("mean_abs", mean);
  metrics.set("max_abs", max_d);
  metrics.set("exact_pixels_frac", static_cast<double>(exact_px) / static_cast<double>(px));
  return detail::block(rs, cs, mean <= tol.mean_abs_max ? "verified" : "refuted", std::move(metrics), std::move(bounds));
}

inline float f32_at(std::span<const std::uint8_t> b, std::size_t i) {
  std::uint32_t u = std::uint32_t{b[4 * i]} | (std::uint32_t{b[4 * i + 1]} << 8) | (std::uint32_t{b[4 * i + 2]} << 16) |
                    (std::uint32_t{b[4 * i + 3]} << 24);
  return std::bit_cast<float>(u);
}

// RMSE of little-endian float32 arrays over mask != 0 (all when mask is empty).
inline std::optional<double> f32_rmse(std::span<const std::uint8_t> ref, std::span<const std::uint8_t> cand,
                                      std::optional<std::span<const std::uint8_t>> mask = std::nullopt) {
  double acc = 0;
  std::size_t n = 0;
  for (std::size_t i = 0; i < ref.size() / 4; ++i) {
    if (mask && !(*mask)[i]) continue;
    double d = static_cast<double>(f32_at(cand, i)) - static_cast<double>(f32_at(ref, i));
    acc += d * d;
    ++n;
  }
  if (!n) return std::nullopt;
  return std::sqrt(acc / static_cast<double>(n));
}

// ---------------------------------------------------------------- receipt

namespace detail {
inline std::optional<std::int64_t> as_int(const Value* v) {
  if (!v) return std::nullopt;
  if (auto p = std::get_if<std::int64_t>(&v->v)) return std::abs(*p) <= MAX_SAFE_INTEGER ? std::optional(*p) : std::nullopt;
  if (auto d = std::get_if<double>(&v->v))
    if (std::isfinite(*d) && *d == std::floor(*d) && std::abs(*d) <= static_cast<double>(MAX_SAFE_INTEGER)) return static_cast<std::int64_t>(*d);
  return std::nullopt;
}
inline bool is_text(const Value* v) { return v && v->is_string() && !v->str().empty(); }
inline bool is_str(const Value* v, std::string_view s) { return v && v->is_string() && v->str() == s; }
inline bool is_bool(const Value* v, bool b) { return v && v->is_bool() && std::get<bool>(v->v) == b; }
inline bool is_hex64(const Value* v) {
  if (!v || !v->is_string() || v->str().size() != 64) return false;
  return std::all_of(v->str().begin(), v->str().end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
inline bool blank(const std::string& s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}
}  // namespace detail

// Return a copy with receipt_sha256 set over the canonical body.
inline Value seal(Value body) {
  body.erase("receipt_sha256");
  std::string h = canonical_sha256(body);
  body.set("receipt_sha256", h);
  return body;
}

struct ReceiptArgs {
  std::string producer, version, backend;
  Value scene, media;
  std::span<const std::uint8_t> content;
  Value outputs = Value(Object{});
  Value reference = nullptr;
  Value reconcile = nullptr;
  std::vector<std::string> does_not_prove;
  std::string seed_rule = std::string(SEED_RULE);
};

inline Value make_receipt(const ReceiptArgs& a) {
  const Value* seed = a.scene.find("seed");
  const Value* t = a.scene.find("t_flicks");
  Value rec;
  rec.set("schema", std::string(RECEIPT_SCHEMA));
  Value prod;
  prod.set("name", a.producer);
  prod.set("version", a.version);
  rec.set("producer", prod);
  rec.set("backend", a.backend);
  rec.set("scene_sha256", canonical_sha256(a.scene));
  rec.set("seed", seed ? *seed : Value(nullptr));
  rec.set("seed_rule", a.seed_rule);
  rec.set("seed_u32", seed && seed->is_string() && a.seed_rule == SEED_RULE ? Value(static_cast<long long>(xmur3(seed->str()))) : Value(nullptr));
  Value time;
  time.set("base", "flicks");
  time.set("per_second", static_cast<long long>(FLICKS_PER_SECOND));
  time.set("t", t ? *t : Value(0));
  rec.set("time", time);
  rec.set("media", a.media);
  rec.set("content_sha256", sha256_hex(a.content));
  rec.set("outputs", a.outputs.is_object() ? a.outputs : Value(Object{}));
  if (a.reconcile.is_null()) rec.set("reconcile", nullptr);
  else {
    Value rc;
    rc.set("reference", a.reference);
    for (const auto& m : a.reconcile.obj()) rc.set(m.key, m.value);
    rec.set("reconcile", rc);
  }
  Array dnp;
  for (const auto& s : a.does_not_prove) dnp.emplace_back(s);
  rec.set("does_not_prove", Value(std::move(dnp)));
  return seal(std::move(rec));
}

namespace detail {
inline void audio_errors(const Value& media, std::vector<std::string>& errs) {
  auto rate = as_int(media.find("rate"));
  bool rate_ok = rate && is_sample_rate(*rate);
  if (!rate_ok) errs.push_back("audio:rate");
  auto ch = as_int(media.find("channels"));
  if (!is_str(media.find("format"), "s16le") || !ch || (*ch != 1 && *ch != 2)) errs.push_back("audio:format");
  if (rate_ok) {
    auto frames = as_int(media.find("frames"));
    auto dur = as_int(media.find("duration_flicks"));
    if (!frames || !dur || *dur != *frames * flicks_per_sample(*rate)) errs.push_back("audio:duration");
  }
  const Value* access = media.find("access");
  if (!access || !access->is_object() || !is_bool(access->find("autoplay"), false)) errs.push_back("audio:autoplay");
  else if (!is_str(access->find("reduced_sound"), "silent") && !is_str(access->find("reduced_sound"), "still-frame"))
    errs.push_back("audio:reduced_sound");
  if (is_str(media.find("content"), "speech") && access && access->is_object()) {
    if (!is_text(access->find("captions"))) errs.push_back("audio:captions");
    if (!is_bool(access->find("transcript"), true)) errs.push_back("audio:transcript");
  }
  const Value* nar = media.find("narration");
  if (nar && !nar->is_null() && !nar->is_object()) errs.push_back("narration:type");
  else if (nar && nar->is_object()) {
    for (const char* k : {"backend", "model", "voice", "text_sha256"})
      if (!is_text(nar->find(k))) errs.push_back(std::string("narration:") + k);
    if (!is_bool(nar->find("reference"), false)) errs.push_back("narration:reference");
    if (is_bool(nar->find("hosted"), true) && !is_text(nar->find("snapshot")) && !is_bool(nar->find("reproducible"), false))
      errs.push_back("narration:reproducible");
  }
}
}  // namespace detail

// Error codes, sorted; empty means well formed with an intact seal. Re-renders nothing.
inline std::vector<std::string> verify_receipt(const Value& rec) {
  using namespace detail;
  if (!rec.is_object()) return {"type"};
  static constexpr const char* required[] = {"schema", "producer", "backend", "scene_sha256", "seed", "seed_rule", "seed_u32",
                                             "time", "media", "content_sha256", "outputs", "reconcile", "does_not_prove",
                                             "receipt_sha256"};
  std::vector<std::string> errs;
  for (const char* k : required) if (!rec.find(k)) errs.push_back(std::string("missing:") + k);
  if (!errs.empty()) { std::sort(errs.begin(), errs.end()); return errs; }
  if (!is_str(rec.find("schema"), RECEIPT_SCHEMA)) errs.push_back("schema");
  const Value& dnp = rec.at("does_not_prove");
  bool dnp_ok = dnp.is_array() && !dnp.arr().empty();
  if (dnp_ok) for (const auto& s : dnp.arr()) if (!s.is_string() || blank(s.str())) dnp_ok = false;
  if (!dnp_ok) errs.push_back("does_not_prove");
  const Value& t = rec.at("time");
  auto tt = as_int(t.find("t"));
  auto ps = as_int(t.find("per_second"));
  if (!t.is_object() || !is_str(t.find("base"), "flicks") || !ps || *ps != FLICKS_PER_SECOND || !tt || *tt < 0) errs.push_back("time");
  for (const char* k : {"scene_sha256", "content_sha256", "receipt_sha256"})
    if (!is_hex64(rec.find(k))) errs.push_back(std::string("hex:") + k);
  const Value& rule = rec.at("seed_rule");
  bool known = rule.is_string() && std::find(SEED_RULES.begin(), SEED_RULES.end(), rule.str()) != SEED_RULES.end();
  if (!known) errs.push_back("seed_rule");
  else if (rule.str() == SEED_RULE && !rec.at("seed").is_null()) {
    bool ok = rec.at("seed").is_string();
    if (ok) {
      try {
        auto u = as_int(rec.find("seed_u32"));
        ok = u && *u == static_cast<std::int64_t>(xmur3(rec.at("seed").str()));
      } catch (const error&) { ok = false; }
    }
    if (!ok) errs.push_back("seed_u32");
  }
  const Value& rc = rec.at("reconcile");
  if (!rc.is_null()) {
    const Value* ref = rc.is_object() ? rc.find("reference") : nullptr;
    const Value* tol = rc.is_object() ? rc.find("tolerance") : nullptr;
    if (!ref || !ref->is_object() || !(ref->find("content_sha256") && ref->at("content_sha256").is_string())) errs.push_back("reconcile:reference");
    else {
      const Value& cs = rec.at("content_sha256");
      std::string want = cs.is_string() ? identity(ref->at("content_sha256").str(), cs.str()) : "DRIFT";
      if (!is_str(rc.find("identity"), want)) errs.push_back("reconcile:identity");
    }
    const Value* verdict = tol && tol->is_object() ? tol->find("verdict") : nullptr;
    if (!is_str(verdict, "verified") && !is_str(verdict, "refuted") && !is_str(verdict, "unverifiable")) errs.push_back("reconcile:verdict");
    else if (is_str(verdict, "unverifiable") && !is_text(tol->find("reason"))) errs.push_back("reconcile:reason");
  }
  const Value& media = rec.at("media");
  const Value* kind = media.is_object() ? media.find("kind") : nullptr;
  if (!is_str(kind, "image") && !is_str(kind, "audio") && !is_str(kind, "video") && !is_str(kind, "document")) errs.push_back("media:kind");
  else if (is_str(kind, "audio")) audio_errors(media, errs);
  if (errs.empty()) {
    Value body = rec;
    body.erase("receipt_sha256");
    if (canonical_sha256(body) != rec.at("receipt_sha256").str()) errs.push_back("seal");
  }
  std::sort(errs.begin(), errs.end());
  errs.erase(std::unique(errs.begin(), errs.end()), errs.end());
  return errs;
}

// ---------------------------------------------------------------- scenes

inline std::vector<std::string> validate_scene(const Value& scene) {
  using namespace detail;
  if (!scene.is_object()) return {"type"};
  const Value* kind = scene.find("kind");
  bool pixels = is_str(kind, "superstack.scene/1"), sound = is_str(kind, "superstack.sound/1");
  if (!pixels && !sound) return {"kind"};
  std::vector<std::string> errs;
  if (const Value* s = scene.find("seed"); s && !s->is_string()) errs.push_back("seed");
  const Value* t = scene.find("t_flicks");
  auto tv = t ? as_int(t) : std::optional<std::int64_t>(0);
  if (!tv || *tv < 0) errs.push_back("t_flicks");
  if (pixels) {
    const Value& f = scene.at("frame");
    auto w = as_int(f.find("width")), h = as_int(f.find("height"));
    if (!f.is_object() || !w || !h || *w <= 0 || *h <= 0) errs.push_back("frame");
  } else {
    auto r = as_int(scene.find("rate"));
    if (!r || !is_sample_rate(*r)) errs.push_back("rate");
    auto c = as_int(scene.find("channels"));
    if (!c || (*c != 1 && *c != 2)) errs.push_back("channels");
    auto d = as_int(scene.find("duration_samples"));
    if (!d || *d <= 0) errs.push_back("duration_samples");
  }
  std::sort(errs.begin(), errs.end());
  return errs;
}

}  // namespace superstack
