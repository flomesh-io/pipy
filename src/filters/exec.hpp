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
  Exec();
  Exec(const pjs::Value &command);

private:
  Exec(const Exec &r);
  ~Exec();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

private:
  pjs::Value m_command;
  pid_t m_pid = 0;
  FileStream* m_stdin = nullptr;
  FileStream* m_stdout = nullptr;
  bool m_session_end = false;

  class ChildProcessMonitor {
  public:
    ChildProcessMonitor() {
      schedule();
    }

    void on_exit(int pid, std::function<void()> callback) {
      m_on_exit[pid] = callback;
    }

  private:
    void schedule();
    void check();
    std::map<int, std::function<void()>> m_on_exit;
    Timer m_timer;
  };

  static auto child_process_monitor() -> ChildProcessMonitor*;
};

} // namespace pipy

#endif // EXEC_HPP