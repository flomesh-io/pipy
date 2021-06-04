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

namespace pipy {

namespace utils {

auto to_string(double n) -> std::string {
  if (std::isnan(n)) return "NaN";
  if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
  double i; std::modf(n, &i);
  if (std::modf(n, &i) == 0) return std::to_string(int64_t(i));
  return std::to_string(n);
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

auto decode_hex(void *out, const char *inp, int len) -> int {
  if (len % 2) return -1;
  for (int i = 0; i < len; i += 2) {
    auto h = inp[i+0];
    auto l = inp[i+1];
    if ('0' <= h && h <= '9') h -= '0';
    else if ('a' <= h && h <= 'f') h -= 'a' - 10;
    else if ('A' <= h && h <= 'F') h -= 'A' - 10;
    else return -1;
    if ('0' <= l && l <= '9') l -= '0';
    else if ('a' <= l && l <= 'f') l -= 'a' - 10;
    else if ('A' <= l && l <= 'F') l -= 'A' - 10;
    else return -1;
    *((char*)out + (i>>1)) = (h << 4) | l;
  }
  return len >> 1;
}

auto encode_hex(char *out, const void *inp, int len) -> int {
  return -1;
}

auto decode_base64(void *out, const char *inp, int len) -> int {
  auto *buf = (char *)out;
  if (len % 4 > 0) return -1;
  uint32_t w = 0, n = 0, c = 0;
  for (int i = 0; i < len; i++) {
    int ch = inp[i];
    if (ch == '=') {
      if (n == 3 && i + 1 == len) {
        buf[c++] = (w >> 10) & 255;
        buf[c++] = (w >> 2) & 255;
        break;
      } else if (n == 2 && i + 2 == len && inp[i+1] == '=') {
        buf[c++] = (w >> 4) & 255;
        break;
      } else {
        return -1;
      }
    }
    else if (ch == '+') ch = 62;
    else if (ch == '/') ch = 63;
    else if ('0' <= ch && ch <= '9') ch = ch - '0' + 52;
    else if ('a' <= ch && ch <= 'z') ch = ch - 'a' + 26;
    else if ('A' <= ch && ch <= 'Z') ch = ch - 'A';
    else return -1;
    w = (w << 6) | ch;
    if (++n == 4) {
      buf[c++] = (w >> 16) & 255;
      buf[c++] = (w >> 8) & 255;
      buf[c++] = (w >> 0) & 255;
      w = n = 0;
    }
  }
  return c;
}

auto encode_base64(char *out, const void *inp, int len) -> int {
  static char tab[] = {
    "ABCDEFGHIJKLMNOP"
    "QRSTUVWXYZabcdef"
    "ghijklmnopqrstuv"
    "wxyz0123456789+/"
  };
  uint32_t w = 0, n = 0, c = 0;
  for (int i = 0; i < len; i++) {
    w = (w << 8) | ((const uint8_t *)inp)[i];
    if (++n == 3) {
      out[c++] = tab[(w >> 18) & 63];
      out[c++] = tab[(w >> 12) & 63];
      out[c++] = tab[(w >> 6) & 63];
      out[c++] = tab[(w >> 0) & 63];
      w = n = 0;
    }
  }
  switch (n) {
    case 1:
      w <<= 16;
      out[c++] = tab[(w >> 18) & 63];
      out[c++] = tab[(w >> 12) & 63];
      out[c++] = '=';
      out[c++] = '=';
      break;
    case 2:
      w <<= 8;
      out[c++] = tab[(w >> 18) & 63];
      out[c++] = tab[(w >> 12) & 63];
      out[c++] = tab[(w >> 6) & 63];
      out[c++] = '=';
      break;
  }
  return c;
}

auto decode_base64url(void *out, const char *inp, int len) -> int {
  auto *buf = (char *)out;
  uint32_t w = 0, n = 0, c = 0;
  for (int i = 0; i < len; i++) {
    int ch = inp[i];
    if (ch == '-') ch = 62;
    else if (ch == '_') ch = 63;
    else if ('0' <= ch && ch <= '9') ch = ch - '0' + 52;
    else if ('a' <= ch && ch <= 'z') ch = ch - 'a' + 26;
    else if ('A' <= ch && ch <= 'Z') ch = ch - 'A';
    else return -1;
    w = (w << 6) | ch;
    if (++n == 4) {
      buf[c++] = (w >> 16) & 255;
      buf[c++] = (w >> 8) & 255;
      buf[c++] = (w >> 0) & 255;
      w = n = 0;
    }
  }
  switch (len % 4) {
  case 3:
    buf[c++] = (w >> 10) & 255;
    buf[c++] = (w >> 2) & 255;
    break;
  case 2:
    buf[c++] = (w >> 4) & 255;
    break;
  case 1:
    return -1;
  }
  return c;
}

auto encode_base64url(char *out, const void *inp, int len) -> int {
  static char tab[] = {
    "ABCDEFGHIJKLMNOP"
    "QRSTUVWXYZabcdef"
    "ghijklmnopqrstuv"
    "wxyz0123456789-_"
  };
  uint32_t w = 0, n = 0, c = 0;
  for (int i = 0; i < len; i++) {
    w = (w << 8) | ((const uint8_t *)inp)[i];
    if (++n == 3) {
      out[c++] = tab[(w >> 18) & 63];
      out[c++] = tab[(w >> 12) & 63];
      out[c++] = tab[(w >> 6) & 63];
      out[c++] = tab[(w >> 0) & 63];
      w = n = 0;
    }
  }
  switch (n) {
    case 1:
      w <<= 16;
      out[c++] = tab[(w >> 18) & 63];
      out[c++] = tab[(w >> 12) & 63];
      break;
    case 2:
      w <<= 8;
      out[c++] = tab[(w >> 18) & 63];
      out[c++] = tab[(w >> 12) & 63];
      out[c++] = tab[(w >> 6) & 63];
      break;
  }
  return c;
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

} // namespace utils

} // namespace pipy