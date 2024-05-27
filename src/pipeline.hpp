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

#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include "pjs/pjs.hpp"
#include "event.hpp"
#include "input.hpp"
#include "list.hpp"
#include "buffer.hpp"

#include <list>
#include <memory>
#include <set>

namespace pipy {

class Worker;
class ModuleBase;
class Pipeline;
class Filter;
class Context;
class InputContext;

//
// PipelineLayout
//

class PipelineLayout :
  public pjs::RefCountMT<PipelineLayout>,
  public List<PipelineLayout>::Item
{
public:

  //
  // PipelineLayout::Chain
  //

  struct Chain : public pjs::RefCount<Chain>, public pjs::Pooled<Chain> {
    pjs::Ref<Chain> next;
    pjs::Ref<PipelineLayout> layout;
  };

  static auto make() -> PipelineLayout* {
    return new PipelineLayout(nullptr, nullptr, -1, std::string(), std::string());
  }

  static auto make(Worker *worker) -> PipelineLayout* {
    return new PipelineLayout(worker, nullptr, -1, std::string(), std::string());
  }

  static auto make(ModuleBase *module) -> PipelineLayout* {
    return new PipelineLayout(nullptr, module, -1, std::string(), std::string());
  }

  static auto make(ModuleBase *module, const std::string &name, const std::string &label = std::string()) -> PipelineLayout* {
    return new PipelineLayout(nullptr, module, -1, name, label);
  }

  static auto make(ModuleBase *module, int index) -> PipelineLayout* {
    return new PipelineLayout(nullptr, module, index, std::string(), std::string());
  }

  static auto make(ModuleBase *module, int index, const std::string &name, const std::string &label = std::string()) -> PipelineLayout* {
    return new PipelineLayout(nullptr, module, index, name, label);
  }

  static auto active_pipeline_count() -> size_t {
    return s_active_pipeline_count;
  }

  static void for_each(std::function<void(PipelineLayout*)> callback) {
    for (auto *p = s_all_pipeline_layouts.head(); p; p = p->next()) {
      callback(p);
    }
  }

  auto worker() const -> Worker* { return m_worker; }
  auto module() const -> ModuleBase* { return m_module; }
  auto index() const -> int { return m_index; }
  auto name() const -> pjs::Str* { return m_name; }
  auto label() const -> pjs::Str* { return m_label; }
  auto name_or_label() const -> pjs::Str*;
  auto allocated() const -> size_t { return m_allocated; }
  auto active() const -> size_t { return m_pipelines.size(); }
  void on_start_location(pjs::Location &loc) { m_on_start_location = loc; }
  void on_start(pjs::Object *e) { m_on_start = e; }
  void on_end(pjs::Function *f) { m_on_end = f; }
  auto append(Filter *filter) -> Filter*;
  void bind();
  void shutdown();

  auto new_context() -> Context*;

private:
  PipelineLayout(Worker *worker, ModuleBase *module, int index, const std::string &name, const std::string &label);
  ~PipelineLayout();

  auto alloc(Context *ctx) -> Pipeline*;
  void end(Pipeline *pipeline, pjs::Value &result);
  void free(Pipeline *pipeline);

  int m_index;
  pjs::Ref<pjs::Str> m_name;
  pjs::Ref<pjs::Str> m_label;
  pjs::Ref<Worker> m_worker;
  pjs::Ref<ModuleBase> m_module;
  pjs::Ref<pjs::Object> m_on_start;
  pjs::Ref<pjs::Function> m_on_end;
  pjs::Location m_on_start_location;
  std::list<std::unique_ptr<Filter>> m_filters;
  Pipeline* m_pool = nullptr;
  List<Pipeline> m_pipelines;
  int m_allocated = 0;
  int m_active = 0;

  thread_local static List<PipelineLayout> s_all_pipeline_layouts;
  thread_local static size_t s_active_pipeline_count;

  friend class pjs::RefCountMT<PipelineLayout>;
  friend class Pipeline;
  friend class Graph;
};

//
// Pipeline
//

class Pipeline :
  public EventProxy,
  public AutoReleased,
  public List<Pipeline>::Item
{
public:
  static auto make(PipelineLayout *layout, Context *ctx) -> Pipeline* {
    return layout->alloc(ctx);
  }

  //
  // Pipeline::StartingPromiseCallback
  //

  class StartingPromiseCallback : public pjs::ObjectTemplate<StartingPromiseCallback, pjs::Promise::Callback> {
    StartingPromiseCallback(Pipeline *pipeline) : m_pipeline(pipeline) {}
    virtual void on_resolved(const pjs::Value &value) override;
    virtual void on_rejected(const pjs::Value &error) override;
    friend class pjs::ObjectTemplate<StartingPromiseCallback, pjs::Promise::Callback>;
    Pipeline* m_pipeline;
  public:
    void close() { m_pipeline = nullptr; }
  };

  //
  // Pipeline::ResultCallback
  //

  class ResultCallback {
    virtual void on_pipeline_result(Pipeline *p, pjs::Value &value) = 0;
    friend class Pipeline;
  };

  auto layout() const -> PipelineLayout* { return m_layout; }
  auto context() const -> Context* { return m_context; }
  auto chain() const -> PipelineLayout::Chain* { return m_chain; }
  void chain(Input *input) { EventProxy::chain(input); }
  void chain(PipelineLayout::Chain *chain, const pjs::Value &args = pjs::Value::undefined) { m_chain = chain; m_chain_args = args; }
  auto chain_args() const -> const pjs::Value& { return m_chain_args; }
  void start(const pjs::Value &args);
  auto start(int argc = 0, pjs::Value *argv = nullptr) -> Pipeline*;
  void on_end(ResultCallback *cb) { m_result_cb = cb; }

private:
  Pipeline(PipelineLayout *layout);
  ~Pipeline();

  virtual void on_input(Event *evt) override;
  virtual void on_reply(Event *evt) override;
  virtual void on_auto_release() override;

  PipelineLayout* m_layout;
  Pipeline* m_next_free = nullptr;
  List<Filter> m_filters;
  pjs::Ref<Context> m_context;
  pjs::Ref<StartingPromiseCallback> m_starting_promise_callback;
  pjs::Ref<PipelineLayout::Chain> m_chain;
  pjs::Value m_chain_args;
  EventBuffer m_pending_events;
  ResultCallback* m_result_cb = nullptr;
  bool m_started = false;

  void wait(pjs::Promise *promise);
  void resolve(const pjs::Value &value);
  void reject(const pjs::Value &value);
  void start_with(const pjs::Value &starting_events);
  void shutdown();
  void reset();

  friend class pjs::RefCount<Pipeline>;
  friend class PipelineLayout;
  friend class Filter;
};

} // namespace pipy

#endif // PIPELINE_HPP
