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
  Exec(const pjs::Value &command);

private:
  Exec(const Exec &r);
  ~Exec();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

private:
  pjs::Value m_command;
  pjs::Ref<FileStream> m_stdin;
  pjs::Ref<FileStream> m_stdout;
  int m_pid = 0;

#ifdef _WIN32
  PROCESS_INFORMATION m_pif = {};
#endif

  void on_process_exit();

  //
  // Exec::ChildProcessMonitor
  //

  class ChildProcessMonitor {
  public:
    ChildProcessMonitor();
    ~ChildProcessMonitor();

    void monitor(Exec *exec);
    void remove(Exec *exec);

  private:
    void wait();

    std::atomic<bool> m_exited;
    std::thread m_wait_thread;
    std::mutex m_mutex;
    std::map<Exec*, Net*> m_filters;
  };

  static ChildProcessMonitor s_child_process_monitor;
};

} // namespace pipy

#endif // EXEC_HPP
