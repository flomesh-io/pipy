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
#include <vector>

#endif // _WIN32

namespace pipy {

#ifdef _WIN32

auto Win32_A2W(const std::string &s) -> std::wstring {
  std::wstring buf;
  utils::Utf16Encoder enc([&](wchar_t c) { buf.push_back(c); });
  pjs::Utf8Decoder dec([&](int c) { enc.input(c); });
  for (const auto c : s) dec.input(c);
  dec.end();
  return std::wstring(std::move(buf));
}

auto Win32_W2A(const std::wstring &s) -> std::string {
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

auto Win32_ConvertSlash(const std::wstring &path) -> std::wstring {
  std::wstring copy(path);
  for (auto &c : copy) {
    if (c == '/') c = '\\';
  }
  return std::wstring(std::move(copy));
}

auto Win32_GetLastError(const std::string &function) -> std::string {
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

#endif // _WIN32

} // namespace pipy
