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

#include "fs.hpp"

#include <sys/stat.h>
#include <limits.h>
#include <dirent.h>
#include <unistd.h>
#include <fstream>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define WINDOWS
# define realpath(N,R) _fullpath((R),(N), _MAX_PATH)
#endif

namespace pipy {
namespace fs {

auto abs_path(const std::string &filename) -> std::string {
  char full_path[PATH_MAX];
  realpath(filename.c_str(), full_path);
  return full_path;
}

bool exists(const std::string &filename) {
  struct stat st;
  if (stat(filename.c_str(), &st)) return false;
  return true;
}

bool is_dir(const std::string &filename) {
  struct stat st;
  if (stat(filename.c_str(), &st)) return false;
  return S_ISDIR(st.st_mode);
}

bool is_file(const std::string &filename) {
  struct stat st;
  if (stat(filename.c_str(), &st)) return false;
  return S_ISREG(st.st_mode);
}

bool make_dir(const std::string &filename) {
# ifdef WINDOWS
  return mkdir(filename.c_str()) == 0;
# else
  return mkdir(filename.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) == 0;
#endif
}

bool read_dir(const std::string &filename, std::list<std::string> &list) {
  if (DIR *dir = opendir(filename.c_str())) {
    while (auto *entry = readdir(dir)) {
      if (entry->d_name[0] == '.') continue;
      std::string name(entry->d_name);
#ifdef WINDOWS
  struct stat s;
  stat(entry->d_name, &s);
  if(s.st_mode & S_IFDIR) name += '/';
#else
      if (entry->d_type == DT_DIR) name += '/';
#endif
      list.push_back(name);
    }
    closedir(dir);
    return true;
  } else {
    return false;
  }
}

bool read_file(const std::string &filename, std::vector<uint8_t> &data) {
  std::ifstream fs(filename, std::ios::in);
  if (!fs.is_open()) return false;
  uint8_t buf[1024];
  while (!fs.eof()) {
    fs.read((char *)buf, sizeof(buf));
    data.insert(data.end(), buf, buf + fs.gcount());
  }
  return true;
}

bool unlink(const std::string &filename) {
  return ::unlink(filename.c_str()) == 0;
}

} // namespace fs
} // namespace pipy
