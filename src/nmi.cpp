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

#include "nmi.hpp"
#include "list.hpp"
#include "worker.hpp"

#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <list>

#include <dlfcn.h>

namespace pipy {
namespace nmi {

thread_local static Data::Producer s_dp("NMI");

//
// Value
//

struct Value {
  pjs::Value v;
  int hold_count = 0;

  template<typename... Args>
  Value(Args... args)
    : v(std::forward<Args>(args)...) {}
};

thread_local static Table<Value> s_values;

//
// LocalRef
//

struct LocalRef :
  public pjs::Pooled<LocalRef>,
  public List<LocalRef>::Item
{
  int id;
};

//
// LocalRefPool
//

class LocalRefPool {
public:
  static LocalRefPool* current() {
    return s_current;
  }

  static void add(int id) {
    if (s_current) {
      if (auto *v = s_values.get(id)) {
        auto *ref = new LocalRef;
        ref->id = id;
        s_current->m_values.push(ref);
        v->hold_count++;
      }
    }
  }

  LocalRefPool() {
    m_back = s_current;
    s_current = this;
  }

  ~LocalRefPool() {
    auto *p = m_values.head();
    while (p) {
      auto *ref = p; p = p->next();
      if (auto *v = s_values.get(ref->id)) {
        if (!--v->hold_count) {
          s_values.free(ref->id);
        }
      }
      delete ref;
    }
    s_current = m_back;
  }

private:
  LocalRefPool* m_back;
  List<LocalRef> m_values;

  thread_local static LocalRefPool* s_current;
};

thread_local LocalRefPool* LocalRefPool::s_current = nullptr;

//
// Pipeline
//

SharedTable<Pipeline*> Pipeline::m_pipeline_table;

void Pipeline::check_thread() {
  if (module()->net() != &Net::current()) {
    throw std::runtime_error("operating native pipeline from a different thread");
  }
}

void Pipeline::input(Event *evt) {
  LocalRefPool lrf;
  auto e = nmi::s_values.alloc(evt);
  lrf.add(e);
  NativeModule::set_current(module());
  m_layout->m_pipeline_process(m_id, m_user_ptr, e);
  NativeModule::set_current(nullptr);
}

void Pipeline::output(Event *evt) {
  m_output->input(evt);
}

void Pipeline::release() {
  if (m_retain_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    NativeModule::set_current(module());
    m_layout->m_pipeline_free(m_id, m_user_ptr);
    NativeModule::set_current(nullptr);
    m_pipeline_table.free(m_id);
    delete this;
  }
}

//
// NativeModule
//

thread_local std::vector<NativeModule*> NativeModule::m_native_modules;
thread_local NativeModule* NativeModule::m_current = nullptr;

auto NativeModule::find(const std::string &filename) -> NativeModule* {
  for (auto *m : m_native_modules) {
    if (m && m->filename()->str() == filename) return m;
  }
  return nullptr;
}

auto NativeModule::load(const std::string &filename, int index) -> NativeModule* {
  auto *m = new NativeModule(index, filename);
  if (m_native_modules.size() <= index) {
    m_native_modules.resize(index + 1);
  }
  m_native_modules[index] = m;
  return m;
}

NativeModule::NativeModule(int index, const std::string &filename)
  : Module(index)
  , m_net(&Net::current())
{
  m_filename = pjs::Str::make(filename);

  auto *handle = dlopen(filename.c_str(), RTLD_NOW);
  if (!handle) {
    std::string msg("cannot load native module '");
    std::string err(dlerror());
    throw std::runtime_error(msg + filename + "' due to: " + err);
  }

  auto *init_fn = dlsym(handle, "pipy_module_init");
  if (!init_fn) {
    std::string msg("pipy_module_init() not found in native module ");
    throw std::runtime_error(msg + filename);
  }

  set_current(this);
  (*(fn_pipy_module_init)init_fn)();
  set_current(nullptr);

  std::list<pjs::Field*> fields;

  for (const auto &vd : m_variable_defs) {
    for (auto &prev : fields) {
      if (prev->name()->str() == vd.name) {
        std::string msg("duplicated variables ");
        msg += vd.name;
        msg += " in native module ";
        msg += filename;
        throw std::runtime_error(msg + filename);
      }
    }
    auto *v = pjs::Variable::make(
      vd.name, vd.value,
      pjs::Field::Enumerable | pjs::Field::Writable,
      vd.id
    );
    fields.push_back(v);
    if (vd.ns) {
      m_exports.emplace_back();
      m_exports.back().ns = vd.ns;
      m_exports.back().name = v->name();
    }
  }

  m_context_class = pjs::Class::make(
    "ContextData",
    pjs::class_of<ContextDataBase>(),
    fields
  );

  for (const auto &pd : m_pipeline_defs) {
    if (pd.name && pd.name != pjs::Str::empty) {
      if (m_pipeline_layouts.count(pd.name) > 0) {
        std::string msg("duplicated pipeline ");
        msg += pd.name->str();
        msg += " in native module ";
        msg += filename;
        throw std::runtime_error(msg + filename);
      }
      m_pipeline_layouts[pd.name] = new PipelineLayout(this, pd.init, pd.free, pd.process);
    } else {
      m_entry_pipeline = new PipelineLayout(this, pd.init, pd.free, pd.process);
    }
  }
}

void NativeModule::define_variable(int id, const char *name, const char *ns, const pjs::Value &value) {
  m_variable_defs.emplace_back();
  auto &v = m_variable_defs.back();
  v.id = id;
  v.name = name;
  v.ns = ns ? pjs::Str::make(ns) : nullptr;
  v.value = value;
}

void NativeModule::define_pipeline(const char *name, fn_pipeline_init init, fn_pipeline_free free, fn_pipeline_process process) {
  m_pipeline_defs.emplace_back();
  auto &p = m_pipeline_defs.back();
  p.name = name ? pjs::Str::make(name) : nullptr;
  p.init = init;
  p.free = free;
  p.process = process;
}

auto NativeModule::pipeline_layout(pjs::Str *name) -> PipelineLayout* {
  if (name) {
    auto i = m_pipeline_layouts.find(name);
    if (i == m_pipeline_layouts.end()) return nullptr;
    return i->second;
  } else {
    return m_entry_pipeline;
  }
}

void NativeModule::schedule(double timeout, const std::function<void()> &fn) {
  m_net->post(
    [=]() {
      if (timeout > 0) {
        auto *tmo = new Timeout;
        tmo->timer.schedule(timeout, [=]() {
          delete tmo;
          callback(fn);
        });
      } else {
        InputContext ic;
        callback(fn);
      }
    }
  );
}

void NativeModule::callback(const std::function<void()> &fn) {
  LocalRefPool lrf;
  set_current(this);
  fn();
  set_current(nullptr);
}

void NativeModule::bind_exports(Worker *worker) {
  for (auto &i : m_exports) {
    worker->add_export(i.ns, i.name, this);
  }
}

auto NativeModule::new_context_data(pjs::Object *prototype) -> pjs::Object* {
  auto obj = new ContextDataBase(m_filename);
  m_context_class->init(obj, prototype);
  return obj;
}

} // namespace nmi
} // namespace pipy

using namespace pipy;

template<typename T>
inline pjs_value to_local_value(T v) {
  auto i = nmi::s_values.alloc(v);
  nmi::LocalRefPool::add(i);
  return i;
}

NMI_EXPORT pjs_value pjs_undefined() {
  return to_local_value(pjs::Value::undefined);
}

NMI_EXPORT pjs_value pjs_boolean(int b) {
  return to_local_value(bool(b));
}

NMI_EXPORT pjs_value pjs_number(double n) {
  return to_local_value(n);
}

NMI_EXPORT pjs_value pjs_string(const char *s, int len) {
  if (len < 0) len = std::strlen(s);
  return to_local_value(pjs::Str::make(s, len));
}

NMI_EXPORT pjs_value pjs_object() {
  return to_local_value(pjs::Object::make());
}

NMI_EXPORT pjs_value pjs_array(int len) {
  return to_local_value(pjs::Array::make(len));
}

NMI_EXPORT pjs_value pjs_copy(pjs_value v, pjs_value src) {
  auto *ra = nmi::s_values.get(v);
  auto *rb = nmi::s_values.get(src);
  if (ra && rb) ra->v = rb->v;
  return v;
}

NMI_EXPORT pjs_value pjs_hold(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    r->hold_count++;
  }
  return v;
}

NMI_EXPORT void pjs_free(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    if (!--r->hold_count) {
      nmi::s_values.free(v);
    }
  }
}

NMI_EXPORT pjs_type pjs_type_of(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    switch (r->v.type()) {
      case pjs::Value::Type::Empty    : return PJS_TYPE_UNDEFINED;
      case pjs::Value::Type::Undefined: return PJS_TYPE_UNDEFINED;
      case pjs::Value::Type::Boolean  : return PJS_TYPE_BOOLEAN;
      case pjs::Value::Type::Number   : return PJS_TYPE_NUMBER;
      case pjs::Value::Type::String   : return PJS_TYPE_STRING;
      case pjs::Value::Type::Object   : return PJS_TYPE_OBJECT;
    }
  }
  return PJS_TYPE_UNDEFINED;
}

NMI_EXPORT int pjs_class_of(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    if (r->v.is_object()) {
      return r->v.o()->type()->id();
    }
  }
  return 0;
}

NMI_EXPORT int pjs_class_id(const char *name) {
  if (auto *c = pjs::Class::get(name)) {
    return c->id();
  }
  return 0;
}

NMI_EXPORT int pjs_is_undefined(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_undefined();
  }
  return false;
}

NMI_EXPORT int pjs_is_null(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_null();
  }
  return false;
}

NMI_EXPORT int pjs_is_nullish(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_nullish();
  }
  return false;
}

NMI_EXPORT int pjs_is_empty_string(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_string() && r->v.s()->length() == 0;
  }
  return false;
}

NMI_EXPORT int pjs_is_instance_of(pjs_value v, int class_id) {
  if (auto *r = nmi::s_values.get(v)) {
    if (r->v.is_object()) {
      if (auto *c = pjs::Class::get(class_id)) {
        return r->v.o()->type()->is_derived_from(c);
      }
    }
  }
  return 0;
}

NMI_EXPORT int pjs_is_array(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_array();
  }
  return false;
}

NMI_EXPORT int pjs_is_function(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_function();
  }
  return false;
}

NMI_EXPORT int pjs_is_equal(pjs_value a, pjs_value b) {
  auto *ra = nmi::s_values.get(a);
  auto *rb = nmi::s_values.get(b);
  if (ra && rb) return pjs::Value::is_equal(ra->v, rb->v);
  return false;
}

NMI_EXPORT int pjs_is_identical(pjs_value a, pjs_value b) {
  auto *ra = nmi::s_values.get(a);
  auto *rb = nmi::s_values.get(b);
  if (ra && rb) return pjs::Value::is_identical(ra->v, rb->v);
  return false;
}

NMI_EXPORT int pjs_to_boolean(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.to_boolean();
  }
  return 0;
}

NMI_EXPORT double pjs_to_number(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.to_number();
  }
  return 0;
}

NMI_EXPORT pjs_value pjs_to_string(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    auto s = r->v.to_string();
    auto v = to_local_value(s);
    s->release();
    return v;
  }
  return 0;
}

NMI_EXPORT int pjs_string_get_length(pjs_value str) {
  if (auto *r = nmi::s_values.get(str)) {
    if (r->v.is_string()) {
      return r->v.s()->length();
    }
  }
  return -1;
}

NMI_EXPORT int pjs_string_get_char_code(pjs_value str, int pos) {
  if (auto *r = nmi::s_values.get(str)) {
    if (r->v.is_string()) {
      return r->v.s()->chr_at(pos);
    }
  }
  return -1;
}

NMI_EXPORT int pjs_string_get_utf8_size(pjs_value str) {
  if (auto *r = nmi::s_values.get(str)) {
    if (r->v.is_string()) {
      return r->v.s()->size();
    }
  }
  return -1;
}

NMI_EXPORT int pjs_string_get_utf8_data(pjs_value str, char *buf, int len) {
  if (auto *r = nmi::s_values.get(str)) {
    if (r->v.is_string()) {
      int n = r->v.s()->size();
      if (len > 0) {
        std::memcpy(buf, r->v.s()->c_str(), std::min(len, n));
      }
      return n;
    }
  }
  return -1;
}

NMI_EXPORT int pjs_object_get_property(pjs_value obj, pjs_value k, pjs_value v) {
  if (auto *r = nmi::s_values.get(obj)) {
    if (r->v.is_object()) {
      auto *rk = nmi::s_values.get(k);
      auto *rv = nmi::s_values.get(v);
      if (rk && rv && rk->v.is_string()) {
        r->v.o()->get(rk->v.s(), rv->v);
        return 1;
      }
    }
  }
  return 0;
}

NMI_EXPORT int pjs_object_set_property(pjs_value obj, pjs_value k, pjs_value v) {
  if (auto *r = nmi::s_values.get(obj)) {
    if (r->v.is_object()) {
      auto *rk = nmi::s_values.get(k);
      auto *rv = nmi::s_values.get(v);
      if (rk && rv && rk->v.is_string()) {
        r->v.o()->set(rk->v.s(), rv->v);
        return 1;
      }
    }
  }
  return 0;
}

NMI_EXPORT int pjs_object_delete(pjs_value obj, pjs_value k) {
  if (auto *r = nmi::s_values.get(obj)) {
    if (r->v.is_object()) {
      if (auto *rk = nmi::s_values.get(k)) {
        if (rk->v.is_string()) {
          r->v.o()->ht_delete(rk->v.s());
          return 1;
        }
      }
    }
  }
  return 0;
}

NMI_EXPORT void pjs_object_iterate(pjs_value obj, int (*cb)(pjs_value k, pjs_value v)) {
  if (auto *r = nmi::s_values.get(obj)) {
    if (r->v.is_object()) {
      r->v.o()->iterate_while(
        [=](pjs::Str *k, pjs::Value &v) {
          nmi::LocalRefPool lrp;
          auto i = nmi::s_values.alloc(k);
          auto j = nmi::s_values.alloc(v);
          lrp.add(i);
          lrp.add(j);
          return (bool)(*cb)(i, j);
        }
      );
    }
  }
}

NMI_EXPORT int pjs_array_get_length(pjs_value arr) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      return r->v.as<pjs::Array>()->length();
    }
  }
  return -1;
}

NMI_EXPORT int pjs_array_set_length(pjs_value arr, int len) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      r->v.as<pjs::Array>()->length(len);
      return 1;
    }
  }
  return 0;
}

NMI_EXPORT int pjs_array_get_element(pjs_value arr, int i, pjs_value v) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      if (auto *rv = nmi::s_values.get(v)) {
        r->v.as<pjs::Array>()->get(i, rv->v);
        return 1;
      }
    }
  }
  return 0;
}

NMI_EXPORT int pjs_array_set_element(pjs_value arr, int i, pjs_value v) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      if (auto *rv = nmi::s_values.get(v)) {
        r->v.as<pjs::Array>()->set(i, rv->v);
        return 1;
      }
    }
  }
  return 0;
}

NMI_EXPORT int pjs_array_delete(pjs_value arr, int i) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      r->v.as<pjs::Array>()->clear(i);
      return 1;
    }
  }
  return 0;
}

NMI_EXPORT int pjs_array_push(pjs_value arr, pjs_value v) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      auto *a = r->v.as<pjs::Array>();
      if (auto *rv = nmi::s_values.get(v)) {
        a->push(rv->v);
        return a->length();
      }
    }
  }
  return -1;
}

NMI_EXPORT pjs_value pjs_array_pop(pjs_value arr) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      pjs::Value v;
      r->v.as<pjs::Array>()->pop(v);
      return to_local_value(v);
    }
  }
  return 0;
}

NMI_EXPORT pjs_value pjs_array_shift(pjs_value arr) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      pjs::Value v;
      r->v.as<pjs::Array>()->shift(v);
      return to_local_value(v);
    }
  }
  return 0;
}

NMI_EXPORT int pjs_array_unshift(pjs_value arr, pjs_value v) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      auto *a = r->v.as<pjs::Array>();
      if (auto *rv = nmi::s_values.get(v)) {
        a->unshift(&rv->v, 1);
        return a->length();
      }
    }
  }
  return -1;
}

NMI_EXPORT pjs_value pjs_array_splice(pjs_value arr, int pos, int del_cnt, int ins_cnt, pjs_value v[]) {
  if (del_cnt < 0) return 0;
  if (ins_cnt < 0) return 0;
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      auto *a = r->v.as<pjs::Array>();
      pjs::Value vs[ins_cnt];
      if (ins_cnt > 0) {
        for (int i = 0; i < ins_cnt; i++) {
          if (auto *rv = nmi::s_values.get(v[i])) {
            vs[i] = rv->v;
          } else {
            return -1;
          }
        }
      }
      auto *ret = a->splice(pos, del_cnt, vs, ins_cnt);
      return to_local_value(ret);
    }
  }
  return 0;
}

NMI_EXPORT int pipy_is_Data(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    return pv->v.is_instance_of<Data>();
  }
  return 0;
}

NMI_EXPORT int pipy_is_MessageStart(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    return pv->v.is_instance_of<MessageStart>();
  }
  return 0;
}

NMI_EXPORT int pipy_is_MessageEnd(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    return pv->v.is_instance_of<MessageEnd>();
  }
  return 0;
}

NMI_EXPORT int pipy_is_StreamEnd(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    return pv->v.is_instance_of<StreamEnd>();
  }
  return 0;
}

NMI_EXPORT pjs_value pipy_Data_new(const char *buf, int len) {
  return to_local_value(Data::make(buf, len, &nmi::s_dp));
}

NMI_EXPORT pjs_value pipy_Data_push(pjs_value obj, pjs_value data) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<Data>()) {
      auto *o = v.as<Data>();
      if (auto *pv2 = nmi::s_values.get(data)) {
        auto &v2 = pv2->v;
        if (v2.is_number()) {
          o->push((char)v2.n(), &nmi::s_dp);
          return obj;
        } else if (v2.is_string()) {
          o->push(v2.s()->str(), &nmi::s_dp);
          return obj;
        } else if (v2.is_instance_of<Data>()) {
          o->push(*v2.as<Data>());
          return obj;
        }
      }
    }
  }
  return 0;
}

NMI_EXPORT pjs_value pipy_Data_pop(pjs_value obj, int len) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<Data>()) {
      Data out;
      v.as<Data>()->pop(len, out);
      return to_local_value(Data::make(std::move(out)));
    }
  }
  return 0;
}

NMI_EXPORT pjs_value pipy_Data_shift(pjs_value obj, int len) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<Data>()) {
      Data out;
      v.as<Data>()->shift(len, out);
      return to_local_value(Data::make(std::move(out)));
    }
  }
  return 0;
}

NMI_EXPORT int pipy_Data_get_size(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<Data>()) {
      return v.as<Data>()->size();
    }
  }
  return -1;
}

NMI_EXPORT int pipy_Data_get_data(pjs_value obj, char *buf, int len) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<Data>()) {
      auto data = v.as<Data>();
      data->to_bytes((uint8_t *)buf, len);
      return data->size();
    }
  }
  return -1;
}

NMI_EXPORT pjs_value pipy_MessageStart_new(pjs_value head) {
  pjs::Object *head_obj = nullptr;
  if (head) {
    auto pv = nmi::s_values.get(head);
    if (!pv) return 0;
    auto &v = pv->v;
    if (!v.is_object()) return 0;
    head_obj = v.o();
  }
  return to_local_value(MessageStart::make(head_obj));
}

NMI_EXPORT pjs_value pipy_MessageStart_get_head(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<MessageStart>()) {
      return to_local_value(v.as<MessageStart>()->head());
    }
  }
  return 0;
}

NMI_EXPORT pjs_value pipy_MessageEnd_new(pjs_value tail, pjs_value payload) {
  pjs::Object *tail_obj = nullptr, *payload_obj = nullptr;
  if (tail) {
    auto pv = nmi::s_values.get(tail);
    if (!pv) return 0;
    auto &v = pv->v;
    if (!v.is_object()) return 0;
    tail_obj = v.o();
  }
  if (payload) {
    auto pv = nmi::s_values.get(payload);
    if (!pv) return 0;
    auto &v = pv->v;
    if (!v.is_object()) return 0;
    payload_obj = v.o();
  }
  return to_local_value(MessageEnd::make(tail_obj, payload_obj));
}

NMI_EXPORT pjs_value pipy_MessageEnd_get_tail(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<MessageEnd>()) {
      return to_local_value(v.as<MessageEnd>()->tail());
    }
  }
  return 0;
}

NMI_EXPORT pjs_value pipy_MessageEnd_get_payload(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<MessageEnd>()) {
      return to_local_value(v.as<MessageEnd>()->payload());
    }
  }
  return 0;
}

NMI_EXPORT pjs_value pipy_StreamEnd_new(pjs_value error) {
  StreamEnd::Error err = StreamEnd::NO_ERROR;
  if (error) {
    auto pv = nmi::s_values.get(error);
    if (!pv) return 0;
    auto &v = pv->v;
    if (!v.is_string()) return 0;
    err = pjs::EnumDef<StreamEnd::Error>::value(v.s());
    if (int(err) < 0) return 0;
  }
  return to_local_value(StreamEnd::make(err));
}

NMI_EXPORT pjs_value pipy_StreamEnd_get_error(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<StreamEnd>()) {
      return to_local_value(v.as<StreamEnd>()->error());
    }
  }
  return 0;
}

NMI_EXPORT void pipy_define_variable(int id, const char *name, const char *ns, pjs_value value) {
  if (auto *m = nmi::NativeModule::current()) {
    if (value) {
      if (auto *pv = nmi::s_values.get(value)) {
        m->define_variable(id, name, ns, pv->v);
      }
    } else {
      m->define_variable(id, name, ns, pjs::Value::undefined);
    }
  }
}

NMI_EXPORT void pipy_define_pipeline(const char *name, fn_pipeline_init init, fn_pipeline_free free, fn_pipeline_process process) {
  if (auto *m = nmi::NativeModule::current()) {
    m->define_pipeline(name, init, free, process);
  }
}

NMI_EXPORT void pipy_hold(pipy_pipeline ppl) {
  if (auto *p = nmi::Pipeline::get(ppl)) {
    p->check_thread();
    p->retain();
  }
}

NMI_EXPORT void pipy_free(pipy_pipeline ppl) {
  if (auto *p = nmi::Pipeline::get(ppl)) {
    auto net = p->module()->net();
    if (&Net::current() == net) {
      p->release();
    } else {
      net->post(
        [=]() { p->release(); }
      );
    }
  }
}

NMI_EXPORT void pipy_output_event(pipy_pipeline ppl, pjs_value evt) {
  if (nmi::NativeModule::current()) {
    if (auto *p = nmi::Pipeline::get(ppl)) {
      p->check_thread();
      if (auto *pv = nmi::s_values.get(evt)) {
        auto &v = pv->v;
        if (v.is_instance_of<pipy::Event>()) {
          p->output(v.as<pipy::Event>());
        }
      }
    }
  }
}

NMI_EXPORT void pipy_get_variable(pipy_pipeline ppl, int id, pjs_value value) {
  if (auto *m = nmi::NativeModule::current()) {
    if (auto *p = nmi::Pipeline::get(ppl)) {
      p->check_thread();
      if (auto *pv = nmi::s_values.get(value)) {
        auto &v = pv->v;
        auto ctx = p->context();
        if (auto *obj = ctx->data(m->index())) {
          obj->type()->get(obj, id, v);
        }
      }
    }
  }
}

NMI_EXPORT void pipy_set_variable(pipy_pipeline ppl, int id, pjs_value value) {
  if (auto *m = nmi::NativeModule::current()) {
    if (auto *p = nmi::Pipeline::get(ppl)) {
      p->check_thread();
      if (auto *pv = nmi::s_values.get(value)) {
        auto &v = pv->v;
        auto ctx = p->context();
        if (auto *obj = ctx->data(m->index())) {
          obj->type()->set(obj, id, v);
        }
      }
    }
  }
}

NMI_EXPORT void pipy_schedule(pipy_pipeline ppl, double timeout, void (*fn)(void *), void *user_ptr) {
  if (auto *p = nmi::Pipeline::get(ppl)) {
    p->retain();
    p->module()->schedule(
      timeout,
      [=]() {
        (*fn)(user_ptr);
        p->release();
      }
    );
  }
}
