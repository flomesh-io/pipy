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

#include <limits.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <Windows.h>
#include <direct.h>
#include <strsafe.h>

#include <ctime>
#define PATH_MAX MAX_PATH
#define realpath(N, R) _fullpath((R), (N), _MAX_PATH)
#define S_ISDIR(mode) (((mode)&_S_IFMT) == _S_IFDIR)
#define S_ISREG(mode) (((mode)&_S_IFMT) == _S_IFREG)
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include <fstream>

namespace pipy {
namespace fs {

auto abs_path(const std::string &filename) -> std::string {
  char full_path[PATH_MAX];
  realpath(filename.c_str(), full_path);
  return full_path;
}

bool exists(const std::string &filename) {
#ifdef _WIN32
  struct _stat st;
  if (_stat(filename.c_str(), &st)) return false;
#else
  struct stat st;
  if (stat(filename.c_str(), &st)) return false;
#endif
  return true;
}

bool is_dir(const std::string &filename) {
#ifdef _WIN32
  struct _stat st;
  if (_stat(filename.c_str(), &st)) return false;
#else
  struct stat st;
  if (stat(filename.c_str(), &st)) return false;
#endif
  return S_ISDIR(st.st_mode);
}

bool is_file(const std::string &filename) {
#ifdef _WIN32
  struct _stat st;
  if (_stat(filename.c_str(), &st)) return false;
#else
  struct stat st;
  if (stat(filename.c_str(), &st)) return false;
#endif
  return S_ISREG(st.st_mode);
}

auto get_file_time(const std::string &filename) -> double {
#ifdef _WIN32
  struct _stat st;
  if (_stat(filename.c_str(), &st)) return 0;
  return st.st_mtime / 1e9;
#else
  struct stat st;
  if (stat(filename.c_str(), &st)) return 0;
#ifdef __APPLE__
  auto &ts = st.st_mtimespec;
#else
  auto &ts = st.st_mtim;
#endif
  return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

bool make_dir(const std::string &filename) {
#ifdef _WIN32
  return _mkdir(filename.c_str()) == 0;
#else
  return mkdir(filename.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) == 0;
#endif
}

bool read_dir(const std::string &filename, std::list<std::string> &list) {
#ifndef _WIN32
  if (DIR *dir = opendir(filename.c_str())) {
    while (auto *entry = readdir(dir)) {
      if (entry->d_name[0] == '.') continue;
      std::string name(entry->d_name);
      if (entry->d_type == DT_DIR) name += '/';
      list.push_back(name);
    }
    closedir(dir);
    return true;
  } else {
    return false;
  }
#else
  WIN32_FIND_DATA ffd;
  HANDLE hFind;
  TCHAR szDir[MAX_PATH];

  if (filename.size() > (MAX_PATH - 3)) {
    return false;
  }
  StringCchCopy(szDir, MAX_PATH, filename.c_str());
  StringCchCat(szDir, MAX_PATH, TEXT("\\*"));

  hFind = FindFirstFile(szDir, &ffd);
  if (INVALID_HANDLE_VALUE == hFind) {
    return false;
  }

  do {
    if (ffd.cFileName[0] == '.') continue;
    std::string name(ffd.cFileName);
    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) name += '/';
    list.push_back(name);
  } while (FindNextFile(hFind, &ffd) != 0);

  if (GetLastError() != ERROR_NO_MORE_FILES) {
    FindClose(hFind);
    return false;
  }

  FindClose(hFind);
  return true;
#endif
}

bool read_file(const std::string &filename, std::vector<uint8_t> &data) {
  std::ifstream fs(filename, std::ios::in);
  if (!fs.is_open()) return false;
  uint8_t buf[1024];
  while (fs.good()) {
    fs.read((char *)buf, sizeof(buf));
    data.insert(data.end(), buf, buf + fs.gcount());
  }
  return true;
}

bool unlink(const std::string &filename) {
#ifdef _WIN32
  return ::_unlink(filename.c_str()) == 0;
#else
  return ::unlink(filename.c_str()) == 0;
#endif
}

} // namespace fs
} // namespace pipy
