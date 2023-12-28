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
#ifdef _WIN32
#include <numeric>
#else
#include <sys/wait.h>
#include <unistd.h>
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
    s_child_process_monitor.remove(m_pid);
#ifndef _WIN32
    kill(m_pid, SIGTERM);
#else
    CloseHandle(m_pif.hThread);
    CloseHandle(m_pif.hProcess);
#endif
  }
  m_pid = 0;
  if (m_stdin) {
    m_stdin->close();
    m_stdin = nullptr;
  }
  if (m_stdout) {
    m_stdout->close();
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

    m_pid = fork();

    if (m_pid == 0) {
      dup2(in[0], 0);
      dup2(out[1], 1);
      execvp(argv[0], argv);
      std::string cmd;
      for (const auto &arg : args) {
        if (!cmd.empty()) cmd += ' ';
        cmd += arg;
      }
      Filter::error("unable to exec: %s", cmd.c_str());
      exit(-1);
    } else if (m_pid < 0) {
      Filter::error("unable to fork");
    } else {
      s_child_process_monitor.monitor(m_pid, this);
    }

    for (i = 0; i < argc; i++) free(argv[i]);
#else
    HANDLE read, write;
    TCHAR PipeNameBuf[MAX_PATH];
    auto nSize = 4096;
    SECURITY_ATTRIBUTES sa = {.nLength = sizeof(SECURITY_ATTRIBUTES),
                              .bInheritHandle = TRUE};

    sprintf(PipeNameBuf, "\\\\.\\pipe\\ExecFilter.%08x.%08x",
            GetCurrentProcessId(), InterlockedIncrement(&pipe_sn));
    read = CreateNamedPipeA(
        PipeNameBuf, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT, 1, nSize, nSize, 120 * 1000, &sa);
    if (INVALID_HANDLE_VALUE == read) {
      Filter::error("Unable to create named pipe due to %s",
                    utils::last_error("CreateNamedPipeA").c_str());
      return;
    }
    write = CreateFileA(PipeNameBuf, GENERIC_WRITE, 0, &sa, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (INVALID_HANDLE_VALUE == write) {
      Filter::error("Unable to create write file due to %s",
                    utils::last_error("CreateNamedPipeA").c_str());
      CloseHandle(read);
      return;
    }

    auto cmd = std::accumulate(
        std::next(args.begin()), args.end(), args.front(),
        [](std::string a, std::string b) { return a + " " + b; });

    m_stdin = FileStream::make(false, write, &s_dp);
    m_stdout = FileStream::make(true, read, &s_dp);
    m_stdout->chain(output());

    STARTUPINFO si = {.cb = sizeof(STARTUPINFO),
                      .dwFlags = STARTF_USESTDHANDLES,
                      .hStdInput = INVALID_HANDLE_VALUE,
                      .hStdOutput = write,
                      .hStdError = write};

    auto success = CreateProcess(NULL, const_cast<char *>(cmd.c_str()), NULL,
                                 NULL, TRUE, 0, NULL, NULL, &si, &m_pif);
    if (success == FALSE) {
      CloseHandle(write);
      CloseHandle(read);
      Filter::error("Unable to exec process due to %s",
                    utils::last_error("CreateProcess").c_str());
    }
    m_pid = m_pif.dwProcessId;
    s_child_process_monitor.monitor(m_pid, this);
#endif
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
    : m_wait_thread([this]() { wait(); })
{
  m_wait_thread.detach();
}

void Exec::ChildProcessMonitor::monitor(int pid, Exec *exec) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto &w = m_waiters[pid];
  w.net = &Net::current();
  w.filter = exec;
}

void Exec::ChildProcessMonitor::remove(int pid) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_waiters.erase(pid);
}

void Exec::ChildProcessMonitor::wait() {
  Log::init();
  for (;;) {
#ifndef _WIN32
    int status;
    auto pid = waitpid(-1, &status, 0);
    if (pid < 0) {
      sleep(1);
    } else if (pid > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) {
#else
    std::vector<PROCESS_INFORMATION> filters;
    int size = 0;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      for (auto const &w : m_waiters) filters.push_back(w.second.filter->pif());
      size = filters.size();
    }
    HANDLE handles[size];
    std::transform(filters.begin(), filters.end(), handles,
                   [](const PROCESS_INFORMATION &exe) {
                     return exe.hProcess;
                   }); 
    if (size <= 0) {
      Sleep(1000);
      continue;
    }
    auto status = WaitForMultipleObjects(size, handles, FALSE, INFINITE);
    if (WAIT_TIMEOUT == status) {
      Sleep(1000);
    } else {
      auto idx = status - WAIT_OBJECT_0;
      if (idx < 0 || idx > (size - 1)) {
        Sleep(1000);
        continue;
      }
      auto pid = filters.at(idx).dwProcessId;
#endif
      Log::debug(Log::SUBPROC, "[exec] child process exited [pid = %d]", pid);
      std::lock_guard<std::mutex> lock(m_mutex);
      auto i = m_waiters.find(pid);
      if (i != m_waiters.end()) {
        auto &w = i->second;
        auto *f = w.filter;
        w.net->post(
          [=]() {
          InputContext ic;
          f->output(StreamEnd::make());
        }
        );
        m_waiters.erase(i);
      }
    }
  }
}

} // namespace pipy
