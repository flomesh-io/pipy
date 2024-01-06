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

#include "exec.hpp"
#include "fstream.hpp"
#include "listener.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace pipy {

thread_local static Data::Producer s_dp("exec()");

#ifdef _WIN32
static volatile long pipe_sn;
#endif

//
// Exec
//

Exec::Exec(const pjs::Value &command)
  : m_command(command)
{
}

Exec::Exec(const Exec &r)
  : m_command(r.m_command)
{
}

Exec::~Exec()
{
}

void Exec::dump(Dump &d) {
  Filter::dump(d);
  d.name = "exec";
}

auto Exec::clone() -> Filter* {
  return new Exec(*this);
}

void Exec::reset() {
  Filter::reset();
  if (m_pid > 0) {
    s_child_process_monitor.remove(this);
#ifndef _WIN32
    kill(m_pid, SIGTERM);
#else
    TerminateProcess(m_pif.hProcess, 0);
    CloseHandle(m_pif.hThread);
    CloseHandle(m_pif.hProcess);
#endif
    m_pid = 0;
  }
  if (m_stdin) {
    m_stdin->close();
    m_stdin = nullptr;
  }
  if (m_stdout) {
    m_stdout->close();
    m_stdout->chain(nullptr);
    m_stdout = nullptr;
  }
}

void Exec::process(Event *evt) {
  if (!m_pid) {
    pjs::Value ret;
    if (!eval(m_command, ret)) return;

    std::list<std::string> args;
    if (ret.is_array()) {
      ret.as<pjs::Array>()->iterate_all(
        [&](pjs::Value &v, int) {
          auto *s = v.to_string();
          args.push_back(s->str());
          s->release();
        }
      );
    } else {
      auto *s = ret.to_string();
      args = utils::split(s->str(), ' ');
      s->release();
    }

    auto argc = args.size();
    if (!argc) {
      Filter::error("command is empty");
      return;
    }

#ifndef _WIN32
    size_t i = 0;
    char *argv[argc + 1];
    for (const auto &arg : args) argv[i++] = strdup(arg.c_str());
    argv[argc] = nullptr;

    int in[2], out[2];
    pipe(in);
    pipe(out);

    m_stdin = FileStream::make(false, in[1], &s_dp);
    m_stdout = FileStream::make(true, out[0], &s_dp);
    m_stdout->chain(output());

    auto pid = fork();

    if (pid == 0) {
      dup2(in[0], 0);
      dup2(out[1], 1);
      execvp(argv[0], argv);
      exit(-1);
    } else if (pid < 0) {
      Filter::error("unable to fork");
    } else {
      ::close(in[0]);
      ::close(out[1]);
      m_pid = pid;
    }

    for (i = 0; i < argc; i++) free(argv[i]);

#else // _WIN32
    char pipe_name[100];
    static std::atomic<int> s_pipe_unique_id(0);
    std::snprintf(
      pipe_name, sizeof(pipe_name),
      "\\\\.\\pipe\\pipy.filter.exec.%08x.%08x.",
      GetCurrentProcessId(),
      s_pipe_unique_id.fetch_add(1)
    );

    auto pipe = CreateNamedPipeA(
      pipe_name,
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      1, DATA_CHUNK_SIZE, DATA_CHUNK_SIZE, 0, NULL
    );

    if (pipe == INVALID_HANDLE_VALUE) {
      Filter::error(
        "unable to create named pipe '%s': %s",
        pipe_name,
        os::windows::get_last_error().c_str()
      );
      return;
    }

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    auto file = CreateFileA(
      pipe_name,
      GENERIC_READ | GENERIC_WRITE,
      0, &sa,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
      NULL
    );

    if (file == INVALID_HANDLE_VALUE) {
      CloseHandle(pipe);
      Filter::error(
        "unable to create file for named pipe '%s': %s",
        pipe_name,
        os::windows::get_last_error().c_str()
      );
      return;
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(STARTUPINFO);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = file;
    si.hStdOutput = file;
    si.hStdError = file;

    std::string cmd_line;
    for (const auto &arg : args) {
      if (cmd_line.empty()) {
        cmd_line += '"';
        cmd_line += arg;
        cmd_line += '"';
      } else {
        cmd_line += ' ';
        cmd_line += arg;
      }
    }

    auto cmd_line_w = os::windows::a2w(cmd_line);
    pjs::vl_array<wchar_t, 1000> buf_cl(cmd_line_w.length() + 1);
    std::memcpy(buf_cl.data(), cmd_line_w.c_str(), buf_cl.size() * sizeof(wchar_t));

    if (!CreateProcessW(
      NULL,
      buf_cl.data(),
      NULL, NULL, TRUE, 0, NULL, NULL,
      &si, &m_pif
    )) {
      CloseHandle(file);
      CloseHandle(pipe);
      Filter::error(
        "unable to create process '%s': %s",
        cmd_line.c_str(),
        os::windows::get_last_error().c_str()
      );
      return;
    }

    m_pid = m_pif.dwProcessId;
    m_stdin = FileStream::make(false, file, &s_dp);
    m_stdout = FileStream::make(true, pipe, &s_dp);
    m_stdout->chain(Filter::output());

    Log::debug(Log::SUBPROC,
      "[exec] child process started [pid = %d]: %s",
      m_pid, cmd_line.c_str()
    );

#endif // _WIN32

    if (m_pid > 0) {
      s_child_process_monitor.monitor(this);
    }
  }

  if (m_pid > 0 && m_stdin) {
    m_stdin->input()->input(evt);
  }
}

//
// Exec::ChildProcessMoinitor
//

Exec::ChildProcessMonitor Exec::s_child_process_monitor;

Exec::ChildProcessMonitor::ChildProcessMonitor()
  : m_exited(false)
  , m_wait_thread([this]() { wait(); })
{
}

Exec::ChildProcessMonitor::~ChildProcessMonitor() {
  m_exited.store(true);
  if (m_wait_thread.joinable()) {
    m_wait_thread.join();
  }
}

void Exec::ChildProcessMonitor::monitor(Exec *exec) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_filters[exec] = &Net::current();
}

void Exec::ChildProcessMonitor::remove(Exec *exec) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_filters.erase(exec);
}

void Exec::ChildProcessMonitor::wait() {
  Log::init();

  while (!m_exited.load()) {
    int pid = 0;

#ifndef _WIN32
    int status;
    auto ret = waitpid(-1, &status, 0);
    if (ret < 0) {
      sleep(1);
    } else if (ret > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) {
      pid = ret;
    }
#else // _WIN32
    m_mutex.lock();
    pjs::vl_array<int> pids(m_filters.size());
    pjs::vl_array<HANDLE> handles(m_filters.size());
    size_t i = 0;
    for (auto const &p : m_filters) {
      auto f = p.first;
      pids[i] = f->m_pid;
      handles[i] = f->m_pif.hProcess;
      i++;
    }
    m_mutex.unlock();
    auto ret = WaitForMultipleObjects(handles.size(), handles.data(), FALSE, 1000);
    if (ret != WAIT_TIMEOUT) {
      auto i = ret - WAIT_OBJECT_0;
      if (0 <= i && i < pids.size()) {
        pid = pids[i];
      }
    }
#endif // _WIN32

    if (pid) {
      Log::debug(Log::SUBPROC, "[exec] child process exited [pid = %d]", pid);
      std::lock_guard<std::mutex> lock(m_mutex);
      Exec *f = nullptr;
      Net *n = nullptr;
      for (const auto &p : m_filters) {
        if (p.first->m_pid == pid) {
          f = p.first;
          n = p.second;
          break;
        }
      }
      if (f) {
        n->post(
          [=]() {
            {
              std::lock_guard<std::mutex> lock(m_mutex);
              if (m_filters.find(f) == m_filters.end()) return;
            }
            InputContext ic;
            f->output(StreamEnd::make());
          }
        );
      }
    }
  }

  Log::shutdown();
}

} // namespace pipy
