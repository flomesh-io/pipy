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
#include "list.hpp"

#include <algorithm>

namespace pipy {
namespace nmi {

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

  std::vector<Entry*> m_sub_tables;

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
      m_sub_tables[x] = sub = reinterpret_cast<Entry*>(buf);
    }
    return &sub[y];
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

#endif // NMI_HPP
