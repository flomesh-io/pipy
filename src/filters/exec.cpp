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
#include "context.hpp"
#include "fstream.hpp"
#include "listener.hpp"
#include "utils.hpp"
#include "logging.hpp"

#include <stdlib.h>
#include <string.h>
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define WINDOWS
#include "win/sys_wait.h"
#else
#include <sys/wait.h>
#endif 

namespace pipy {

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

void Exec::dump(std::ostream &out) {
  out << "exec";
}

auto Exec::clone() -> Filter* {
  return new Exec(*this);
}

void Exec::reset() {
  Filter::reset();
  if (m_pid > 0) {
    child_process_monitor()->remove(m_pid);
#ifndef WINDOWS
    kill(m_pid, SIGTERM);
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
  static Data::Producer s_dp("exec");
#ifdef WINDOWS
  return;
#else
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

    m_stdin = FileStream::make(in[1], &s_dp);
    m_stdout = FileStream::make(out[0], &s_dp);
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
      child_process_monitor()->monitor(m_pid, this);
    }

    for (i = 0; i < argc; i++) free(argv[i]);
  }

  if (m_pid > 0 && m_stdin) {
    m_stdin->input()->input(evt);
  }
#endif
}

auto Exec::child_process_monitor() -> ChildProcessMonitor* {
  static ChildProcessMonitor monitor;
  return &monitor;
}

void Exec::ChildProcessMonitor::schedule() {
  m_timer.schedule(
    1.0,
    [this]() {
      check();
    }
  );
}

void Exec::ChildProcessMonitor::check() {
  int status;
  auto pid = waitpid(0, &status, WNOHANG);
  if (pid > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) {
    Log::debug("[exec] child process exited [pid = %d]", pid);
    auto i = m_processes.find(pid);
    if (i != m_processes.end()) {
      auto exec = i->second;
      auto ctx = exec->context();
      m_processes.erase(i);
      exec->output(StreamEnd::make());
      ctx->group()->notify(ctx);
    }
  }
  schedule();
}

} // namespace pipy
