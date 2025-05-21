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

#ifndef WORKER_HPP
#define WORKER_HPP

#include "context.hpp"
#include "listener.hpp"
#include "message.hpp"
#include "signal.hpp"

#include <list>
#include <memory>
#include <set>
#include <vector>

namespace pipy {

class Thread;
class PipelineLayout;

//
// Worker
//

class Worker : public pjs::RefCount<Worker>, public pjs::Instance {
public:
  static auto make(pjs::Promise::Period *period) -> Worker* {
    return new Worker(period);
  }

  static auto current() -> Worker* {
    return s_current;
  }

  void set_current() { s_current = this; }
  auto root_fiber() const -> pjs::Fiber* { return m_root_fiber; }
  auto new_context(Context *base = nullptr) -> Context*;
  auto load_module(pjs::Module *referer, const std::string &path, pjs::Value &result) -> pjs::Module*;
  auto load_module(pjs::Module *referer, const std::string &path) -> pjs::Module*;
  auto load_module(const std::string &path, pjs::Value &result) -> pjs::Module*;
  void add_listener(Listener *listener);
  void remove_listener(Listener *listener);
  void stop(bool force);

private:
  Worker(pjs::Promise::Period *period);
  ~Worker();

  pjs::Ref<pjs::Promise::Period> m_period;
  pjs::Ref<pjs::Fiber> m_root_fiber;
  std::map<std::string, std::unique_ptr<pjs::Module>> m_module_map;
  std::set<PipelineLayout*> m_pipeline_templates;
  std::set<Listener*> m_listeners;
  bool m_waiting_for_exit_callbacks = false;

  void stop_all();

  void append_pipeline_template(PipelineLayout *pt);
  void remove_pipeline_template(PipelineLayout *pt);

  thread_local static pjs::Ref<Worker> s_current;

  friend class pjs::RefCount<Worker>;
  friend class PipelineLayout;
};

} // namespace pipy

#endif // WORKER_HPP
