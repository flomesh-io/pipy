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
#include <sys/wait.h>

namespace pipy {

thread_local static Data::Producer s_dp("exec()");

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
    kill(m_pid, SIGTERM);
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
      Log::error("[exec] command is empty");
      return;
    }

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
      Log::error("[exec] unable to exec: %s", cmd.c_str());
      exit(-1);
    } else if (m_pid < 0) {
      Log::error("[exec] unable to fork");
    } else {
      s_child_process_monitor.monitor(m_pid, this);
    }

    for (i = 0; i < argc; i++) free(argv[i]);
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
    int status;
    auto pid = waitpid(0, &status, 0);
    if (pid > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) {
      Log::debug("[exec] child process exited [pid = %d]", pid);
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
