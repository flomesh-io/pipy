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
#include "log.hpp"

#include <strsafe.h>
#include <io.h>
#include <vector>

#endif // _WIN32

namespace pipy {
namespace os {

#ifdef _WIN32

//
// StdioServer
//

class StdioServer {
public:
  StdioServer(HANDLE handle, const char *pipe_name, bool read = false)
    : m_read(read)
    , m_std_handle(handle)
    , m_connect_semaphore(CreateSemaphoreW(NULL, 0, 1, NULL))
    , m_shutdown(false)
    , m_pipe_name(pipe_name)
    , m_thread([this]() {
      Log::init();
      main();
      Log::shutdown();
    }) {}

  ~StdioServer() {
    m_shutdown = true;
    if (m_thread.joinable()) {
      m_thread.join();
    }
    CloseHandle(m_connect_semaphore);
  }

  auto connect() -> HANDLE;

private:
  bool m_read;
  HANDLE m_std_handle;
  HANDLE m_connect_semaphore;
  std::atomic<bool> m_shutdown;
  std::string m_pipe_name;
  std::thread m_thread;

  void main();
  void pump(HANDLE pipe);
};

auto StdioServer::connect() -> HANDLE {
  auto ret = WaitForSingleObject(m_connect_semaphore, INFINITE);
  if (ret != WAIT_OBJECT_0) {
    Log::error(
      "unable to wait for named pipeline '%s': %s",
      m_pipe_name.c_str(),
      os::windows::get_last_error().c_str()
    );
    return INVALID_HANDLE_VALUE;
  }

  auto file = CreateFileA(
    m_pipe_name.c_str(),
    m_read ? GENERIC_READ : GENERIC_WRITE,
    0, NULL,
    OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED,
    NULL
  );

  if (file == INVALID_HANDLE_VALUE) {
    Log::error(
      "unable to create file for named pipe '%s': %s",
      m_pipe_name.c_str(),
      os::windows::get_last_error().c_str()
    );
  }

  return file;
}

void StdioServer::main() {
  while (!m_shutdown) {
    auto pipe = CreateNamedPipeA(
      m_pipe_name.c_str(),
      FILE_FLAG_OVERLAPPED | (m_read ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND),
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
      PIPE_UNLIMITED_INSTANCES,
      DATA_CHUNK_SIZE, DATA_CHUNK_SIZE, 0, NULL
    );

    if (pipe == INVALID_HANDLE_VALUE) {
      Log::error(
        "unable to create named pipe '%s': %s",
        m_pipe_name.c_str(),
        os::windows::get_last_error().c_str()
      );
      return;
    }

    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ConnectNamedPipe(pipe, &ov)) {
      if (GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(pipe);
        Log::error(
          "unable to connect named pipe '%s': %s",
          m_pipe_name.c_str(),
          os::windows::get_last_error().c_str()
        );
        return;
      }
    }

    ReleaseSemaphore(m_connect_semaphore, 1, NULL);

    while (!m_shutdown) {
      auto ret = WaitForSingleObject(ov.hEvent, 100);
      if (ret == WAIT_TIMEOUT) continue;
      if (ret == WAIT_OBJECT_0) {
        CloseHandle(ov.hEvent);
        std::thread t([=]() { pump(pipe); });
        t.detach();
        break;
      }
      CloseHandle(ov.hEvent);
      CloseHandle(pipe);
      Log::error(
        "unable to wait for named pipe connection '%s': %s",
        m_pipe_name.c_str(),
        os::windows::get_last_error().c_str()
      );
      return;
    }
  }
}

void StdioServer::pump(HANDLE pipe) {
  Log::init();

  DWORD len;
  BYTE buf[DATA_CHUNK_SIZE];
  OVERLAPPED ov;
  ZeroMemory(&ov, sizeof(ov));

  for (;;) {
    if (m_read) {
      if (!ReadFile(m_std_handle, buf, sizeof(buf), &len, NULL)) {
        if (GetLastError() != ERROR_BROKEN_PIPE) {
          Log::error(
            "read error from std handle %d: %s",
            m_std_handle,
            os::windows::get_last_error().c_str()
          );
        }
        break;
      }
      if (!WriteFile(pipe, buf, len, &len, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) {
          Log::error(
            "write error to named pipe %s: %s",
            m_pipe_name.c_str(),
            os::windows::get_last_error().c_str()
          );
          break;
        }
        if (!GetOverlappedResult(pipe, &ov, &len, TRUE)) {
          Log::error(
            "unable to get overlapped result while writing to named pipe %s: %s",
            m_pipe_name.c_str(),
            os::windows::get_last_error().c_str()
          );
          break;
        }
      }
    } else {
      if (!ReadFile(pipe, buf, sizeof(buf), &len, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING) {
          Log::error(
            "read error from named pipe %s: %s",
            m_pipe_name.c_str(),
            os::windows::get_last_error().c_str()
          );
          break;
        }
        if (!GetOverlappedResult(pipe, &ov, &len, TRUE)) {
          if (GetLastError() != ERROR_BROKEN_PIPE) {
            Log::error(
              "unable to get overlapped result while reading from named pipe %s: %s",
              m_pipe_name.c_str(),
              os::windows::get_last_error().c_str()
            );
          }
          break;
        }
      }
      if (!WriteFile(m_std_handle, buf, len, &len, NULL)) {
        Log::error(
          "write error to std handle %d: %s",
          m_std_handle,
          os::windows::get_last_error().c_str()
        );
        break;
      }
    }
  }

  CloseHandle(pipe);
  Log::shutdown();
}

static StdioServer* s_stdin_server = nullptr;
static StdioServer* s_stdout_server = nullptr;
static StdioServer* s_stderr_server = nullptr;

void init() {
  SetConsoleCP(65001);
  SetConsoleOutputCP(65001);
}

void cleanup() {
  delete s_stdin_server;
  delete s_stdout_server;
  delete s_stderr_server;
}

auto process_id() -> int {
  return GetCurrentProcessId();
}

auto FileHandle::std_input() -> FileHandle {
  if (!s_stdin_server) {
    s_stdin_server = new StdioServer(
      GetStdHandle(STD_INPUT_HANDLE),
      "\\\\.\\pipe\\pipy.stdin",
      true
    );
  }
  return FileHandle(s_stdin_server->connect());
}

auto FileHandle::std_output() -> FileHandle {
  if (!s_stdout_server) {
    s_stdout_server = new StdioServer(
      GetStdHandle(STD_OUTPUT_HANDLE),
      "\\\\.\\pipe\\pipy.stdout"
    );
  }
  return FileHandle(s_stdout_server->connect());
}

auto FileHandle::std_error() -> FileHandle {
  if (!s_stderr_server) {
    s_stderr_server = new StdioServer(
      GetStdHandle(STD_ERROR_HANDLE),
      "\\\\.\\pipe\\pipy.stderr"
    );
  }
  return FileHandle(s_stderr_server->connect());
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

void cleanup()
{
}

auto process_id() -> int {
  return getpid();
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
