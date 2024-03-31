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

#ifndef OS_PLATFORM_HPP
#define OS_PLATFORM_HPP

#include "net.hpp"

namespace pipy {
namespace os {

void init();
void cleanup();
auto process_id() -> int;

} // namespace os
} // namespace pipy

#ifdef _WIN32

#include <Windows.h>
#include <UserEnv.h>

#include <string>
#include <list>

// Remove name pollution from Windows.h
#undef NO_ERROR
#undef DELETE
#undef ERROR
#undef min
#undef max
#undef s_addr
#undef s_host
#undef s_net
#undef s_imp
#undef s_impno
#undef s_lh

#define SIGNAL_STOP   SIGINT
#define SIGNAL_RELOAD SIGBREAK
#define SIGNAL_ADMIN  SIGTERM

namespace pipy {
namespace os {

class FileHandle {
public:
  FileHandle() : m_handle(INVALID_HANDLE_VALUE) {}

  static auto std_input() -> FileHandle;
  static auto std_output() -> FileHandle;
  static auto std_error() -> FileHandle;
  static auto read(const std::string &filename) -> FileHandle;
  static auto write(const std::string &filename) -> FileHandle;
  static auto append(const std::string &filename) -> FileHandle;

  auto get() const -> HANDLE { return m_handle; }
  bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }
  void seek(size_t pos);
  void close();

private:
  FileHandle(HANDLE handle) : m_handle(handle) {}
  HANDLE m_handle;
};

namespace windows {

auto a2w(const std::string &s) -> std::wstring;
auto w2a(const std::wstring &s) -> std::string;
auto convert_slash(const std::string &path) -> std::string;
auto convert_slash(const std::wstring &path) -> std::wstring;
auto encode_argv(const std::list<std::string> &argv) -> std::string;
auto get_last_error() -> std::string;

} // namespace windows
} // namespace os
} // namespace pipy

#else // !_WIN32

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#define SIGNAL_STOP   SIGINT
#define SIGNAL_RELOAD SIGHUP
#define SIGNAL_ADMIN  SIGTSTP

namespace pipy {
namespace os {

class FileHandle {
public:
  FileHandle() : m_file(nullptr) {}

  static auto std_input() -> FileHandle;
  static auto std_output() -> FileHandle;
  static auto std_error() -> FileHandle;
  static auto read(const std::string &filename) -> FileHandle;
  static auto write(const std::string &filename) -> FileHandle;
  static auto append(const std::string &filename) -> FileHandle;

  auto get() const -> int { return fileno(m_file); }
  bool valid() const { return m_file; }
  void seek(size_t pos);
  void close();

private:
  FileHandle(FILE *file) : m_file(file) {}
  FILE* m_file;
};

} // namespace os
} // namespace pipy

#endif // _WIN32

#endif // OS_PLATFORM_HPP
