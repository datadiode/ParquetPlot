/*
    Lib String
    MIT License, Copyright © 2026 PuqiAR (im@puqiar.top)
*/

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>

#include <format>
#include <ostream>
#include "vector.hpp"

namespace PuqiAR::LibString {
class StringUtils {
public:
  static bool is_pure_ascii(const char *data, size_t n) noexcept {
    for (size_t i = 0; i < n; ++i) {
      if (static_cast<unsigned char>(data[i]) >= 128)
        return false;
    }
    return true;
  }

  static bool is_pure_ascii(const char32_t *data, size_t n) noexcept {
    for (size_t i = 0; i < n; ++i) {
      if (data[i] >= 128)
        return false;
    }
    return true;
  }

  static size_t utf8_decode_one(const char *s, size_t n, char32_t &out) {
    unsigned char c0 = static_cast<unsigned char>(s[0]);

    if (c0 < 0x80) {
      out = c0;
      return 1;
    }

    if ((c0 >> 5) == 0x6 && n >= 2) {
      unsigned char c1 = static_cast<unsigned char>(s[1]);
      out = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
      return 2;
    }

    if ((c0 >> 4) == 0xE && n >= 3) {
      unsigned char c1 = static_cast<unsigned char>(s[1]);
      unsigned char c2 = static_cast<unsigned char>(s[2]);
      out = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
      return 3;
    }

    if ((c0 >> 3) == 0x1E && n >= 4) {
      unsigned char c1 = static_cast<unsigned char>(s[1]);
      unsigned char c2 = static_cast<unsigned char>(s[2]);
      unsigned char c3 = static_cast<unsigned char>(s[3]);
      out = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) |
            (c3 & 0x3F);
      return 4;
    }

    out = 0xFFFD;
    return 1;
  }
};

class String {
public:
  using u32 = char32_t;
  static constexpr uint8_t SSO_SIZE = 23;

  enum class Mode : uint8_t {
    ASCII_SSO, // ASCII
    ASCII_HEP, // ASCII heap
    UTF32_HEP, // UTF32 heap
  };

private:
  // In heap modes the length is solely given by the size of the active vector;
  // _sso_length is only meaningful in ASCII_SSO mode. mode and _sso_length share
  // the last byte, which lies beyond the storage of either vector.
  union {
    less::vector<unsigned char> ascii;
    less::vector<u32> utf32;
    struct
    {
        unsigned char sso[SSO_SIZE]; // non null terminate
        Mode mode : 2;
        unsigned char _sso_length : 6;
    };
  };

  void set_sso_length(size_t n) noexcept {
    assert(n <= SSO_SIZE);
    _sso_length = static_cast<unsigned char>(n);
  }

  void copyfrom(const String &other) {
    destroy();
    mode = other.mode;
    _sso_length = other._sso_length;

    if (mode == Mode::ASCII_SSO) {
      memcpy(sso, other.sso, sizeof(unsigned char) * _sso_length);
    } else if (mode == Mode::ASCII_HEP) {
      new (&ascii) less::vector<unsigned char>(other.ascii);
    } else {
      new (&utf32) less::vector<u32>(other.utf32);
    }
  }

  void movefrom(String &&other) noexcept {
    destroy();

    mode = other.mode;
    _sso_length = other._sso_length;

    switch (mode) {
    case Mode::ASCII_SSO:
      std::memcpy(sso, other.sso, other._sso_length);
      break;

    case Mode::ASCII_HEP:
      new (&ascii) less::vector<unsigned char>(std::move(other.ascii));
      break;

    case Mode::UTF32_HEP:
      new (&utf32) less::vector<u32>(std::move(other.utf32));
      break;
    }

    other.mode = Mode::ASCII_SSO;
    other._sso_length = 0;
  }

  void destroy() noexcept {
    if (mode == Mode::ASCII_SSO) {
      // pass
    }
    if (mode == Mode::ASCII_HEP) {
      ascii.~vector();
    }
    if (mode == Mode::UTF32_HEP) {
      utf32.~vector();
    }
  }

  void ensure_utf32() {
    if (mode == Mode::UTF32_HEP)
      return;

    less::vector<u32> tmp;
    tmp.reserve(size());

    if (mode == Mode::ASCII_SSO) {
      for (size_t i = 0; i < _sso_length; ++i)
        tmp.push_back(static_cast<u32>(sso[i]));
    } else // ASCII_HEP
    {
      for (unsigned char c : ascii)
        tmp.push_back(static_cast<u32>(c));
    }

    destroy();
    mode = Mode::UTF32_HEP;
    new (&utf32) less::vector<u32>(std::move(tmp));
  }

  void promote_sso_ascii_to_heap() noexcept {
    assert(mode == Mode::ASCII_SSO &&
           "promote_sso_ascii_to_heap: mode is not ascii sso");

    less::vector<unsigned char> tmp;
    tmp.reserve(_sso_length);
    for (size_t i = 0; i < _sso_length; ++i)
      tmp.push_back(sso[i]);

    mode = Mode::ASCII_HEP;
    new (&ascii) less::vector<unsigned char>(std::move(tmp));
  }

  void init(const char *data) {
    assert(data);
    size_t n = std::strlen(data);
    init(data, n);
  }

  void init(const char *data, size_t n) {
    destroy();

    _sso_length = 0;

    // ASCII 快路径
    if (n <= SSO_SIZE && StringUtils::is_pure_ascii(data, n)) {
      mode = Mode::ASCII_SSO;
      std::memcpy(sso, data, n);
      set_sso_length(n);
      return;
    }

    if (StringUtils::is_pure_ascii(data, n)) {
      mode = Mode::ASCII_HEP;
      new (&ascii) less::vector<unsigned char>(data, data + n);
      return;
    }

    // UTF-8 decode
    mode = Mode::UTF32_HEP;
    new (&utf32) less::vector<u32>();
    utf32.reserve(n);

    for (size_t i = 0; i < n;) {
      u32 cp;
      size_t step = StringUtils::utf8_decode_one(data + i, n - i, cp);
      utf32.push_back(cp);
      i += step;
    }

    utf32.shrink_to_fit();
  }

  void init(const u32 *data) {
    assert(data);
    size_t n = 0;
    while (data[n] != 0)
      ++n;
    init(data, n);
  }

  void init(const u32 *data, size_t n) {
    destroy();

    _sso_length = 0;

    if (n <= SSO_SIZE && StringUtils::is_pure_ascii(data, n)) {
      mode = Mode::ASCII_SSO;
      for (size_t i = 0; i < n; ++i)
        sso[i] = static_cast<unsigned char>(data[i]);
      set_sso_length(n);
      return;
    }

    if (StringUtils::is_pure_ascii(data, n)) {
      mode = Mode::ASCII_HEP;
      new (&ascii) less::vector<unsigned char>();
      ascii.reserve(n);
      for (size_t i = 0; i < n; ++i)
        ascii.push_back(static_cast<unsigned char>(data[i]));
      return;
    }

    mode = Mode::UTF32_HEP;
    new (&utf32) less::vector<u32>();
    utf32.assign(data, data + n);
  }

public:
  size_t size() const noexcept {
    if (mode == Mode::ASCII_SSO)
      return _sso_length;
    if (mode == Mode::ASCII_HEP)
      return ascii.size();
    return utf32.size();
  }
  size_t length() const noexcept { return size(); }

  bool empty() const noexcept { return size() == 0; }
  void reserve(size_t n) {
    if (mode == Mode::ASCII_HEP)
      ascii.reserve(n);
    else if (mode == Mode::UTF32_HEP)
      utf32.reserve(n);
  }

  void clear() noexcept {
    if (mode == Mode::ASCII_SSO) {
      _sso_length = 0;
    } else if (mode == Mode::ASCII_HEP) {
      ascii.clear();
    } else {
      utf32.clear();
    }
  }

  void shrink_to_fit() noexcept {
    if (mode == Mode::ASCII_HEP) {
      ascii.shrink_to_fit();
    } else if (mode == Mode::UTF32_HEP) {
      utf32.shrink_to_fit();
    }
  }

  ~String() noexcept { destroy(); }
  String() noexcept {
    mode = Mode::ASCII_SSO;
    _sso_length = 0;
  }
  String(const String &other) noexcept : String() { copyfrom(other); }
  String(String &&other) noexcept : String() { movefrom(std::move(other)); }
  String(const char *str) : String() { init(str); }
  String(const char32_t *str) : String() { init(str); }
  String(char32_t c) : String() {
    init("");
    push_back(c);
  }
  String(char c) : String() {
    init("");
    push_back(static_cast<char32_t>(c));
  }
  String(const std::string &s) : String() { init(s.data(), s.size()); }

  String &operator=(const String &other) {
    if (this != &other) {
      destroy();
      copyfrom(other);
    }
    return *this;
  }

  String &operator=(String &&other) noexcept {
    if (this != &other)
      movefrom(std::move(other));
    return *this;
  }

  String &operator+=(const String &rhs) {
    if (rhs.size() == 0)
      return *this;

    // 两边都是 ASCII
    bool this_ascii = (mode == Mode::ASCII_SSO || mode == Mode::ASCII_HEP);
    bool rhs_ascii =
        (rhs.mode == Mode::ASCII_SSO || rhs.mode == Mode::ASCII_HEP);

    if (this_ascii && rhs_ascii) {
      size_t newlen = size() + rhs.size();

      // SSO 可容纳
      if (mode == Mode::ASCII_SSO && newlen <= SSO_SIZE) {
        if (rhs.mode == Mode::ASCII_SSO)
          std::memcpy(sso + size(), rhs.sso, rhs.size());
        else
          std::memcpy(sso + size(), rhs.ascii.data(), rhs.size());

        set_sso_length(newlen);
        return *this;
      }

      if (mode == Mode::ASCII_SSO)
        promote_sso_ascii_to_heap();

      // 追加
      if (rhs.mode == Mode::ASCII_SSO)
        ascii.insert(ascii.end(), rhs.sso, rhs.sso + rhs.size());
      else
        ascii.insert(ascii.end(), rhs.ascii.begin(), rhs.ascii.end());

      return *this;
    }

    // 必须 UTF32

    if (mode != Mode::UTF32_HEP) {
      less::vector<u32> tmp;
      tmp.reserve(size() + rhs.size());

      if (mode == Mode::ASCII_SSO) {
        for (size_t i = 0, n = size(); i < n; ++i)
          tmp.push_back(static_cast<u32>(sso[i]));
      } else // ASCII_HEP
      {
        for (unsigned char c : ascii)
          tmp.push_back(static_cast<u32>(c));
      }

      destroy();
      mode = Mode::UTF32_HEP;
      new (&utf32) less::vector<u32>(std::move(tmp));
    }

    if (rhs.mode == Mode::UTF32_HEP) {
      utf32.insert(utf32.end(), rhs.utf32.begin(), rhs.utf32.end());
    } else if (rhs.mode == Mode::ASCII_SSO) {
      for (size_t i = 0, n = rhs.size(); i < n; ++i)
        utf32.push_back(static_cast<u32>(rhs.sso[i]));
    } else // ASCII_HEP
    {
      for (unsigned char c : rhs.ascii)
        utf32.push_back(static_cast<u32>(c));
    }

    return *this;
  }

  String &operator+=(const char *utf8) {
    String tmp(utf8);
    return (*this += tmp);
  }

  friend String operator+(String lhs, const String &rhs) {
    lhs += rhs;
    return lhs;
  }

  void push_back(u32 cp) {
    if (cp < 128) {
      if (mode == Mode::ASCII_SSO && size() < SSO_SIZE) {
        sso[_sso_length] = static_cast<unsigned char>(cp);
        set_sso_length(_sso_length + 1u);
        return;
      }

      if (mode == Mode::ASCII_SSO)
        promote_sso_ascii_to_heap();

      if (mode == Mode::ASCII_HEP) {
        ascii.push_back(static_cast<unsigned char>(cp));
        return;
      }
    }

    ensure_utf32();
    utf32.push_back(cp);
  }

  void pop_back() {
    assert(size() > 0);

    if (mode == Mode::ASCII_SSO) {
      set_sso_length(_sso_length - 1u);
      return;
    }

    if (mode == Mode::ASCII_HEP) {
      ascii.pop_back();
      return;
    }

    utf32.pop_back();
  }

  String &append(const char *utf8) {
    String tmp(utf8);
    *this += tmp;
    return *this;
  }

  String &append(const char32_t *u32str) {
    String tmp(u32str);
    *this += tmp;
    return *this;
  }

  String &append(size_t count, u32 cp) {
    for (size_t i = 0; i < count; ++i)
      push_back(cp);
    return *this;
  }

  void resize(size_t new_size, u32 fill = 0) {
    if (new_size <= size()) {
      erase(new_size);
      return;
    }

    append(new_size - size(), fill);
  }

  u32 front() const {
    assert(size() > 0);
    return (*this)[0];
  }

  u32 back() const {
    assert(size() > 0);
    return (*this)[size() - 1];
  }

  std::string toStdString() const {
    std::string out;

    if (mode == Mode::ASCII_SSO) {
      out.assign(reinterpret_cast<const char *>(sso), size());
      return out;
    }

    if (mode == Mode::ASCII_HEP) {
      out.assign(ascii.begin(), ascii.end());
      return out;
    }

    // UTF32_HEP -> UTF-8 encode
    for (u32 cp : utf32) {
      if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
      } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
      // 非法码点
    }

    return out;
  }
  friend std::ostream &operator<<(std::ostream &os, const String &s) {
    return os << s.toStdString();
  }

  friend bool operator==(const String &a, const String &b) noexcept {
    if (a.size() != b.size())
      return false;

    // 同模式
    if (a.mode == b.mode) {
      if (a.mode == Mode::ASCII_SSO)
        return std::memcmp(a.sso, b.sso, a.size()) == 0;

      if (a.mode == Mode::ASCII_HEP)
        return a.ascii == b.ascii;

      return a.utf32 == b.utf32;
    }

    // 不同模式ASCII / UTF32
    const bool a_ascii =
        (a.mode == Mode::ASCII_SSO || a.mode == Mode::ASCII_HEP);
    const bool b_ascii =
        (b.mode == Mode::ASCII_SSO || b.mode == Mode::ASCII_HEP);

    if (a_ascii && b_ascii) {
      if (a.mode == Mode::ASCII_SSO)
        return std::memcmp(a.sso, b.ascii.data(), a.size()) == 0;
      else
        return std::memcmp(a.ascii.data(), b.sso, a.size()) == 0;
    }

    // ASCII / UTF32
    const String &ascii_str = a_ascii ? a : b;
    const String &utf32_str = a_ascii ? b : a;

    if (ascii_str.mode == Mode::ASCII_SSO) {
      for (size_t i = 0, n = ascii_str.size(); i < n; ++i)
        if (static_cast<u32>(ascii_str.sso[i]) != utf32_str.utf32[i])
          return false;
    } else {
      for (size_t i = 0, n = ascii_str.size(); i < n; ++i)
        if (static_cast<u32>(ascii_str.ascii[i]) != utf32_str.utf32[i])
          return false;
    }

    return true;
  }

  friend bool operator!=(const String &a, const String &b) noexcept {
    return !(a == b);
  }
  // std::hash
  friend struct std::hash<String>;

  // read only
  u32 operator[](size_t i) const {
    assert(i < size());

    if (mode == Mode::ASCII_SSO)
      return static_cast<u32>(sso[i]);
    if (mode == Mode::ASCII_HEP)
      return static_cast<u32>(ascii[i]);
    return utf32[i];
  }
  u32 at(size_t i) const {
    if (i >= size())
      throw std::out_of_range("String::at");
    return (*this)[i];
  }

  bool starts_with(const String &prefix) const {
    if (prefix.size() > size())
      return false;

    for (size_t i = 0, n = prefix.size(); i < n; ++i)
      if ((*this)[i] != prefix[i])
        return false;

    return true;
  }

  bool ends_with(const String &suffix) const {
    if (suffix.size() > size())
      return false;

    size_t offset = size() - suffix.size();

    for (size_t i = 0, n = suffix.size(); i < n; ++i)
      if ((*this)[offset + i] != suffix[i])
        return false;

    return true;
  }

  bool contains(u32 cp) const {
    if (mode == Mode::ASCII_SSO) {
      for (size_t i = 0, n = size(); i < n; ++i)
        if (sso[i] == cp)
          return true;
      return false;
    }

    if (mode == Mode::ASCII_HEP) {
      if (cp >= 128)
        return false;
      for (unsigned char c : ascii)
        if (c == cp)
          return true;
      return false;
    }

    for (u32 c : utf32)
      if (c == cp)
        return true;

    return false;
  }

  String substr(size_t pos, size_t count = size_t(-1)) const {
    if (pos >= size())
      return String();

    size_t len = (size() - pos < count) ? (size() - pos) : count;

    String out;

    // ASCII_SSO
    if (mode == Mode::ASCII_SSO) {
      if (len <= SSO_SIZE) {
        out.mode = Mode::ASCII_SSO;
        std::memcpy(out.sso, sso + pos, len);
        out.set_sso_length(len);
      } else {
        out.mode = Mode::ASCII_HEP;
        new (&out.ascii) less::vector<unsigned char>(sso + pos, sso + pos + len);
      }
      return out;
    }

    // ASCII_HEP
    if (mode == Mode::ASCII_HEP) {
      if (len <= SSO_SIZE) {
        out.mode = Mode::ASCII_SSO;
        std::memcpy(out.sso, ascii.data() + pos, len);
        out.set_sso_length(len);
      } else {
        out.mode = Mode::ASCII_HEP;
        new (&out.ascii) less::vector<unsigned char>(ascii.begin() + pos,
                                                    ascii.begin() + pos + len);
      }
      return out;
    }

    // UTF32
    out.mode = Mode::UTF32_HEP;
    new (&out.utf32)
        less::vector<u32>(utf32.begin() + pos, utf32.begin() + pos + len);
    return out;
  }

  String &erase(size_t pos, size_t count = size_t(-1)) {
    if (pos >= size())
      return *this;

    size_t len = (size() - pos < count) ? (size() - pos) : count;

    if (mode == Mode::ASCII_SSO) {
      std::memmove(sso + pos, sso + pos + len, size() - pos - len);
      set_sso_length(size() - len);
      return *this;
    }

    if (mode == Mode::ASCII_HEP) {
      ascii.erase(ascii.begin() + pos, ascii.begin() + pos + len);
      return *this;
    }

    utf32.erase(utf32.begin() + pos, utf32.begin() + pos + len);
    return *this;
  }

  String &insert(size_t pos, const String &other) {
    if (pos > size())
      pos = size();
    if (other.size() == 0)
      return *this;

    bool this_ascii = (mode != Mode::UTF32_HEP);
    bool other_ascii = (other.mode != Mode::UTF32_HEP);

    // ASCII 合并路径
    if (this_ascii && other_ascii) {
      size_t newlen = size() + other.size();

      if (mode == Mode::ASCII_SSO && newlen <= SSO_SIZE) {
        std::memmove(sso + pos + other.size(), sso + pos, size() - pos);

        if (other.mode == Mode::ASCII_SSO)
          std::memcpy(sso + pos, other.sso, other.size());
        else
          std::memcpy(sso + pos, other.ascii.data(), other.size());

        set_sso_length(newlen);
        return *this;
      }

      if (mode == Mode::ASCII_SSO)
        promote_sso_ascii_to_heap();

      if (other.mode == Mode::ASCII_SSO)
        ascii.insert(ascii.begin() + pos, other.sso, other.sso + other.size());
      else
        ascii.insert(ascii.begin() + pos, other.ascii.begin(),
                     other.ascii.end());

      return *this;
    }

    // UTF32 路径
    ensure_utf32();

    if (other.mode == Mode::UTF32_HEP)
      utf32.insert(utf32.begin() + pos, other.utf32.begin(), other.utf32.end());
    else if (other.mode == Mode::ASCII_SSO)
      for (size_t i = 0, n = other.size(); i < n; ++i)
        utf32.insert(utf32.begin() + pos + i, static_cast<u32>(other.sso[i]));
    else
      for (size_t i = 0, n = other.size(); i < n; ++i)
        utf32.insert(utf32.begin() + pos + i, static_cast<u32>(other.ascii[i]));

    return *this;
  }

  int compare(const String &other) const noexcept {
    const size_t this_size = size();
    const size_t other_size = other.size();
    size_t n = (this_size < other_size) ? this_size : other_size;

    for (size_t i = 0; i < n; ++i) {
      u32 a = (*this)[i];
      u32 b = other[i];
      if (a != b)
        return (a < b) ? -1 : 1;
    }

    if (this_size == other_size)
      return 0;
    return (this_size < other_size) ? -1 : 1;
  }

  size_t find(const String &needle, size_t pos = 0) const {
    const size_t this_size = size();
    const size_t needle_size = needle.size();
    if (needle_size == 0)
      return pos <= this_size ? pos : size_t(-1);
    if (needle_size > this_size || pos >= this_size)
      return size_t(-1);

    size_t limit = this_size - needle_size;

    for (size_t i = pos; i <= limit; ++i) {
      size_t j = 0;
      for (; j < needle_size; ++j)
        if ((*this)[i + j] != needle[j])
          break;

      if (j == needle_size)
        return i;
    }

    return size_t(-1);
  }

  size_t rfind(const String &needle) const {
    const size_t this_size = size();
    const size_t needle_size = needle.size();
    if (needle_size == 0)
      return this_size;
    if (needle_size > this_size)
      return size_t(-1);

    for (size_t i = this_size - needle_size + 1; i-- > 0;) {
      size_t j = 0;
      for (; j < needle_size; ++j)
        if ((*this)[i + j] != needle[j])
          break;

      if (j == needle_size)
        return i;
    }

    return size_t(-1);
  }

  String &replace(size_t pos, size_t len, const String &repl) {
    if (pos >= size())
      return *this;

    size_t erase_len = (size() - pos < len) ? (size() - pos) : len;

    // ASCII路径
    bool this_ascii = (mode != Mode::UTF32_HEP);
    bool repl_ascii = (repl.mode != Mode::UTF32_HEP);

    if (this_ascii && repl_ascii) {
      size_t newlen = size() - erase_len + repl.size();

      // SSO容纳
      if (mode == Mode::ASCII_SSO && newlen <= SSO_SIZE) {
        std::memmove(sso + pos + repl.size(), sso + pos + erase_len,
                     size() - pos - erase_len);

        if (repl.mode == Mode::ASCII_SSO)
          std::memcpy(sso + pos, repl.sso, repl.size());
        else
          std::memcpy(sso + pos, repl.ascii.data(), repl.size());

        set_sso_length(newlen);
        return *this;
      }

      if (mode == Mode::ASCII_SSO)
        promote_sso_ascii_to_heap();

      ascii.erase(ascii.begin() + pos, ascii.begin() + pos + erase_len);

      if (repl.mode == Mode::ASCII_SSO)
        ascii.insert(ascii.begin() + pos, repl.sso, repl.sso + repl.size());
      else
        ascii.insert(ascii.begin() + pos, repl.ascii.begin(), repl.ascii.end());

      return *this;
    }

    // UTF32路径
    ensure_utf32();

    utf32.erase(utf32.begin() + pos, utf32.begin() + pos + erase_len);

    if (repl.mode == Mode::UTF32_HEP)
      utf32.insert(utf32.begin() + pos, repl.utf32.begin(), repl.utf32.end());
    else if (repl.mode == Mode::ASCII_SSO)
      for (size_t i = 0, n = repl.size(); i < n; ++i)
        utf32.insert(utf32.begin() + pos + i, static_cast<u32>(repl.sso[i]));
    else
      for (size_t i = 0, n = repl.size(); i < n; ++i)
        utf32.insert(utf32.begin() + pos + i, static_cast<u32>(repl.ascii[i]));

    return *this;
  }
};

// mode/_sso_length must live past the end of either vector so that they
// survive placement-new of the heap representation.
static_assert(sizeof(less::vector<unsigned char>) <= String::SSO_SIZE);
static_assert(sizeof(less::vector<char32_t>) <= String::SSO_SIZE);
static_assert(sizeof(String) == String::SSO_SIZE + 1);
}; // namespace PuqiAR::LibString

namespace std {
template <> struct hash<PuqiAR::LibString::String> {
  size_t operator()(const PuqiAR::LibString::String &s) const noexcept {
    using String = PuqiAR::LibString::String;
    using u32 = String::u32;

    const size_t FNV_offset = 1469598103934665603ull;
    const size_t FNV_prime = 1099511628211ull;

    size_t h = FNV_offset;

    if (s.mode == String::Mode::ASCII_SSO) {
      for (size_t i = 0, n = s.size(); i < n; ++i) {
        h ^= s.sso[i];
        h *= FNV_prime;
      }
      return h;
    }

    if (s.mode == String::Mode::ASCII_HEP) {
      for (unsigned char c : s.ascii) {
        h ^= c;
        h *= FNV_prime;
      }
      return h;
    }

    // UTF32
    for (u32 cp : s.utf32) {
      h ^= static_cast<size_t>(cp);
      h *= FNV_prime;
    }

    return h;
  }
};

template <> struct std::formatter<PuqiAR::LibString::String, char> {
  // 不支持自定义格式说明符
  constexpr auto parse(std::format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const PuqiAR::LibString::String &s, FormatContext &ctx) const {
    return std::format_to(ctx.out(), "{}", s.toStdString());
  }
};

} // namespace std
