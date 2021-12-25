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
#include "list.hpp"

#include <list>
#include <memory>
#include <set>

namespace pipy {

class Context;
class Module;
class Filter;
class Pipeline;
class Message;

//
// PipelineDef
//

class PipelineDef :
  public pjs::RefCount<PipelineDef>,
  public List<PipelineDef>::Item
{
public:
  enum Type {
    NAMED,
    LISTEN,
    TASK,
  };

  static auto make(Module *module, Type type, const std::string &name) -> PipelineDef* {
    return new PipelineDef(module, type, name);
  }

  static void for_each(std::function<void(PipelineDef*)> callback) {
    for (auto *p = s_all_pipeline_defs.head(); p; p = p->next()) {
      callback(p);
    }
  }

  auto module() const -> Module* { return m_module; }
  auto type() const -> Type { return m_type; }
  auto name() const -> const std::string& { return m_name; }
  auto allocated() const -> size_t { return m_allocated; }
  auto active() const -> size_t { return m_pipelines.size(); }
  auto append(Filter *filter) -> Filter*;
  void bind();
  void shutdown();

private:
  PipelineDef(Module *module, Type type, const std::string &name);
  ~PipelineDef();

  auto alloc(Context *ctx) -> Pipeline*;
  void free(Pipeline *pipeline);

  Type m_type;
  std::string m_name;
  std::list<std::unique_ptr<Filter>> m_filters;
  pjs::Ref<Module> m_module;
  Pipeline* m_pool = nullptr;
  List<Pipeline> m_pipelines;
  size_t m_allocated = 0;

  static List<PipelineDef> s_all_pipeline_defs;

  friend class pjs::RefCount<PipelineDef>;
  friend class Pipeline;
  friend class Graph;
};

//
// Pipeline
//

class Pipeline :
  public pjs::RefCount<Pipeline>,
  public List<Pipeline>::Item,
  public EventFunction
{
public:
  class AutoReleasePool
  {
  public:
    AutoReleasePool();
    ~AutoReleasePool();

  private:
    AutoReleasePool* m_next;
    Pipeline* m_pipelines = nullptr;
    bool m_cleaning_up = false;

    static AutoReleasePool* s_stack;

    static void add(Pipeline *pipeline);

    friend class Pipeline;
  };

  static auto make(PipelineDef *def, Context *ctx) -> Pipeline* {
    return def->alloc(ctx);
  }

  static void auto_release(Pipeline *pipeline) {
    if (pipeline) pipeline->auto_release();
  }

  auto def() const -> PipelineDef* { return m_def; }
  auto context() const -> Context* { return m_context; }

  virtual void chain(Input *input) override;

private:
  Pipeline(PipelineDef *def);
  ~Pipeline();

  virtual void on_event(Event *evt) override;

  void auto_release();
  void finalize();

  PipelineDef* m_def;
  Pipeline* m_next_free = nullptr;
  Pipeline* m_next_auto_release = nullptr;
  bool m_auto_release = false;
  List<Filter> m_filters;
  pjs::Ref<Context> m_context;

  void shutdown();
  void reset();

  friend class pjs::RefCount<Pipeline>;
  friend class PipelineDef;
  friend class Filter;
};

} // namespace pipy

#endif // PIPELINE_HPP
