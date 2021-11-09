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

#include <unistd.h>
#include <map>

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
  virtual void dump(std::ostream &out) override;

private:
  pjs::Value m_command;
  pid_t m_pid = 0;
  pjs::Ref<FileStream> m_stdin;
  pjs::Ref<FileStream> m_stdout;
  pjs::Ref<EventTarget::Input> m_output;

  class ChildProcessMonitor {
  public:
    ChildProcessMonitor() {
      schedule();
    }

    void monitor(int pid, EventTarget::Input *output) {
      m_processes[pid] = output;
    }

  private:
    void schedule();
    void check();
    std::map<int, pjs::Ref<EventTarget::Input>> m_processes;
    Timer m_timer;
  };

  static auto child_process_monitor() -> ChildProcessMonitor*;
};

} // namespace pipy

#endif // EXEC_HPP
