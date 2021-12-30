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

#ifndef PJS_TYPES_HPP
#define PJS_TYPES_HPP

#include <cassert>
#include <cmath>
#include <cxxabi.h>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace pjs {

class Array;
class Boolean;
class Class;
class Context;
class Field;
class Function;
class Number;
class Object;
class RegExp;
class String;
class Value;

template<class T> Class* class_of();

//
// Pooled
//

class DefaultPooledBase {};

template<class T, class Base = DefaultPooledBase>
class Pooled : public Base {
private:
  enum { INVALIDATE_FREE_SPACE = 0 }; // for debugging only

public:
  using Base::Base;

  void* operator new(size_t) {
    if (auto p = m_free) {
      m_free = *(void**)p;
      return p;
    } else {
      return new char[std::max(sizeof(T), sizeof(void*))];
    }
  }

  void operator delete(void *p) {
    *(void**)p = m_free;
    m_free = p;
    if (INVALIDATE_FREE_SPACE) {
      std::memset(
        (uint8_t *)p + sizeof(void*),
        0xfe, sizeof(T) - sizeof(void*)
      );
    }
  }

private:
  static void* m_free;
};

template<class T, class Base>
void* Pooled<T, Base>::m_free = nullptr;

//
// RefCount
//

template<class T>
class RefCount {
public:

  //
  // RefCount::WeakPtr
  //

  class WeakPtr : public Pooled<WeakPtr> {
  public:
    T* ptr() const {
      return static_cast<T*>(m_ptr);
    }

    T* init_ptr() const {
      return static_cast<T*>(m_init_ptr);
    }

    void retain() {
      m_refs++;
    }

    void release() {
      if (!--m_refs) {
        delete this;
      }
    }

  private:
    WeakPtr(RefCount<T> *ptr)
      : m_init_ptr(ptr)
      , m_ptr(ptr) {}

    RefCount<T>* m_init_ptr;
    RefCount<T>* m_ptr;
    int m_refs = 1;

    friend class RefCount<T>;
  };

  int ref_count() const { return m_refs; }

  WeakPtr* weak_ptr() {
    if (!m_weak_ptr) {
      m_weak_ptr = new WeakPtr(this);
    }
    return m_weak_ptr;
  }

  T* retain() {
    m_refs++;
    return static_cast<T*>(this);
  }

  void release() {
    if (--m_refs <= 0) {
      static_cast<T*>(this)->finalize();
    }
  }

protected:
  ~RefCount() {
    if (m_weak_ptr) {
      m_weak_ptr->m_ptr = nullptr;
      m_weak_ptr->release();
    }
  }

  int m_refs = 0;
  WeakPtr* m_weak_ptr = nullptr;

  void finalize() {
    delete static_cast<T*>(this);
  }
};

//
// Ref
//

template<class T>
class Ref {
public:
  Ref()             : m_p(nullptr) {}
  Ref(T *p)         : m_p(p) { if (p) p->retain(); }
  Ref(const Ref &r) : m_p(r.m_p) { if (m_p) m_p->retain(); }
  Ref(Ref &&r)      : m_p(r.m_p) { r.m_p = nullptr; }

  template<class U> Ref(U *p)            : m_p(static_cast<T*>(p)) { if (p) p->retain(); }
  template<class U> Ref(const Ref<U> &r) : m_p(static_cast<T*>(r.get())) { if (m_p) m_p->retain(); }
  template<class U> Ref(Ref<U> &&r)      : m_p(static_cast<T*>(r.get())) { r.m_p = nullptr; }

  ~Ref() { reset(); }

  Ref& operator=(T *p)         { if (m_p != p) { reset(); m_p = p; if (p) p->retain(); } return *this; }
  Ref& operator=(const Ref &r) { if (m_p != r.m_p) { reset(); m_p = r.m_p; if (m_p) m_p->retain(); } return *this; }
  Ref& operator=(Ref &&r)      { if (m_p != r.m_p) { reset(); m_p = r.m_p; } r.m_p = nullptr; return *this; }

  T* get() const { return m_p; }
  T* operator->() const { return m_p; }
  T& operator*() const { return *m_p; }
  operator T*() const { return m_p; }

  auto release() -> T* {
    auto p = m_p;
    m_p = nullptr;
    return p;
  }

  template<class U> operator Ref<U>() { return Ref<U>(static_cast<U*>(m_p)); }

private:
  T* m_p;

  void reset() { if (m_p) m_p->release(); }
};

//
// WeakRef
//

template<class T>
class WeakRef {
public:
  WeakRef() {}
  WeakRef(T *p) : m_weak_ptr(p ? p->weak_ptr() : nullptr) {}
  WeakRef(const WeakRef &r) : m_weak_ptr(r.m_weak_ptr) {}

  T* ptr() const { return m_weak_ptr ? static_cast<T*>(m_weak_ptr->ptr()) : nullptr; }
  T* init_ptr() const { return m_weak_ptr ? static_cast<T*>(m_weak_ptr->init_ptr()) : nullptr; }
  T* operator->() const { return ptr(); }
  T& operator*() const { return *ptr(); }
  operator T*() const { return ptr(); }

private:
  Ref<class T::WeakPtr> m_weak_ptr;
};

} // namespace pjs

namespace std {

template<class T>
struct equal_to<pjs::Ref<T>> {
  bool operator()(const pjs::Ref<T> &a, const pjs::Ref<T> &b) const {
    return a.get() == b.get();
  }
};

template<class T>
struct less<pjs::Ref<T>> {
  bool operator()(const pjs::Ref<T> &a, const pjs::Ref<T> &b) const {
    return a.get() < b.get();
  }
};

template<class T>
struct hash<pjs::Ref<T>> {
  size_t operator()(const pjs::Ref<T> &k) const {
    hash<T*> h;
    return h(k.get());
  }
};

template<class T>
struct equal_to<pjs::WeakRef<T>> {
  bool operator()(const pjs::WeakRef<T> &a, const pjs::WeakRef<T> &b) const {
    return a.init_ptr() == b.init_ptr();
  }
};

template<class T>
struct less<pjs::WeakRef<T>> {
  bool operator()(const pjs::WeakRef<T> &a, const pjs::WeakRef<T> &b) const {
    return a.init_ptr() < b.init_ptr();
  }
};

template<class T>
struct hash<pjs::WeakRef<T>> {
  size_t operator()(const pjs::WeakRef<T> &k) const {
    hash<void*> h;
    return h(k.init_ptr());
  }
};

} // namespace std

namespace pjs {

//
// PooledArray
//

template<class T>
class PooledArray {
public:
  static auto make(size_t size) -> PooledArray* {
    auto slot = slot_of_size(size);
    if (slot < m_pools.size()) {
      if (auto pool = m_pools[slot]) {
        m_pools[slot] = *(PooledArray**)pool;
        new (pool) PooledArray(size);
        return pool;
      }
    }
    auto p = std::malloc(sizeof(PooledArray) + sizeof(T) * size_of_slot(slot));
    new (p) PooledArray(size);
    return reinterpret_cast<PooledArray*>(p);
  }

  void free() {
    auto size = m_size;
    auto slot = slot_of_size(size);
    this->~PooledArray();
    if (slot >= m_pools.size()) {
      m_pools.resize(slot + 1);
    }
    *(PooledArray**)this = m_pools[slot];
    m_pools[slot] = this;
  }

  auto size() const -> size_t {
    return m_size;
  }

  auto elements() -> T* {
    return m_elements;
  }

  auto at(size_t i) -> T& {
    return m_elements[i];
  }

  T& operator[](size_t i) {
    return at(i);
  }

private:
  size_t m_size;
  T m_elements[0];

  PooledArray(size_t size) : m_size(size) {
    for (size_t i = 0; i < size; i++) {
      new (m_elements + i) T();
    }
  }

  ~PooledArray() {
    for (size_t i = 0, n = m_size; i < n; i++) {
      m_elements[i].~T();
    }
  }

  static auto slot_of_size(size_t size) -> size_t {
    if (size < 256) return size;
    auto power = sizeof(unsigned int) * 8 - __builtin_clz(size - 1);
    return (power - 8) + 256;
  }

  static auto size_of_slot(size_t slot) -> size_t {
    if (slot < 256) return slot;
    return 1 << (slot - 256 + 8);
  }

  static std::vector<PooledArray*> m_pools;
};

template<class T>
std::vector<PooledArray<T>*> PooledArray<T>::m_pools;

//
// OrderedHash
// TODO: Use pooled allocators
//

template<class K, class V>
class OrderedHash : public Pooled<OrderedHash<K, V>, RefCount<OrderedHash<K, V>>> {
public:
  struct Entry {
    K k;
    V v;
    Entry(const K &k, const V &v) : k(k), v(v) {}
  };

  class Iterator {
  public:
    Iterator(OrderedHash<K, V> *h)
      : m_h(h)
      , m_i(h->m_entries.begin())
      , m_next(h->m_iterators)
    {
      if (m_next) m_next->m_prev = this;
      h->m_iterators = this;
      h->retain();
    }

    ~Iterator() {
      if (m_next) m_next->m_prev = m_prev;
      if (m_prev) m_prev->m_next = m_next; else m_h->m_iterators = m_next;
      m_h->release();
    }

    auto next() -> Entry* {
      if (m_i == m_h->m_entries.end()) return nullptr;
      return &*m_i++;
    }

  private:
    OrderedHash<K, V> *m_h;
    typename std::list<Entry>::iterator m_i;
    Iterator* m_next;
    Iterator* m_prev = nullptr;

    friend class OrderedHash<K, V>;
  };

  template<typename... Args>
  static auto make(Args&&... args) -> OrderedHash<K, V>* {
    return new OrderedHash<K, V>(std::forward<Args>(args)...);
  }

  auto size() const -> size_t {
    return m_entries.size();
  }

  bool has(const K &k) {
    return m_map.count(k) > 0;
  }

  bool get(const K &k, V &v) {
    auto i = m_map.find(k);
    if (i == m_map.end()) return false;
    v = i->second->v;
    return true;
  }

  bool use(const K &k, V &v) {
    auto i = m_map.find(k);
    if (i == m_map.end()) return false;
    v = i->second->v;
    auto a = i->second;
    auto b = a; ++b;
    m_entries.splice(m_entries.end(), m_entries, a, b);
    return true;
  }

  bool set(const K &k, const V &v) {
    auto i = m_map.find(k);
    if (i == m_map.end()) {
      auto p = m_entries.emplace(m_entries.end(), k, v);
      m_map[k] = p;
      return true;
    } else {
      i->second->v = v;
      return false;
    }
  }

  bool erase(const K &k) {
    auto i = m_map.find(k);
    if (i == m_map.end()) return false;
    for (auto p = m_iterators; p; p = p->m_next) {
      if (p->m_i == i->second) p->m_i++;
    }
    m_entries.erase(i->second);
    m_map.erase(i);
    return true;
  }

  void clear() {
    m_map.clear();
    m_entries.clear();
    for (auto p = m_iterators; p; p = p->m_next) {
      p->m_i = m_entries.end();
    }
  }

private:
  OrderedHash() {}

  OrderedHash(const OrderedHash &rval)
    : m_entries(rval.m_entries)
  {
    for (auto i = m_entries.begin(); i != m_entries.end(); i++) {
      m_map[i->k] = i;
    }
  }

  std::list<Entry> m_entries;
  std::unordered_map<K, typename std::list<Entry>::iterator> m_map;
  Iterator* m_iterators = nullptr;

  friend class Iterator;
};

//
// Str
//

class Str : public Pooled<Str, RefCount<Str>> {
public:
  static const Ref<Str> empty;
  static const Ref<Str> nan;
  static const Ref<Str> pos_inf;
  static const Ref<Str> neg_inf;
  static const Ref<Str> undefined;
  static const Ref<Str> null;
  static const Ref<Str> bool_true;
  static const Ref<Str> bool_false;

  static auto make(const std::string &str) -> Str* {
    const auto i = ht().find(str);
    if (i != ht().end()) return i->second;
    return new Str(str);
  }

  static auto make(const char *str, size_t len) -> Str* {
    return make(std::string(str, len));
  }

  static auto make(double n) -> Str* {
    if (std::isnan(n)) return nan;
    if (std::isinf(n)) return n > 0 ? pos_inf : neg_inf;
    double i; std::modf(n, &i);
    if (std::modf(n, &i) == 0) return make(std::to_string(int64_t(i)));
    return make(std::to_string(n));
  }

  auto length() const -> size_t { return m_str.length(); }
  auto str() const -> const std::string& { return m_str; }
  auto c_str() const -> const char* { return m_str.c_str(); }
  auto parse_int() const -> double;
  auto parse_float() const -> double;

private:
  std::string m_str;

  Str(const std::string &str) : m_str(str) {
    ht()[str] = this;
  }

  ~Str() {
    ht().erase(m_str);
  }

  static auto ht() -> std::unordered_map<std::string, Str*>&;

  friend class RefCount<Str>;
};

//
// ConstStr
//

class ConstStr {
public:
  ConstStr(const char *str)
    : m_str(Str::make(str)) {}

  Str* get() const { return m_str.get(); }
  operator Str*() const { return get(); }

private:
  Ref<Str> m_str;
};

//
// Field
//

class Field : public RefCount<Field> {
public:
  enum Type {
    Variable,
    Accessor,
    Method,
  };

  enum Option {
    Enumerable   = 1<<0,
    Writable     = 1<<1,
    Configurable = 1<<2,
  };

  auto key() const -> Str* { return m_key; }
  auto name() const -> const std::string& { return m_key->str(); }
  auto type() const -> Type { return m_type; }
  bool is_variable() const { return m_type == Variable; }
  bool is_accessor() const { return m_type == Accessor; }
  bool is_method() const { return m_type == Method; }
  bool is_enumerable() const { return m_options & Enumerable; }
  bool is_writable() const { return m_options & Writable; }
  bool is_configurable() const { return m_options & Configurable; }
  auto id() const -> int { return m_id; }

protected:
  Field(const std::string &name, Type type, int options = 0, int id = -1)
    : m_key(Str::make(name))
    , m_type(type)
    , m_options(options)
    , m_id(id) {}

  virtual ~Field() {}

  Ref<Str> m_key;
  Type m_type;
  int m_options;
  int m_id;

  friend class RefCount<Field>;
};

//
// Class
//

class Class : public RefCount<Class> {
public:
  static auto make(const std::string &name, Class *super, const std::list<Field*> &fields) -> Class* {
    return new Class(name, super, fields);
  }

  static auto all() -> const std::map<std::string, Class*>& { return m_class_map; }

  static auto get(const std::string &name) -> Class* {
    auto i = m_class_map.find(name);
    return i == m_class_map.end() ? nullptr : i->second;
  }

  auto name() const -> const std::string& { return m_name; }
  void set_ctor(const std::function<Object*(Context&)> &ctor) { m_ctor = ctor; }
  void set_geti(const std::function<void(Object*, int, Value&)> &geti) { m_geti = geti; }
  void set_seti(const std::function<void(Object*, int, const Value&)> &seti) { m_seti = seti; }
  bool has_ctor() const { return (bool)m_ctor; }
  bool has_geti() const { return (bool)m_geti; }
  bool has_seti() const { return (bool)m_seti; }
  auto object_count() const -> size_t { return m_object_count; }

  auto field_count() const -> size_t {
    return m_fields.size();
  }

  auto field(int i) -> Field* {
    return m_fields[i].get();
  }

  auto find_field(Str *key) -> int {
    auto i = m_field_map.find(key);
    if (i == m_field_map.end()) return -1;
    return i->second;
  }

  auto init(Object *obj, Object *prototype = nullptr) -> Object*;
  void free(Object *obj);

  auto construct(Context &ctx) -> Object* { return m_ctor ? m_ctor(ctx) : nullptr; }
  auto construct() -> Object*;

  void get(Object *obj, int id, Value &val);
  void set(Object *obj, int id, const Value &val);
  void geti(Object *obj, int i, Value &val) { m_geti(obj, i, val); }
  void seti(Object *obj, int i, const Value &val) { m_seti(obj, i, val); }

  bool is_derived_from(Class *s) {
    for (auto p = this; p; p = p->m_super) {
      if (p == s) {
        return true;
      }
    }
    return false;
  }

private:
  Class(
    const std::string &name,
    Class *super,
    const std::list<Field*> &fields
  );

  ~Class();

  Class* m_super = nullptr;
  std::string m_name;
  std::function<Object*(Context&)> m_ctor;
  std::function<void(Object*, int, Value&)> m_geti;
  std::function<void(Object*, int, const Value&)> m_seti;
  std::vector<Ref<Field>> m_fields;
  std::vector<int> m_field_index;
  std::unordered_map<Str*, int> m_field_map;
  size_t m_object_count = 0;

  static std::map<std::string, Class*> m_class_map;

  friend class RefCount<Class>;
};

//
// ClassDef
//

template<class T>
class ClassDef {
public:
  static Class* get() {
    if (!m_c) {
      init();
      if (!m_c) {
        auto s = m_super;
        if (!s) s = class_of<Object>();
        int status;
        auto c_name = typeid(T).name();
        auto cxx_name = abi::__cxa_demangle(c_name, 0, 0, &status);
        m_c = Class::make(cxx_name ? cxx_name : c_name, s, m_fields);
        m_c->set_ctor(m_ctor);
        m_c->set_geti(m_geti);
        m_c->set_seti(m_seti);
      }
      m_c->retain();
    }
    return m_c;
  }

private:
  static void init();

  template<class S>
  static void super() { m_super = class_of<S>(); }
  static void super(Class *super) { m_super = super; }
  static void ctor(std::function<Object*(Context&)> f = [](Context&) -> Object* { return T::make(); }) { m_ctor = f; };
  static void geti(std::function<void(Object*, int, Value&)> f) { m_geti = f; }
  static void seti(std::function<void(Object*, int, const Value&)> f) { m_seti = f; }

  static void variable(const std::string &name);
  static void variable(const std::string &name, typename T::Field id, int options = Field::Enumerable | Field::Writable);
  static void variable(const std::string &name, const Value &value, int options = Field::Enumerable | Field::Writable);
  static void variable(const std::string &name, const Value &value, typename T::Field id, int options = Field::Enumerable | Field::Writable);
  static void variable(const std::string &name, Class *clazz, int options = Field::Enumerable | Field::Writable);
  static void variable(const std::string &name, Class *clazz, typename T::Field id, int options = Field::Enumerable | Field::Writable);

  static void accessor(
    const std::string &name,
    std::function<void(Object*, Value&)> getter,
    std::function<void(Object*, const Value&)> setter = nullptr
  );

  static void method(
    const std::string &name,
    std::function<void(Context&, Object*, Value&)> invoke,
    Class *constructor_class = nullptr
  );

  static Class* m_c;
  static Class* m_super;
  static std::list<Field*> m_fields;
  static std::function<Object*(Context&)> m_ctor;
  static std::function<void(Object*, int, Value&)> m_geti;
  static std::function<void(Object*, int, const Value&)> m_seti;
};

template<class T>
Class* class_of() { return ClassDef<T>::get(); }

template<class T> Class* ClassDef<T>::m_c = nullptr;
template<class T> Class* ClassDef<T>::m_super = nullptr;
template<class T> std::list<Field*> ClassDef<T>::m_fields;
template<class T> std::function<Object*(Context&)> ClassDef<T>::m_ctor = [](Context&) -> Object* { return nullptr; };
template<class T> std::function<void(Object*, int, Value&)> ClassDef<T>::m_geti;
template<class T> std::function<void(Object*, int, const Value&)> ClassDef<T>::m_seti;

//
// EnumDef
//

template<class T>
class EnumDef {
public:
  static auto name(T v) -> Str* {
    init_if_not_yet();
    auto n = int(v);
    if (n < 0) return nullptr;
    if (n >= m_val_to_str.size()) return nullptr;
    return m_val_to_str[n];
  }

  static auto value(Str *s) -> T {
    init_if_not_yet();
    auto i = m_str_to_val.find(s);
    if (i == m_str_to_val.end()) return (T)(-1);
    return i->second;
  }

  static auto value(Str *s, T def) -> T {
    if (!s) return def;
    return value(s);
  }

private:
  static void init_if_not_yet() {
    if (m_initialized) return;
    init();
    m_initialized = true;
  }

  static void init();

  static void define(T val, const char *str) {
    auto s = Str::make(str)->retain();
    auto n = int(val);
    if (n >= m_val_to_str.size()) m_val_to_str.resize(n + 1);
    m_val_to_str[n] = s;
    m_str_to_val[s] = val;
  }

  static bool m_initialized;
  static std::vector<Str*> m_val_to_str;
  static std::map<Str*, T> m_str_to_val;
};

template<class T> bool EnumDef<T>::m_initialized = false;
template<class T> std::vector<Str*> EnumDef<T>::m_val_to_str;
template<class T> std::map<Str*, T> EnumDef<T>::m_str_to_val;

//
// Accessor
//

class Accessor : public Field {
public:
  void get(Object *obj, Value &val) {
    m_getter(obj, val);
  }

  void set(Object *obj, const Value &val) {
    if (m_setter) m_setter(obj, val);
  }

private:
  Accessor(
    const std::string &name,
    std::function<void(Object*, Value&)> getter,
    std::function<void(Object*, const Value&)> setter = nullptr
  ) : Field(name, Field::Accessor)
    , m_getter(getter)
    , m_setter(setter) {}

  std::function<void(Object*, Value&)> m_getter;
  std::function<void(Object*, const Value&)> m_setter;

  template<class T>
  friend class ClassDef;
};

template<class T>
void ClassDef<T>::accessor(
  const std::string &name,
  std::function<void(Object*, Value&)> getter,
  std::function<void(Object*, const Value&)> setter
) {
  m_fields.push_back(new Accessor(name, getter, setter));
}

//
// Value
//

class Value {
public:
  enum class Type {
    Empty,
    Undefined,
    Boolean,
    Number,
    String,
    Object,
  };

  static const Value empty;
  static const Value undefined;
  static const Value null;

  Value() : m_t(Type::Undefined) {}
  Value(const Value *v) { if (v) assign(*v); else m_t = Type::Empty; }
  Value(const Value &v) { assign(v); }
  Value(Value &&v) : m_t(v.m_t), m_v(v.m_v) { v.m_t = Type::Empty; }
  Value(bool b) : m_t(Type::Boolean) { m_v.b = b; }
  Value(int n) : m_t(Type::Number) { m_v.n = n; }
  Value(unsigned int n) : m_t(Type::Number) { m_v.n = n; }
  Value(uint64_t n) : m_t(Type::Number) { m_v.n = n; }
  Value(double n) : m_t(Type::Number) { m_v.n = n; }
  Value(const char *s) : m_t(Type::String) { m_v.s = Str::make(s)->retain(); }
  Value(const char *s, size_t n) : m_t(Type::String) { m_v.s = Str::make(s, n)->retain(); }
  Value(const std::string &s) : m_t(Type::String) { m_v.s = Str::make(s)->retain(); }
  Value(Str *s) : m_t(Type::String) { m_v.s = s; s->retain(); }
  Value(Object *o) : m_t(Type::Object) { m_v.o = o; if (o) retain(o); }

  ~Value() { release(); }

  auto operator=(const Value &v) -> Value& { release(); assign(v); return *this; }
  auto operator=(Value &&v) -> Value& { release(); m_t = v.m_t; m_v = v.m_v; v.m_t = Type::Empty; return *this; }
  bool operator==(const Value &v) { return equals(v); }
  bool operator!=(const Value &v) { return !equals(v); }

  auto type() const -> Type { return m_t; }
  auto b() const -> bool { return m_v.b; }
  auto n() const -> double { return m_v.n; }
  auto s() const -> Str* { return m_v.s; }
  auto o() const -> Object* { return m_v.o; }
  auto f() const -> Function*;

  template<class T> auto as() const -> T* { return static_cast<T*>(o()); }
  template<class T> bool is() const { return is_class(class_of<T>()); }
  template<class T> bool is_instance_of() const { return is_instance_of(class_of<T>()); }

  bool is_empty() const { return m_t == Type::Empty; }
  bool is_undefined() const { return m_t == Type::Undefined; }
  bool is_null() const { return m_t == Type::Object && o() == nullptr; }
  bool is_nullish() const { return is_undefined() || is_null(); }
  bool is_boolean() const { return m_t == Type::Boolean; }
  bool is_number() const { return m_t == Type::Number; }
  bool is_string() const { return m_t == Type::String; }
  bool is_object() const { return m_t == Type::Object; }
  bool is_class(Class *c) const { return m_t == Type::Object && o() && type_of(o()) == c; }
  bool is_instance_of(Class *c) const { return m_t == Type::Object && o() && type_of(o())->is_derived_from(c); }
  bool is_function() const { return is_instance_of(class_of<Function>()); }
  bool is_array() const { return is_instance_of(class_of<Array>()); }

  void set(bool b) { release(); m_t = Type::Boolean; m_v.b = b; }
  void set(int n) { release(); m_t = Type::Number; m_v.n = n; }
  void set(unsigned int n) { release(); m_t = Type::Number; m_v.n = n; }
  void set(uint64_t n) { release(); m_t = Type::Number; m_v.n = n; }
  void set(double n) { release(); m_t = Type::Number; m_v.n = n; }
  void set(const char *s) { release(); m_t = Type::String; m_v.s = Str::make(s)->retain(); }
  void set(const std::string &s) { release(); m_t = Type::String; m_v.s = Str::make(s)->retain(); }
  void set(Str *s) { s->retain(); release(); m_t = Type::String; m_v.s = s; }
  void set(Object *o) { if (o) retain(o); release(); m_t = Type::Object; m_v.o = o; }

  auto to_boolean() const -> bool {
    switch (m_t) {
      case Value::Type::Empty: return false;
      case Value::Type::Undefined: return false;
      case Value::Type::Boolean: return b();
      case Value::Type::Number: return n() != 0 && !std::isnan(n());
      case Value::Type::String: return s()->length() > 0;
      case Value::Type::Object: return o() ? true : false;
    }
    return false;
  }

  auto to_number() const -> double {
    switch (m_t) {
      case Value::Type::Empty: return 0;
      case Value::Type::Undefined: return 0;
      case Value::Type::Boolean: return b() ? 1 : 0;
      case Value::Type::Number: return n();
      case Value::Type::String: return s()->parse_float();
      case Value::Type::Object: {
        if (!o()) return 0;
        Value v; value_of(o(), v);
        switch (v.type()) {
          case Value::Type::Empty: return 0;
          case Value::Type::Undefined: return 0;
          case Value::Type::Boolean: return b() ? 1 : 0;
          case Value::Type::Number: return n();
          case Value::Type::String: return s()->parse_float();
          case Value::Type::Object: return v.o() ? std::numeric_limits<double>::quiet_NaN() : 0;
        }
      }
    }
    return 0;
  }

  auto to_string() const -> Str* {
    switch (m_t) {
      case Value::Type::Empty: return Str::undefined->retain();
      case Value::Type::Undefined: return Str::undefined->retain();
      case Value::Type::Boolean: return (b() ? Str::bool_true : Str::bool_false)->retain();
      case Value::Type::Number: return Str::make(n())->retain();
      case Value::Type::String: return s()->retain();
      case Value::Type::Object: return o() ? to_string(o())->retain() : Str::null->retain();
    }
    return Str::empty.get();
  }

  auto to_object() const -> Object* {
    switch (m_t) {
      case Value::Type::Empty: return nullptr;
      case Value::Type::Undefined: return nullptr;
      case Value::Type::Boolean: return retain(box_boolean());
      case Value::Type::Number: return retain(box_number());
      case Value::Type::String: return retain(box_string());
      case Value::Type::Object: return o() ? retain(o()) : nullptr;
    }
    return nullptr;
  }

private:
  union {
    bool b;
    double n;
    Str *s;
    Object *o;
  } m_v;

  Type m_t;

  void release() {
    switch (m_t) {
      case Type::String: m_v.s->release(); break;
      case Type::Object: if (m_v.o) release(m_v.o); break;
      default: break;
    }
  }

  void assign(const Value &v) {
    m_t = v.m_t;
    m_v = v.m_v;
    switch (m_t) {
      case Type::String: s()->retain(); break;
      case Type::Object: if (o()) retain(o()); break;
      default: break;
    }
  }

  bool equals(const Value &r) {
    if (type() != r.type()) return false;
    switch (m_t) {
      case Type::Boolean: return b() == r.b();
      case Type::Number: return n() == r.n();
      case Type::String: return s() == r.s();
      case Type::Object: return o() == r.o();
      default: return true;
    }
  }

  static auto retain(Object *obj) -> Object*;
  static void release(Object *obj);
  static auto type_of(Object *obj) -> Class*;
  static void value_of(Object *obj, Value &out);
  static auto to_string(Object *obj) -> Str*;

  auto box_boolean() const -> Object*;
  auto box_number() const -> Object*;
  auto box_string() const -> Object*;
};

} // namespace pjs

namespace std {

template<>
struct equal_to<pjs::Value> {
  bool operator()(const pjs::Value &a, const pjs::Value &b) const {
    if (a.type() != b.type()) return false;
    if (a.is_string() || a.is_object()) {
      return a.o() == b.o();
    } else if (a.is_number()) {
      return a.n() == b.n();
    } else if (a.is_boolean()) {
      return a.b() == b.b();
    } else {
      return true;
    }
  }
};

template<>
struct less<pjs::Value> {
  bool operator()(const pjs::Value &a, const pjs::Value &b) const {
    if (a.type() == b.type()) {
      if (a.is_string() || a.is_object()) {
        return a.o() < b.o();
      } else if (a.is_number()) {
        return a.n() < b.n();
      } else if (a.is_boolean()) {
        return a.b() < b.b();
      } else {
        return false;
      }
    } else {
      return a.type() < b.type();
    }
  }
};

template<>
struct hash<pjs::Value> {
  size_t operator()(const pjs::Value &v) const {
    if (v.is_string()) {
      hash<std::string> h;
      return h(v.s()->str());
    } else if (v.is_object()) {
      hash<pjs::Object*> h;
      return h(v.o());
    } else {
      hash<double> h;
      return h(v.n());
    }
  }
};

} // namespace std

namespace pjs {

//
// Data
//

typedef PooledArray<Value> Data;

//
// Object
//

class Object : public Pooled<Object>, public RefCount<Object> {
public:
  enum class Field {};

  static auto make() -> Object* {
    auto obj = new Object();
    class_of<Object>()->init(obj);
    return obj;
  }

  auto type() const -> Class* { return m_class; }
  auto data() const -> Data* { return m_data; }

  template<class T> auto as() -> T* { return static_cast<T*>(this); }
  template<class T> auto as() const -> const T* { return static_cast<const T*>(this); }
  template<class T> bool is() const { return m_class == class_of<T>(); }
  template<class T> bool is_instance_of() const { return m_class->is_derived_from(class_of<T>()); }

  bool is_function() const { return is_instance_of<Function>(); }
  bool is_array() const { return is_instance_of<Array>(); }

  bool has(Str *key);
  void get(Str *key, Value &val);
  void set(Str *key, const Value &val);
  auto ht_size() const -> size_t { return m_hash ? m_hash->size() : 0; }
  bool ht_has(Str *key) { return m_hash ? m_hash->has(key) : false; }
  void ht_get(Str *key, Value &val);
  void ht_set(Str *key, const Value &val);
  bool ht_delete(Str *key);

  void get(const std::string &key, Value &val) {
    Ref<Str> s(Str::make(key));
    get(s, val);
  }

  void set(const std::string &key, const Value &val) {
    Ref<Str> s(Str::make(key));
    set(s, val);
  }

  void ht_get(const std::string &key, Value &val) {
    Ref<Str> s(Str::make(key));
    ht_get(s, val);
  }

  void ht_set(const std::string &key, const Value &val) {
    Ref<Str> s(Str::make(key));
    ht_set(s, val);
  }

  void iterate_all(std::function<void(Str*, Value&)> callback);
  bool iterate_while(std::function<bool(Str*, Value&)> callback);

  virtual void value_of(Value &out);
  virtual auto to_string() const -> std::string;

  static auto assign(Object *obj, Object *obj2) -> Object* {
    if (!obj) return nullptr;
    if (obj2) obj2->iterate_all([&](Str *key, Value &val) {
      obj->set(key, val);
    });
    return obj;
  }

  static auto entries(Object *obj) -> Array*;
  static auto from_entries(Array *arr) -> Object*;
  static auto keys(Object *obj) -> Array*;
  static auto values(Object *obj) -> Array*;

protected:
  Object() {}
  ~Object() { if (m_class) m_class->free(this); }

  virtual void finalize() { delete this; }

private:
  Class* m_class = nullptr;
  Data* m_data = nullptr;
  Ref<OrderedHash<Ref<Str>, Value>> m_hash;

  friend class RefCount<Object>;
  friend class Class;
};

template<class T, class Base = Object>
class ObjectTemplate : public Pooled<T, Base> {
public:
  template<typename... Args>
  static auto make(Args&&... args) -> T* {
    auto *obj = new T(std::forward<Args>(args)...);
    pjs::class_of<T>()->init(obj);
    return obj;
  }

  virtual void finalize() override {
    delete static_cast<T*>(this);
  }

protected:
  using Pooled<T, Base>::Pooled;
};

inline auto Value::retain(Object *obj) -> Object* { obj->retain(); return obj; }
inline void Value::release(Object *obj) { obj->release(); }
inline auto Value::type_of(Object *obj) -> Class* { return obj->type(); }
inline void Value::value_of(Object *obj, Value &out) { obj->value_of(out); }
inline auto Value::to_string(Object *obj) -> Str* { return Str::make(obj->to_string()); }

//
// Variable
//

class Variable : public Field {
public:
  template<typename... Args>
  static auto make(Args&&... args) -> Variable* {
    return new Variable(std::forward<Args>(args)...);
  }

  auto value() const -> const Value& { return m_value; }

private:
  Variable(const std::string &name, int options = 0, int id = -1) : Field(name, Field::Variable, options, id) {}
  Variable(const std::string &name, const Value &value, int options = 0, int id = -1) : Field(name, Field::Variable, options, id), m_value(value) {}

  Value m_value;
};

template<class T>
void ClassDef<T>::variable(const std::string &name) {
  m_fields.push_back(Variable::make(name, Field::Enumerable | Field::Writable));
}

template<class T>
void ClassDef<T>::variable(const std::string &name, typename T::Field id, int options) {
  m_fields.push_back(Variable::make(name, options, int(id)));
}

template<class T>
void ClassDef<T>::variable(const std::string &name, const Value &value, int options) {
  m_fields.push_back(Variable::make(name, value, options));
}

template<class T>
void ClassDef<T>::variable(const std::string &name, const Value &value, typename T::Field id, int options) {
  m_fields.push_back(Variable::make(name, value, options, int(id)));
}

template<class T>
void ClassDef<T>::variable(const std::string &name, Class *clazz, int options) {
  m_fields.push_back(Variable::make(name, clazz->construct(), options));
}

template<class T>
void ClassDef<T>::variable(const std::string &name, Class *clazz, typename T::Field id, int options) {
  m_fields.push_back(Variable::make(name, clazz->construct(), options, int(id)));
}

inline auto Class::init(Object *obj, Object *prototype) -> Object* {
  auto size = m_fields.size();
  auto data = Data::make(size);
  if (!prototype) {
    for (size_t i = 0; i < size; i++) {
      auto f = m_fields[i].get();
      if (f->is_variable()) {
        const auto &v = static_cast<Variable*>(f)->value();
        data->at(i) = v;
      }
    }
  } else if (prototype->type() == this) {
    for (size_t i = 0; i < size; i++) {
      auto f = m_fields[i].get();
      if (f->is_variable()) {
        data->at(i) = prototype->m_data->at(i);
      }
    }
    obj->m_hash = prototype->m_hash;
  } else {
    Object::assign(obj, prototype);
  }
  obj->m_class = this;
  obj->m_data = data;
  retain();
  m_object_count++;
  return obj;
}

inline void Class::free(Object *obj) {
  obj->m_data->free();
  release();
  m_object_count--;
}

inline void Class::get(Object *obj, int id, Value &val) {
  val = obj->m_data->at(m_field_index[id]);
}

inline void Class::set(Object *obj, int id, const Value &val) {
  obj->m_data->at(m_field_index[id]) = val;
}

template<class T>
inline void get(Object *obj, typename T::Field id, Value &val) {
  auto c = class_of<T>();
  c->get(obj, int(id), val);
}

template<class T>
inline void set(Object *obj, typename T::Field id, const Value &val) {
  auto c = class_of<T>();
  c->set(obj, int(id), val);
}

inline bool Object::has(Str *key) {
  auto i = m_class->find_field(key);
  if (i >= 0) return true;
  else return ht_has(key);
}

inline void Object::get(Str *key, Value &val) {
  auto i = m_class->find_field(key);
  if (i >= 0) val = m_data->at(i);
  else ht_get(key, val);
}

inline void Object::set(Str *key, const Value &val) {
  auto i = m_class->find_field(key);
  if (i >= 0) m_data->at(i) = val;
  else ht_set(key, val);
}

inline void Object::ht_get(Str *key, Value &val) {
  if (!m_hash || !m_hash->get(key, val)) {
    val = Value::undefined;
  }
}

inline void Object::ht_set(Str *key, const Value &val) {
  if (!m_hash) m_hash = OrderedHash<Ref<Str>, Value>::make();
  m_hash->set(key, val);
}

inline bool Object::ht_delete(Str *key) {
  if (!m_hash) return false;
  return m_hash->erase(key);
}

inline void Object::iterate_all(std::function<void(Str*, Value&)> callback) {
  for (size_t i = 0, n = m_class->field_count(); i < n; i++) {
    auto f = m_class->field(i);
    if (f->is_enumerable()) {
      callback(f->key(), m_data->at(i));
    }
  }
  if (m_hash) {
    OrderedHash<Ref<Str>, Value>::Iterator iterator(m_hash);
    while (auto *ent = iterator.next()) {
      callback(ent->k, ent->v);
    }
  }
}

inline bool Object::iterate_while(std::function<bool(Str*, Value&)> callback) {
  for (size_t i = 0, n = m_class->field_count(); i < n; i++) {
    auto f = m_class->field(i);
    if (f->is_enumerable()) {
      if (!callback(f->key(), m_data->at(i))) {
        return false;
      }
    }
  }
  if (m_hash) {
    OrderedHash<Ref<Str>, Value>::Iterator iterator(m_hash);
    while (auto *ent = iterator.next()) {
      if (!callback(ent->k, ent->v)) {
        return false;
      }
    }
  }
  return true;
}

//
// Scope
//

class Scope : public Pooled<Scope, RefCount<Scope>> {
public:
  struct Variable {
    Ref<Str> name;
    bool is_closure = false;
  };

  static auto make(Scope *parent, int size, Variable *variables = nullptr) -> Scope* {
    return new Scope(parent, size, variables);
  }

  auto parent() const -> Scope* { return m_parent; }
  auto size() const -> int { return m_data->size(); }
  auto value(int i) -> Value& { return m_data->at(i); }
  auto values() -> Value* { return m_data->elements(); }
  auto variables() const -> Variable* { return m_variables; }

  void clear() {
    auto values = m_data->elements();
    for (size_t i = 0, n = m_data->size(); i < n; i++) {
      if (!m_variables || !m_variables[i].is_closure) {
        values[i] = Value::undefined;
      }
    }
  }

private:
  Scope(Scope *parent, int size, Variable *variables)
    : m_parent(parent)
    , m_data(Data::make(size))
    , m_variables(variables) {}

  ~Scope() { m_data->free(); }

  Ref<Scope> m_parent;
  Data* m_data;
  Variable* m_variables;

  friend class RefCount<Scope>;
};

//
// Context
//

class Context {
public:
  struct Location {
    std::string name;
    int file = 0;
    int line = 0;
    int column = 0;
  };

  struct Error {
    std::string message;
    std::vector<Location> backtrace;
    auto where() const -> const Location*;
  };

  Context()
    : m_root(this)
    , m_caller(nullptr)
    , m_narg(0)
    , m_argc(0)
    , m_argv(nullptr)
    , m_error(std::make_shared<Error>()) {}

  Context(Object *g, Ref<Object> *l = nullptr)
    : m_root(this)
    , m_caller(nullptr)
    , m_g(g)
    , m_l(l)
    , m_narg(0)
    , m_argc(0)
    , m_argv(nullptr)
    , m_error(std::make_shared<Error>()) {}

  Context(Context &ctx, int narg = 0, Scope *scope = nullptr)
    : m_root(ctx.m_root)
    , m_caller(&ctx)
    , m_g(ctx.m_g)
    , m_l(ctx.m_l)
    , m_scope(scope)
    , m_narg(narg)
    , m_argc(0)
    , m_argv(nullptr)
    , m_error(ctx.m_error) {}

  auto root() const -> Context* { return m_root; }
  auto g() const -> Object* { return m_g; }
  auto l(int i) const -> Object* { return m_l ? m_l[i].get() : nullptr; }
  auto scope() const -> Scope* { return m_scope; }
  auto argc() const -> int { return m_argc; }
  auto arg(int i) const -> Value& { return m_argv[i]; }

  void init(int argc, Value argv[]) {
    m_argc = argc;
    m_argv = argv;
    if (auto *scope = m_scope.get()) {
      int n = std::min(argc, m_narg);
      auto data = scope->values();
      auto size = scope->size();
      for (int i = 0; i < n; i++) data[i] = argv[i];
      while (n < size) data[n++] = Value::undefined;
    }
  }

  template<typename... Args>
  bool arguments(int n, Args... argv) {
    return get_args(true, n, 0, argv...);
  }

  template<typename... Args>
  bool try_arguments(int n, Args... argv) {
    return get_args(false, n, 0, argv...);
  }

  void reset();
  bool ok() const { return !m_has_error; }
  auto error() const -> Error& { return *m_error; }
  void error(const std::string &msg);
  void error(const std::runtime_error &err);
  void error_argument_count(int n);
  void error_argument_count(int min, int max);
  void error_argument_type(int i, const char *type);
  void backtrace(int module, int line, int column);
  void backtrace(const std::string &name);

private:
  Context* m_root;
  Context* m_caller;
  Ref<Object> m_g, *m_l;
  Ref<Scope> m_scope;
  int m_narg;
  int m_argc;
  Value* m_argv;
  bool m_has_error = false;
  std::shared_ptr<Error> m_error;

  template<typename T, typename... Rest>
  bool get_args(bool set_error, int n, int i, T a, Rest... rest) {
    if (i >= argc()) {
      if (i >= n) return true;
      if (set_error) error_argument_count(n);
      return false;
    }
    if (i < n || !arg(i).is_nullish()) {
      if (!get_arg(set_error, i, a)) {
        return false;
      }
    }
    return get_args(set_error, n, i + 1, rest...);
  }

  template<typename T>
  bool get_args(bool set_error, int n, int i, T a) {
    if (i >= argc()) {
      if (i >= n) return true;
      if (set_error) error_argument_count(n);
      return false;
    }
    if (i >= n && arg(i).is_nullish()) return true;
    return get_arg(set_error, i, a);
  }

  bool get_arg(bool set_error, int i, Value *v) {
    *v = arg(i);
    return true;
  }

  bool get_arg(bool set_error, int i, bool *b) {
    if (!arg(i).is_boolean()) {
      if (set_error) error_argument_type(i, "a boolean");
      return false;
    }
    *b = arg(i).b();
    return true;
  }

  bool get_arg(bool set_error, int i, int *n) {
    if (!arg(i).is_number()) {
      if (set_error) error_argument_type(i, "a number");
      return false;
    }
    *n = int(arg(i).n());
    return true;
  }

  bool get_arg(bool set_error, int i, double *n) {
    if (!arg(i).is_number()) {
      if (set_error) error_argument_type(i, "a number");
      return false;
    }
    *n = arg(i).n();
    return true;
  }

  bool get_arg(bool set_error, int i, Str **s) {
    if (!arg(i).is_string()) {
      if (set_error) error_argument_type(i, "a string");
      return false;
    }
    *s = arg(i).s();
    return true;
  }

  bool get_arg(bool set_error, int i, std::string *s) {
    if (!arg(i).is_string()) {
      if (set_error) error_argument_type(i, "a string");
      return false;
    }
    *s = arg(i).s()->str();
    return true;
  }

  bool get_arg(bool set_error, int i, Object **o) {
    if (!arg(i).is_object()) {
      if (set_error) error_argument_type(i, "an object");
      return false;
    }
    *o = arg(i).o();
    return true;
  }

  bool get_arg(bool set_error, int i, Function **f) {
    if (!arg(i).is_function()) {
      if (set_error) error_argument_type(i, "a function");
      return false;
    }
    *f = arg(i).as<Function>();
    return true;
  }

  template<class T>
  bool get_arg(bool set_error, int i, T **o) {
    if (!arg(i).is_null() && !arg(i).is_instance_of<T>()) {
      if (set_error) {
        std::string type("an instance of ");
        type += class_of<T>()->name();
        error_argument_type(i, type.c_str());
      }
      return false;
    }
    *o = arg(i).is_null() ? nullptr : arg(i).as<T>();
    return true;
  }
};

inline auto Class::construct() -> Object* {
  Context ctx;
  return construct(ctx);
}

//
// Method
//

class Method : public Field {
public:
  static auto make(
    const std::string &name,
    int argc, int nvar, Scope::Variable *variables,
    std::function<void(Context&, Object*, Value&)> invoke,
    Class *constructor_class = nullptr
  ) -> Method* {
    assert(nvar >= argc);
    return new Method(name, argc, nvar, variables, invoke, constructor_class);
  }

  auto constructor_class() const -> Class* { return m_constructor_class; }

  void invoke(Context &ctx, Scope *scope, Object *thiz, int argc, Value argv[], Value &retv) {
    auto callee_scope = Scope::make(scope, m_nvar, m_variables);
    Context fctx(ctx, m_argc, callee_scope);
    fctx.init(argc, argv);
    retv = Value::undefined;
    m_invoke(fctx, thiz, retv);
    callee_scope->clear();
    if (!fctx.ok()) fctx.backtrace(name());
  }

  auto construct(Context &ctx, int argc, Value argv[]) -> Object* {
    if (!m_constructor_class) {
      ctx.error("function is not a constructor");
      return nullptr;
    }
    Context fctx(ctx); // No need for a scope since JS ctors are not supported yet
    fctx.init(argc, argv);
    auto *obj = m_constructor_class->construct(fctx);
    if (!fctx.ok()) fctx.backtrace(name());
    return obj;
  }

private:
  Method(
    const std::string &name,
    int argc, int nvar, Scope::Variable *variables,
    std::function<void(Context&, Object*, Value&)> invoke,
    Class *constructor_class = nullptr
  ) : Field(name, Field::Method)
    , m_argc(argc)
    , m_nvar(nvar)
    , m_variables(variables)
    , m_invoke(invoke)
    , m_constructor_class(constructor_class) {}

  int m_argc;
  int m_nvar;
  std::function<void(Context&, Object*, Value&)> m_invoke;
  Scope::Variable* m_variables;
  Class* m_constructor_class;
};

template<class T>
void ClassDef<T>::method(
  const std::string &name,
  std::function<void(Context&, Object*, Value&)> invoke,
  Class *constructor_class
) {
  m_fields.push_back(Method::make(name, 0, 0, nullptr, invoke, constructor_class));
}

//
// Function
//

class Function : public ObjectTemplate<Function> {
public:
  virtual auto to_string() const -> std::string override;

  auto scope() const -> Scope* { return m_scope; }
  auto method() const -> Method* { return m_method; }
  auto thiz() const -> Object* { return m_this; }

  void operator()(Context &ctx, int argc, Value argv[], Value &retv) {
    m_method->invoke(ctx, m_scope, m_this, argc, argv, retv);
  }

  auto construct(Context &ctx, int argc, Value argv[]) -> Object* {
    return m_method->construct(ctx, argc, argv);
  }

protected:
  Function() {}

  Function(Method *method, Object *thiz = nullptr, Scope *scope = nullptr)
    : m_method(method)
    , m_this(thiz)
    , m_scope(scope) {}

  Ref<Method> m_method;
  Ref<Object> m_this;
  Ref<Scope> m_scope;

  friend class ObjectTemplate<Function>;
};

inline auto Value::f() const -> Function* {
  return static_cast<Function*>(m_v.o);
}

template<class T>
class FunctionTemplate : public ObjectTemplate<T, Function> {
protected:
  FunctionTemplate() {
    auto c = class_of<T>();
    Function::m_method = Method::make(
      c->name(), 0, 0, nullptr,
      [this](Context &ctx, Object *obj, Value &ret) {
        (*static_cast<T*>(this))(ctx, obj, ret);
      }
    );
  }

  void operator()(Context &ctx, Object *obj, Value &ret) {}
};

//
// Constructor
//

template<class T>
class Constructor;

template<class T>
class ConstructorTemplate : public ObjectTemplate<Constructor<T>, Function> {
protected:
  ConstructorTemplate() {
    auto c = class_of<T>();
    Function::m_method = Method::make(
      c->name(), 0, 0, nullptr,
      [this](Context &ctx, Object *obj, Value &ret) {
        (*static_cast<Constructor<T>*>(this))(ctx, obj, ret);
      }, c);
  }

  void operator()(Context &ctx, Object *obj, Value &ret) {}
};

template<class T>
class Constructor : public ConstructorTemplate<T> {};

//
// PropertyCache
//

class PropertyCache {
public:
  PropertyCache() {}
  PropertyCache(Str *key) : m_const_key(key) {}
  PropertyCache(const std::string &key) : m_const_key(Str::make(key)) {}

  bool has(Object *obj) { return has(obj, m_const_key); }
  bool del(Object *obj) { return del(obj, m_const_key); }
  void get(Object *obj, Value &val) { return get(obj, m_const_key, val); }
  void set(Object *obj, const Value &val) { return set(obj, m_const_key, val); }

  bool get(Object *obj, bool &b) {
    Value v; get(obj, v);
    if (v.is_undefined()) return false;
    b = v.to_boolean();
    return true;
  }

  bool get(Object *obj, double &n) {
    Value v; get(obj, v);
    if (!v.is_number()) return false;
    n = v.n();
    return true;
  }

  bool get(Object *obj, int &n) {
    Value v; get(obj, v);
    if (!v.is_number()) return false;
    n = (int)v.n();
    return true;
  }

  bool get(Object *obj, Str* &s) {
    Value v; get(obj, v);
    if (!v.is_string()) return false;
    s = v.s();
    return true;
  }

  bool get(Object *obj, Object* &o) {
    Value v; get(obj, v);
    if (!v.is_object()) return false;
    o = v.o();
    return true;
  }

  bool has(Object *obj, Str *key) {
    auto i = find(obj->type(), key);
    if (i >= 0) return true;
    else return obj->ht_has(key);
  }

  bool del(Object *obj, Str *key) {
    auto i = find(obj->type(), key);
    if (i >= 0) return false;
    obj->ht_delete(key);
    return true;
  }

  void get(Object *obj, Str *key, Value &val) {
    auto type = obj->type();
    auto i = find(type, key);
    if (i >= 0) {
      auto f = type->field(i);
      if (f->is_accessor()) {
        static_cast<Accessor*>(f)->get(obj, val);
        return;
      }
      if (f->is_method()) {
        auto v = Function::make(static_cast<Method*>(f), obj);
        val.set(v);
        return;
      }
      val = obj->data()->at(i);
      return;
    }
    obj->ht_get(key, val);
  }

  void set(Object *obj, Str *key, const Value &val) {
    auto type = obj->type();
    auto i = find(type, key);
    if (i >= 0) {
      auto f = type->field(i);
      if (f->is_accessor()) {
        static_cast<Accessor*>(f)->set(obj, val);
        return;
      }
      if (f->is_writable()) {
        obj->data()->at(i) = val;
        return;
      }
    }
    obj->ht_set(key, val);
  }

private:
  Ref<Str> m_const_key;
  Ref<Str> m_key;
  Ref<Class> m_class;
  int m_index = -1;

  int find(Class *type, Str *key) {
    auto i = m_index;
    if (type != m_class || key != m_key) {
      m_class = type;
      m_key = key;
      m_index = (i = type->find_field(key));
    }
    return i;
  }
};

//
// Boolean
//

class Boolean : public ObjectTemplate<Boolean> {
public:
  auto value() const -> bool { return m_b; }

  virtual void value_of(Value &out) override;
  virtual auto to_string() const -> std::string override;

private:
  Boolean(bool b) : m_b(b) {}

  bool m_b;

  friend class ObjectTemplate<Boolean>;
};

inline auto Value::box_boolean() const -> Object* {
  return Boolean::make(b());
}

template<>
class Constructor<Boolean> : public ConstructorTemplate<Boolean> {
public:
  void operator()(Context &ctx, Object *obj, Value &ret) {
    ret.set(ctx.argc() > 0 ? ctx.arg(0).to_boolean() : false);
  }
};

//
// Number
//

class Number : public ObjectTemplate<Number> {
public:
  auto value() const -> double { return m_n; }

  virtual void value_of(Value &out) override;
  virtual auto to_string() const -> std::string override;

private:
  Number(double n) : m_n(n) {}

  double m_n;

  friend class ObjectTemplate<Number>;
};

inline auto Value::box_number() const -> Object* {
  return Number::make(n());
}

template<>
class Constructor<Number> : public ConstructorTemplate<Number> {
public:
  void operator()(Context &ctx, Object *obj, Value &ret) {
    ret.set(ctx.argc() > 0 ? ctx.arg(0).to_number() : 0);
  }
};

//
// String
//

class String : public ObjectTemplate<String> {
public:
  auto value() const -> Str* { return m_s; }

  virtual void value_of(Value &out) override;
  virtual auto to_string() const -> std::string override;

  auto length() -> int { return m_s->length(); }

  bool ends_with(Str *search, int length = -1);
  auto replace(Str *pattern, Str *replacement, bool all = false) -> Str*;
  auto replace(RegExp *pattern, Str *replacement) -> Str*;
  auto split(Str *separator = nullptr, int limit = -1) -> Array*;
  bool starts_with(Str *search, int position = 0);
  auto substring(int start) -> Str*;
  auto substring(int start, int end) -> Str*;
  auto to_lower_case() -> Str*;
  auto to_upper_case() -> Str*;

private:
  String(Str *s) : m_s(s) {}
  String(const std::string &str) : m_s(Str::make(str)) {}

  Ref<Str> m_s;

  friend class ObjectTemplate<String>;
};

inline auto Value::box_string() const -> Object* {
  return String::make(s());
}

template<>
class Constructor<String> : public ConstructorTemplate<String> {
public:
  void operator()(Context &ctx, Object *obj, Value &ret) {
    if (ctx.argc() > 0) {
      auto s = ctx.arg(0).to_string();
      ret.set(s);
      s->release();
    } else {
      ret.set(Str::empty);
    }
  }
};

//
// Array
//

class Array : public ObjectTemplate<Array> {
public:
  static const size_t MAX_SIZE = 0x100000;

  auto length() const -> int { return m_size; }

  void length(int n) {
    if (n < 0) n = 0;
    if (n < m_size) {
      auto end = std::min((int)m_data->size(), m_size);
      auto values = m_data->elements();
      for (auto i = n; i < end; i++) {
        values[i] = Value::empty;
      }
    }
    m_size = n;
  }

  void get(int i, Value &v) const {
    if (0 <= i && i < m_data->size()) {
      v = m_data->at(i);
    } else {
      v = Value::undefined;
    }
  }

  void set(int i, const Value &v) {
    if (i >= m_data->size()) {
      auto new_size = 1 << power(i + 1);
      if (new_size > MAX_SIZE) return; // TODO: report error
      auto *data = Data::make(new_size);
      auto *new_values = data->elements();
      auto *old_values = m_data->elements();
      auto end = std::min((int)m_data->size(), m_size);
      for (int i = 0; i < end; i++) new_values[i] = std::move(old_values[i]);
      m_data->free();
      m_data = data;
      new_values[i] = v;
    } else {
      m_data->at(i) = v;
    }
    m_size = std::max(m_size, i + 1);
  }

  void clear(int i) {
    if (i < 0 || i >= m_size || i >= m_data->size()) return;
    m_data->at(i) = Value::empty;
  }

  int iterate_while(std::function<bool(Value&, int)> callback) {
    auto values = m_data->elements();
    for (int i = 0, n = std::min((int)m_data->size(), m_size); i < n; i++) {
      auto &v = values[i];
      if (!v.is_empty() && !callback(v, i)) return i;
    }
    return m_size;
  }

  void iterate_all(std::function<void(Value&, int)> callback) {
    iterate_while([&](Value &v, int i) -> bool {
      callback(v, i);
      return true;
    });
  }

  void fill(const Value &v, int start);
  void fill(const Value &v, int start, int end);
  auto filter(std::function<bool(Value&, int)> callback) -> Array*;
  void find(std::function<bool(Value&, int)> callback, Value &result);
  auto findIndex(std::function<bool(Value&, int)> callback) -> int;
  auto flat(int depth = 1) -> Array*;
  auto flatMap(std::function<bool(Value&, int, Value&)> callback) -> Array*;
  void forEach(std::function<bool(Value&, int)> callback);
  auto join(Str *separator = nullptr) -> Str*;
  auto map(std::function<bool(Value&, int, Value&)> callback) -> Array*;
  void pop(Value &result);
  void push(const Value &v) { set(m_size, v); }
  void reduce(std::function<bool(Value&, Value&, int)> callback, Value &result);
  void reduce(std::function<bool(Value&, Value&, int)> callback, Value &initial, Value &result);
  auto reverse() -> Array*;
  void shift(Value &result);
  auto slice(int start, int end) -> Array*;
  void sort();
  void sort(const std::function<bool(const Value&, const Value&)> &comparator);

private:
  Array(size_t size = 0)
    : m_data(Data::make(std::max(2, 1 << power(std::max(0, int(size))))))
    , m_size(size) {}

  ~Array() {
    length(0);
    free(m_data);
  }

  Data* m_data;
  int m_size;

  static auto power(size_t size) -> size_t {
    return sizeof(unsigned int) * 8 - __builtin_clz(size - 1);
  }

  friend class ObjectTemplate<Array>;
};

//
// RegExp
//

class RegExp : public ObjectTemplate<RegExp> {
public:
  auto regex() const -> const std::regex& { return m_regex; }

  auto source() const -> Str* { return m_source; }
  bool global() const { return m_global; }
  bool ignore_case() const { return m_regex.flags() & std::regex::icase; }
  auto last_index() const -> int { return m_last_index; }

  auto exec(Str *str) -> Array*;
  bool test(Str *str);

private:
  RegExp(Str *pattern);
  RegExp(Str *pattern, Str *flags);

  Ref<Str> m_source;
  std::regex m_regex;
  std::smatch m_match;
  bool m_global;
  int m_last_index = 0;

  static auto chars_to_flags(Str *chars, bool &global) -> std::regex::flag_type;

  friend class ObjectTemplate<RegExp>;
};

} // namespace pjs

#endif // PJS_TYPES_HPP