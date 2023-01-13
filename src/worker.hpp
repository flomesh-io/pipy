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
#include "nmi.hpp"

#include <list>
#include <set>
#include <vector>

namespace pipy {

class PipelineLayout;
class Reader;
class Task;

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
  auto global_object() const -> pjs::Object* { return m_global_object; }
  bool handling_signal(int sig);
  auto find_js_module(const std::string &path) -> JSModule*;
  auto load_js_module(const std::string &path) -> JSModule*;
  auto load_native_module(const std::string &path) -> nmi::NativeModule*;
  void add_listener(Listener *listener, PipelineLayout *layout, const Listener::Options &options);
  void add_reader(Reader *reader);
  void add_task(Task *task);
  void add_export(pjs::Str *ns, pjs::Str *name, Module *module);
  auto get_export(pjs::Str *ns, pjs::Str *name) -> int;
  auto new_loading_context() -> Context*;
  auto new_runtime_context(Context *base = nullptr) -> Context*;
  bool solve(pjs::Context &ctx, pjs::Str *filename, pjs::Value &result);
  bool bind();
  bool start(bool force);
  void stop();

private:
  Worker(bool is_graph_enabled);
  ~Worker();

  typedef pjs::PooledArray<pjs::Ref<pjs::Object>> ContextData;
  typedef std::map<pjs::Ref<pjs::Str>, Module*> Namespace;

  struct ListeningPipeline {
    PipelineLayout* pipeline_layout;
    Listener::Options options;
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
  pjs::Ref<pjs::Object> m_global_object;
  std::vector<Module*> m_modules;
  std::map<std::string, JSModule*> m_module_map;
  std::map<std::string, nmi::NativeModule*> m_native_module_map;
  std::map<Listener*, ListeningPipeline> m_listeners;
  std::set<Reader*> m_readers;
  std::set<Task*> m_tasks;
  std::map<pjs::Ref<pjs::Str>, Namespace> m_namespaces;
  std::map<pjs::Ref<pjs::Str>, SolvedFile> m_solved_files;
  bool m_graph_enabled = false;

  auto new_module_index() -> int;
  void add_module(Module *m);
  void remove_module(int i);

  thread_local static pjs::Ref<Worker> s_current;

  friend class pjs::RefCount<Worker>;
  friend class JSModule;
};

} // namespace pipy

#endif // WORKER_HPP
