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
#include "timer.hpp"
#include "nmi.hpp"

#include <list>
#include <set>
#include <vector>

namespace pipy {

class Thread;
class PipelineLayout;
class Task;
class Watch;

//
// Worker
//

class Worker : public pjs::RefCount<Worker> {
public:
  static auto make(bool is_graph_enabled = false) -> Worker* {
    return new Worker(is_graph_enabled);
  }

  static auto current() -> Worker* {
    return s_current;
  }

  auto root() const -> Module* { return m_root; }
  auto thread() const -> Thread* { return m_thread; }
  auto global_object() const -> pjs::Object* { return m_global_object; }
  bool handling_signal(int sig);
  auto find_js_module(const std::string &path) -> JSModule*;
  auto load_js_module(const std::string &path) -> JSModule*;
  auto load_native_module(const std::string &path) -> nmi::NativeModule*;
  void add_listener(Listener *listener, PipelineLayout *layout, const Listener::Options &options);
  void remove_listener(Listener *listener);
  bool update_listeners(bool force);
  void add_task(Task *task);
  void add_watch(Watch *watch);
  void add_exit(PipelineLayout *layout);
  void add_export(pjs::Str *ns, pjs::Str *name, Module *module);
  auto get_export(pjs::Str *ns, pjs::Str *name) -> int;
  auto new_loading_context() -> Context*;
  auto new_runtime_context(Context *base = nullptr) -> Context*;
  bool solve(pjs::Context &ctx, pjs::Str *filename, pjs::Value &result);
  bool bind();
  bool start(bool force);
  void stop(bool force);

private:
  Worker(bool is_graph_enabled);
  ~Worker();

  typedef pjs::PooledArray<pjs::Ref<pjs::Object>> ContextData;
  typedef std::map<pjs::Ref<pjs::Str>, Module*> Namespace;

  struct ListeningPipeline {
    PipelineLayout* pipeline_layout;
    Listener::Options options;
  };

  class Exit : public EventTarget {
  public:
    Exit(Worker *worker, PipelineLayout *pipeline_layout)
      : m_worker(worker)
      , m_pipeline_layout(pipeline_layout) {}
    bool done() const { return m_stream_end; }
    void start();
    void end();
    virtual void on_event(Event *evt) override;
  private:
    Worker* m_worker;
    pjs::Ref<PipelineLayout> m_pipeline_layout;
    pjs::Ref<Pipeline> m_pipeline;
    bool m_stream_end = false;
  };

  struct SolvedFile {
    int index;
    pjs::Ref<pjs::Str> filename;
    pjs::Source source;
    std::unique_ptr<pjs::Expr> expr;
    pjs::Value result;
    bool solving = false;
  };

  Module* m_root = nullptr;
  Timer m_keep_alive;
  pjs::Ref<Thread> m_thread;
  pjs::Ref<pjs::Object> m_global_object;
  std::vector<Module*> m_modules;
  std::map<std::string, JSModule*> m_module_map;
  std::map<std::string, nmi::NativeModule*> m_native_module_map;
  std::map<Listener*, ListeningPipeline> m_listeners;
  std::set<Task*> m_tasks;
  std::set<Watch*> m_watches;
  std::list<Exit*> m_exits;
  std::map<pjs::Ref<pjs::Str>, Namespace> m_namespaces;
  std::map<pjs::Ref<pjs::Str>, SolvedFile> m_solved_files;
  bool m_graph_enabled = false;

  auto new_module_index() -> int;
  void add_module(Module *m);
  void remove_module(int i);
  void keep_alive();
  void on_exit(Exit *exit);
  void end_all();

  thread_local static pjs::Ref<Worker> s_current;

  friend class pjs::RefCount<Worker>;
  friend class JSModule;
};

} // namespace pipy

#endif // WORKER_HPP
