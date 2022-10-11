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
#include <algorithm>
#include <list>

#include <dlfcn.h>

namespace pipy {
namespace nmi {

static Data::Producer s_dp("NMI");

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

static Table<Value> s_values;

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
  }

private:
  LocalRefPool* m_back;
  List<LocalRef> m_values;

  static LocalRefPool* s_current;
};

LocalRefPool* LocalRefPool::s_current = nullptr;

//
// Pipeline
//

Table<Pipeline*> Pipeline::m_pipeline_table;

void Pipeline::input(Event *evt) {
  LocalRefPool lrf;
  auto e = nmi::s_values.alloc(evt);
  lrf.add(e);
  m_process(m_id, m_user_ptr, e);
}

void Pipeline::free() {
  m_free(m_id, m_user_ptr);
  m_pipeline_table.free(m_id);
  delete this;
}

//
// NativeModule
//

std::list<NativeModule*> NativeModule::m_native_modules;

auto NativeModule::load(const std::string &filename) -> NativeModule* {
  for (auto *m : m_native_modules) {
    if (m->m_path == filename) return m;
  }
  auto *m = new NativeModule(filename);
  m->m_index = m_native_modules.size();
  m->m_path = filename;
  m_native_modules.push_back(m);
  return m;
}

void NativeModule::for_each(const std::function<void(NativeModule*)> &cb) {
  for (auto *m : m_native_modules) {
    cb(m);
  }
}

NativeModule::NativeModule(const std::string &filename)
  : m_filename(pjs::Str::make(filename))
{
  auto *handle = dlopen(filename.c_str(), RTLD_NOW);
  if (!handle) {
    std::string msg("cannot load native module ");
    throw std::runtime_error(msg + filename);
  }

  auto *init_fn = dlsym(handle, "pipy_module_init");
  if (!init_fn) {
    std::string msg("pipy_module_init() not found in native module ");
    throw std::runtime_error(msg + filename);
  }

  struct pipy_native_module *mod = (*(pipy_module_init_fn)init_fn)();
  if (!mod) {
    std::string msg("pipy_module_init() failed in native module ");
    throw std::runtime_error(msg + filename);
  }

  std::list<pjs::Field*> fields;
  int variable_id = 0;
  for (const auto *p = mod->variables; *p; p++) {
    auto *vd = *p;
    std::string name(vd->name);
    for (auto &prev : fields) {
      if (prev->name()->str() == name) {
        std::string msg("duplicated variables ");
        msg += name;
        msg += " in native module ";
        msg += filename;
        throw std::runtime_error(msg + filename);
      }
    }
    auto *v = s_values.get(vd->init_value);
    fields.push_back(
      pjs::Variable::make(
        name, v ? v->v : pjs::Value::undefined,
        pjs::Field::Enumerable | pjs::Field::Writable,
        variable_id++
      )
    );
    if (auto *ns = vd->export_to) {
      m_exports.emplace_back();
      m_exports.back().ns = pjs::Str::make(ns);
      m_exports.back().name = pjs::Str::make(name);
    }
  }

  m_context_class = pjs::Class::make(
    "ContextData",
    pjs::class_of<ContextDataBase>(),
    fields
  );

  for (const auto *p = mod->pipelines; *p; p++) {
    auto *pd = *p;
    if (pd->name && pd->name[0]) {
      pjs::Ref<pjs::Str> name(pjs::Str::make(pd->name));
      if (m_pipeline_layouts.count(name) > 0) {
        std::string msg("duplicated pipeline ");
        msg += name->str();
        msg += " in native module ";
        msg += filename;
        throw std::runtime_error(msg + filename);
      }
      m_pipeline_layouts[name] = new PipelineLayout(this, pd);
    } else {
      m_entry_pipeline = new PipelineLayout(this, pd);
    }
  }
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

pjs_value pjs_undefined() {
  auto i = nmi::s_values.alloc();
  nmi::LocalRefPool::add(i);
  return i;
}

pjs_value pjs_boolean(int b) {
  auto i = nmi::s_values.alloc(bool(b));
  nmi::LocalRefPool::add(i);
  return i;
}

pjs_value pjs_number(double n) {
  auto i = nmi::s_values.alloc(n);
  nmi::LocalRefPool::add(i);
  return i;
}

pjs_value pjs_string(const char *s, int len) {
  auto i = nmi::s_values.alloc(pjs::Str::make(s, len));
  nmi::LocalRefPool::add(i);
  return i;
}

pjs_value pjs_object() {
  auto i = nmi::s_values.alloc(pjs::Object::make());
  nmi::LocalRefPool::add(i);
  return i;
}

pjs_value pjs_array(int len) {
  auto i = nmi::s_values.alloc(pjs::Array::make(len));
  nmi::LocalRefPool::add(i);
  return i;
}

pjs_value pjs_copy(pjs_value v, pjs_value src) {
  auto *ra = nmi::s_values.get(v);
  auto *rb = nmi::s_values.get(src);
  if (ra && rb) ra->v = rb->v;
  return v;
}

void pjs_hold(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    r->hold_count++;
  }
}

void pjs_free(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    if (!--r->hold_count) {
      nmi::s_values.free(v);
    }
  }
}

pjs_type pjs_type_of(pjs_value v) {
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

int pjs_class_of(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    if (r->v.is_object()) {
      return r->v.o()->type()->id();
    }
  }
  return 0;
}

int pjs_class_id(const char *name) {
  if (auto *c = pjs::Class::get(name)) {
    return c->id();
  }
  return 0;
}

int pjs_is_null(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_null();
  }
  return false;
}

int pjs_is_nullish(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_nullish();
  }
  return false;
}

int pjs_is_instance_of(pjs_value v, int class_id) {
  if (auto *r = nmi::s_values.get(v)) {
    if (r->v.is_object()) {
      if (auto *c = pjs::Class::get(class_id)) {
        return r->v.o()->type()->is_derived_from(c);
      }
    }
  }
  return 0;
}

int pjs_is_array(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_array();
  }
  return false;
}

int pjs_is_function(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.is_function();
  }
  return false;
}

int pjs_is_equal(pjs_value a, pjs_value b) {
  auto *ra = nmi::s_values.get(a);
  auto *rb = nmi::s_values.get(b);
  if (ra && rb) return pjs::Value::is_equal(ra->v, rb->v);
  return false;
}

int pjs_is_identical(pjs_value a, pjs_value b) {
  auto *ra = nmi::s_values.get(a);
  auto *rb = nmi::s_values.get(b);
  if (ra && rb) return pjs::Value::is_identical(ra->v, rb->v);
  return false;
}

int pjs_to_boolean(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.to_boolean();
  }
  return 0;
}

double pjs_to_number(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    return r->v.to_number();
  }
  return 0;
}

pjs_value pjs_to_string(pjs_value v) {
  if (auto *r = nmi::s_values.get(v)) {
    auto s = r->v.to_string();
    auto v = nmi::s_values.alloc(s);
    nmi::LocalRefPool::add(v);
    s->release();
    return v;
  }
  return 0;
}

int pjs_string_get_length(pjs_value str) {
  if (auto *r = nmi::s_values.get(str)) {
    if (r->v.is_string()) {
      return r->v.s()->length();
    }
  }
  return -1;
}

int pjs_string_get_char_code(pjs_value str, int pos) {
  if (auto *r = nmi::s_values.get(str)) {
    if (r->v.is_string()) {
      return r->v.s()->chr_at(pos);
    }
  }
  return -1;
}

int pjs_string_get_utf8_size(pjs_value str) {
  if (auto *r = nmi::s_values.get(str)) {
    if (r->v.is_string()) {
      return r->v.s()->size();
    }
  }
  return -1;
}

int pjs_string_get_utf8_data(pjs_value str, char *buf, int len) {
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

int pjs_object_get_property(pjs_value obj, pjs_value k, pjs_value v) {
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

int pjs_object_set_property(pjs_value obj, pjs_value k, pjs_value v) {
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

int pjs_object_delete(pjs_value obj, pjs_value k) {
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

void pjs_object_iterate(pjs_value obj, int (*cb)(pjs_value k, pjs_value v)) {
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

int pjs_array_get_length(pjs_value arr) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      return r->v.as<pjs::Array>()->length();
    }
  }
  return -1;
}

int pjs_array_set_length(pjs_value arr, int len) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      r->v.as<pjs::Array>()->length(len);
      return 1;
    }
  }
  return 0;
}

int pjs_array_get_element(pjs_value arr, int i, pjs_value v) {
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

int pjs_array_set_element(pjs_value arr, int i, pjs_value v) {
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

int pjs_array_delete(pjs_value arr, int i) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      r->v.as<pjs::Array>()->clear(i);
      return 1;
    }
  }
  return 0;
}

int pjs_array_push(pjs_value arr, int cnt, pjs_value v, ...) {
  if (cnt < 1) return -1;
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      auto *a = r->v.as<pjs::Array>();
      if (auto *rv = nmi::s_values.get(v)) {
        a->push(rv->v);
        if (cnt > 1) {
          va_list ap;
          va_start(ap, v);
          for (int i = 1; i < cnt; i++) {
            auto v = va_arg(ap, pjs_value);
            if (auto *rv = nmi::s_values.get(v)) {
              a->push(rv->v);
            } else {
              va_end(ap);
              return -1;
            }
          }
          va_end(ap);
        }
        return a->length();
      }
    }
  }
  return -1;
}

pjs_value pjs_array_pop(pjs_value arr) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      pjs::Value v;
      r->v.as<pjs::Array>()->pop(v);
      auto i = nmi::s_values.alloc(v);
      nmi::LocalRefPool::add(i);
      return i;
    }
  }
  return 0;
}

pjs_value pjs_array_shift(pjs_value arr) {
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      pjs::Value v;
      r->v.as<pjs::Array>()->shift(v);
      auto i = nmi::s_values.alloc(v);
      nmi::LocalRefPool::add(i);
      return i;
    }
  }
  return 0;
}

int pjs_array_unshift(pjs_value arr, int cnt, pjs_value v, ...) {
  if (cnt < 1) return -1;
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      auto *a = r->v.as<pjs::Array>();
      pjs::Value vs[cnt];
      if (auto *rv = nmi::s_values.get(v)) {
        vs[0] = rv->v;
        if (cnt > 1) {
          va_list ap;
          va_start(ap, v);
          for (int i = 1; i < cnt; i++) {
            auto v = va_arg(ap, pjs_value);
            if (auto *rv = nmi::s_values.get(v)) {
              vs[i] = rv->v;
            } else {
              va_end(ap);
              return -1;
            }
          }
          va_end(ap);
        }
        a->unshift(vs, cnt);
        return a->length();
      }
    }
  }
  return -1;
}

pjs_value pjs_array_splice(pjs_value arr, int pos, int del_cnt, int ins_cnt, ...) {
  if (del_cnt < 0) return 0;
  if (ins_cnt < 0) return 0;
  if (auto *r = nmi::s_values.get(arr)) {
    if (r->v.is_array()) {
      auto *a = r->v.as<pjs::Array>();
      pjs::Value vs[ins_cnt];
      if (ins_cnt > 0) {
        va_list ap;
        va_start(ap, ins_cnt);
        for (int i = 0; i < ins_cnt; i++) {
          auto v = va_arg(ap, pjs_value);
          if (auto *rv = nmi::s_values.get(v)) {
            vs[i] = rv->v;
          } else {
            va_end(ap);
            return -1;
          }
        }
        va_end(ap);
      }
      auto *ret = a->splice(pos, del_cnt, vs, ins_cnt);
      auto i = nmi::s_values.alloc(ret);
      nmi::LocalRefPool::add(i);
      return i;
    }
  }
  return 0;
}

pjs_value pipy_Data_new(const char *buf, int len) {
  auto i = nmi::s_values.alloc(Data::make(buf, len, &nmi::s_dp));
  nmi::LocalRefPool::add(i);
  return i;
}

pjs_value pipy_Data_push(pjs_value obj, pjs_value data) {
}

pjs_value pipy_Data_pop(pjs_value obj, int len) {
}

pjs_value pipy_Data_shift(pjs_value obj, int len) {
}

int pipy_Data_get_size(pjs_value obj) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<pipy::Data>()) {
      return v.as<pipy::Data>()->size();
    }
  }
  return -1;
}

int pipy_Data_get_data(pjs_value obj, char *buf, int len) {
  if (auto *pv = nmi::s_values.get(obj)) {
    auto &v = pv->v;
    if (v.is_instance_of<pipy::Data>()) {
      auto data = v.as<pipy::Data>();
      data->to_bytes((uint8_t *)buf, len);
      return data->size();
    }
  }
  return -1;
}
