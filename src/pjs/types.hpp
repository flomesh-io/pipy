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

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cxxabi.h>
#endif

namespace pjs {

class Source;
class Array;
class Boolean;
class Class;
class Instance;
class Module;
class Fiber;
class Context;
class Scope;
class Error;
class Field;
class Variable;
class Method;
class Function;
class Int;
class Number;
class Object;
class Promise;
class PromiseAggregator;
class PromiseDependency;
class RegExp;
class String;
class Value;
class SharedObject;
class SharedValue;

#ifdef _MSC_VER
inline static uint32_t clz(uint32_t x) {
  unsigned long cnt;
  return _BitScanReverse(&cnt, x) ? 31 - cnt : 32;
}
#else
inline static uint32_t clz(uint32_t x) {
  return __builtin_clz(x);
}
#endif

template<class T> Class* class_of();
template<class T> T* coerce(Object *obj);

//
// Variable Length Array
//

template<typename T, size_t L = 100>
class vl_array {
public:
  vl_array(size_t l) : m_size(l) {
    if (l > L) {
      m_data = m_heap = new T[l];
    } else {
      m_data = m_stack;
      m_heap = nullptr;
    }
  }

  ~vl_array() {
    delete [] m_heap;
  }

  size_t size() const { return m_size; }
  T* data() { return m_data; }
  T& at(size_t i) { return *(data() + i); }

  operator T*() { return data(); }
  T* operator + (size_t n) { return data() + n; }
  T& operator [](size_t i) { return at(i); }

private:
  size_t m_size;
  T* m_data;
  T* m_heap;
  T m_stack[L];
};

//
// RefCountMT
//

template<class T>
class RefCountMT {
public:
  auto retain() -> T* {
    m_refs.fetch_add(1, std::memory_order_relaxed);
    return static_cast<T*>(this);
  }

  void release() {
    if (m_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      static_cast<T*>(this)->finalize();
    }
  }

protected:
  RefCountMT() : m_refs(0) {}

  void finalize() {
    delete static_cast<T*>(this);
  }

private:
  std::atomic<int> m_refs;
};

//
// Pool
//

class Pool : public RefCountMT<Pool> {
public:
  static auto all() -> std::map<std::string, Pool*> &;

  Pool(const std::string &name, size_t size);
  ~Pool();

  auto name() const -> const std::string& { return m_name; }
  auto size() const -> size_t { return m_size; }
  auto allocated() const -> int { return m_allocated; }
  auto pooled() const -> int { return m_pooled; }

  auto alloc() -> void*;
  void free(void *p);
  void clean();

private:
  enum { CURVE_LENGTH = 3 };

  struct Head {
    Pool* pool;
    Head* next;
  };

  std::string m_name;
  size_t m_size;
  Head* m_free_list;
  std::atomic<Head*> m_return_list;
  int m_allocated;
  int m_pooled;
  int m_curve[CURVE_LENGTH] = { 0 };
  size_t m_curve_pointer = 0;

  void add_return(Head *h);
  void accept_returns();

  friend class RefCountMT<Pool>;
};

//
// PooledClass
//

class PooledClass {
public:
  PooledClass(const char *c_name, size_t size);
  ~PooledClass();

  auto pool() const -> Pool& { return *m_pool; }

private:
  Pool* m_pool;
};

//
// Pooled
//

class DefaultPooledBase {};

template<class T, class Base = DefaultPooledBase>
class Pooled : public Base {
public:
  using Base::Base;

  void* operator new(size_t) { return pool().alloc(); }
  void operator delete(void *p) { pool().free(p); }

private:
  static auto pool() -> Pool& {
    thread_local static PooledClass s_class(typeid(T).name(), sizeof(T));
    return s_class.pool();
  }
};

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

    //
    // RefCount::WeakPtr::Watcher
    //

    class Watcher {
    public:
      Watcher() : m_ptr(nullptr) {}

      Watcher(WeakPtr *ptr) {
        watch(ptr);
      }

      ~Watcher() {
        if (m_ptr && !m_notified) {
          if (m_next) m_next->m_prev = m_prev;
          if (m_prev) m_prev->m_next = m_next;
          else m_ptr->m_watchers = m_next;
        }
      }

      void watch(WeakPtr *ptr) {
        m_ptr = ptr;
        m_next = ptr->m_watchers;
        if (ptr->m_watchers) {
          ptr->m_watchers->m_prev = this;
        }
        ptr->m_watchers = this;
      }

      auto ptr() const -> WeakPtr* {
        return m_ptr;
      }

    private:
      virtual void on_weak_ptr_gone() = 0;

      WeakPtr* m_ptr;
      Watcher* m_prev = nullptr;
      Watcher* m_next = nullptr;
      bool m_notified = false;

      friend class WeakPtr;
    };

    T* ptr() const {
      return static_cast<T*>(m_ptr);
    }

    T* original_ptr() const {
      return static_cast<T*>(m_original_ptr);
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
      : m_original_ptr(ptr)
      , m_ptr(ptr) {}

    RefCount<T>* m_original_ptr;
    RefCount<T>* m_ptr;
    int m_refs = 1;

    void free() {
      m_ptr = nullptr;
      while (m_watchers) {
        auto p = m_watchers;
        auto n = p->m_next;
        if (n) n->m_prev = nullptr;
        m_watchers = n;
        p->m_notified = true;
        p->on_weak_ptr_gone();
      }
    }

  private:
    Watcher* m_watchers = nullptr;

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

  T* pass() {
    m_refs--;
    return static_cast<T*>(this);
  }

protected:
  ~RefCount() {
    if (m_weak_ptr) {
      m_weak_ptr->free();
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

  auto get() const -> const Ref<class T::WeakPtr>& { return m_weak_ptr; }
  T* ptr() const { return m_weak_ptr ? static_cast<T*>(m_weak_ptr->ptr()) : nullptr; }
  T* original_ptr() const { return m_weak_ptr ? static_cast<T*>(m_weak_ptr->original_ptr()) : nullptr; }
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
    return a.original_ptr() == b.original_ptr();
  }
};

template<class T>
struct less<pjs::WeakRef<T>> {
  bool operator()(const pjs::WeakRef<T> &a, const pjs::WeakRef<T> &b) const {
    return a.original_ptr() < b.original_ptr();
  }
};

template<class T>
struct hash<pjs::WeakRef<T>> {
  size_t operator()(const pjs::WeakRef<T> &k) const {
    hash<void*> h;
    return h(k.original_ptr());
  }
};

} // namespace std

namespace pjs {

//
// PooledArrayBase
//

class PooledArrayBase {
public:

  //
  // PooledArrayBase::Pool
  //

  class Pool {
  public:
    Pool(size_t alloc_size)
      : m_alloc_size(alloc_size) {}

    auto alloc() -> PooledArrayBase* {
      if (auto p = m_free) {
        m_free = p->m_next;
        return p;
      } else {
        p = (PooledArrayBase*)std::malloc(m_alloc_size);
        p->m_pool = this;
        return p;
      }
    }

    void free(PooledArrayBase *p) {
      p->m_next = m_free;
      m_free = p;
    }

  private:
    size_t m_alloc_size;
    PooledArrayBase* m_free = nullptr;
  };

  void recycle() {
    m_pool->free(this);
  }

private:
  Pool* m_pool;
  PooledArrayBase* m_next;
};

//
// PooledArray
//

template<class T>
class PooledArray : public PooledArrayBase {
public:
  static auto make(size_t size) -> PooledArray* {
    auto p = alloc(size);
    new (p) PooledArray(size);
    return p;
  }

  static auto make(size_t size, const T &initial) -> PooledArray* {
    auto p = alloc(size);
    new (p) PooledArray(size, initial);
    return p;
  }

  void free() {
    this->~PooledArray();
    recycle();
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

  PooledArray(size_t size, const T &initial) : m_size(size) {
    for (size_t i = 0; i < size; i++) {
      new (m_elements + i) T(initial);
    }
  }

  ~PooledArray() {
    for (size_t i = 0, n = m_size; i < n; i++) {
      m_elements[i].~T();
    }
  }

  static auto alloc(size_t size) -> PooledArray* {
    auto &pools = m_pools;
    auto slot = slot_of_size(size);
    for (auto i = pools.size(); i <= slot; i++) {
      pools.emplace_back(new Pool(sizeof(PooledArray) + sizeof(T) * size_of_slot(i)));
    }
    return static_cast<PooledArray*>(pools[slot]->alloc());
  }

  static auto slot_of_size(size_t size) -> size_t {
    if (size < 256) return size;
    auto power = sizeof(unsigned int) * 8 - clz(size - 1);
    return (power - 8) + 256;
  }

  static auto size_of_slot(size_t slot) -> size_t {
    if (slot < 256) return slot;
    return 1 << (slot - 256 + 8);
  }

  thread_local static std::vector<std::unique_ptr<PooledArrayBase::Pool>> m_pools;
};

template<class T>
thread_local std::vector<std::unique_ptr<PooledArrayBase::Pool>> PooledArray<T>::m_pools;

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
  thread_local static const Ref<Str> empty;
  thread_local static const Ref<Str> nan;
  thread_local static const Ref<Str> pos_inf;
  thread_local static const Ref<Str> neg_inf;
  thread_local static const Ref<Str> undefined;
  thread_local static const Ref<Str> null;
  thread_local static const Ref<Str> bool_true;
  thread_local static const Ref<Str> bool_false;

  //
  // Str::CharData
  //

  class CharData : public RefCountMT<CharData>, public Pooled<CharData> {
  public:
    auto str() const -> const std::string& { return m_str; }
    auto c_str() const -> const char * { return m_str.c_str(); }
    auto size() const -> size_t { return m_str.length(); }
    auto length() const -> int { return m_length; }

    auto pos_to_chr(int i) const -> int;
    auto chr_to_pos(int i) const -> int;
    auto chr_at(int i) const -> int;

  private:
    enum { CHUNK_SIZE = 32 };

    CharData(std::string &&str);
    ~CharData() {}

    const std::string m_str;
    int m_length;
    std::vector<uint32_t> m_chunks;

    friend class RefCountMT<CharData>;
    friend class Str;
  };

  static auto max_size() -> size_t {
    return s_max_size;
  }

  static auto make(const std::string &str) -> Str* {
    if (str.length() > s_max_size) {
      auto sub = str.substr(0, s_max_size);
      if (auto s = local_map().get(sub)) return s;
      return new Str(std::move(sub));
    } else {
      if (auto s = local_map().get(str)) return s;
      return new Str(str);
    }
  }

  static auto make(std::string &&str) -> Str* {
    if (str.length() > s_max_size) str.resize(s_max_size);
    if (auto s = local_map().get(str)) return s;
    return new Str(std::move(str));
  }

  static auto make(const char *str, size_t len) -> Str* {
    std::string s(str, std::min(s_max_size, len));
    return make(std::move(s));
  }

  static auto make(const char *str) -> Str* {
    return make(str, std::strlen(str));
  }

  static auto make(CharData *data) -> Str* {
    if (auto s = local_map().get(data->str())) return s;
    return new Str(data);
  }

  static auto make(const uint32_t *codes, size_t len) -> Str*;
  static auto make(double n) -> Str*;
  static auto make(int n) -> Str*;
  static auto make(int64_t n) -> Str*;
  static auto make(uint64_t n) -> Str*;

  auto length() const -> int { return m_char_data->length(); }
  auto size() const -> size_t { return m_char_data->size(); }
  auto data() const -> CharData* { return m_char_data; }
  auto str() const -> const std::string& { return m_char_data->str(); }
  auto c_str() const -> const char* { return m_char_data->c_str(); }

  auto pos_to_chr(int i) const -> int { return m_char_data->pos_to_chr(i); }
  auto chr_to_pos(int i) const -> int { return m_char_data->chr_to_pos(i); }
  auto chr_at(int i) const -> int { return m_char_data->chr_at(i); }

  auto parse_int(int base = 10) const -> double;
  bool parse_int64(int64_t &i, int base = 10);
  auto parse_float() const -> double;
  auto substring(int start, int end) -> std::string;

private:

  //
  // Str::LocalMap
  //

  class LocalMap {
  public:
    ~LocalMap() {
      m_destructed = true;
    }

    auto get(const std::string &k) -> Str* {
      if (m_destructed) return nullptr;
      auto i = m_hash.find(k);
      if (i == m_hash.end()) return nullptr;
      return i->second;
    }

    void set(const std::string &k, Str *s) {
      if (m_destructed) return;
      m_hash[k] = s;
    }

    void erase(const std::string &k) {
      if (m_destructed) return;
      m_hash.erase(k);
    }

  private:
    std::unordered_map<std::string, Str*> m_hash;
    bool m_destructed = false;
  };

  Ref<CharData> m_char_data;

#ifdef PIPY_ASSERT_SAME_THREAD
  std::thread::id m_thread_id;
#endif

  Str(CharData *char_data)
    : m_char_data(char_data)
#ifdef PIPY_ASSERT_SAME_THREAD
    , m_thread_id(std::this_thread::get_id())
#endif
  {
    local_map().set(char_data->str(), this);
  }

  Str(const std::string &str) : Str(new CharData(std::string(str))) {}
  Str(std::string &&str) : Str(new CharData(std::move(str))) {}

  ~Str() {
    assert_same_thread(*this);
    local_map().erase(m_char_data->str());
  }

  static size_t s_max_size;

  static auto local_map() -> LocalMap&;

  static void assert_same_thread(const Str &str) {
#ifdef PIPY_ASSERT_SAME_THREAD
    auto current_thread_id = std::this_thread::get_id();
    if (current_thread_id != str.m_thread_id) {
      throw std::runtime_error("cross-thread access");
    }
#endif
  }

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

  auto name() const -> Str* { return m_name; }
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
    : m_name(Str::make(name))
    , m_type(type)
    , m_options(options)
    , m_id(id) {}

  virtual ~Field() {}

  Ref<Str> m_name;
  Type m_type;
  int m_options;
  int m_id;

  friend class RefCount<Field>;
};

//
// ClassMap
//

class ClassMap : public RefCount<ClassMap> {
public:
  static auto get() -> ClassMap* {
    if (!m_singleton) {
      m_singleton = new ClassMap;
    }
    return m_singleton;
  }

  auto get(const std::string &name) -> Class* {
    auto i = m_class_map.find(name);
    return i == m_class_map.end() ? nullptr : i->second;
  }

  auto get(size_t id) -> Class* {
    if (id >= m_class_slots.size()) return nullptr;
    return m_class_slots[id].class_ptr;
  }

  auto all() -> const std::map<std::string, Class*>& { return m_class_map; }

  auto add(Class *c) -> size_t;
  void remove(Class *c);

private:
  struct Slot {
    Class *class_ptr;
    size_t next_slot;
  };

  std::map<std::string, Class*> m_class_map;
  std::vector<Slot> m_class_slots;
  size_t m_class_slot_free = 0;

  thread_local static Ref<ClassMap> m_singleton;
};

//
// Class
//

class Class : public RefCount<Class> {
public:
  static auto make(const std::string &name, Class *super, const std::list<Field*> &fields) -> Class* {
    return new Class(name, super, fields);
  }

  static auto all() -> const std::map<std::string, Class*>& {
    return ClassMap::get()->all();
  }

  static auto get(const std::string &name) -> Class* {
    return ClassMap::get()->get(name);
  }

  static auto get(size_t id) -> Class* {
    return ClassMap::get()->get(id);
  }

  auto name() const -> pjs::Str* { return m_name; }
  auto id() const -> size_t { return m_id; }
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

  void assign(Object *obj, Object *src);

private:
  Class(
    const std::string &name,
    Class *super,
    const std::list<Field*> &fields
  );

  ~Class();

  Class* m_super = nullptr;
  Ref<Str> m_name;
  Ref<ClassMap> m_class_map;
  std::function<Object*(Context&)> m_ctor;
  std::function<void(Object*, int, Value&)> m_geti;
  std::function<void(Object*, int, const Value&)> m_seti;
  std::vector<Ref<Field>> m_fields;
  std::vector<Variable*> m_variables;
  std::vector<int> m_field_index;
  std::unordered_map<Str*, int> m_field_map;
  size_t m_id;
  size_t m_object_count = 0;

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
      m_init_data = new InitData;
      init();
      if (!m_c) {
        auto s = m_init_data->super;
        if (!s) s = class_of<Object>();
        auto c_name = typeid(T).name();
#ifdef _MSC_VER
        auto cxx_name = c_name;
#else
        int status;
        auto cxx_name = abi::__cxa_demangle(c_name, 0, 0, &status);
#endif
        m_c = Class::make(cxx_name ? cxx_name : c_name, s, m_init_data->fields);
        m_c->set_ctor(m_init_data->ctor);
        m_c->set_geti(m_init_data->geti);
        m_c->set_seti(m_init_data->seti);
      }
      delete m_init_data;
      m_init_data = nullptr;
    }
    return m_c;
  }

  static Field* field(const char *name);
  static Method* method(const char *name);

private:
  static void init();

  template<class S>
  static void super() { m_init_data->super = class_of<S>(); }
  static void super(Class *super) { m_init_data->super = super; }
  static void ctor() { m_init_data->ctor = [](Context&) -> Object* { return T::make(); }; }
  static void ctor(std::function<Object*(Context&)> f) { m_init_data->ctor = f; };
  static void geti(std::function<void(Object*, int, Value&)> f) { m_init_data->geti = f; }
  static void seti(std::function<void(Object*, int, const Value&)> f) { m_init_data->seti = f; }

  static void variable(const std::string &name);
  static void variable(const std::string &name, const Value &value, int options = Field::Enumerable | Field::Writable);
  static void variable(const std::string &name, Class *clazz, int options = Field::Enumerable | Field::Writable);

  static void accessor(
    const std::string &name,
    std::function<void(Object*, Value&)> getter,
    std::function<void(Object*, const Value&)> setter = nullptr,
    int options = 0
  );

  static void method(
    const std::string &name,
    std::function<void(Context&, Object*, Value&)> invoke,
    Class *constructor_class = nullptr
  );

  template<class U>
  static void field(const std::string &name, const std::function<U*(T*)> &locate);

  struct InitData {
    Class* super = nullptr;
    std::list<Field*> fields;
    std::function<Object*(Context&)> ctor;
    std::function<void(Object*, int, Value&)> geti;
    std::function<void(Object*, int, const Value&)> seti;
  };

  thread_local static Ref<Class> m_c;
  thread_local static InitData* m_init_data;
};

template<class T>
Class* class_of() { return ClassDef<T>::get(); }

template<class T> thread_local Ref<Class> ClassDef<T>::m_c;
template<class T> thread_local typename ClassDef<T>::InitData* ClassDef<T>::m_init_data = nullptr;

template<class T>
Field* ClassDef<T>::field(const char *name) {
  Ref<Str> s(Str::make(name));
  auto c = get();
  auto i = c->find_field(s);
  return i >= 0 ? c->field(i) : nullptr;
}

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

  static auto all_names() -> std::vector<Str*> {
    std::vector<Str*> names(m_str_to_val.size());
    size_t i = 0;
    for (const auto &p : m_str_to_val) names[i++] = p.first;
    return names;
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

  thread_local static bool m_initialized;
  thread_local static std::vector<Str*> m_val_to_str;
  thread_local static std::map<Str*, T> m_str_to_val;
};

template<class T> thread_local bool EnumDef<T>::m_initialized = false;
template<class T> thread_local std::vector<Str*> EnumDef<T>::m_val_to_str;
template<class T> thread_local std::map<Str*, T> EnumDef<T>::m_str_to_val;

//
// EnumValue
//

template<class T>
class EnumValue {
public:
  EnumValue() {}
  EnumValue(T v): m_value(v) {}

  operator T() const { return m_value; }
  operator int() const { return int(m_value); }
  auto get() const -> T { return m_value; }
  void set(T value) { m_value = value; }

  bool set(Str *name) {
    auto v = EnumDef<T>::value(name);
    if (int(v) < 0) return false;
    m_value = v;
    return true;
  }

  auto name() const -> Str* {
    return EnumDef<T>::name(m_value);
  }

private:
  T m_value;
};

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
    std::function<void(Object*, const Value&)> setter = nullptr,
    int options = 0
  ) : Field(name, Field::Accessor, options)
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
  std::function<void(Object*, const Value&)> setter,
  int options
) {
  m_init_data->fields.push_back(new Accessor(name, getter, setter, options));
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
  Value(Value &&v) : m_v(v.m_v), m_t(v.m_t) { v.m_t = Type::Empty; }
  Value(bool b) : m_t(Type::Boolean) { m_v.b = b; }
  Value(int n) : m_t(Type::Number) { m_v.n = n; }
  Value(unsigned int n) : m_t(Type::Number) { m_v.n = n; }
  Value(int64_t n);
  Value(uint64_t n);
  Value(double n) : m_t(Type::Number) { m_v.n = n; }
  Value(const char *s) : m_t(Type::String) { m_v.s = Str::make(s)->retain(); }
  Value(const char *s, size_t n) : m_t(Type::String) { m_v.s = Str::make(s, n)->retain(); }
  Value(const std::string &s) : m_t(Type::String) { m_v.s = Str::make(s)->retain(); }
  Value(Str *s) : m_t(s ? Type::String : Type::Undefined) { m_v.s = s; if (s) s->retain(); }
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
  template<class T> bool to(T &v) const;
  template<class T> bool to(EnumValue<T> &v) const;
  template<class T> bool to(Ref<T> &v) const;
  template<class T> void from(const T &v);
  template<class T> void from(const EnumValue<T> &v);
  template<class T> void from(const Ref<T> &v);

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
  bool is_error() const { return is_instance_of(class_of<Error>()); }
  bool is_promise() const { return is_instance_of(class_of<Promise>()); }
  bool is_array() const { return is_instance_of(class_of<Array>()); }
  bool is_number_like() const { return is_number() || is<Number>() || is<Int>(); }
  bool is_string_like() const { return is_string() || is<String>(); }

  void set(bool b) { release(); m_t = Type::Boolean; m_v.b = b; }
  void set(int n) { release(); m_t = Type::Number; m_v.n = n; }
  void set(unsigned int n) { release(); m_t = Type::Number; m_v.n = n; }
  void set(int64_t n);
  void set(uint64_t n);
  void set(double n) { release(); m_t = Type::Number; m_v.n = n; }
  void set(const char *s) { release(); m_t = Type::String; m_v.s = Str::make(s)->retain(); }
  void set(const std::string &s) { release(); m_t = Type::String; m_v.s = Str::make(s)->retain(); }
  void set(std::string &&s) { release(); m_t = Type::String; m_v.s = Str::make(std::move(s))->retain(); }
  void set(Str *s) { if (s) s->retain(); release(); m_t = s ? Type::String : Type::Undefined; m_v.s = s; }
  void set(Object *o) { if (o) retain(o); release(); m_t = Type::Object; m_v.o = o; }

  auto to_boolean() const -> bool {
    switch (m_t) {
      case Value::Type::Empty: return false;
      case Value::Type::Undefined: return false;
      case Value::Type::Boolean: return b();
      case Value::Type::Number: return n() != 0 && !std::isnan(n());
      case Value::Type::String: return s()->size() > 0;
      case Value::Type::Object: return o() ? true : false;
    }
    return false;
  }

  auto to_number() const -> double {
    switch (m_t) {
      case Value::Type::Empty: return 0;
      case Value::Type::Undefined: return NAN;
      case Value::Type::Boolean: return b() ? 1 : 0;
      case Value::Type::Number: return n();
      case Value::Type::String: return s()->parse_float();
      case Value::Type::Object: return value_of();
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

  auto to_int32() const -> int {
    switch (m_t) {
      case Value::Type::Empty: return 0;
      case Value::Type::Undefined: return 0;
      case Value::Type::Boolean: return b();
      case Value::Type::Number: return int64_t(n());
      case Value::Type::String: return int64_t(s()->parse_int());
      case Value::Type::Object: return int64_t(value_of());
    }
    return 0;
  }

  auto to_int64() const -> int64_t;
  auto to_int() const -> Int*;

  static bool is_identical(const Value &a, const Value &b);
  static bool is_equal(const Value &a, const Value &b);

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
  static auto to_string(Object *obj) -> Str*;

  auto value_of() const -> double;
  auto box_boolean() const -> Object*;
  auto box_number() const -> Object*;
  auto box_string() const -> Object*;
};

template<> inline bool Value::to<Value>(Value &v) const {
  v = *this;
  return true;
}

template<> inline bool Value::to<bool>(bool &v) const {
  v = to_boolean();
  return true;
}

template<> inline bool Value::to<int>(int &v) const {
  v = int(to_number());
  return true;
}

template<> inline bool Value::to<int64_t>(int64_t &v) const {
  v = to_int64();
  return true;
}

template<> inline bool Value::to<uint64_t>(uint64_t &v) const {
  v = to_int64();
  return true;
}

template<> inline bool Value::to<double>(double &v) const {
  v = to_number();
  return true;
}

template<class T> bool Value::to(EnumValue<T> &v) const {
  return is_string() ? v.set(s()) : false;
}

template<class T> bool Value::to(Ref<T> &v) const {
  if (is_instance_of<T>()) {
    v = as<T>();
    return true;
  } else {
    v = nullptr;
    return false;
  }
}

template<> inline bool Value::to(Ref<Str> &v) const {
  if (is_nullish()) {
    v = nullptr;
  } else {
    (v = to_string())->release();
  }
  return true;
}

template<> inline void Value::from<Value>(const Value &v) {
  *this = v;
}

template<> inline void Value::from<bool>(const bool &v) {
  set(v);
}

template<> inline void Value::from<int>(const int &v) {
  set(v);
}

template<> inline void Value::from<int64_t>(const int64_t &v) {
  set(v);
}

template<> inline void Value::from<uint64_t>(const uint64_t &v) {
  set(v);
}

template<> inline void Value::from<double>(const double &v) {
  set(v);
}

template<class T> void Value::from(const EnumValue<T> &v) {
  set(v.name());
}

template<class T> void Value::from(const Ref<T> &v) {
  set(v.get());
}

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
  static auto make() -> Object* {
    auto obj = new Object();
    class_of<Object>()->init(obj);
    return obj;
  }

  static auto make(Class *c) -> Object* {
    auto obj = new Object();
    c->init(obj);
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
  bool get(Str *key, Value &val);
  void set(Str *key, const Value &val);
  auto ht_size() const -> size_t { return m_hash ? m_hash->size() : 0; }
  bool ht_has(Str *key) { return m_hash ? m_hash->has(key) : false; }
  bool ht_get(Str *key, Value &val);
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

  void iterate_all(const std::function<void(Str*, Value&)> &callback);
  bool iterate_while(const std::function<bool(Str*, Value&)> &callback);
  bool iterate_hash(const std::function<bool(Str*, Value&)> &callback);

  virtual void value_of(Value &out);
  virtual auto to_string() const -> std::string;
  virtual auto dump() -> Object*;

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

  static void assert_same_thread(const Object &obj) {
#ifdef PIPY_ASSERT_SAME_THREAD
    auto current_thread_id = std::this_thread::get_id();
    if (current_thread_id != obj.m_thread_id) {
      throw std::runtime_error("cross-thread access");
    }
#endif
  }

protected:
  Object()
#ifdef PIPY_ASSERT_SAME_THREAD
    : m_thread_id(std::this_thread::get_id())
#endif
  {}

  ~Object() {
    assert_same_thread(*this);
    if (m_class) m_class->free(this);
  }

  virtual void finalize() { delete this; }

private:
  Class* m_class = nullptr;
  Data* m_data = nullptr;
  Ref<OrderedHash<Ref<Str>, Value>> m_hash;

#ifdef PIPY_ASSERT_SAME_THREAD
  std::thread::id m_thread_id;
#endif

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

protected:
  virtual void finalize() override {
    delete static_cast<T*>(this);
  }

  using Pooled<T, Base>::Pooled;
};

template<class T>
T* coerce(Object *obj) {
  if (obj && obj->is_instance_of<T>()) {
    return obj->as<T>();
  } else {
    auto coerced = T::make();
    if (obj) class_of<T>()->assign(coerced, obj);
    return coerced;
  }
}

inline auto Value::retain(Object *obj) -> Object* { obj->retain(); return obj; }
inline void Value::release(Object *obj) { obj->release(); }
inline auto Value::type_of(Object *obj) -> Class* { return obj->type(); }
inline auto Value::to_string(Object *obj) -> Str* { return Str::make(obj->to_string()); }

inline auto Value::value_of() const -> double {
  if (!o()) return 0;
  Value v; o()->value_of(v);
  switch (v.type()) {
    case Value::Type::Empty: return 0;
    case Value::Type::Undefined: return 0;
    case Value::Type::Boolean: return v.b() ? 1 : 0;
    case Value::Type::Number: return v.n();
    case Value::Type::String: return v.s()->parse_float();
    case Value::Type::Object: return v.o() ? std::numeric_limits<double>::quiet_NaN() : 0;
  }
  return 0;
}

//
// SharedValue
//

class SharedValue {
public:
  SharedValue() : m_t(Value::Type::Empty) {}
  SharedValue(const Value &v) { from_value(v); }
  ~SharedValue() { release(); }

  auto operator=(const Value &v) -> SharedValue& { release(); from_value(v); return *this; }
  void to_value(Value &v) const;

private:
  Value::Type m_t;
  union {
    bool b;
    double n;
    Str::CharData* s;
    SharedObject* o;
  } m_v;

  void from_value(const Value &v);
  void release();
};

//
// SharedObject
//

class SharedObject : public RefCountMT<SharedObject>, public Pooled<SharedObject> {
public:
  static auto make(Object *o) -> SharedObject* {
    return o ? new SharedObject(o) : nullptr;
  }

  auto to_object() -> Object*;

private:
  SharedObject(Object *o);
  ~SharedObject();

  struct Entry {
    Ref<Str::CharData> k;
    SharedValue v;
  };

  struct EntryBlock : public Pooled<EntryBlock> {
    EntryBlock* next = nullptr;
    Entry entries[8];
    int length = 0;
  };

  EntryBlock* m_entry_blocks = nullptr;

  friend class RefCountMT<SharedObject>;
};

//
// Variable
//

class Variable : public Field {
public:
  template<typename... Args>
  static auto make(Args&&... args) -> Variable* {
    return new Variable(std::forward<Args>(args)...);
  }

  auto index() const -> size_t { return m_index; }
  auto value() const -> const Value& { return m_value; }

private:
  Variable(const std::string &name, int options = 0, int id = -1) : Field(name, Field::Variable, options, id) {}
  Variable(const std::string &name, const Value &value, int options = 0, int id = -1) : Field(name, Field::Variable, options, id), m_value(value) {}

  size_t m_index = 0;
  Value m_value;

  friend class Class;
};

template<class T>
void ClassDef<T>::variable(const std::string &name) {
  m_init_data->fields.push_back(Variable::make(name, Field::Enumerable | Field::Writable));
}

template<class T>
void ClassDef<T>::variable(const std::string &name, const Value &value, int options) {
  m_init_data->fields.push_back(Variable::make(name, value, options));
}

template<class T>
void ClassDef<T>::variable(const std::string &name, Class *clazz, int options) {
  m_init_data->fields.push_back(Variable::make(name, clazz->construct(), options));
}

inline auto Class::init(Object *obj, Object *prototype) -> Object* {
  auto size = m_variables.size();
  auto data = Data::make(size);
  if (!prototype) {
    for (size_t i = 0; i < size; i++) {
      auto v = m_variables[i];
      data->at(i) = v->value();
    }
  } else if (prototype->type() == this) {
    for (size_t i = 0; i < size; i++) {
      data->at(i) = prototype->data()->at(i);
    }
    obj->m_hash = prototype->m_hash;
  } else {
    for (size_t i = 0; i < size; i++) {
      auto v = m_variables[i];
      data->at(i) = v->value();
    }
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
  m_object_count--;
  release();
}

inline bool Object::has(Str *key) {
  assert_same_thread(*this);
  auto i = m_class->find_field(key);
  if (i >= 0) return true;
  else return ht_has(key);
}

inline bool Object::ht_get(Str *key, Value &val) {
  assert_same_thread(*this);
  if (!m_hash || !m_hash->get(key, val)) {
    val = Value::undefined;
    return false;
  }
  return true;
}

inline void Object::ht_set(Str *key, const Value &val) {
  assert_same_thread(*this);
  if (!m_hash) m_hash = OrderedHash<Ref<Str>, Value>::make();
  m_hash->set(key, val);
}

inline bool Object::ht_delete(Str *key) {
  assert_same_thread(*this);
  if (!m_hash) return false;
  return m_hash->erase(key);
}

inline void Object::iterate_all(const std::function<void(Str*, Value&)> &callback) {
  assert_same_thread(*this);
  for (size_t i = 0, n = m_class->field_count(); i < n; i++) {
    auto f = m_class->field(i);
    if (!f->is_enumerable()) continue;
    if (f->is_accessor()) {
      Value v;
      static_cast<Accessor*>(f)->get(this, v);
      callback(f->name(), v);
    } else if (f->is_variable()) {
      callback(f->name(), m_data->at(static_cast<Variable*>(f)->index()));
    }
  }
  if (m_hash) {
    OrderedHash<Ref<Str>, Value>::Iterator iterator(m_hash);
    while (auto *ent = iterator.next()) {
      callback(ent->k, ent->v);
    }
  }
}

inline bool Object::iterate_while(const std::function<bool(Str*, Value&)> &callback) {
  assert_same_thread(*this);
  for (size_t i = 0, n = m_class->field_count(); i < n; i++) {
    auto f = m_class->field(i);
    if (!f->is_enumerable()) continue;
    if (f->is_accessor()) {
      Value v;
      static_cast<Accessor*>(f)->get(this, v);
      if (!callback(f->name(), v)) return false;
    } else if (f->is_variable()) {
      if (!callback(f->name(), m_data->at(static_cast<Variable*>(f)->index()))) return false;
    }
  }
  return iterate_hash(callback);
}

inline bool Object::iterate_hash(const std::function<bool(Str*, Value&)> &callback) {
  assert_same_thread(*this);
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
// Instance
//

class Instance {
public:
  Instance(Object *global);
  ~Instance();

  auto global() const -> Object* { return m_global; }
  auto module(int i) const -> Module* { return m_modules[i]; }
  auto new_fiber() -> Fiber*;

private:
  void add(Scope *scope);
  void remove(Scope *scope);

  Ref<Object> m_global;
  std::vector<Module*> m_modules;
  Scope* m_scopes = nullptr;

  friend class Module;
  friend class Scope;
};

//
// Fiber
//

class Fiber : public Pooled<Fiber, RefCount<Fiber>> {
public:
  auto clone() -> Fiber*;
  auto data(int i) -> Data*;

private:
  struct ModuleData {
    Data *data = nullptr;
    ModuleData() {}
    ModuleData(const ModuleData &r);
    ModuleData& operator=(ModuleData &&r) {
      if (data) data->free();
      data = r.data;
      r.data = nullptr;
      return *this;
    }
    ~ModuleData() { if (data) data->free(); }
  };

  Fiber(Instance *instance, int size)
    : m_instance(instance)
    , m_data_list(PooledArray<ModuleData>::make(size)) {}

  ~Fiber() { m_data_list->free(); }

  Instance* m_instance;
  PooledArray<ModuleData>* m_data_list;

  friend class RefCount<Fiber>;
  friend class Instance;
};

//
// Scope
//

class Scope : public Pooled<Scope, RefCount<Scope>> {
public:
  struct Variable {
    Ref<Str> name;
    int index = 0;
    bool is_fiber = false;
    bool is_closure = false;
  };

  static auto make(Instance *instance, Scope *parent, size_t size, std::vector<Variable> &variables) -> Scope* {
    return new Scope(instance, parent, size, variables);
  }

  auto parent() const -> Scope* { return m_parent; }
  auto size() const -> size_t { return m_data->size(); }
  auto value(int i) -> Value& { return m_data->at(i); }
  auto values() -> Value* { return m_data->elements(); }
  auto variables() const -> std::vector<Variable>& { return m_variables; }

  void init(int argc, const Value *args) {
    auto data = m_data->elements();
    auto size = m_data->size();
    for (int i = 0; i < argc; i++) data[i] = args[i];
    for (int i = argc; i < size; i++) data[i] = Value::undefined;
  }

  void clear(bool all = false) {
    auto values = m_data->elements();
    for (size_t i = 0, n = m_data->size(); i < n; i++) {
      if (all || !m_variables[i].is_closure) {
        values[i] = Value::undefined;
      }
    }
  }

private:
  Scope(Instance *instance, Scope *parent, size_t size, std::vector<Variable> &variables)
    : m_instance(instance)
    , m_parent(parent)
    , m_data(Data::make(size))
    , m_variables(variables) { if (instance) instance->add(this); }

  ~Scope() {
    m_data->free();
    if (m_instance) m_instance->remove(this);
  }

  Instance* m_instance;
  Scope* m_prev;
  Scope* m_next;
  Ref<Scope> m_parent;
  Data* m_data;
  std::vector<Variable> &m_variables;

  friend class RefCount<Scope>;
  friend class Instance;
};

//
// Source
//

class Source {
public:
  std::string filename;
  std::string content;
};

//
// Context
//

class Context : public RefCount<Context> {
public:
  struct Location {
    Module* module = nullptr;
    const Source* source = nullptr;
    std::string name;
    int line = 0;
    int column = 0;
  };

  struct Error {
    std::string message;
    std::vector<Location> backtrace;
    auto where() const -> const Location*;
  };

  Context(Instance *instance, Ref<Object> *l = nullptr, Fiber *fiber = nullptr)
    : m_instance(instance)
    , m_root(this)
    , m_caller(nullptr)
    , m_g(instance ? instance->global() : nullptr)
    , m_l(l)
    , m_fiber(fiber)
    , m_level(0)
    , m_argc(0)
    , m_argv(nullptr)
    , m_error(std::make_shared<Error>()) {}

  Context(Context &ctx, int argc, Value *argv, Scope *scope)
    : m_instance(ctx.m_instance)
    , m_root(ctx.m_root)
    , m_caller(&ctx)
    , m_g(ctx.m_g)
    , m_l(ctx.m_l)
    , m_scope(scope)
    , m_level(ctx.m_level + 1)
    , m_argc(argc)
    , m_argv(argv)
    , m_error(ctx.m_error) {}

  auto instance() const -> Instance* { return m_instance; }
  auto root() const -> Context* { return m_root; }
  auto caller() const -> Context* { return m_caller; }
  auto g() const -> Object* { return m_g; }
  auto l(int i) const -> Object* { return i >= 0 && m_l ? m_l[i].get() : nullptr; }
  auto fiber() const -> Fiber* { return m_fiber; }
  auto scope() const -> Scope* { return m_scope; }
  void scope(Scope *scope) { m_scope = scope; }
  auto level() const -> int { return m_level; }
  auto argc() const -> int { return m_argc; }
  auto argv() const -> Value* { return m_argv; }
  auto arg(int i) const -> Value& { return m_argv[i]; }
  auto call_site() const -> const Location& { return m_call_site; }

  void reset();
  bool ok() const { return !m_has_error; }
  auto error() const -> Error& { return *m_error; }
  void error(const std::string &msg);
  void error(const std::runtime_error &err);
  void error_argument_count(int n);
  void error_argument_count(int min, int max);
  void error_argument_type(int i, const char *type);
  void error_invalid_enum_value(int i);
  void trace(Module *module, int line, int column);
  void trace(const Source *source, int line, int column);
  void backtrace(const Source *source, int line, int column);
  void backtrace(const std::string &name);

  auto new_scope(int argc, int nvar, std::vector<Scope::Variable> &variables) -> Scope* {
    auto *scope = Scope::make(m_instance, m_scope, nvar, variables);
    scope->init(std::min(m_argc, argc), m_argv);
    m_scope = scope;
    return scope;
  }

  bool is_undefined(int i) const { return i >= argc() || arg(i).is_undefined(); }
  bool is_null(int i) const { return i < argc() && arg(i).is_null(); }
  bool is_nullish(int i) const { return i < argc() && arg(i).is_nullish(); }
  bool is_boolean(int i) const { return i < argc() && arg(i).is_boolean(); }
  bool is_number(int i) const { return i < argc() && arg(i).is_number(); }
  bool is_string(int i) const { return i < argc() && arg(i).is_string(); }
  bool is_object(int i) const { return i < argc() && arg(i).is_object(); }
  bool is_class(int i, Class *c) const { return i < argc() && arg(i).is_class(c); }
  bool is_instance_of(int i, Class *c) const { return i < argc() && arg(i).is_instance_of(c); }
  bool is_function(int i) const { return i < argc() && arg(i).is_function(); }
  bool is_array(int i) const { return i < argc() && arg(i).is_array(); }
  bool is_number_like(int i) const { return i < argc() && arg(i).is_number_like(); }
  bool is_string_like(int i) const { return i < argc() && arg(i).is_string_like(); }

  template<class T> bool is(int i) const { return is_class(i, class_of<T>()); }
  template<class T> bool is_instance_of(int i) const { return is_instance_of(i, class_of<T>()); }

  bool get(int i, bool &v);
  bool get(int i, int &v);
  bool get(int i, int64_t &v);
  bool get(int i, double &v);
  bool get(int i, Str* &v);

  bool get(int i, Value &v) {
    if (i >= argc()) return false;
    v = arg(i);
    return true;
  }

  template<class T>
  bool get(int i, EnumValue<T> &v) {
    if (i >= argc()) return false;
    auto &a = arg(i);
    if (a.is_string()) return v.set(a.s());
    return false;
  }

  template<class T>
  bool get(int i, T* &v) {
    if (i >= argc()) return false;
    auto &a = arg(i);
    if (a.is_null()) { v = nullptr; return true; }
    if (a.is_instance_of<T>()) { v = a.as<T>(); return true; }
    return false;
  }

  bool check(int i, bool &v) {
    auto &a = arg(i);
    if (!a.is_boolean()) {
      error_argument_type(i, "a boolean");
      return false;
    }
    v = a.b();
    return true;
  }

  bool check(int i, bool &v, bool def) {
    auto &a = arg(i);
    if (i >= argc() || a.is_undefined()) {
      v = def;
      return true;
    }
    return check(i, v);
  }

  bool check(int i, int &v) {
    auto &a = arg(i);
    if (!a.is_number()) {
      error_argument_type(i, "a number");
      return false;
    }
    v = a.n();
    return true;
  }

  bool check(int i, int &v, int def) {
    auto &a = arg(i);
    if (i >= argc() || a.is_undefined()) {
      v = def;
      return true;
    }
    return check(i, v);
  }

  bool check(int i, double &v) {
    auto &a = arg(i);
    if (!a.is_number()) {
      error_argument_type(i, "a number");
      return false;
    }
    v = a.n();
    return true;
  }

  bool check(int i, double &v, double def) {
    auto &a = arg(i);
    if (i >= argc() || a.is_undefined()) {
      v = def;
      return true;
    }
    return check(i, v);
  }

  bool check(int i, Str* &v) {
    auto &a = arg(i);
    if (!a.is_string()) {
      error_argument_type(i, "a string");
      return false;
    }
    v = a.s();
    return true;
  }

  bool check(int i, Str* &v, Str* def) {
    auto &a = arg(i);
    if (i >= argc() || a.is_undefined()) {
      v = def;
      return true;
    }
    return check(i, v);
  }

  template<class T>
  bool check(int i, EnumValue<T> &v) {
    auto &a = arg(i);
    if (!a.is_string()) {
      error_argument_type(i, "a string");
      return false;
    }
    if (!v.set(a.s())) {
      error_invalid_enum_value(i);
      return false;
    }
    return true;
  }

  template<class T>
  bool check(int i, EnumValue<T> &v, T def) {
    auto &a = arg(i);
    if (i >= argc() || a.is_undefined()) {
      v = def;
      return true;
    }
    return check(i, v);
  }

  template<class T>
  bool check(int i, T* &v) {
    auto &a = arg(i);
    if (a.is_null() || !a.is_object() || !a.is_instance_of<T>()) {
      std::string type("an instance of ");
      type += class_of<T>()->name()->str();
      error_argument_type(i, type.c_str());
      return false;
    }
    v = a.as<T>();
    return true;
  }

  template<class T>
  bool check(int i, T* &v, T *def) {
    auto &a = arg(i);
    if (i >= argc() || a.is_nullish()) {
      v = def;
      return true;
    }
    return check(i, v);
  }

  template<typename... Args>
  bool arguments(int n, Args... argv) {
    return get_args(true, n, 0, argv...);
  }

  template<typename... Args>
  bool try_arguments(int n, Args... argv) {
    return get_args(false, n, 0, argv...);
  }

protected:
  virtual void finalize() {
    delete this;
  }

private:
  Instance* m_instance;
  Context* m_root;
  Context* m_caller;
  Context* m_prev;
  Context* m_next;
  Ref<Object> m_g, *m_l;
  Ref<Fiber> m_fiber;
  Ref<Scope> m_scope;
  int m_level;
  int m_argc;
  Value* m_argv;
  Location m_call_site;
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
    auto &a = arg(i);
    if (a.is_boolean()) {
      *b = a.b();
      return true;
    } else {
      if (set_error) error_argument_type(i, "a boolean");
      return false;
    }
  }

  bool get_arg(bool set_error, int i, int *n) {
    auto &a = arg(i);
    if (a.is_number()) {
      *n = int(a.n());
      return true;
    } else if (a.is<Number>() || a.is<Int>()) {
      *n = a.to_int32();
      return true;
    } else {
      if (set_error) error_argument_type(i, "a number");
      return false;
    }
  }

  bool get_arg(bool set_error, int i, int64_t *n) {
    auto &a = arg(i);
    if (a.is_number()) {
      *n = int64_t(a.n());
      return true;
    } else if (a.is<Number>() || a.is<Int>()) {
      *n = a.to_int64();
      return true;
    } else {
      if (set_error) error_argument_type(i, "a number");
      return false;
    }
  }

  bool get_arg(bool set_error, int i, double *n) {
    auto &a = arg(i);
    if (a.is_number()) {
      *n = a.n();
      return true;
    } else if (a.is<Number>() || a.is<Int>()) {
      *n = a.to_number();
      return true;
    } else {
      if (set_error) error_argument_type(i, "a number");
      return false;
    }
  }

  bool get_arg(bool set_error, int i, Str **s) {
    auto &a = arg(i);
    if (a.is_string()) {
      *s = a.s();
      return true;
    } else {
      if (set_error) error_argument_type(i, "a string");
      return false;
    }
  }

  bool get_arg(bool set_error, int i, std::string *s) {
    auto &a = arg(i);
    if (a.is_string()) {
      *s = a.s()->str();
      return true;
    } else {
      if (set_error) error_argument_type(i, "a string");
      return false;
    }
  }

  bool get_arg(bool set_error, int i, Object **o) {
    auto &a = arg(i);
    if (a.is_object()) {
      *o = a.o();
      return true;
    } else {
      if (set_error) error_argument_type(i, "an object");
      return false;
    }
  }

  bool get_arg(bool set_error, int i, Function **f) {
    auto &a = arg(i);
    if (a.is_null()) {
      *f = nullptr;
      return true;
    } else if (a.is_function()) {
      *f = a.as<Function>();
      return true;
    } else {
      if (set_error) error_argument_type(i, "a function");
      return false;
    }
  }

  bool get_arg(bool set_error, int i, Array **o) {
    auto &a = arg(i);
    if (a.is_null()) {
      *o = nullptr;
      return true;
    } else if (a.is_array()) {
      *o = a.as<Array>();
      return true;
    } else {
      if (set_error) error_argument_type(i, "an array");
      return false;
    }
  }

  template<typename T>
  bool get_arg(bool set_error, int i, EnumValue<T> *e) {
    auto &a = arg(i);
    if (a.is_string()) {
      if (e->set(a.s())) {
        return true;
      } else {
        if (set_error) error_invalid_enum_value(i);
        return false;
      }
    } else {
      if (set_error) error_argument_type(i, "a string");
      return false;
    }
  }

  template<class T>
  bool get_arg(bool set_error, int i, T **o) {
    auto &a = arg(i);
    if (a.is_null()) {
      *o = nullptr;
      return true;
    } else if (a.is_instance_of<T>()) {
      *o = a.as<T>();
      return true;
    } else {
      if (set_error) {
        std::string type("an instance of ");
        type += class_of<T>()->name()->str();
        error_argument_type(i, type.c_str());
      }
      return false;
    }
  }

  friend class RefCount<Context>;
  friend class Instance;
};

template<class T, class Base = Context>
class ContextTemplate : public Pooled<T, Base> {
public:
  template<typename... Args>
  static auto make(Args&&... args) -> T* {
    return new T(std::forward<Args>(args)...);
  }

protected:
  virtual void finalize() override {
    delete static_cast<T*>(this);
  }

  using Pooled<T, Base>::Pooled;
};

inline auto Class::construct() -> Object* {
  Context ctx(nullptr);
  return construct(ctx);
}

inline void Instance::add(Scope *s) {
  s->m_prev = nullptr;
  s->m_next = m_scopes;
  if (m_scopes) m_scopes->m_prev = s;
  m_scopes = s;
}

inline void Instance::remove(Scope *s) {
  if (auto p = s->m_prev) p->m_next = s->m_next; else m_scopes = s->m_next;
  if (auto n = s->m_next) n->m_prev = s->m_prev;
  s->m_instance = nullptr;
}

//
// Method
//

class Method : public Field {
public:
  static auto make(
    const std::string &name,
    std::function<void(Context&, Object*, Value&)> invoke,
    Class *constructor_class = nullptr
  ) -> Method* {
    return new Method(name, invoke, constructor_class);
  }

  auto constructor_class() const -> Class* { return m_constructor_class; }

  void invoke(Context &ctx, Scope *scope, Object *thiz, int argc, Value argv[], Value &retv) {
    Context fctx(ctx, argc, argv, scope);
    retv = Value::undefined;
    if (fctx.level() > 100) {
      fctx.error("call stack overflow");
      fctx.backtrace(name()->str());
    } else {
      m_invoke(fctx, thiz, retv);
      if (!fctx.ok()) fctx.backtrace(name()->str());
    }
  }

  auto construct(Context &ctx, int argc, Value argv[]) -> Object* {
    if (!m_constructor_class) {
      ctx.error("function is not a constructor");
      return nullptr;
    }
    Context fctx(ctx, argc, argv, nullptr); // No need for a scope since JS ctors are not supported yet
    auto *obj = m_constructor_class->construct(fctx);
    if (!fctx.ok()) fctx.backtrace(name()->str());
    return obj;
  }

private:
  Method(
    const std::string &name,
    std::function<void(Context&, Object*, Value&)> invoke,
    Class *constructor_class = nullptr
  ) : Field(name, Field::Method)
    , m_invoke(invoke)
    , m_constructor_class(constructor_class) {}

  std::function<void(Context&, Object*, Value&)> m_invoke;
  Class* m_constructor_class;
};

template<class T>
Method* ClassDef<T>::method(const char *name) {
  Ref<Str> s(Str::make(name));
  auto c = get();
  auto i = c->find_field(s);
  auto f = i >= 0 ? c->field(i) : nullptr;
  return f && f->is_method() ? static_cast<Method*>(f) : nullptr;
}

template<class T>
void ClassDef<T>::method(
  const std::string &name,
  std::function<void(Context&, Object*, Value&)> invoke,
  Class *constructor_class
) {
  m_init_data->fields.push_back(Method::make(name, invoke, constructor_class));
}

template<class T>
template<class U>
void ClassDef<T>::field(const std::string &name, const std::function<U*(T*)> &locate) {
  accessor(
    name,
    [=](Object *obj, Value &ret) { ret.from(*locate(obj->as<T>())); },
    [=](Object *obj, const Value &ret) { ret.to(*locate(obj->as<T>())); },
    Field::Enumerable
  );
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
      c->name()->str(),
      [this](Context &ctx, Object *obj, Value &ret) {
        (*static_cast<T*>(this))(ctx, obj, ret);
      }
    );
  }

  void operator()(Context &ctx, Object *obj, Value &ret) {}
};

//
// Class::get()/set()
//

inline void Class::get(Object *obj, int id, Value &val) {
  Object::assert_same_thread(*obj);
  auto f = field(m_field_index[id]);
  if (f->is_variable()) {
    val = obj->data()->at(static_cast<Variable*>(f)->index());
  } else if (f->is_accessor()) {
    static_cast<Accessor*>(f)->get(obj, val);
  } else if (f->is_method()) {
    val.set(Function::make(static_cast<Method*>(f), obj));
  } else {
    val = Value::undefined;
  }
}

inline void Class::set(Object *obj, int id, const Value &val) {
  Object::assert_same_thread(*obj);
  auto f = field(m_field_index[id]);
  if (f->is_variable()) {
    obj->m_data->at(static_cast<Variable*>(f)->index()) = val;
  } else if (f->is_accessor()) {
    static_cast<Accessor*>(f)->set(obj, val);
  }
}

//
// Object::get()/set()
//

inline bool Object::get(Str *key, Value &val) {
  assert_same_thread(*this);
  auto i = m_class->find_field(key);
  if (i < 0) return ht_get(key, val);
  auto f = m_class->field(i);
  if (f->is_variable()) {
    val = m_data->at(static_cast<Variable*>(f)->index());
  } else if (f->is_accessor()) {
    static_cast<Accessor*>(f)->get(this, val);
  } else if (f->is_method()) {
    val.set(Function::make(static_cast<Method*>(f), this));
  } else {
    return false;
  }
  return true;
}

inline void Object::set(Str *key, const Value &val) {
  assert_same_thread(*this);
  auto i = m_class->find_field(key);
  if (i < 0) { ht_set(key, val); return; }
  auto f = m_class->field(i);
  if (f->is_variable()) {
    m_data->at(static_cast<Variable*>(f)->index()) = val;
  } else if (f->is_accessor()) {
    static_cast<Accessor*>(f)->set(this, val);
  }
}

//
// Promise
//

class Promise : public ObjectTemplate<Promise> {
public:
  enum State {
    PENDING,
    RESOLVED,
    REJECTED,
    CANCELED,
  };

  //
  // Promise::Period
  //

  class Period : public pjs::RefCount<Period> {
  public:
    static auto current() -> Period*;
    static auto make() -> Period*;

    void set_current();
    void run(int max_iterations = 1);
    void resume(int max_iterations = 1);
    void pause();
    void end();

  private:
    Period() {}

    Promise* m_settled_queue_head = nullptr;
    Promise* m_settled_queue_tail = nullptr;
    bool m_paused = false;
    bool m_ended = false;

    bool run_queue();

    thread_local static pjs::Ref<Period> s_current;

    friend class Promise;
  };

  //
  // Promise::Callback
  //

  class Callback : public ObjectTemplate<Callback> {
  public:
    auto resolved() -> Function*;
    auto rejected() -> Function*;
    virtual void on_resolved(const Value &value) { if (m_cb) m_cb(RESOLVED, value); }
    virtual void on_rejected(const Value &error) { if (m_cb) m_cb(REJECTED, error); }
  protected:
    Callback(const std::function<void(State, const Value &)> &cb = nullptr) : m_cb(cb) {}
  private:
    std::function<void(State, const Value &)> m_cb;
    friend class ObjectTemplate<Callback>;
  };

  //
  // Promise::Settler
  //

  class Settler : public ObjectTemplate<Settler> {
  public:
    void resolve(const Value &value) { m_promise->settle(RESOLVED, value); }
    void reject(const Value &error) { m_promise->settle(REJECTED, error); }
  private:
    Settler(Promise *promise) : m_promise(promise) {}
    ~Settler() { m_promise->cancel(); }
    Ref<Promise> m_promise;
    friend class ObjectTemplate<Settler>;
  };

  //
  // Promise::Result
  //

  struct Result : public ObjectTemplate<Result> {
    Value status;
    Value value;
    Value reason;
  };

  static auto resolve(const Value &value) -> Promise*;
  static auto reject(const Value &error) -> Promise*;
  static auto all(Array *promises) -> Promise*;
  static auto all_settled(Array *promises) -> Promise*;
  static auto any(Array *promises) -> Promise*;
  static auto race(Array *promises) -> Promise*;

  auto then(
    Context *context,
    const Value &on_resolved,
    const Value &on_rejected
  ) -> Promise*;

  auto then(
    Context *context,
    Function *on_resolved,
    Function *on_rejected = nullptr,
    Function *on_finally = nullptr
  ) -> Promise*;

private:

  //
  // Promise::Then
  //

  class Then : public Pooled<Then> {
    Then(
      Context *context,
      Function *on_resolved,
      Function *on_rejected,
      Function *on_finally
    );

    Then(
      Context *context,
      const Value &resolved_value,
      const Value &rejected_value
    );

    void execute(State state, const Value &result);
    void execute(Context *ctx, State state, const Value &result);

    Then* m_next = nullptr;
    Ref<Context> m_context;
    Ref<Function> m_on_resolved;
    Ref<Function> m_on_rejected;
    Ref<Function> m_on_finally;
    Ref<Promise> m_promise;
    Value m_resolved_value;
    Value m_rejected_value;

    friend class Promise;
  };

  Promise() {}
  ~Promise() { clear_thens(); }

  void add_then(Then *then);
  void clear_thens();
  void settle(State state, const Value &result);
  void cancel();
  void enqueue();
  void dequeue(bool run);

  State m_state = PENDING;
  Value m_result;
  Then* m_thens_head = nullptr;
  Then* m_thens_tail = nullptr;
  Promise* m_next = nullptr;
  Ref<Promise> m_dependent;
  bool m_queued = false;

  thread_local static Promise *s_settled_queue_head;
  thread_local static Promise *s_settled_queue_tail;

  friend class ObjectTemplate<Promise>;
  friend class PromiseDependency;
};

//
// PromiseAggregator
//

class PromiseAggregator :
  public Pooled<PromiseAggregator>,
  public RefCount<PromiseAggregator>
{
public:
  enum Type {
    ALL,
    ALL_SETTLED,
    ANY,
    RACE,
  };

  PromiseAggregator(Type type, Promise::Settler *handler, Array *promises);
  ~PromiseAggregator();

private:
  Type m_type;
  Ref<Promise::Settler> m_settler;
  PooledArray<Ref<PromiseDependency>>* m_dependencies = nullptr;
  int m_counter = 0;

  void settle(PromiseDependency *dep);

  friend class PromiseDependency;
};

//
// PromiseDependency
//

class PromiseDependency :
  public ObjectTemplate<PromiseDependency, Promise::Callback>,
  public Promise::WeakPtr::Watcher
{
public:
  void init();
  auto state() const -> Promise::State { return m_state; }
  auto result() const -> const Value& { return m_result; }

private:
  PromiseDependency(PromiseAggregator *aggregator, Promise *promise)
    : m_aggregator(aggregator)
    , m_promise(promise) {}

  Ref<PromiseAggregator> m_aggregator;
  WeakRef<Promise> m_promise;
  Promise::State m_state = Promise::PENDING;
  Value m_result;

  virtual void on_resolved(const Value &value) override;
  virtual void on_rejected(const Value &error) override;
  virtual void on_weak_ptr_gone() override;

  friend class ObjectTemplate<PromiseDependency, Promise::Callback>;
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
      c->name()->str(),
      [this](Context &ctx, Object *obj, Value &ret) {
        try {
          (*static_cast<Constructor<T>*>(this))(ctx, obj, ret);
        } catch (std::runtime_error &err) {
          ctx.error(err);
        }
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
      val = obj->data()->at(static_cast<Variable*>(f)->index());
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
      if (f->is_variable() && f->is_writable()) {
        obj->data()->at(static_cast<Variable*>(f)->index()) = val;
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

inline bool Context::get(int i, bool &v) {
  if (i >= argc()) return false;
  auto &a = arg(i);
  if (a.is_boolean()) { v = a.b(); return true; }
  if (a.is<Boolean>()) { v = a.as<Boolean>()->value(); return true; }
  return false;
}

//
// Int
//

class Int : public ObjectTemplate<Int> {
public:
  enum class Type {
    i8, i16, i32, i64,
    u8, u16, u32, u64,
  };

  static auto promote(Type t, Type u) -> Type;
  static auto convert(Type t, int64_t i) -> int64_t;
  static auto convert(Type t, double n) -> int64_t;
  static auto convert(Type t, const std::string &s) -> int64_t;

  auto to_number() const -> double;
  auto to_string(char *str, size_t len) const -> size_t;

  auto toBytes() const -> Array*;

  auto type() const -> Type { return m_t; }
  auto width() const -> int { return (1 << (int(m_t) & 3)) << 3; }
  auto value() const -> int64_t { return m_i; }
  auto low() const -> double { return uint32_t(m_i); }
  auto high() const -> double { return isUnsigned() ? double(uint64_t(m_i) >> 32) : double(m_i >> 32); }
  bool isUnsigned() const { return int(m_t) >= int(Type::u8); }

  bool is_zero() const { return m_i == 0; }
  bool eql(const Int *i) const;
  auto cmp(const Int *i) const -> int;
  auto neg() const -> Int*;
  auto inc() const -> Int*;
  auto dec() const -> Int*;
  auto add(const Int *i) const -> Int*;
  auto sub(const Int *i) const -> Int*;
  auto mul(const Int *i) const -> Int*;
  auto div(const Int *i) const -> Int*;
  auto mod(const Int *i) const -> Int*;
  auto shl(int n) const -> Int*;
  auto shr(int n) const -> Int*;
  auto bitwise_shr(int n) const -> Int*;
  auto bitwise_not() const -> Int*;
  auto bitwise_and(const Int *i) const -> Int*;
  auto bitwise_or (const Int *i) const -> Int*;
  auto bitwise_xor(const Int *i) const -> Int*;

  virtual void value_of(Value &out) override;
  virtual auto to_string() const -> std::string override;

private:
  Int(int i) : m_t(Type::i32), m_i(i) {}
  Int(int l, int h) : m_t(Type::i64), m_i(uint32_t(l) + (int64_t(h) << 32)) {}
  Int(int64_t i) : m_t(Type::i64), m_i(i) {}
  Int(double n) : m_t(Type::i64), m_i(n) {}
  Int(Int *i) : m_t(i->m_t), m_i(i->m_i) {}
  Int(Str *s) : m_t(Type::i64), m_i(convert(Type::i64, s->str())) {}
  Int(Array *bytes);
  Int(Type t) : m_t(t), m_i(0) {}
  Int(Type t, int64_t i) : m_t(t), m_i(convert(t, i)) {}
  Int(Type t, int l, int h) : m_t(t), m_i(convert(t, uint32_t(l) + (int64_t(h) << 32))) {}
  Int(Type t, double n) : m_t(t), m_i(convert(t, n)) {}
  Int(Type t, Int *i) : m_t(t), m_i(convert(t, i->m_i)) {}
  Int(Type t, Str *s) : m_t(t), m_i(convert(t, s->str())) {}
  Int(Type t, Array *bytes) : m_t(t), m_i(0) { fill(bytes); }

  Type m_t;
  int64_t m_i;

  void fill(Array *bytes);

  friend class ObjectTemplate<Int>;
};

inline Value::Value(int64_t n)
  : Value(Int::make(Int::Type::i64, n)) {}

inline Value::Value(uint64_t n)
  : Value(Int::make(Int::Type::u64, int64_t(n))) {}

inline void Value::set(int64_t n) {
  set(Int::make(Int::Type::i64, n));
}

inline void Value::set(uint64_t n) {
  set(Int::make(Int::Type::u64, int64_t(n)));
}

inline auto Value::to_int64() const -> int64_t {
  return is<Int>() ? as<Int>()->value() : to_number();
}

inline auto Value::to_int() const -> Int* {
  return (is<Int>() ? as<Int>() : Int::make(to_number()))->retain()->as<Int>();
}

//
// Number
//

class Number : public ObjectTemplate<Number> {
public:
  auto value() const -> double { return m_n; }

  static bool is_nan(double n);
  static bool is_finite(double n);
  static bool is_integer(double n);

  static size_t to_string(char *str, size_t len, double n, int radix = 10);
  static size_t to_precision(char *str, size_t len, double n, int precision);
  static size_t to_fixed(char *str, size_t len, double n, int digits);
  static size_t to_exponential(char *str, size_t len, double n);
  static size_t to_exponential(char *str, size_t len, double n, int digits);

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

inline bool Context::get(int i, int &v) {
  if (i >= argc()) return false;
  auto &a = arg(i);
  if (a.is_number()) { v = a.n(); return true; }
  if (a.is<Number>()) { v = a.as<Number>()->value(); return true; }
  if (a.is<Int>()) { v = a.as<Int>()->value(); return true; }
  return false;
}

inline bool Context::get(int i, int64_t &v) {
  if (i >= argc()) return false;
  auto &a = arg(i);
  if (a.is_number()) { v = a.n(); return true; }
  if (a.is<Number>()) { v = a.as<Number>()->value(); return true; }
  if (a.is<Int>()) { v = a.as<Int>()->value(); return true; }
  return false;
}

inline bool Context::get(int i, double &v) {
  if (i >= argc()) return false;
  auto &a = arg(i);
  if (a.is_number()) { v = a.n(); return true; }
  if (a.is<Number>()) { v = a.as<Number>()->value(); return true; }
  if (a.is<Int>()) { v = a.as<Int>()->value(); return true; }
  return false;
}

//
// String
//

class String : public ObjectTemplate<String> {
public:
  auto value() const -> Str* { return m_s; }

  virtual void value_of(Value &out) override;
  virtual auto to_string() const -> std::string override;

  auto str() const -> Str* { return m_s; }
  auto length() -> int { return m_s->length(); }

  auto charAt(int i) -> Str*;
  auto charCodeAt(int i) -> int;
  bool endsWith(Str *search);
  bool endsWith(Str *search, int length);
  bool includes(Str *search, int position = 0);
  auto indexOf(Str *search, int position = 0) -> int;
  auto lastIndexOf(Str *search, int position) -> int;
  auto lastIndexOf(Str *search) -> int;
  auto padEnd(int length, Str *padding) -> Str*;
  auto padStart(int length, Str *padding) -> Str*;
  auto repeat(int count) -> Str*;
  auto replace(Str *pattern, Str *replacement, bool all = false) -> Str*;
  auto replace(RegExp *pattern, Str *replacement) -> Str*;
  auto search(RegExp *pattern) -> int;
  auto slice(int start) -> Str*;
  auto slice(int start, int end) -> Str*;
  auto split(Str *separator = nullptr) -> Array*;
  auto split(Str *separator, int limit) -> Array*;
  bool startsWith(Str *search, int position = 0);
  auto substring(int start) -> Str*;
  auto substring(int start, int end) -> Str*;
  auto toLowerCase() -> Str*;
  auto toUpperCase() -> Str*;
  auto trim() -> Str*;
  auto trimEnd() -> Str*;
  auto trimStart() -> Str*;

private:
  String(Str *s) : m_s(s) {}
  String(const std::string &str) : m_s(Str::make(str)) {}

  Ref<Str> m_s;

  static auto fill(char *buf, size_t size, Str *str, int len) -> size_t;

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

inline bool Context::get(int i, Str* &v) {
  if (i >= argc()) return false;
  auto &a = arg(i);
  if (a.is_string()) { v = a.s(); return true; }
  if (a.is<String>()) { v = a.as<String>()->value(); return true; }
  return false;
}

//
// Error
//

class Error : public ObjectTemplate<Error> {
public:
  auto name() const -> Str*;
  auto message() const -> Str* { return m_message; }
  auto cause() const -> Error* { return m_cause; }
  auto stack() const -> Str* { return m_stack; }

private:
  Error(Str *message = nullptr, Object *cause = nullptr)
    : m_message(message ? message : Str::empty.get())
    , m_cause(cause) {}

  Error(const Context::Error &error);

  Ref<Str> m_message;
  Ref<Error> m_cause;
  Ref<Str> m_stack;

  friend class ObjectTemplate<Error>;
};

//
// Array
//

class Array : public ObjectTemplate<Array> {
public:
  static const size_t MAX_SIZE = 0x100000;

  auto elements() const -> Data* { return m_data; }

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
      const auto &e = m_data->at(i);
      v = (e.is_empty() ? Value::undefined : e);
    } else {
      v = Value::undefined;
    }
  }

  void set(int i, const Value &v) {
    if (i < 0) return;
    auto old_size = m_data->size();
    auto new_size = 1 << power(i + 1);
    if (new_size > MAX_SIZE) return; // TODO: report error
    if (new_size > old_size) {
      auto *data = Data::make(new_size, Value::empty);
      auto *new_values = data->elements();
      auto *old_values = m_data->elements();
      for (int i = 0; i < old_size; i++) new_values[i] = std::move(old_values[i]);
      m_data->recycle();
      m_data = data;
    }
    m_data->at(i) = v;
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

  int iterate_backward_while(std::function<bool(Value&, int)> callback) {
    auto values = m_data->elements();
    for (int i = std::min((int)m_data->size(), m_size) - 1; i >= 0; i--) {
      auto &v = values[i];
      if (!v.is_empty() && !callback(v, i)) return i;
    }
    return -1;
  }

  void iterate_backward_all(std::function<void(Value&, int)> callback) {
    iterate_backward_while([&](Value &v, int i) -> bool {
      callback(v, i);
      return true;
    });
  }

  void copyWithin(int target, int start, int end);
  void fill(const Value &v, int start);
  void fill(const Value &v, int start, int end);
  auto filter(std::function<bool(Value&, int)> callback) -> Array*;
  void find(std::function<bool(Value&, int)> callback, Value &result);
  auto findIndex(std::function<bool(Value&, int)> callback) -> int;
  auto flat(int depth = 1) -> Array*;
  auto flatMap(std::function<bool(Value&, int, Value&)> callback) -> Array*;
  void forEach(std::function<bool(Value&, int)> callback);
  auto indexOf(const Value &value, int start = 0) -> int;
  auto join(Str *separator = nullptr) -> Str*;
  auto lastIndexOf(const Value &value, int start = -1) -> int;
  auto map(std::function<bool(Value&, int, Value&)> callback) -> Array*;
  void pop(Value &result);
  void push(const Value &v) { set(m_size, v); }
  void reduce(std::function<bool(Value&, Value&, int)> callback, Value &result);
  void reduce(std::function<bool(Value&, Value&, int)> callback, Value &initial, Value &result);
  void reduceRight(std::function<bool(Value&, Value&, int)> callback, Value &result);
  void reduceRight(std::function<bool(Value&, Value&, int)> callback, Value &initial, Value &result);
  auto reverse() -> Array*;
  void shift(Value &result);
  auto slice(int start, int end) -> Array*;
  void sort();
  void sort(const std::function<bool(const Value&, const Value&)> &comparator);
  auto splice(int start, int delete_count, const Value *values, int count) -> Array*;
  void unshift(const Value *values, int count);

private:
  Array(size_t size = 0)
    : m_data(Data::make(std::max(1, 1 << power(size)), Value::empty))
    , m_size(size) {}

  Array(int argc, const Value argv[]);

  ~Array() {
    length(0);
    m_data->recycle();
  }

  Data* m_data;
  int m_size;

  static auto power(size_t size) -> size_t {
    return size <= 1 ? 1 : sizeof(unsigned int) * 8 - clz(size - 1);
  }

  friend class ObjectTemplate<Array>;
};

template<>
class Constructor<Array> : public ConstructorTemplate<Array> {
public:
  void operator()(Context &ctx, Object *obj, Value &ret) {
    ret.set(Array::make(ctx.argc(), ctx.argv()));
  }
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

//
// Utf8Decoder
//

class Utf8Decoder {
public:
  static size_t max_output_size(size_t input_size) {
    return input_size * 2;
  }

  static size_t encode(uint32_t code, char *output, size_t size);

  Utf8Decoder(const std::function<void(int)> &output)
    : m_output(output) {}

  void reset();
  bool input(char c);
  bool end();

private:
  const std::function<void(int)> m_output;
  uint32_t m_codepoint = 0;
  int m_shift = 0;
};

} // namespace pjs

#endif // PJS_TYPES_HPP
