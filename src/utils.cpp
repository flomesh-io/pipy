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

NS_BEGIN

namespace utils {

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

auto escape(const std::string str) -> std::string {
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

} // namespace utils

NS_END