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
#include <sys/wait.h>

namespace pipy {

//
// Exec
//

Exec::Exec()
{
}

Exec::Exec(const pjs::Value &command)
  : m_command(command)
{
}

Exec::Exec(const Exec &r)
  : Exec(r.m_command)
{
}

Exec::~Exec()
{
}

auto Exec::help() -> std::list<std::string> {
  return {
    "exec(command)",
    "Spawns a child process and connects to its input/output",
    "command = <string|array|function> Command line to execute",
  };
}

void Exec::dump(std::ostream &out) {
  out << "exec";
}

auto Exec::clone() -> Filter* {
  return new Exec(*this);
}

void Exec::reset() {
  if (m_pid > 0) {
    child_process_monitor()->on_exit(m_pid, nullptr);
    kill(m_pid, SIGTERM);
  }
  m_pid = 0;
  if (m_stdin) {
    m_stdin->on_delete(nullptr);
    m_stdin->end();
    m_stdin = nullptr;
  }
  if (m_stdout) {
    m_stdout->on_delete(nullptr);
    m_stdout->on_read(nullptr);
    m_stdout->end();
    m_stdout = nullptr;
  }
  m_session_end = false;
}

void Exec::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (inp->is<SessionEnd>()) {
    if (m_stdin) {
      m_stdin->end();
    }
    m_session_end = true;
    return;
  }

  if (!m_pid) {
    pjs::Value ret;
    if (!eval(*ctx, m_command, ret)) return;

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

    m_stdin = new FileStream(in[1]);
    m_stdin->on_delete([this]() { m_stdin = nullptr; });

    m_stdout = new FileStream(out[0]);
    m_stdout->on_delete([this]() { m_stdout = nullptr; });
    m_stdout->on_read(
      [=](Event *inp) {
        output(inp);
        ctx->group()->notify(ctx);
      }
    );

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
      child_process_monitor()->on_exit(
        m_pid,
        [=]() {
          output(SessionEnd::make());
          ctx->group()->notify(ctx);
        }
      );
    }

    for (i = 0; i < argc; i++) free(argv[i]);
  }

  if (m_pid > 0 && m_stdin) {
    if (auto data = inp->as<Data>()) {
      m_stdin->write(data);
    } else if (inp->is<MessageEnd>()) {
      m_stdin->flush();
    }
  }
}

auto Exec::child_process_monitor() -> ChildProcessMonitor* {
  static ChildProcessMonitor monitor;
  return &monitor;
}

void Exec::ChildProcessMonitor::schedule() {
  m_timer.schedule(1, [this]() { check(); });
}

void Exec::ChildProcessMonitor::check() {
  int status;
  auto pid = waitpid(0, &status, WNOHANG);
  if (pid > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) {
    Log::debug("[exec] child process exited [pid = %d]", pid);
    auto i = m_on_exit.find(pid);
    if (i != m_on_exit.end()) {
      auto f = i->second;
      m_on_exit.erase(i);
      if (f) f();
    }
  }
  schedule();
}

} // namespace pipy