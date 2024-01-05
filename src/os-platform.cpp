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

#include "os-platform.hpp"

#ifdef _WIN32

#include "pjs/pjs.hpp"
#include "utils.hpp"

#include <strsafe.h>
#include <io.h>
#include <vector>

#endif // _WIN32

namespace pipy {
namespace os {

#ifdef _WIN32

void init() {
  SetConsoleCP(65001);
  SetConsoleOutputCP(65001);
}

auto FileHandle::std_input() -> FileHandle {
  auto h = GetStdHandle(STD_INPUT_HANDLE);
  return FileHandle(h);
}

auto FileHandle::std_output() -> FileHandle {
  auto h = GetStdHandle(STD_OUTPUT_HANDLE);
  return FileHandle(h);
}

auto FileHandle::std_error() -> FileHandle {
  auto h = GetStdHandle(STD_ERROR_HANDLE);
  return FileHandle(h);
}

auto FileHandle::read(const std::string &filename) -> FileHandle {
  auto wpath = windows::convert_slash(windows::a2w(filename));
  auto h = CreateFileW(
    wpath.c_str(),
    GENERIC_READ,
    FILE_SHARE_READ,
    NULL,
    OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED,
    NULL
  );
  return FileHandle(h);
}

auto FileHandle::write(const std::string &filename) -> FileHandle {
  auto wpath = windows::convert_slash(windows::a2w(filename));
  auto h = CreateFileW(
    wpath.c_str(),
    GENERIC_WRITE,
    0,
    NULL,
    CREATE_ALWAYS,
    FILE_FLAG_OVERLAPPED,
    NULL
  );
  return FileHandle(h);
}

auto FileHandle::append(const std::string &filename) -> FileHandle {
  auto wpath = windows::convert_slash(windows::a2w(filename));
  auto h = CreateFileW(
    wpath.c_str(),
    GENERIC_WRITE,
    0,
    NULL,
    OPEN_ALWAYS,
    FILE_FLAG_OVERLAPPED,
    NULL
  );
  if (h != INVALID_HANDLE_VALUE) SetFilePointer(h, 0, NULL, FILE_END);
  return FileHandle(h);
}

void FileHandle::seek(size_t pos) {
  SetFilePointer(m_handle, pos, NULL, FILE_BEGIN);
}

void FileHandle::close() {
  CloseHandle(m_handle);
}

namespace windows {

auto a2w(const std::string &s) -> std::wstring {
  std::wstring buf;
  utils::Utf16Encoder enc([&](wchar_t c) { buf.push_back(c); });
  pjs::Utf8Decoder dec([&](int c) { enc.input(c); });
  for (const auto c : s) dec.input(c);
  dec.end();
  return std::wstring(std::move(buf));
}

auto w2a(const std::wstring &s) -> std::string {
  std::string buf;
  utils::Utf16Decoder dec(
    [&](uint32_t c) {
      char utf[5];
      auto len = pjs::Utf8Decoder::encode(c, utf, sizeof(utf));
      buf.append(utf, len);
    }
  );
  for (const auto c : s) dec.input(c);
  dec.flush();
  return std::string(std::move(buf));
}

auto convert_slash(const std::wstring &path) -> std::wstring {
  std::wstring copy(path);
  for (auto &c : copy) {
    if (c == '/') c = '\\';
  }
  return std::wstring(std::move(copy));
}

auto get_last_error() -> std::string {
  wchar_t *msg = nullptr;
  auto dw = GetLastError();
  FormatMessageW(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL,
    GetLastError(),
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPWSTR)&msg, 0, NULL
  );
  auto ret = w2a(msg);
  LocalFree(msg);
  return std::string(std::move(ret));
}

auto get_last_error(const std::string &function) -> std::string {
  LPVOID lpMsgBuf;
  LPVOID lpDisplayBuf;
  DWORD dw = GetLastError();
  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR)&lpMsgBuf, 0, NULL);
  lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
      (lstrlen((LPCTSTR)lpMsgBuf) + function.size() + 100) * sizeof(TCHAR));
  StringCchPrintf((LPTSTR)lpDisplayBuf, LocalSize(lpDisplayBuf) / sizeof(TCHAR),
                  TEXT("%s failed with error %d: %s"), function.c_str(), dw, lpMsgBuf);
  std::string error((LPCTSTR)lpDisplayBuf);
  LocalFree(lpMsgBuf);
  LocalFree(lpDisplayBuf);
  return error;
}

} // namespace windows

#else // !_WIN32

void init()
{
}

auto FileHandle::std_input() -> FileHandle {
  return FileHandle(stdin);
}

auto FileHandle::std_output() -> FileHandle {
  return FileHandle(stdout);
}

auto FileHandle::std_error() -> FileHandle {
  return FileHandle(stderr);
}

auto FileHandle::read(const std::string &filename) -> FileHandle {
  auto f = fopen(filename.c_str(), "rb");
  return FileHandle(f);
}

auto FileHandle::write(const std::string &filename) -> FileHandle {
  auto f = fopen(filename.c_str(), "wb");
  return FileHandle(f);
}

auto FileHandle::append(const std::string &filename) -> FileHandle {
  auto f = fopen(filename.c_str(), "ab");
  return FileHandle(f);
}

void FileHandle::seek(size_t pos) {
  fseek(m_file, pos, SEEK_SET);
}

void FileHandle::close() {
  fclose(m_file);
}

#endif // _WIN32

} // namespace os
} // namespace pipy
