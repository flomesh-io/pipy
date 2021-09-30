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

#include <cstring>
#include "tar.hpp"
#include "utils.hpp"

namespace pipy {

Tarball::Tarball(const char *data, size_t size) {
  auto get_str = [](const char *s, int n) -> std::string {
    int i = 0; while (i < n && s[i]) i++;
    return std::string(s, i);
  };

  auto get_oct = [](const char *s, int n) -> int {
    int x = 0;
    for (int i = 0; i < n; i++) {
      auto c = s[i];
      if (c < '0' || c > '7') break;
      x = (x << 3) + (c - '0');
    }
    return x;
  };

  std::string pax_extended_path;

  for (size_t i = 0; i < size; i += 512) {
    auto ptr = data + i;
    auto type = *(ptr + 156);
    auto filename = get_str(ptr, 100);
    auto filesize = get_oct(ptr + 124, 12);
    auto checksum = get_oct(ptr + 148, 8);

    if (type && type != '0' && type != '5' && type != 'x') throw std::runtime_error("unsupported file type in tarball");

    int sum = 8 * ' ';
    for (int i = 0; i < 148; i++) sum += (unsigned char)ptr[i];
    for (int i = 148 + 8; i < 512; i++) sum += (unsigned char)ptr[i];

    if (type == 'x') {
      auto p = ptr + 512;
      auto i = 0;
      auto getc = [&]() -> int {
        if (i >= filesize) return -1;
        return p[i++];
      };

      while (i < filesize) {
        auto p = i;
        auto c = 0;
        auto n = 0;
        for (;;) {
          c = getc();
          if (c < '0' || c > '9') break;
          n = (n * 10) + (c - '0');
        }
        if (c != ' ') throw std::runtime_error("invalid tarball format");

        std::string k;
        for (;;) {
          c = getc();
          if (c <= 0 || c == '=') break;
          k += c;
        }
        if (c != '=') throw std::runtime_error("invalid tarball format");

        std::string v;
        for (;;) {
          c = getc();
          if (c <= 0 || c == '\n') break;
          v += c;
        }
        if (c != '\n') throw std::runtime_error("invalid tarball format");

        if (k == "path") {
          pax_extended_path = v;
        }

        i = p + n;
      }

    } else {
      if (!pax_extended_path.empty()) {
        filename = pax_extended_path;
        pax_extended_path.clear();

      } else {
        if (!std::strcmp(ptr + 257, "ustar")) {
          auto prefix = get_str(ptr + 345, 155);
          if (!prefix.empty()) {
            if (prefix.back() != '/') prefix += '/';
            filename = prefix + filename;
          }
        }
      }

      if (type == '0') {
        auto name = utils::path_normalize(filename);
        m_files[name] = { ptr + 512, size_t(filesize) };
      }
    }

    auto len = (filesize + 511) / 512 * 512;
    i += len;
  }
}

void Tarball::list(std::set<std::string> &paths) {
  for (const auto &i : m_files) {
    paths.insert(i.first);
  }
}

auto Tarball::get(const std::string &path, size_t &size) -> const char* {
  auto i = m_files.find(path);
  if (i == m_files.end()) return nullptr;
  size = i->second.size;
  return i->second.data;
}

} // namespace pipy