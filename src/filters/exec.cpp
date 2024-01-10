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

//
// Exec::Options
//

Exec::Options::Options(pjs::Object *options) {
  Value(options, "stderr")
    .get(stderr_buffer)
    .check_nullable();
  Value(options, "onExit")
    .get(on_exit_f)
    .check_nullable();
}

//
// Exec
//

Exec::Exec(const pjs::Value &command, const Options &options)
  : m_options(options)
  , m_command(command)
  , m_stdout_reader(this)
  , m_stderr_reader(this)
{
}

Exec::Exec(const Exec &r)
  : m_options(r.m_options)
  , m_command(r.m_command)
  , m_stdout_reader(this)
  , m_stderr_reader(this)
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
  if (m_child_proc.pid > 0) {
    s_child_process_monitor.remove(this);
#ifndef _WIN32
    kill(m_child_proc.pid, SIGTERM);
#else
    TerminateProcess(m_child_proc.process, 0);
#endif
    m_child_proc.pid = 0;
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
  if (m_stderr) {
    m_stderr->close();
    m_stderr->chain(nullptr);
    m_stderr = nullptr;
  }
#ifdef _WIN32
  m_pipe_stdin.close();
  m_pipe_stdout.close();
  m_pipe_stderr.close();
#endif
  m_stdout_reader.reset();
  m_stderr_reader.reset();
  m_child_proc_exited = false;
}

void Exec::process(Event *evt) {
  if (!m_child_proc.pid) {
    pjs::Value ret;
    if (!eval(m_command, ret)) return;

    if (ret.is_array()) {
      std::list<std::string> args;
      ret.as<pjs::Array>()->iterate_all(
        [&](pjs::Value &v, int) {
          auto *s = v.to_string();
          args.push_back(s->str());
          s->release();
        }
      );
      exec_argv(args);

    } else {
      auto *s = ret.to_string();
      exec_line(s->str());
      s->release();
    }

    if (m_child_proc.pid > 0) {
      s_child_process_monitor.add(this);
    }
  }

  if (m_child_proc.pid > 0 && m_stdin) {
    m_stdin->input()->input(evt);
  }
}

void Exec::on_process_exit(int exit_code) {
  m_child_proc_exit_code = exit_code;
  m_child_proc_exited = true;
  check_ending();
}

void Exec::check_ending() {
  if (m_child_proc_exited && m_stdout_reader.ended() && m_stderr_reader.ended()) {
    if (auto f = m_options.on_exit_f.get()) {
      pjs::Value args[2], ret;
      args[0].set(m_child_proc_exit_code);
      int argc = 1;
      if (m_options.stderr_buffer) {
        args[argc++].set(Data::make(std::move(m_stderr_reader.buffer())));
      }
      Filter::callback(f, argc, args, ret);
    }
    Filter::output(StreamEnd::make());
  }
}

#ifndef _WIN32

bool Exec::exec_argv(const std::list<std::string> &args) {
  auto argc = args.size();
  if (!argc) {
    Filter::error("exec() with no arguments");
    return false;
  }

  size_t i = 0;
  pjs::vl_array<char*> argv(argc + 1);
  for (const auto &arg : args) argv[i++] = strdup(arg.c_str());
  argv[argc] = nullptr;

  int in[2], out[2], err[2];
  pipe(in);
  pipe(out);
  pipe(err);

  m_stdin = FileStream::make(false, in[1], &s_dp);
  m_stdout = FileStream::make(true, out[0], &s_dp);
  m_stderr = FileStream::make(true, err[0], &s_dp);
  m_stdout->chain(m_stdout_reader.input());
  m_stderr->chain(m_stderr_reader.input());

  auto pid = fork();

  if (pid == 0) {
    dup2(in[0], 0);
    dup2(out[1], 1);
    dup2(err[1], 2);
    execvp(argv[0], argv);
    std::terminate();
  }

  for (i = 0; i < argc; i++) free(argv[i]);

  if (pid < 0) {
    Filter::error("unable to fork");
    return false;
  }

  ::close(in[0]);
  ::close(out[1]);
  ::close(err[1]);
  m_child_proc.pid = pid;

  Log::debug(Log::SUBPROC,
    "[exec] child process started [pid = %d]: %s",
    pid, args.front().c_str()
  );

  return true;
}

bool Exec::exec_line(const std::string &line) {
  return exec_argv(utils::split_argv(line));
}

#else // _WIN32

bool Exec::exec_argv(const std::list<std::string> &args) {
  return exec_line(os::windows::encode_argv(args));
}

bool Exec::exec_line(const std::string &line) {
  if (!m_pipe_stdin.open(this, "stdin", false)) return false;
  if (!m_pipe_stdout.open(this, "stdout", true)) return false;
  if (!m_pipe_stderr.open(this, "stderr", true)) return false;

  STARTUPINFOW si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(STARTUPINFO);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = m_pipe_stdin.file;
  si.hStdOutput = m_pipe_stdout.file;
  si.hStdError = m_pipe_stderr.file;

  auto line_w = os::windows::a2w(line);
  pjs::vl_array<wchar_t, 1000> buf_line(line_w.length() + 1);
  std::memcpy(buf_line.data(), line_w.c_str(), buf_line.size() * sizeof(wchar_t));

  PROCESS_INFORMATION pi;
  if (!CreateProcessW(
    NULL,
    buf_line.data(),
    NULL, NULL, TRUE, 0, NULL, NULL,
    &si, &pi
  )) {
    Filter::error(
      "unable to create process '%s': %s",
      line.c_str(),
      os::windows::get_last_error().c_str()
    );
    return false;
  }

  m_child_proc.pid = pi.dwProcessId;
  m_child_proc.process = pi.hProcess;
  m_child_proc.thread = pi.hThread;

  m_stdin = FileStream::make(false, m_pipe_stdin.pipe, &s_dp);
  m_stdout = FileStream::make(true, m_pipe_stdout.pipe, &s_dp);
  m_stderr = FileStream::make(true, m_pipe_stderr.pipe, &s_dp);
  m_stdout->chain(m_stdout_reader.input());
  m_stderr->chain(m_stderr_reader.input());
  m_pipe_stdin.pipe = INVALID_HANDLE_VALUE;
  m_pipe_stdout.pipe = INVALID_HANDLE_VALUE;
  m_pipe_stderr.pipe = INVALID_HANDLE_VALUE;

  CloseHandle(m_pipe_stdin.file);
  CloseHandle(m_pipe_stdout.file);
  CloseHandle(m_pipe_stderr.file);
  m_pipe_stdin.file = INVALID_HANDLE_VALUE;
  m_pipe_stdout.file = INVALID_HANDLE_VALUE;
  m_pipe_stderr.file = INVALID_HANDLE_VALUE;

  Log::debug(Log::SUBPROC,
    "[exec] child process started [pid = %d]: %s",
    m_child_proc.pid, line.c_str()
  );

  return true;
}

//
// Exec::StdioPipe
//

bool Exec::StdioPipe::open(Filter *filter, const char *postfix, bool is_output) {
  char pipe_name[100];
  static std::atomic<int> s_pipe_unique_id(0);
  std::snprintf(
    pipe_name, sizeof(pipe_name),
    "\\\\.\\pipe\\pipy.filter.exec.%08x.%08x.%s",
    GetCurrentProcessId(),
    s_pipe_unique_id.fetch_add(1),
    postfix
  );

  pipe = CreateNamedPipeA(
    pipe_name,
    FILE_FLAG_OVERLAPPED | (is_output ? PIPE_ACCESS_INBOUND: PIPE_ACCESS_OUTBOUND),
    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
    1, DATA_CHUNK_SIZE, DATA_CHUNK_SIZE, 0, NULL
  );

  if (pipe == INVALID_HANDLE_VALUE) {
    filter->error(
      "unable to create named pipe '%s': %s",
      pipe_name,
      os::windows::get_last_error().c_str()
    );
    return false;
  }

  SECURITY_ATTRIBUTES sa;
  ZeroMemory(&sa, sizeof(sa));
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;

  file = CreateFileA(
    pipe_name,
    is_output ? GENERIC_WRITE : GENERIC_READ,
    0, &sa,
    OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED,
    NULL
  );

  if (file == INVALID_HANDLE_VALUE) {
    CloseHandle(pipe);
    filter->error(
      "unable to create file for named pipe '%s': %s",
      pipe_name,
      os::windows::get_last_error().c_str()
    );
    return false;
  }

  return true;
}

void Exec::StdioPipe::close() {
  if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}

#endif // _WIN32

//
// Exec::StdoutReader
//

void Exec::StdoutReader::on_event(Event *evt) {
  if (evt->is<StreamEnd>()) {
    evt->retain();
    evt->release();
    m_ended = true;
    m_exec->check_ending();
  } else {
    m_exec->Filter::output(evt);
  }
}

//
// Exec::StderrReader
//

void Exec::StderrReader::on_event(Event *evt) {
  if (auto data = evt->as<Data>()) {
    if (m_exec->m_options.stderr_buffer) {
      m_buffer.push(*data);
    } else {
      m_exec->Filter::output(evt);
    }
  } else if (evt->is<StreamEnd>()) {
    evt->retain();
    evt->release();
    m_ended = true;
    m_exec->check_ending();
  }
}

//
// Exec::ChildProcessMoinitor
//

Exec::ChildProcessMonitor Exec::s_child_process_monitor;

Exec::ChildProcessMonitor::ChildProcessMonitor()
  : m_exited(false)
  , m_thread([this]() { main(); })
{
}

Exec::ChildProcessMonitor::~ChildProcessMonitor() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_exited.store(true);
    m_workload_cv.notify_one();
  }
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void Exec::ChildProcessMonitor::add(Exec *exec) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto &m = m_monitors[exec->m_child_proc.pid];
  m.proc = exec->m_child_proc;
  m.exec = exec;
  m.net = &Net::current();
  m_workload_cv.notify_one();
}

void Exec::ChildProcessMonitor::remove(Exec *exec) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto i = m_monitors.find(exec->m_child_proc.pid);
  if (i != m_monitors.end()) i->second.exec = nullptr;
}

void Exec::ChildProcessMonitor::main() {
  Log::init();

  while (!m_exited.load()) {
    int pid = 0, exit_code = 0;
    std::unique_lock<std::mutex> lock(m_mutex);
    m_workload_cv.wait(lock, [&]{ return !m_monitors.empty() || m_exited.load(); });

#ifndef _WIN32
    lock.unlock();
    int status;
    auto ret = wait(&status);
    if (ret > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) {
      pid = ret;
      exit_code = WEXITSTATUS(status);
    }
#else // _WIN32
    size_t n = m_monitors.size(), i = 0;
    pjs::vl_array<int> pids(n);
    pjs::vl_array<HANDLE> handles(n);
    for (auto const &p : m_monitors) {
      const auto &m = p.second;
      if (m.proc.pid) {
        pids[i] = m.proc.pid;
        handles[i] = m.proc.process;
        i++;
      }
    }
    lock.unlock();
    if (i > 0) {
      auto ret = WaitForMultipleObjects(i, handles.data(), FALSE, 1000);
      if (ret != WAIT_TIMEOUT) {
        auto i = ret - WAIT_OBJECT_0;
        if (0 <= i && i < pids.size()) {
          DWORD code;
          GetExitCodeProcess(handles[i], &code);
          pid = pids[i];
          exit_code = code;
        }
      }
    }
#endif // _WIN32

    if (pid) {
      Log::debug(Log::SUBPROC, "[exec] child process exited [pid = %d]", pid);
      std::lock_guard<std::mutex> lock(m_mutex);
      auto i = m_monitors.find(pid);
      if (i != m_monitors.end()) {
        auto &m = i->second;
        m.proc.pid = 0;
        m.net->post(
          [=]() {
            Exec *exec = nullptr;
            {
              std::lock_guard<std::mutex> lock(m_mutex);
              auto i = m_monitors.find(pid);
              if (i != m_monitors.end()) {
                const auto &m = i->second;
#ifdef _WIN32
                CloseHandle(m.proc.process);
                CloseHandle(m.proc.thread);
#endif
                exec = m.exec;
                m_monitors.erase(i);
              }
            }
            if (exec) {
              InputContext ic;
              exec->on_process_exit(exit_code);
            }
          }
        );
      }
    }
  }

  Log::shutdown();
}

} // namespace pipy
