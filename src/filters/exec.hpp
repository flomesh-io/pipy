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

#ifndef EXEC_HPP
#define EXEC_HPP

#include "filter.hpp"
#include "timer.hpp"
#include "os-platform.hpp"
#include "options.hpp"

#include <atomic>
#include <map>
#include <mutex>

namespace pipy {

class FileStream;

//
// Exec
//

class Exec : public Filter {
public:
  struct Options : public pipy::Options {
    bool std_err = false;
    bool pty = false;
    pjs::Ref<pjs::Function> on_start_f;
    pjs::Ref<pjs::Function> on_exit_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Exec(const pjs::Value &command, const Options &options = Options());

private:
  Exec(const Exec &r);
  ~Exec();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  //
  // Exec::StdoutReader
  //

  class StdoutReader : public EventTarget {
  public:
    StdoutReader(Exec *exec) : m_exec(exec) {}
    bool ended() const { return m_ended; }
    void reset() { m_ended = false; }
  private:
    Exec* m_exec;
    bool m_ended = false;
    virtual void on_event(Event *evt) override;
  };

  //
  // Exec::StderrReader
  //

  class StderrReader : public EventTarget {
  public:
    StderrReader(Exec *exec) : m_exec(exec) {}
    auto buffer() -> Data& { return m_buffer; }
    bool ended() const { return m_ended; }
    void reset() { m_buffer.clear(); m_ended = false; }
  private:
    Exec* m_exec;
    Data m_buffer;
    bool m_ended = false;
    virtual void on_event(Event *evt) override;
  };

#ifndef _WIN32

  struct ChildProcess { int pid = 0; };

#else // _WIN32

  struct ChildProcess { int pid = 0; HANDLE process, thread; };

  struct StdioPipe {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool open(Filter *filter, const char *postfix, bool is_output);
    void close();
  };

  StdioPipe m_pipe_stdin;
  StdioPipe m_pipe_stdout;
  StdioPipe m_pipe_stderr;

#endif // _WIN32

  Options m_options;
  pjs::Value m_command;
  pjs::Ref<FileStream> m_stdin;
  pjs::Ref<FileStream> m_stdout;
  pjs::Ref<FileStream> m_stderr;
  ChildProcess m_child_proc;
  StdoutReader m_stdout_reader;
  StderrReader m_stderr_reader;
  int m_child_proc_exit_code = 0;
  bool m_child_proc_exited = false;

  void on_process_exit(int exit_code);
  void check_ending();
  bool exec_argv(const std::list<std::string> &args);
  bool exec_line(const std::string &line);
  void kill_process();

  //
  // Exec::ChildProcessMonitor
  //

  class ChildProcessMonitor {
  public:
    ChildProcessMonitor();
    ~ChildProcessMonitor();

    void add(Exec *exec);
    void remove(Exec *exec);

  private:
    void main();

    struct Monitor {
      ChildProcess proc;
      Exec *exec;
      Net *net;
    };

    std::atomic<bool> m_exited;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_workload_cv;
    std::map<int, Monitor> m_monitors;
  };

  static ChildProcessMonitor s_child_process_monitor;
};

} // namespace pipy

#endif // EXEC_HPP
