/*
 *  Copyright (c) 2019 by flomesh.io
 *
 *  Unless prior written consent has been obtained from the copyright
 *  owner, the following shall not be allowed.
 *
 *  1. The distribution of any source codes, header files, make files,
 *     or libraries of the software.
 *
 *  2. Disclosure of any source codes pertaining to the software to any
 *     additional parties.
 *
 *  3. Alteration or removal of any notices in or on the software or
 *     within the documentation included within the software.
 *
 *  ALL SOURCE CODE AS WELL AS ALL DOCUMENTATION INCLUDED WITH THIS
 *  SOFTWARE IS PROVIDED IN AN “AS IS” CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "utils.hpp"

#include <sys/stat.h>
#include <cmath>
#include <chrono>

namespace pipy {

namespace utils {

auto to_string(double n) -> std::string {
  if (std::isnan(n)) return "NaN";
  if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
  double i; std::modf(n, &i);
  if (std::modf(n, &i) == 0) return std::to_string(int64_t(i));
  return std::to_string(n);
}

auto now() -> double {
  auto t = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
  return double(ms);
}

auto get_param(const std::map<std::string, std::string> &params, const char *name, const char *value) -> std::string {
  auto it = params.find(name);
  if (it == params.end()) {
    if (value) {
      return value;
    } else {
      std::string msg("missing parameter ");
      throw std::runtime_error(msg + name);
    }
  }
  return it->second;
}

bool get_host_port(const std::string &str, std::string &host, int &port) {
  auto p = str.find_last_of(':');
  if (p == std::string::npos) return false;
  auto s1 = str.substr(0 , p);
  auto s2 = str.substr(p + 1);
  if (s2.length() > 5) return false;
  for (auto c : s2) if (c < '0' || c > '9') return false;
  auto n = std::atoi(s2.c_str());
  if (n < 1 || n > 65535) return false;
  host = s1;
  port = n;
  return true;
}

bool get_ip_port(const std::string &str, std::string &ip, int &port) {
  std::string host;
  int n = 0;
  if (!get_host_port(str, host, n)) return false;
  if (host.empty()) host = "0.0.0.0";
  int a, b, c, d;
  if (std::sscanf(host.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;
  if (a < 0 || a > 255) return false;
  if (b < 0 || b > 255) return false;
  if (c < 0 || c > 255) return false;
  if (d < 0 || d > 255) return false;
  ip = host;
  port = n;
  return true;
}

auto get_file_time(const std::string &filename) -> uint64_t {
  struct stat st;
  if (stat(filename.c_str(), &st)) return 0;
  return (uint64_t)st.st_mtime * 1000;
}

auto get_byte_size(const std::string &str) -> size_t {
  if (str.empty()) return 0;
  size_t n = std::atoi(str.c_str());
  switch (std::tolower(str.back())) {
    case 't': n *= 1024;
    case 'g': n *= 1024;
    case 'm': n *= 1024;
    case 'k': n *= 1024;
  }
  return n;
}

auto get_seconds(const std::string &str) -> double {
  if (str.empty()) return 0;
  auto n = std::atof(str.c_str());
  switch (std::tolower(str.back())) {
    case 'd': n *= 24;
    case 'h': n *= 60;
    case 'm': n *= 60;
    case 's': n *= 1;
  }
  return n;
}

auto trim(const std::string &str) -> std::string {
  size_t n = str.length(), i = 0, j = n;
  while (i < n && str[i] <= ' ') i++;
  while (j > 0 && str[j-1] <= ' ') j--;
  return j >= i ? str.substr(i, j - i) : std::string();
}

auto split(const std::string &str, char sep) -> std::list<std::string> {
  std::list<std::string> list;
  size_t p = 0;
  for (size_t i = 0; i < str.length(); i++) {
    if (str[i] == sep) {
      list.push_back(str.substr(p, i - p));
      p = i + 1;
    }
  }
  list.push_back(str.substr(p, str.length() - p));
  return list;
}

auto lower(const std::string &str) -> std::string {
  auto lstr = str;
  for (size_t i = 0; i < lstr.length(); i++) lstr[i] = std::tolower(lstr[i]);
  return lstr;
}

auto escape(const std::string &str) -> std::string {
  std::string str2;
  for (auto c : str) {
    switch (c) {
      case '"' : str2 += "\\\""; break;
      case '\\': str2 += "\\\\"; break;
      case '\a': str2 += "\\a"; break;
      case '\b': str2 += "\\b"; break;
      case '\f': str2 += "\\f"; break;
      case '\n': str2 += "\\n"; break;
      case '\r': str2 += "\\r"; break;
      case '\t': str2 += "\\t"; break;
      case '\v': str2 += "\\v"; break;
      default: str2 += c; break;
    }
  }
  return str2;
}

auto unescape(const std::string &str) -> std::string {
  std::string str2;
  for (size_t i = 0; i < str.size(); i++) {
    auto ch = str[i];
    if (ch == '\\') {
      ch = str[++i];
      if (!ch) break;
      switch (ch) {
        case 'a': ch = '\a'; break;
        case 'b': ch = '\b'; break;
        case 'f': ch = '\f'; break;
        case 'n': ch = '\n'; break;
        case 'r': ch = '\r'; break;
        case 't': ch = '\t'; break;
        case 'v': ch = '\v'; break;
        case '0': ch = '\0'; break;
      }
    }
    str2 += ch;
  }
  return str2;
}

auto encode_hex(char *out, const void *inp, int len) -> int {
  int n = 0;
  HexEncoder encoder(
    [&](char c) {
      out[n++] = c;
    }
  );
  const auto *buf = (const uint8_t *)inp;
  for (int i = 0; i < len; i++) {
    encoder.input(buf[i]);
  }
  return n;
}

auto decode_hex(void *out, const char *inp, int len) -> int {
  if (len % 2) return -1;
  auto *buf = (uint8_t *)out;
  int n = 0;
  HexDecoder decoder(
    [&](uint8_t b) {
      buf[n++] = b;
    }
  );
  for (int i = 0; i < len; i++) {
    decoder.input(inp[i]);
  }
  return n;
}

auto encode_base64(char *out, const void *inp, int len) -> int {
  int n = 0;
  Base64Encoder encoder(
    [&](char c) {
      out[n++] = c;
    }
  );
  const auto *buf = (const uint8_t *)inp;
  for (int i = 0; i < len; i++) {
    encoder.input(buf[i]);
  }
  encoder.flush();
  return n;
}

auto decode_base64(void *out, const char *inp, int len) -> int {
  if (len % 4 > 0) return -1;
  auto *buf = (uint8_t *)out;
  int n = 0;
  Base64Decoder decoder(
    [&](uint8_t b) {
      buf[n++] = b;
    }
  );
  for (int i = 0; i < len; i++) {
    int c = inp[i];
    if (!decoder.input(c)) return -1;
  }
  return decoder.complete() ? n : -1;
}

auto encode_base64url(char *out, const void *inp, int len) -> int {
  int n = 0;
  Base64UrlEncoder encoder(
    [&](char c) {
      out[n++] = c;
    }
  );
  const auto *buf = (const uint8_t *)inp;
  for (int i = 0; i < len; i++) {
    encoder.input(buf[i]);
  }
  encoder.flush();
  return n;
}

auto decode_base64url(void *out, const char *inp, int len) -> int {
  auto *buf = (uint8_t *)out;
  int n = 0;
  Base64UrlDecoder decoder(
    [&](uint8_t b) {
      buf[n++] = b;
    }
  );
  for (int i = 0; i < len; i++) {
    if (!decoder.input(inp[i])) {
      return -1;
    }
  }
  return decoder.flush() ? n : -1;
}

auto path_join(const std::string &base, const std::string &path) -> std::string {
  if (!base.empty() && base.back() == '/') {
    if (!path.empty() && path.front() == '/') {
      return base + path.substr(1);
    } else {
      return base + path;
    }
  } else {
    if (!path.empty() && path.front() == '/') {
      return base + path;
    } else {
      return base + '/' + path;
    }
  }
}

auto path_normalize(const std::string &path) -> std::string {
  std::string output;
  for (size_t i = 0, j = 0; i < path.length(); i = j + 1) {
    j = i;
    while (j < path.length() && path[j] != '/') j++;
    if (j == 0) continue;
    if (path[i] == '.') {
      if (j == 1) continue;
      if (j == 2 && path[i+1] == '.') {
        auto p = output.find_last_of('/');
        if (p == std::string::npos) output.clear();
        else output = output.substr(0, p);
        continue;
      }
    }
    output += '/';
    output += path.substr(i, j - i);
  }
  return output;
}

//
// HexEncoder
//

static char s_hex_tab[] = {
  "0123456789abcdef"
};

void HexEncoder::input(uint8_t b) {
  m_output(s_hex_tab[(b >> 4) & 15]);
  m_output(s_hex_tab[(b >> 0) & 15]);
}

//
// HexDecoder
//

bool HexDecoder::input(char c) {
  if ('0' <= c && c <= '9') c = c - '0'; else
  if ('a' <= c && c <= 'f') c = c - 'a' + 10; else
  if ('A' <= c && c <= 'F') c = c - 'A' + 10; else
  return false;
  m_byte = (m_byte << 4) | c;
  if (++m_shift == 2) {
    m_output(m_byte);
    m_byte = 0;
    m_shift = 0;
  }
  return true;
}

//
// Base64Encoder
//

static char s_base64_tab[] = {
  "ABCDEFGHIJKLMNOP"
  "QRSTUVWXYZabcdef"
  "ghijklmnopqrstuv"
  "wxyz0123456789+/"
};

void Base64Encoder::input(uint8_t b) {
  auto triplet = m_triplet = (m_triplet << 8) | b;
  if (++m_shift == 3) {
    auto &out = m_output;
    out(s_base64_tab[(triplet >> 18) & 63]);
    out(s_base64_tab[(triplet >> 12) & 63]);
    out(s_base64_tab[(triplet >>  6) & 63]);
    out(s_base64_tab[(triplet >>  0) & 63]);
    m_shift = 0;
    m_triplet = 0;
  }
}

void Base64Encoder::flush() {
  auto triplet = m_triplet;
  auto &out = m_output;
  switch (m_shift) {
    case 1:
      triplet <<= 16;
      out(s_base64_tab[(triplet >> 18) & 63]);
      out(s_base64_tab[(triplet >> 12) & 63]);
      out('=');
      out('=');
      break;
    case 2:
      triplet <<= 8;
      out(s_base64_tab[(triplet >> 18) & 63]);
      out(s_base64_tab[(triplet >> 12) & 63]);
      out(s_base64_tab[(triplet >>  6) & 63]);
      out('=');
      break;
  }
}

//
// Base64Decoder
//

bool Base64Decoder::input(char c) {
  if (m_done) {
    if (c == '=' && m_shift == 3) {
      m_shift = 0;
      return true;
    } else {
      return false;
    }
  }
  if (c == '=') {
    m_done = true;
    if (m_shift == 3) {
      m_output((m_triplet >> 10) & 255);
      m_output((m_triplet >>  2) & 255);
      m_shift = 0;
      return true;
    } else if (m_shift == 2) {
      m_output((m_triplet >> 4) & 255);
      m_shift = 3;
      return true;
    } else {
      return false;
    }
  }
  else if (c == '+') c = 62;
  else if (c == '/') c = 63;
  else if ('0' <= c && c <= '9') c = c - '0' + 52;
  else if ('a' <= c && c <= 'z') c = c - 'a' + 26;
  else if ('A' <= c && c <= 'Z') c = c - 'A';
  else return false;
  auto triplet = m_triplet = (m_triplet << 6) | c;
  if (++m_shift == 4) {
    auto &out = m_output;
    out((triplet >> 16) & 255);
    out((triplet >>  8) & 255);
    out((triplet >>  0) & 255);
    m_shift = 0;
    m_triplet = 0;
  }
  return true;
}

//
// Base64UrlEncoder
//

static char s_base64url_tab[] = {
  "ABCDEFGHIJKLMNOP"
  "QRSTUVWXYZabcdef"
  "ghijklmnopqrstuv"
  "wxyz0123456789-_"
};

void Base64UrlEncoder::input(uint8_t b) {
  auto triplet = m_triplet = (m_triplet << 8) | b;
  if (++m_shift == 3) {
    auto &out = m_output;
    out(s_base64url_tab[(triplet >> 18) & 63]);
    out(s_base64url_tab[(triplet >> 12) & 63]);
    out(s_base64url_tab[(triplet >>  6) & 63]);
    out(s_base64url_tab[(triplet >>  0) & 63]);
    m_shift = 0;
    m_triplet = 0;
  }
}

void Base64UrlEncoder::flush() {
  auto triplet = m_triplet;
  auto &out = m_output;
  switch (m_shift) {
    case 1:
      triplet <<= 16;
      out(s_base64url_tab[(triplet >> 18) & 63]);
      out(s_base64url_tab[(triplet >> 12) & 63]);
      break;
    case 2:
      triplet <<= 8;
      out(s_base64url_tab[(triplet >> 18) & 63]);
      out(s_base64url_tab[(triplet >> 12) & 63]);
      out(s_base64url_tab[(triplet >>  6) & 63]);
      break;
  }
}

//
// Base64UrlDecoder
//

bool Base64UrlDecoder::input(char c) {
  if (m_done) return false;
  else if (c == '-') c = 62;
  else if (c == '_') c = 63;
  else if ('0' <= c && c <= '9') c = c - '0' + 52;
  else if ('a' <= c && c <= 'z') c = c - 'a' + 26;
  else if ('A' <= c && c <= 'Z') c = c - 'A';
  else return false;
  auto triplet = m_triplet = (m_triplet << 6) | c;
  if (++m_shift == 4) {
    auto &out = m_output;
    out((triplet >> 16) & 255);
    out((triplet >>  8) & 255);
    out((triplet >>  0) & 255);
    m_shift = 0;
    m_triplet = 0;
  }
  return true;
}

bool Base64UrlDecoder::flush() {
  if (m_done) return false;
  else m_done = true;
  if (m_shift == 3) {
    m_output((m_triplet >> 10) & 255);
    m_output((m_triplet >>  2) & 255);
    return true;
  } else if (m_shift == 2) {
    m_output((m_triplet >> 4) & 255);
    return true;
  } else if (m_shift == 0) {
    return true;
  } else {
    return false;
  }
}

} // namespace utils

} // namespace pipy