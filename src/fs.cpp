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
#include "os-platform.hpp"

#ifdef _WIN32

#include "pjs/pjs.hpp"

#else // !_WIN32

#include <sys/stat.h>
#include <limits.h>
#include <dirent.h>
#include <unistd.h>
#include <fstream>

#endif // _WIN32

namespace pipy {
namespace fs {

#ifndef _WIN32

bool Stat::is_file()             const { return S_ISREG(mode); }
bool Stat::is_directory()        const { return S_ISDIR(mode); }
bool Stat::is_character_device() const { return S_ISCHR(mode); }
bool Stat::is_block_device()     const { return S_ISBLK(mode); }
bool Stat::is_fifo()             const { return S_ISFIFO(mode); }
bool Stat::is_symbolic_link()    const { return S_ISLNK(mode); }
bool Stat::is_socket()           const { return S_ISSOCK(mode); }

inline static auto ts2secs(const struct timespec &ts) -> double {
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

auto abs_path(const std::string &filename) -> std::string {
  char full_path[PATH_MAX];
  realpath(filename.c_str(), full_path);
  return full_path;
}

bool stat(const std::string &filename, Stat &s) {
  struct stat st;
  if (stat(filename.c_str(), &st)) return false;
  s.mode = st.st_mode;
  s.size = st.st_size;
#ifdef __APPLE__
  s.atime = ts2secs(st.st_atimespec);
  s.mtime = ts2secs(st.st_mtimespec);
  s.ctime = ts2secs(st.st_ctimespec);
#else
  s.atime = ts2secs(st.st_atim);
  s.mtime = ts2secs(st.st_mtim);
  s.ctime = ts2secs(st.st_ctim);
#endif
  return true;
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

auto get_file_time(const std::string &filename) -> double {
  struct stat st;
  if (stat(filename.c_str(), &st)) return 0;
#ifdef __APPLE__
  auto &ts = st.st_mtimespec;
#else
  auto &ts = st.st_mtim;
#endif
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

void change_dir(const std::string &filename) {
  chdir(filename.c_str());
}

bool make_dir(const std::string &filename) {
  return mkdir(filename.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) == 0;
}

bool read_dir(const std::string &filename, std::list<std::string> &list) {
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

bool write_file(const std::string &filename, const std::vector<uint8_t> &data) {
  std::ofstream fs(filename, std::ios::out | std::ios::trunc);
  if (!fs.is_open()) return false;
  fs.write((const char *)data.data(), data.size());
  return true;
}

bool unlink(const std::string &filename) {
  return ::unlink(filename.c_str()) == 0;
}

#else // _WIN32

bool Stat::is_file()              const { return !(mode & FILE_ATTRIBUTE_DIRECTORY); }
bool Stat::is_directory()         const { return mode & FILE_ATTRIBUTE_DIRECTORY; }
bool Stat::is_character_device()  const { return false; }
bool Stat::is_block_device()      const { return false; }
bool Stat::is_fifo()              const { return false; }
bool Stat::is_symbolic_link()     const { return false; }
bool Stat::is_socket()            const { return false; }

inline static auto ft2secs(const FILETIME &ft) -> double {
  const auto msec = (
    ((uint64_t)ft.dwHighDateTime << 32) |
    ((uint64_t)ft.dwLowDateTime)
  ) / 10000;
  return (double)msec / 1000.0;
}

auto abs_path(const std::string &filename) -> std::string {
  wchar_t buf[MAX_PATH];
  auto inp = Win32_ConvertSlash(Win32_A2W(filename));
  auto len = GetFullPathNameW(inp.c_str(), sizeof(buf) / sizeof(buf[0]), buf, NULL);
  if (len <= sizeof(buf) / sizeof(buf[0])) {
    std::wstring ws(buf, len);
    return Win32_W2A(ws);
  }
  pjs::vl_array<wchar_t, 1000> wca(len);
  GetFullPathNameW(inp.c_str(), len, wca.data(), NULL);
  return Win32_W2A(std::wstring(wca, len));
}

bool stat(const std::string &filename, Stat &s) {
  WIN32_FILE_ATTRIBUTE_DATA attrs;
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  if (GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attrs)) {
    s.mode = attrs.dwFileAttributes;
    s.size = attrs.nFileSizeLow;
    s.atime = ft2secs(attrs.ftLastAccessTime);
    s.mtime = ft2secs(attrs.ftLastWriteTime);
    s.ctime = ft2secs(attrs.ftCreationTime);
    return true;
  }
  return false;
}

bool exists(const std::string &filename) {
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  auto attrs = GetFileAttributesW(wpath.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES;
}

bool is_dir(const std::string &filename) {
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  auto attrs = GetFileAttributesW(wpath.c_str());
  return (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool is_file(const std::string &filename) {
  return !is_dir(filename);
}

auto get_file_time(const std::string &filename) -> double {
  WIN32_FILE_ATTRIBUTE_DATA attrs;
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  if (GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attrs)) {
    const auto &ft = attrs.ftLastWriteTime;
    return ft2secs(ft);
  }
  return 0;
}

void change_dir(const std::string &filename) {
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  SetCurrentDirectoryW(wpath.c_str());
}

bool make_dir(const std::string &filename) {
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  return CreateDirectoryW(wpath.c_str(), NULL);
}

bool read_dir(const std::string &filename, std::list<std::string> &list) {
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  if (wpath.back() != L'\\') wpath.push_back('\\');
  wpath.push_back(L'*');
  WIN32_FIND_DATAW data;
  HANDLE h = FindFirstFileW(wpath.c_str(), &data);
  if (h == INVALID_HANDLE_VALUE) return false;
  do {
    if (data.cFileName[0] == L'.') continue;
    auto name = Win32_W2A(data.cFileName);
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) name += '/';
    list.push_back(name);
  } while (FindNextFileW(h, &data));
  FindClose(h);
  return true;
}

bool read_file(const std::string &filename, std::vector<uint8_t> &data) {
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  auto h = CreateFileW(
    wpath.c_str(),
    GENERIC_READ,
    FILE_SHARE_READ,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER n;
  bool ok = false;
  if (GetFileSizeEx(h, &n)) {
    DWORD size = n.LowPart, read = 0;
    data.resize(size);
    if (ReadFile(h, data.data(), size, &read, NULL)) {
      data.resize(read);
      ok = true;
    }
  }
  CloseHandle(h);
  return ok;
}

bool write_file(const std::string &filename, const std::vector<uint8_t> &data) {
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  auto h = CreateFileW(
    wpath.c_str(),
    GENERIC_READ,
    0,
    NULL,
    TRUNCATE_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  auto ok = WriteFile(h, data.data(), data.size(), &written, NULL);
  CloseHandle(h);
  return ok && written == data.size();
}

bool unlink(const std::string &filename) {
  auto wpath = Win32_ConvertSlash(Win32_A2W(filename));
  return DeleteFileW(wpath.c_str());
}

#endif // _WIN32

} // namespace fs
} // namespace pipy
