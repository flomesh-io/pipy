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
#include "pjs/pjs.hpp"
#include "context.hpp"
#include "module.hpp"
#include "event.hpp"

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
// Table
//

class TableBase {
private:
  enum { SUB_TABLE_WIDTH = 8 };

  struct Entry {
    int next_free = 0;
    char data[0];
  };

  std::vector<char*> m_sub_tables;

  int m_size = 0;
  int m_free = 0;
  int m_data_size;

protected:
  TableBase(int data_size)
    : m_data_size(data_size) {}

  Entry* get_entry(int i, bool create) {
    int y = i & ((1 << SUB_TABLE_WIDTH) - 1);
    int x = i >> SUB_TABLE_WIDTH;
    if (x >= m_sub_tables.size()) {
      if (!create) return nullptr;
      m_sub_tables.resize(x + 1);
    }
    auto sub = m_sub_tables[x];
    if (!sub) {
      if (!create) return nullptr;
      auto *buf = new char[(sizeof(Entry) + m_data_size) * (1 << SUB_TABLE_WIDTH)];
      m_sub_tables[x] = sub = buf;
    }
    return reinterpret_cast<Entry*>(sub + (sizeof(Entry) + m_data_size) * y);
  }

  Entry* alloc_entry(int *i) {
    Entry *ent;
    auto id = m_free;
    if (!id) {
      id = ++m_size;
      ent = get_entry(id, true);
    } else {
      ent = get_entry(id, false);
      m_free = ent->next_free;
    }
    ent->next_free = -1;
    *i = id;
    return ent;
  }

  Entry* free_entry(int i) {
    if (auto *ent = get_entry(i, false)) {
      if (ent->next_free < 0) {
        ent->next_free = m_free;
        m_free = i;
        return ent;
      }
    }
    return nullptr;
  }
};

//
// Table
//

template<class T>
class Table : public TableBase {
public:
  Table() : TableBase(sizeof(T)) {}

  T* get(int i) {
    if (i <= 0) return nullptr;
    if (auto *ent = get_entry(i, false)) {
      return reinterpret_cast<T*>(ent->data);
    } else {
      return nullptr;
    }
  }

  template<typename... Args>
  int alloc(Args... args) {
    int i;
    auto *ent = alloc_entry(&i);
    new (ent->data) T(std::forward<Args>(args)...);
    return i;
  }

  void free(int i) {
    if (auto *ent = free_entry(i)) {
      ((T*)ent->data)->~T();
    }
  }
};

//
// NativeModule
//

class NativeModule : public pipy::Module {
public:
  static auto find(const std::string &filename) -> NativeModule*;
  static auto load(const std::string &filename, int index) -> NativeModule*;
  static auto current() -> NativeModule* { return m_current; }
  static void set_current(NativeModule *m) { m_current = m; }

  auto filename() const -> pjs::Str* { return m_filename; }
  void define_variable(int id, const char *name, const char *ns, const pjs::Value &value);
  void define_pipeline(const char *name, fn_pipeline_init init, fn_pipeline_free free, fn_pipeline_process process);
  auto pipeline_layout(pjs::Str *name) -> PipelineLayout*;

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

  pjs::Ref<pjs::Class> m_context_class;
  std::list<VariableDef> m_variable_defs;
  std::list<PipelineDef> m_pipeline_defs;
  std::list<Export> m_exports;
  std::map<pjs::Ref<pjs::Str>, PipelineLayout*> m_pipeline_layouts;
  PipelineLayout* m_entry_pipeline = nullptr;

  virtual void bind_exports(Worker *worker) override;
  virtual void bind_imports(Worker *worker) override {}
  virtual void make_pipelines() override {}
  virtual void bind_pipelines() override {}
  virtual auto new_context(Context *base) -> Context* override { return nullptr; }
  virtual auto new_context_data(pjs::Object *prototype) -> pjs::Object* override;
  virtual void unload() override {}

  thread_local static std::vector<NativeModule*> m_native_modules;
  thread_local static NativeModule* m_current;
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

  auto context() -> Context* { return m_context; }
  void input(Event *evt);
  void output(Event *evt);
  void free();

private:
  Pipeline(PipelineLayout *layout, Context *ctx, EventTarget::Input *out)
    : m_layout(layout)
    , m_id(m_pipeline_table.alloc(this))
    , m_context(ctx)
    , m_output(out)
  {
    NativeModule::set_current(layout->m_module);
    layout->m_pipeline_init(m_id, &m_user_ptr);
    NativeModule::set_current(nullptr);
  }

  PipelineLayout* m_layout;
  int m_id;
  void* m_user_ptr = nullptr;
  pjs::Ref<Context> m_context;
  pjs::Ref<EventTarget::Input> m_output;

  thread_local static Table<Pipeline*> m_pipeline_table;

  friend class PipelineLayout;
};



} // nmi
} // pipy

#endif // NMI_HPP
