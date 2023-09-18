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

#ifndef NMI_HPP
#define NMI_HPP

#include "pipy/nmi.h"
#include "pipy/nmi-cpp.h"
#include "pjs/pjs.hpp"
#include "context.hpp"
#include "module.hpp"
#include "event.hpp"
#include "table.hpp"

#include <list>
#include <string>
#include <vector>

namespace pipy {

class Worker;

namespace nmi {

class Pipeline;
class PipelineLayout;
class NativeModule;

//
// NativeModule
//

class NativeModule : public pipy::Module {
public:
  static auto find(const std::string &filename) -> NativeModule*;
  static auto load(const std::string &filename, int index) -> NativeModule*;
  static auto current() -> NativeModule* { return m_current; }
  static void set_current(NativeModule *m) { m_current = m; }

  auto net() const -> Net* { return m_net; }
  auto filename() const -> pjs::Str* { return m_filename; }
  auto define_variable(int id, const char *name, const char *ns, const pjs::Value &value) -> int;
  void define_pipeline(const char *name, fn_pipeline_init init, fn_pipeline_free free, fn_pipeline_process process);
  auto pipeline_layout(pjs::Str *name) -> PipelineLayout*;
  void schedule(double timeout, const std::function<void()> &fn);

private:
  NativeModule(int index, const std::string &filename);

  struct VariableDef {
    int id;
    std::string name;
    pjs::Ref<pjs::Str> ns;
    pjs::Value value;
  };

  struct PipelineDef {
    pjs::Ref<pjs::Str> name;
    fn_pipeline_init init;
    fn_pipeline_free free;
    fn_pipeline_process process;
  };

  struct Export {
    pjs::Ref<pjs::Str> ns;
    pjs::Ref<pjs::Str> name;
  };

  struct Timeout : public pjs::Pooled<Timeout> {
    Timer timer;
  };

  Net* m_net;
  pjs::Ref<pjs::Class> m_context_class;
  std::list<VariableDef> m_variable_defs;
  std::list<PipelineDef> m_pipeline_defs;
  std::list<Export> m_exports;
  std::map<pjs::Ref<pjs::Str>, PipelineLayout*> m_pipeline_layouts;
  PipelineLayout* m_entry_pipeline = nullptr;

  void callback(const std::function<void()> &fn);

  virtual void bind_exports(Worker *worker) override;
  virtual void bind_imports(Worker *worker) override {}
  virtual void make_pipelines() override {}
  virtual void bind_pipelines() override {}
  virtual auto new_context(Context *base) -> Context* override { return nullptr; }
  virtual auto new_context_data(pjs::Object *prototype) -> pjs::Object* override;
  virtual void unload() override {}

  thread_local static std::vector<NativeModule*> m_native_modules;
  thread_local static NativeModule* m_current;
  thread_local static int m_last_variable_id;
};

//
// PipelineLayout
//

class PipelineLayout {
public:
  PipelineLayout(
    NativeModule *mod,
    fn_pipeline_init init,
    fn_pipeline_free free,
    fn_pipeline_process process
  )
    : m_module(mod)
    , m_pipeline_init(init)
    , m_pipeline_free(free)
    , m_pipeline_process(process) {}

private:
  NativeModule* m_module;
  fn_pipeline_init m_pipeline_init;
  fn_pipeline_free m_pipeline_free;
  fn_pipeline_process m_pipeline_process;

  friend class Pipeline;
};

//
// Pipeline
//

class Pipeline : public pjs::Pooled<Pipeline> {
public:
  static auto get(int id) -> Pipeline* {
    auto *pp = m_pipeline_table.get(id);
    return pp ? *pp : nullptr;
  }

  static auto make(PipelineLayout *layout, Context *ctx, EventTarget::Input *out) -> Pipeline* {
    return new Pipeline(layout, ctx, out);
  }

  auto module() -> NativeModule* { return m_layout->m_module; }
  auto context() -> Context* { return m_context; }
  void check_thread();
  void input(Event *evt);
  void output(Event *evt);
  void retain() { m_retain_count.fetch_add(1, std::memory_order_relaxed); }
  void release();

private:
  Pipeline(PipelineLayout *layout, Context *ctx, EventTarget::Input *out);

  PipelineLayout* m_layout;
  int m_id;
  void* m_user_ptr = nullptr;
  pjs::Ref<Context> m_context;
  pjs::Ref<EventTarget::Input> m_output;
  std::atomic<int> m_retain_count;

  static SharedTable<Pipeline*> m_pipeline_table;

  friend class PipelineLayout;
};

//
// NativeObject
//

class NativeObject : public pjs::ObjectTemplate<NativeObject> {
public:
  auto ptr() const -> void* { return m_ptr; }

private:
  NativeObject(void *ptr, fn_object_free free)
    : m_ptr(ptr)
    , m_free(free) {}

  ~NativeObject() {
    if (m_free) {
      m_free(m_ptr);
    }
  }

  void* m_ptr;
  fn_object_free m_free;

  friend class pjs::ObjectTemplate<NativeObject>;
};

} // nmi
} // pipy

#endif // NMI_HPP
