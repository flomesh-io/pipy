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

#include "types.hpp"

#include <cstdio>
#include <cstring>

namespace pjs {

//
// Pool
//

auto Pool::all() -> std::map<std::string, Pool*> & {
  thread_local static std::map<std::string, Pool*> a;
  return a;
}

Pool::Pool(const std::string &name, size_t size)
  : m_name(name)
  , m_size(std::max(size, sizeof(void*)))
  , m_free_list(nullptr)
  , m_return_list(nullptr)
  , m_allocated(0)
  , m_pooled(0)
{
  retain();
  if (!name.empty()) {
    all()[name] = this;
  }
}

Pool::~Pool() {
  for (auto *p = m_free_list; p; ) {
    auto h = p; p = p->next;
    std::free(h);
  }
  for (auto *p = m_return_list.load(); p; ) {
    auto h = p; p = p->next;
    std::free(h);
  }
}

auto Pool::alloc() -> void* {
  accept_returns();
  m_allocated++;
  if (auto *h = m_free_list) {
    m_free_list = h->next;
    m_pooled--;
    retain();
    return (char*)h + sizeof(Head);
  } else {
    h = (Head*)std::malloc(sizeof(Head) + m_size);
    h->pool = this;
    h->next = nullptr;
    retain();
    return (char*)h + sizeof(Head);
  }
}

void Pool::free(void *p) {
#ifdef PIPY_SOIL_FREED_SPACE
  std::memset(p, 0xfe, m_size);
#endif
  auto *h = (Head*)((char*)p - sizeof(Head));
  if (h->pool == this) {
    h->next = m_free_list;
    m_free_list = h;
    m_allocated--;
    m_pooled++;
    release();
  } else {
    h->pool->add_return(h);
  }
}

void Pool::add_return(Head *h) {
  auto *p = m_return_list.load(std::memory_order_relaxed);
  do {
    h->next = p;
  } while (!m_return_list.compare_exchange_weak(
    p, h,
    std::memory_order_release,
    std::memory_order_relaxed
  ));
  release();
}

void Pool::accept_returns() {
  if (auto *h = m_return_list.load(std::memory_order_relaxed)) {
    while (!m_return_list.compare_exchange_weak(
      h, nullptr,
      std::memory_order_acquire,
      std::memory_order_relaxed
    )) {}
    int n = 1;
    auto *p = h;
    while (p->next) {p = p->next; n++; }
    p->next = m_free_list;
    m_free_list = h;
    m_allocated -= n;
    m_pooled += n;
  }
}

void Pool::clean() {
  int max = 0;
  for (int i = 0; i < CURVE_LENGTH; i++) {
    if (m_curve[i] > max) max = m_curve[i];
  }
  int room = max + (max >> 2) - m_allocated;
  if (room >= 0) {
    while (m_pooled > room) {
      auto *h = m_free_list;
      m_free_list = h->next;
      std::free(h);
      m_pooled--;
    }
  }
  m_curve[m_curve_pointer++ % CURVE_LENGTH] = m_allocated;
}

//
// PooledClass
//

PooledClass::PooledClass(const char *c_name, size_t size) {
#if defined(_MSC_VER)
  auto cxx_name = c_name;
#else
  int status;
  auto cxx_name = c_name ? abi::__cxa_demangle(c_name, 0, 0, &status) : nullptr;
#endif
  m_pool = new Pool(cxx_name ? cxx_name : (c_name ? c_name : ""), size);
}

PooledClass::~PooledClass() {
  m_pool->release();
}

//
// Str
//

thread_local const Ref<Str> Str::empty(Str::make(""));
thread_local const Ref<Str> Str::nan(Str::make("NaN"));
thread_local const Ref<Str> Str::pos_inf(Str::make("Infinity"));
thread_local const Ref<Str> Str::neg_inf(Str::make("-Infinity"));
thread_local const Ref<Str> Str::undefined(Str::make("undefined"));
thread_local const Ref<Str> Str::null(Str::make("null"));
thread_local const Ref<Str> Str::bool_true(Str::make("true"));
thread_local const Ref<Str> Str::bool_false(Str::make("false"));

size_t Str::s_max_size = 256 * 0x400 * 0x400;

thread_local static char s_shared_str_tmp_buf[0x10000];

static auto str_make_tmp_buf(size_t size) -> char* {
  if (size < sizeof(s_shared_str_tmp_buf)) {
    return s_shared_str_tmp_buf;
  } else {
    return new char[size];
  }
}

static void str_free_tmp_buf(char *buf) {
  if (buf != s_shared_str_tmp_buf) {
    delete [] buf;
  }
}

auto Str::local_map() -> LocalMap& {
  thread_local static LocalMap s_local_map;
  return s_local_map;
}

auto Str::make(const uint32_t *codes, size_t len) -> Str* {
  if (len > s_max_size) len = s_max_size;
  auto buf = str_make_tmp_buf(len);
  int p = 0;
  for (size_t i = 0; i < len; i++) {
    auto n = Utf8Decoder::encode(codes[i], buf + p, len - p);
    if (!n) break;
    p += n;
  }
  auto *s = make(buf, p);
  str_free_tmp_buf(buf);
  return s;
}

auto Str::make(double n) -> Str* {
  if (std::isnan(n)) return nan;
  if (std::isinf(n)) return std::signbit(n) ? neg_inf : pos_inf;
  char str[100];
  auto len = Number::to_string(str, sizeof(str), n);
  return make(str, len);
}

auto Str::make(int n) -> Str* {
  char str[100];
  auto len = std::snprintf(str, sizeof(str), "%d", n);
  return make(str, len);
}

auto Str::make(int64_t n) -> Str* {
  char str[100];
  auto len = std::snprintf(str, sizeof(str), "%lld", (long long)n);
  return make(str, len);
}

auto Str::make(uint64_t n) -> Str* {
  char str[100];
  auto len = std::snprintf(str, sizeof(str), "%llu", (unsigned long long)n);
  return make(str, len);
}

auto Str::parse_int(int base) const -> double {
  char *p = nullptr;
  auto n = std::strtoll(c_str(), &p, base);
  while (*p && std::isblank(*p)) p++;
  return *p ? NAN : n;
}

bool Str::parse_int64(int64_t &i, int base) {
  char *p = nullptr;
  i = std::strtoll(c_str(), &p, base);
  while (*p && std::isblank(*p)) p++;
  return !*p;
}

auto Str::parse_float() const -> double {
  char *p = nullptr;
  auto n = std::strtod(c_str(), &p);
  while (*p && std::isblank(*p)) p++;
  return *p ? NAN : n;
}

auto Str::substring(int start, int end) -> std::string {
  auto a = chr_to_pos(start);
  auto b = chr_to_pos(end);
  return m_char_data->str().substr(a, b - a);
}

//
// Str::CharData
//

Str::CharData::CharData(std::string &&str) : m_str(std::move(str)) {
  int n = 0, p = 0, i = 0;
  Utf8Decoder decoder(
    [&](int cp) {
      if (n > 0) {
        if (n % CHUNK_SIZE == 0) {
          m_chunks.push_back(p);
        }
      }
      n++;
    }
  );
  for (const auto c : m_str) {
    if (!(c & 0x80) || (c & 0x40)) p = i;
    if (!decoder.input(c)) break;
    i++;
  }
  m_length = n;
}

auto Str::CharData::pos_to_chr(int i) const -> int {
  int p = 0, n = 0;
  if (i >= size()) return m_length;
  if (i < 0) i = 0;
  if (!m_chunks.empty() && i >= m_chunks[0]) {
    int a = 0, b = m_chunks.size();
    while (a + 1 < b) {
      int m = (a + b) >> 1;
      int p = m_chunks[m];
      if (i > p) {
        a = m;
      } else if (i < p) {
        b = m;
      } else {
        return CHUNK_SIZE * (m + 1);
      }
    }
    p = m_chunks[a];
    n = CHUNK_SIZE * (a + 1);
    if (a + 1 < m_chunks.size()) {
      auto q = m_chunks[a + 1];
      if (q - p == CHUNK_SIZE) {
        return n + (i - p);
      }
    }
  }
  while (p < i) {
    auto c = m_str[p];
    if (c & 0x80) {
      if ((c & 0xe0) == 0xc0) p += 2; else
      if ((c & 0xf0) == 0xe0) p += 3; else
      if ((c & 0xf8) == 0xf0) p += 4; else
      break;
    } else {
      p++;
    }
    n++;
  }
  if (p > i) n--;
  return n;
}

auto Str::CharData::chr_to_pos(int i) const -> int {
  int chk = i / CHUNK_SIZE;
  int off = i % CHUNK_SIZE;
  int min, max;
  if (chk <= 0) {
    min = 0;
    max = m_chunks.empty() ? size() : m_chunks[0];
  } else if (chk - 1 >= m_chunks.size()) {
    min = max = size();
  } else {
    min = m_chunks[chk - 1];
    max = chk >= m_chunks.size() ? size() : m_chunks[chk];
  }
  if (!off) return min;
  if (chk < m_chunks.size() && max - min == CHUNK_SIZE) {
    return std::min(max, min + off);
  }
  auto p = min;
  for (int n = off; n > 0 && p < max; n--) {
    auto c = m_str[p];
    if (c & 0x80) {
      if ((c & 0xe0) == 0xc0) p += 2; else
      if ((c & 0xf0) == 0xe0) p += 3; else
      if ((c & 0xf8) == 0xf0) p += 4; else
      break;
    } else {
      p++;
    }
  }
  return p;
}

auto Str::CharData::chr_at(int i) const -> int {
  i = chr_to_pos(i);
  if (i >= size()) return -1;
  auto c = m_str[i];
  auto n = size();
  if (c & 0x80) {
    if ((c & 0xe0) == 0xc0 && i + 1 < n) {
      return
        ((c          & 0x1f) << 6)|
        ((m_str[i+1] & 0x3f) << 0);
    }
    if ((c & 0xf0) == 0xe0 && i + 2 < n) {
      return
        ((c          & 0x0f) << 12)|
        ((m_str[i+1] & 0x3f) <<  6)|
        ((m_str[i+2] & 0x3f) <<  0);
    }
    if ((c & 0xf8) == 0xf0 && i + 3 < n) {
      return
        ((c          & 0x07) << 18)|
        ((m_str[i+1] & 0x3f) << 12)|
        ((m_str[i+2] & 0x3f) <<  6)|
        ((m_str[i+3] & 0x3f) <<  0);
    }
    return -1;
  } else {
    return c;
  }
}

//
// Value
//

const Value Value::empty((Value*)nullptr);
const Value Value::undefined;
const Value Value::null((Object*)nullptr);

bool Value::is_identical(const Value &a, const Value &b) {
  if (a.type() != b.type()) {
    return false;
  } else {
    switch (a.type()) {
      case Value::Type::Empty: return true;
      case Value::Type::Undefined: return true;
      case Value::Type::Boolean: return a.b() == b.b();
      case Value::Type::Number: return a.n() == b.n();
      case Value::Type::String: return a.s() == b.s();
      case Value::Type::Object: return a.o() == b.o();
    }
  }
  return true;
}

bool Value::is_equal(const Value &a, const Value &b) {
  if (a.type() == b.type()) {
    switch (a.type()) {
      case Value::Type::Empty: return true;
      case Value::Type::Undefined: return true;
      case Value::Type::Boolean: return a.b() == b.b();
      case Value::Type::Number: return a.n() == b.n();
      case Value::Type::String: return a.s() == b.s();
      case Value::Type::Object: return a.o() == b.o();
      default: return false;
    }
  } else if (a.is_object() && b.is_object()) {
    return a.o() == b.o();
  } else if (a.is_nullish() && b.is_nullish()) {
    return true;
  } else if (a.is_nullish() || b.is_nullish()) {
    return false;
  } else if (a.is_boolean() || a.is_number() || b.is_boolean() || b.is_number()) {
    auto na = a.to_number();
    auto nb = b.to_number();
    return na == nb;
  } else {
    auto sa = a.to_string();
    auto sb = b.to_string();
    auto ret = (sa == sb);
    sa->release();
    sb->release();
    return ret;
  }
}

//
// Instance
//

Instance::~Instance() {
  while (m_scopes) {
    auto s = m_scopes;
    remove(s);
    s->retain();
    s->clear(true);
    s->release();
  }
}

//
// Context
//

auto Context::Error::where() const -> const Location* {
  for (const auto &loc : backtrace) {
    if (loc.line > 0 && loc.column > 0) {
      return &loc;
    }
  }
  return nullptr;
}

void Context::reset() {
  for (auto *c = this; c; c = c->m_caller) c->m_has_error = false;
  m_error->message.clear();
  m_error->backtrace.clear();
}

void Context::error(const std::string &msg) {
  for (auto *c = this; c; c = c->m_caller) c->m_has_error = true;
  m_error->message = msg;
}

void Context::error(const std::runtime_error &err) {
  error(err.what());
}

void Context::error_argument_count(int n) {
  char s[200];
  std::sprintf(s, "requires %d or more arguments", n);
  error(s);
}

void Context::error_argument_count(int min, int max) {
  char s[200];
  std::sprintf(s, "requires %d to %d arguments", min, max);
  error(s);
}

void Context::error_argument_type(int i, const char *type) {
  char s[200];
  std::sprintf(s, "argument #%d expects %s", i + 1, type);
  error(s);
}

void Context::error_invalid_enum_value(int i) {
  char s[200];
  std::sprintf(s, "argument #%d has an invalid enum value", i + 1);
  error(s);
}

void Context::trace(const Source *source, int line, int column) {
  m_call_site.source = source;
  m_call_site.line = line;
  m_call_site.column = column;
}

void Context::backtrace(const Source *source, int line, int column) {
  Location l;
  l.source = source;
  l.line = line;
  l.column = column;
  m_error->backtrace.push_back(l);
}

void Context::backtrace(const std::string &name) {
  auto &bt = m_error->backtrace;
  if (!bt.empty() && bt.back().name.empty()) {
    bt.back().name = name;
  } else {
    Location l;
    l.name = name;
    l.line = 0;
    l.column = 0;
    bt.push_back(l);
  }
}

//
// ClassMap
//

thread_local Ref<ClassMap> ClassMap::m_singleton;

auto ClassMap::add(Class *c) -> size_t {
  auto id = m_class_slot_free;
  if (!id) {
    id = m_class_slots.size();
    m_class_slots.push_back({ c });
  } else {
    m_class_slot_free = m_class_slots[id].next_slot;
    m_class_slots[id].class_ptr = c;
  }
  if (c->name() != Str::empty) m_class_map[c->name()->str()] = c;
  return id;
}

void ClassMap::remove(Class *c) {
  if (c->name() != Str::empty) m_class_map.erase(c->name()->str());
  auto &slot = m_class_slots[c->id()];
  slot.class_ptr = nullptr;
  slot.next_slot = m_class_slot_free;
  m_class_slot_free = c->id();
}

//
// Class
//

Class::Class(
  const std::string &name,
  Class *super,
  const std::list<Field*> &fields)
  : m_super(super)
  , m_name(pjs::Str::make(name))
  , m_class_map(ClassMap::get())
{
  if (super) {
    m_field_map = super->m_field_map;
    m_fields = super->m_fields;
    m_variables = super->m_variables;
  }
  for (const auto f : fields) {
    auto k = f->name();
    auto p = m_field_map.find(k);
    if (p != m_field_map.end()) {
      m_fields[p->second] = f;
    } else {
      auto i = m_fields.size();
      m_field_map[k] = i;
      m_fields.push_back(f);
      if (f->id() >= 0) {
        if (f->id() >= m_field_index.size()) {
          m_field_index.resize(f->id() + 1);
        }
        m_field_index[f->id()] = i;
      }
    }
    if (f->is_variable()) {
      auto v = static_cast<Variable*>(f);
      v->m_index = m_variables.size();
      m_variables.push_back(v);
    }
  }
  for (auto &p : m_field_map) p.first->retain();
  m_id = m_class_map->add(this);
}

Class::~Class() {
  for (auto &p : m_field_map) {
    p.first->release();
  }
  if (m_class_map) m_class_map->remove(this);
}

void Class::assign(Object *obj, Object *src) {
  for (size_t i = 0, n = m_fields.size(); i < n; i++) {
    auto *f = m_fields[i].get();
    if (!f->is_method()) {
      auto *k = f->name();
      Value v;
      if (src->get(k, v)) {
        if (f->is_accessor()) {
          static_cast<Accessor*>(f)->set(obj, v);
        } else {
          auto index = static_cast<Variable*>(f)->index();
          obj->data()->at(index) = v;
        }
      }
    }
  }
}

//
// Object
//

template<> void ClassDef<Object>::init() {
  method("toString", [](Context &ctx, Object *obj, Value &ret) { ret.set(obj->to_string()); });
  method("valueOf", [](Context &ctx, Object *obj, Value &ret) { obj->value_of(ret); });
  m_c = Class::make("Object", nullptr, m_init_data->fields);
  m_c->set_ctor([](Context &ctx) -> Object* { return Object::make(); });
}

template<> void ClassDef<Constructor<Object>>::init() {
  super<Function>();
  ctor();

  method("assign", [](Context &ctx, Object*, Value &ret) {
    Value val;
    if (!ctx.arguments(1, &val)) return;
    Object *obj = val.to_object();
    ret.set(obj);
    if (obj) {
      for (int i = 1; i < ctx.argc(); i++) {
        if (auto obj2 = ctx.arg(i).to_object()) {
          Object::assign(obj, obj2);
          obj2->release();
        }
      }
      obj->release();
    }
  });

  method("entries", [](Context &ctx, Object*, Value &ret) {
    Object *obj;
    if (!ctx.arguments(1, &obj)) return;
    ret.set(Object::entries(obj));
  });

  method("fromEntries", [](Context &ctx, Object*, Value &ret) {
    Array *arr;
    if (!ctx.arguments(1, &arr)) return;
    ret.set(Object::from_entries(arr));
  });

  method("keys", [](Context &ctx, Object*, Value &ret) {
    Object *obj;
    if (!ctx.arguments(1, &obj)) return;
    ret.set(Object::keys(obj));
  });

  method("values", [](Context &ctx, Object*, Value &ret) {
    Object *obj;
    if (!ctx.arguments(1, &obj)) return;
    ret.set(Object::values(obj));
  });
}

void Object::value_of(Value &out) {
  out.set(this);
}

auto Object::to_string() const -> std::string {
  char s[256];
  std::sprintf(s, "[object %s]", m_class->name()->c_str());
  return s;
}

auto Object::dump() -> Object* {
  return this;
}

auto Object::entries(Object *obj) -> Array* {
  if (!obj) return nullptr;
  auto *a = Array::make(obj->ht_size());
  int i = 0;
  obj->iterate_all([&](Str *k, Value &v) {
    auto *ent = Array::make(2);
    ent->set(0, k);
    ent->set(1, v);
    a->set(i++, ent);
  });
  return a;
}

auto Object::from_entries(Array *arr) -> Object* {
  if (!arr) return nullptr;
  auto obj = make();
  arr->iterate_all([=](Value &v, int) {
    if (v.is_array()) {
      auto entry = v.as<Array>();
      Value k, v;
      entry->get(0, k);
      entry->get(1, v);
      auto s = k.to_string();
      obj->set(s, v);
      s->release();
    }
  });
  return obj;
}

auto Object::keys(Object *obj) -> Array* {
  if (!obj) return nullptr;
  auto *a = Array::make(obj->ht_size());
  int i = 0;
  obj->iterate_all([&](Str *k, Value &v) {
    a->set(i++, k);
  });
  return a;
}

auto Object::values(Object *obj) -> Array* {
  if (!obj) return nullptr;
  auto *a = Array::make(obj->ht_size());
  int i = 0;
  obj->iterate_all([&](Str *k, Value &v) {
    a->set(i++, v);
  });
  return a;
}

//
// SharedValue
//

void SharedValue::to_value(Value &v) const {
  switch (m_t) {
    case Value::Type::Boolean: v.set(m_v.b); break;
    case Value::Type::Number: v.set(m_v.n); break;
    case Value::Type::String: v.set(Str::make(m_v.s)); break;
    case Value::Type::Object: v.set(m_v.o ? m_v.o->to_object() : nullptr); break;
    default: break;
  }
}

void SharedValue::from_value(const Value &v) {
  switch (m_t = v.type()) {
    case Value::Type::Boolean: m_v.b = v.b(); break;
    case Value::Type::Number: m_v.n = v.n(); break;
    case Value::Type::String: m_v.s = v.s()->data()->retain(); break;
    case Value::Type::Object: if (auto o = m_v.o = SharedObject::make(v.o())) o->retain(); break;
    default: break;
  }
}

void SharedValue::release() {
  switch (m_t) {
    case Value::Type::String: m_v.s->release(); break;
    case Value::Type::Object: if (auto o = m_v.o) o->release(); break;
    default: break;
  }
}

//
// SharedObject
//

SharedObject::SharedObject(Object *o) {
  auto p = &m_entry_blocks;
  auto b = *p;
  o->iterate_all(
    [&](Str *k, Value &v) {
      if (!b || b->length >= sizeof(b->entries) / sizeof(Entry)) {
        b = *p = new EntryBlock;
        p = &b->next;
      }
      auto &e = b->entries[b->length++];
      e.k = k->data();
      e.v = v;
    }
  );
}

SharedObject::~SharedObject() {
  auto b = m_entry_blocks;
  while (b) {
    auto block = b; b = b->next;
    delete block;
  }
}

auto SharedObject::to_object() -> Object* {
  auto obj = Object::make();
  for (auto b = m_entry_blocks; b; b = b->next) {
    for (auto i = 0, n = b->length; i < n; i++) {
      const auto &e = b->entries[i];
      if (e.k) {
        Value v; e.v.to_value(v);
        obj->set(Str::make(e.k), v);
      }
    }
  }
  return obj;
}

//
// Boolean
//

template<> void ClassDef<Boolean>::init() {
  ctor([](Context &ctx) -> Object* {
    return Boolean::make(ctx.argc() > 0 ? ctx.arg(0).to_boolean() : false);
  });
}

template<> void ClassDef<Constructor<Boolean>>::init() {
  super<Function>();
  ctor();
}

void Boolean::value_of(Value &out) {
  out.set(m_b);
}

auto Boolean::to_string() const -> std::string {
  return m_b ? "true" : "false";
}

//
// Int
//

template<> void EnumDef<Int::Type>::init() {
  define(Int::Type::i8, "i8");
  define(Int::Type::u8, "u8");
  define(Int::Type::i16, "i16");
  define(Int::Type::u16, "u16");
  define(Int::Type::i32, "i32");
  define(Int::Type::u32, "u32");
  define(Int::Type::i64, "i64");
  define(Int::Type::u64, "u64");
}

template<> void ClassDef<Int>::init() {
  ctor([](Context &ctx) -> Object* {
    EnumValue<Int::Type> t = Int::Type::i32;
    double n; Str *s; Array *a; Int *i; int l, h;
    if (ctx.is_string_like(0)) {
      switch (ctx.argc()) {
        case 1:
          if (ctx.get(0, t)) {
            return Int::make(t.get());
          } else {
            ctx.get(0, s);
            return Int::make(s);
          }
        case 2:
          if (!ctx.check(0, t)) return nullptr;
          if (ctx.get(1, i)) return Int::make(t, i);
          if (ctx.get(1, n)) return Int::make(t, n);
          if (ctx.get(1, s)) return Int::make(t, s);
          if (ctx.get(1, a)) return Int::make(t, a);
          ctx.error_argument_type(1, "a number, a string or an array");
          return nullptr;
        default:
          if (!ctx.arguments(3, &t, &l, &h)) return nullptr;
          return Int::make(t, l, h);
      }
    } else {
      if (ctx.get(0, i)) return Int::make(i);
      if (ctx.get(0, s)) return Int::make(t, s);
      if (ctx.get(0, a)) return Int::make(t, a);
      if (ctx.get(0, n)) {
        int h = 0;
        if (ctx.get(1, h)) return Int::make(int(n), h);
        return Int::make(n);
      }
      ctx.error_argument_type(0, "a number, a string or an array");
      return nullptr;
    }
  });

  accessor("type", [](Object *obj, Value &ret) { ret.set(EnumDef<Int::Type>::name(obj->as<Int>()->type())); });
  accessor("width", [](Object *obj, Value &ret) { ret.set(obj->as<Int>()->width()); });
  accessor("low", [](Object *obj, Value &ret) { ret.set(obj->as<Int>()->low()); });
  accessor("high", [](Object *obj, Value &ret) { ret.set(obj->as<Int>()->high()); });
  accessor("isUnsigned", [](Object *obj, Value &ret) { ret.set(obj->as<Int>()->isUnsigned()); });

  method("toBytes", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Int>()->toBytes());
  });
}

template<> void ClassDef<Constructor<Int>>::init() {
  super<Function>();
  ctor();

  method("parse", [](Context &ctx, Object*, Value &ret) {
    Str *s;
    int base = 10;
    if (!ctx.arguments(1, &s, &base)) return;
    int64_t i;
    ret.set(s->parse_int64(i, base) ? Int::make(i) : nullptr);
  });
}

Int::Int(Array *bytes) : m_i(0) {
  int n = bytes ? bytes->length() : 0;
  if (n > 4) m_t = Type::u64; else
  if (n > 2) m_t = Type::u32; else
  if (n > 1) m_t = Type::u16; else m_t = Type::u8;
  fill(bytes);
}

void Int::fill(Array *bytes) {
  if (bytes) {
    auto n = width() >> 3;
    for (int i = 0; i < n; i++) {
      Value v;
      bytes->get(i, v);
      int b = v.to_number();
      m_i |= (int64_t)(uint8_t)b << (i << 3);
    }
  }
}

auto Int::promote(Type t, Type u) -> Type {
  static const Type s_table[8][8] = {
    //      i8         i16        i32        i64        u8         u16        u32        u64
    { Type::i8 , Type::i16, Type::i32, Type::i64, Type::i16, Type::i32, Type::i64, Type::u64 }, // i8
    { Type::i16, Type::i16, Type::i32, Type::i64, Type::i16, Type::i32, Type::i64, Type::u64 }, // i16
    { Type::i32, Type::i32, Type::i32, Type::i64, Type::i32, Type::i32, Type::i64, Type::u64 }, // i32
    { Type::i64, Type::i64, Type::i64, Type::i64, Type::i64, Type::i64, Type::i64, Type::u64 }, // i64
    { Type::i16, Type::i16, Type::i32, Type::i64, Type::u8 , Type::u16, Type::u32, Type::u64 }, // u8
    { Type::i32, Type::i32, Type::i32, Type::i64, Type::u16, Type::u16, Type::u32, Type::u64 }, // u16
    { Type::i64, Type::i64, Type::i64, Type::i64, Type::u32, Type::u32, Type::u32, Type::u64 }, // u32
    { Type::u64, Type::u64, Type::u64, Type::u64, Type::u64, Type::u64, Type::u64, Type::u64 }, // u64
  };
  return s_table[int(t)][int(u)];
}

auto Int::convert(Type t, int64_t i) -> int64_t {
  switch (t) {
    case Type::i8 : return int8_t(i);
    case Type::i16: return int16_t(i);
    case Type::i32: return int32_t(i);
    case Type::u8 : return uint8_t(i);
    case Type::u16: return uint16_t(i);
    case Type::u32: return uint32_t(i);
    default: return i;
  }
}

auto Int::convert(Type t, double n) -> int64_t {
  switch (t) {
    case Type::i8 : return int8_t(n);
    case Type::i16: return int16_t(n);
    case Type::i32: return int32_t(n);
    case Type::u8 : return uint8_t(n);
    case Type::u16: return uint16_t(n);
    case Type::u32: return uint32_t(n);
    default: return n;
  }
}

auto Int::convert(Type t, const std::string &s) -> int64_t {
  auto i = std::atoll(s.c_str());
  return convert(t, int64_t(i));
}

auto Int::to_number() const -> double {
  if (isUnsigned()) {
    return uint64_t(m_i);
  } else {
    return m_i;
  }
}

auto Int::to_string(char *str, size_t len) const -> size_t {
  if (isUnsigned()) {
    return std::snprintf(str, len, "%llu", (unsigned long long)m_i);
  } else {
    return std::snprintf(str, len, "%lld", (long long)m_i);
  }
}

auto Int::toBytes() const -> Array* {
  auto n = width() >> 3;
  auto a = Array::make(n);
  for (int i = 0; i < n; i++) {
    a->set(i, 0xff & (m_i >> (i << 3)));
  }
  return a;
}

bool Int::eql(const Int *i) const {
  if (i->m_i != m_i) return false;
  if (i->isUnsigned() != isUnsigned()) {
    if (m_i & (1ull << 63)) return false;
  }
  return true;
}

auto Int::cmp(const Int *i) const -> int {
  if (eql(i)) return 0;
  if (isUnsigned()) {
    if (i->isUnsigned() || i->m_i > 0) {
      return uint64_t(m_i) > uint64_t(i->m_i) ? 1 : -1;
    } else {
      return 1;
    }
  } else {
    if (i->isUnsigned()) {
      if (m_i <= 0) {
        return -1;
      } else {
        return uint64_t(m_i) > uint64_t(i->m_i) ? 1 : -1;
      }
    } else {
      return m_i > i->m_i ? 1 : -1;
    }
  }
}

auto Int::neg() const -> Int* {
  return Int::make(m_t, -m_i);
}

auto Int::inc() const -> Int* {
  return Int::make(m_t, m_i + 1);
}

auto Int::dec() const -> Int* {
  return Int::make(m_t, m_i - 1);
}

auto Int::add(const Int *i) const -> Int* {
  auto t = promote(m_t, i->m_t);
  auto a = convert(t, m_i);
  auto b = convert(t, i->m_i);
  return Int::make(t, a + b);
}

auto Int::sub(const Int *i) const -> Int* {
  auto t = promote(m_t, i->m_t);
  auto a = convert(t, m_i);
  auto b = convert(t, i->m_i);
  return Int::make(t, a - b);
}

auto Int::mul(const Int *i) const -> Int* {
  auto t = promote(m_t, i->m_t);
  auto a = convert(t, m_i);
  auto b = convert(t, i->m_i);
  return Int::make(t, a * b);
}

auto Int::div(const Int *i) const -> Int* {
  auto t = promote(m_t, i->m_t);
  auto a = convert(t, m_i);
  auto b = convert(t, i->m_i);
  if (int(t) >= int(Type::u8)) {
    return Int::make(t, int64_t(uint64_t(a) / uint64_t(b)));
  } else {
    return Int::make(t, a / b);
  }
}

auto Int::mod(const Int *i) const -> Int* {
  auto t = promote(m_t, i->m_t);
  auto a = convert(t, m_i);
  auto b = convert(t, i->m_i);
  if (int(t) >= int(Type::u8)) {
    return Int::make(t, int64_t(uint64_t(a) % uint64_t(b)));
  } else {
    return Int::make(t, a % b);
  }
}

auto Int::shl(int n) const -> Int* {
  return Int::make(m_t, m_i << n);
}

auto Int::shr(int n) const -> Int* {
  return Int::make(m_t, m_i >> n);
}

auto Int::bitwise_shr(int n) const -> Int* {
  return Int::make(m_t, int64_t(uint64_t(m_i) >> n));
}

auto Int::bitwise_not() const -> Int* {
  return Int::make(m_t, ~m_i);
}

auto Int::bitwise_and(const Int *i) const -> Int* {
  auto t = promote(m_t, i->m_t);
  auto a = convert(t, m_i);
  auto b = convert(t, i->m_i);
  return Int::make(t, a & b);
}

auto Int::bitwise_or(const Int *i) const -> Int* {
  auto t = promote(m_t, i->m_t);
  auto a = convert(t, m_i);
  auto b = convert(t, i->m_i);
  return Int::make(t, a | b);
}

auto Int::bitwise_xor(const Int *i) const -> Int* {
  auto t = promote(m_t, i->m_t);
  auto a = convert(t, m_i);
  auto b = convert(t, i->m_i);
  return Int::make(t, a ^ b);
}

void Int::value_of(Value &out) {
  out.set(to_number());
}

auto Int::to_string() const -> std::string {
  char str[100];
  auto len = to_string(str, sizeof(str));
  return std::string(str, len);
}

//
// Number
//

template<> void ClassDef<Number>::init() {
  ctor([](Context &ctx) -> Object* {
    return Number::make(ctx.argc() > 0 ? ctx.arg(0).to_number() : 0);
  });

  method("toString", [](Context &ctx, Object *obj, Value &ret) {
    int radix = 10;
    if (!ctx.arguments(0, &radix)) return;
    if (radix < 2 || radix > 36) {
      ctx.error("invalid radix");
      return;
    }
    auto n = obj->as<Number>()->value();
    char str[200];
    auto len = Number::to_string(str, sizeof(str), n, radix);
    ret.set(Str::make(str, len));
  });

  method("toExponential", [](Context &ctx, Object *obj, Value &ret) {
    auto n = obj->as<Number>()->value();
    Value digits;
    if (!ctx.arguments(0, &digits)) return;
    if (digits.is_undefined()) {
      char str[200];
      auto len = Number::to_exponential(str, sizeof(str), n);
      ret.set(Str::make(str, len));
    } else if (digits.is_number()) {
      auto d = digits.n();
      if (d < 0 || d > 100) {
        ctx.error("invalid fraction digits");
        return;
      }
      char str[200];
      auto len = Number::to_exponential(str, sizeof(str), n, d);
      ret.set(Str::make(str, len));
    } else {
      ctx.error_argument_type(0, "a number");
    }
  });

  method("toFixed", [](Context &ctx, Object *obj, Value &ret) {
    auto n = obj->as<Number>()->value();
    int digits = 0;
    if (!ctx.arguments(0, &digits)) return;
    if (digits < 0 || digits > 100) {
      ctx.error("invalid digits");
      return;
    }
    char str[200];
    auto len = Number::to_fixed(str, sizeof(str), n, digits);
    ret.set(Str::make(str, len));
  });

  method("toPrecision", [](Context &ctx, Object *obj, Value &ret) {
    auto n = obj->as<Number>()->value();
    Value digits;
    if (!ctx.arguments(0, &digits)) return;
    if (digits.is_undefined()) {
      char str[200];
      auto len = Number::to_string(str, sizeof(str), n);
      ret.set(Str::make(str, len));
    } else if (digits.is_number()) {
      auto d = digits.n();
      if (d < 1 || d > 100) {
        ctx.error("invalid precision");
        return;
      }
      char str[200];
      auto len = Number::to_precision(str, sizeof(str), n, d);
      ret.set(Str::make(str, len));
    } else {
      ctx.error_argument_type(0, "a number");
    }
  });
}

template<> void ClassDef<Constructor<Number>>::init() {
  super<Function>();
  ctor();

  variable("EPSILON", std::numeric_limits<double>::epsilon());
  variable("MAX_SAFE_INTEGER", double(1ull << 53));
  variable("MAX_VALUE", std::numeric_limits<double>::max());
  variable("MIN_SAFE_INTEGER", -double(1ull << 53));
  variable("MIN_VALUE", std::numeric_limits<double>::min());
  variable("NaN", std::numeric_limits<double>::quiet_NaN());
  variable("NEGATIVE_INFINITY", -std::numeric_limits<double>::infinity());
  variable("POSITIVE_INFINITY", std::numeric_limits<double>::infinity());

  method("isNaN", [](Context &ctx, Object*, Value &ret) {
    double n;
    if (!ctx.arguments(1, &n)) return;
    ret.set(Number::is_nan(n));
  });

  method("isFinite", [](Context &ctx, Object*, Value &ret) {
    double n;
    if (!ctx.arguments(1, &n)) return;
    ret.set(Number::is_finite(n));
  });

  method("isInteger", [](Context &ctx, Object*, Value &ret) {
    double n;
    if (!ctx.arguments(1, &n)) return;
    ret.set(Number::is_integer(n));
  });

  method("parseFloat", [](Context &ctx, Object*, Value &ret) {
    Str *s;
    if (!ctx.arguments(1, &s)) return;
    ret.set(s->parse_float());
  });

  method("parseInt", [](Context &ctx, Object*, Value &ret) {
    Str *s;
    int base = 10;
    if (!ctx.arguments(1, &s, &base)) return;
    ret.set(s->parse_int(base));
  });
}

bool Number::is_nan(double n) {
  return std::isnan(n);
}

bool Number::is_finite(double n) {
  return std::isfinite(n);
}

bool Number::is_integer(double n) {
  double i;
  if (std::isnan(n)) return false;
  if (std::isinf(n)) return false;
  return (std::modf(n, &i) == 0);
}

static size_t special_number_to_string(char *str, size_t len, double n) {
  if (std::isnan(n)) {
    std::strcpy(str, Str::nan->c_str());
    return Str::nan->size();
  }
  if (std::isinf(n)) {
    if (std::signbit(n)) {
      std::strcpy(str, Str::neg_inf->c_str());
      return Str::neg_inf->size();
    } else {
      std::strcpy(str, Str::pos_inf->c_str());
      return Str::pos_inf->size();
    }
  }
  return 0;
}

static size_t number_to_string(char *str, size_t len, double n, int digits, int radix = 10) {
  static const char s_symbols[] = { "0123456789abcdefghijklmnopqrstuvwxyz" };
  if (auto l = special_number_to_string(str, len, n)) return l;
  if (radix == 10) {
    auto d = digits; if (d < 0) d = -d;
    auto l = std::snprintf(str, len, "%.*f", d, n);
    if (digits < 0) {
      while (l > 1 && str[l-1] == '0') l--;
      if (l > 1 && str[l-1] == '.') l--;
    }
    return l;
  }
  bool sign = std::signbit(n);
  double i, f = std::modf(n, &i);
  size_t p = 0;
  do {
    auto j = std::trunc(i / radix);
    auto c = s_symbols[(int)std::fabs(i - j * radix)];
    str[p++] = c;
    i = j;
  } while (i != 0 && p < len);
  if (sign && p < len) {
    str[p++] = '-';
    f = -f;
  }
  for (size_t i = 0, n = p >> 1; i < n; i++) {
    auto j = p - i - 1;
    auto t = str[i];
    str[i] = str[j];
    str[j] = t;
  }
  if (p >= len) return p;
  if (digits && f != 0) {
    auto n = digits;
    if (n < 0) n = -n;
    if (p < len) str[p++] = '.';
    while (p < len && n > 0 && f != 0) {
      f = std::modf(f * radix, &i);
      str[p++] = s_symbols[int(i)];
      n--;
    }
    if (digits < 0) {
      while (str[p-1] == '0') p--;
      if (str[p-1] == '.') p--;
    }
  }
  return p;
}

size_t Number::to_string(char *str, size_t len, double n, int radix) {
  return number_to_string(str, len, n, -12, radix);
}

size_t Number::to_precision(char *str, size_t len, double n, int precision) {
  if (auto l = special_number_to_string(str, len, n)) return l;
  auto max = std::numeric_limits<double>::digits10 + 1;
  if (precision < 0) precision = 0;
  if (precision > max) precision = max;
  return std::snprintf(str, len, "%.*g", precision, n);
}

size_t Number::to_fixed(char *str, size_t len, double n, int digits) {
  return number_to_string(str, len, n, digits, 10);
}

size_t Number::to_exponential(char *str, size_t len, double n) {
  if (auto l = special_number_to_string(str, len, n)) return l;
  auto max = std::numeric_limits<double>::digits10 + 1;
  len = std::snprintf(str, len, "%.*e", max, n);
  auto p = len;
  do p--; while (p > 0 && str[p] != 'e');
  if (p > 0) {
    auto i = p - 1;
    while (i > 0 && str[i-1] == '0') i--;
    if (i > 0) {
      std::memmove(str + i, str + p, len - p);
      len -= p - i;
    }
  }
  return len;
}

size_t Number::to_exponential(char *str, size_t len, double n, int digits) {
  if (auto l = special_number_to_string(str, len, n)) return l;
  auto max = std::numeric_limits<double>::digits10 + 1;
  if (digits < 0) digits = 0;
  if (digits > max) digits = max;
  return std::snprintf(str, len, "%.*e", digits, n);
}

void Number::value_of(Value &out) {
  out.set(m_n);
}

auto Number::to_string() const -> std::string {
  if (std::isnan(m_n)) return Str::nan->str();
  if (std::isinf(m_n)) return std::signbit(m_n) ? Str::neg_inf->str() : Str::pos_inf->str();
  char str[100];
  auto len = to_string(str, sizeof(str), m_n);
  return std::string(str, len);
}

//
// String
//

template<> void ClassDef<String>::init() {
  ctor([](Context &ctx) -> Object* {
    return String::make(ctx.argc() > 0 ? ctx.arg(0).to_string() : Str::empty.get());
  });

  accessor("length", [](Object *obj, Value &ret) { ret.set(obj->as<String>()->length()); });

  method("charAt", [](Context &ctx, Object *obj, Value &ret) {
    int i = 0;
    if (!ctx.arguments(0, &i)) return;
    ret.set(obj->as<String>()->charAt(i));
  });

  method("charCodeAt", [](Context &ctx, Object *obj, Value &ret) {
    int i = 0;
    if (!ctx.arguments(0, &i)) return;
    auto n = obj->as<String>()->charCodeAt(i);
    if (n >= 0) ret.set(n); else ret.set(NAN);
  });

  method("codePointAt", [](Context &ctx, Object *obj, Value &ret) {
    int i = 0;
    if (!ctx.arguments(0, &i)) return;
    auto n = obj->as<String>()->charCodeAt(i);
    if (n >= 0) ret.set(n); else ret.set(NAN);
  });

  method("concat", [](Context &ctx, Object *obj, Value &ret) {
    auto s = obj->as<String>()->str();
    int size = s->size(), p = size, n = ctx.argc();
    Str *strs[n];
    for (int i = 0; i < n; i++) {
      auto s = ctx.arg(i).to_string();
      strs[i] = s;
      size += s->size();
    }
    if (size > Str::max_size()) size = Str::max_size();
    auto buf = str_make_tmp_buf(size);
    std::memcpy(buf, s->c_str(), s->size());
    for (int i = 0; i < n; i++) {
      auto s = strs[i];
      auto n = std::min(s->size(), size_t(size - p));
      if (n > 0) {
        std::memcpy(buf + p, s->c_str(), n);
        p += n;
      }
      s->release();
    }
    ret.set(Str::make(buf, size));
    str_free_tmp_buf(buf);
  });

  method("endsWith", [](Context &ctx, Object *obj, Value &ret) {
    Str *search;
    int length = obj->as<String>()->length();
    if (!ctx.arguments(1, &search, &length)) return;
    ret.set(obj->as<String>()->endsWith(search, length));
  });

  method("includes", [](Context &ctx, Object *obj, Value &ret) {
    Str *search;
    int position = 0;
    if (!ctx.arguments(1, &search, &position)) return;
    ret.set(obj->as<String>()->includes(search, position));
  });

  method("indexOf", [](Context &ctx, Object *obj, Value &ret) {
    Str *search;
    int position = 0;
    if (!ctx.arguments(1, &search, &position)) return;
    ret.set(obj->as<String>()->indexOf(search, position));
  });

  method("lastIndexOf", [](Context &ctx, Object *obj, Value &ret) {
    Str *search;
    int position = obj->as<String>()->str()->length();
    if (!ctx.arguments(1, &search, &position)) return;
    ret.set(obj->as<String>()->lastIndexOf(search, position));
  });

  method("match", [](Context &ctx, Object *obj, Value &ret) {
    RegExp *pattern;
    if (!ctx.arguments(1, &pattern)) return;
    ret.set(pattern->exec(obj->as<String>()->str()));
  });

  method("padEnd", [](Context &ctx, Object *obj, Value &ret) {
    int length;
    Str *padding = nullptr;
    if (!ctx.arguments(1, &length, &padding)) return;
    ret.set(obj->as<String>()->padEnd(length, padding));
  });

  method("padStart", [](Context &ctx, Object *obj, Value &ret) {
    int length;
    Str *padding = nullptr;
    if (!ctx.arguments(1, &length, &padding)) return;
    ret.set(obj->as<String>()->padStart(length, padding));
  });

  method("repeat", [](Context &ctx, Object *obj, Value &ret) {
    int count;
    if (!ctx.arguments(1, &count)) return;
    ret.set(obj->as<String>()->repeat(count));
  });

  method("replace", [](Context &ctx, Object *obj, Value &ret) {
    Str *pattern, *replacement;
    RegExp *reg_exp;
    if (ctx.try_arguments(2, &pattern, &replacement)) {
      ret.set(obj->as<String>()->replace(pattern, replacement));
    } else if (ctx.arguments(2, &reg_exp, &replacement)) {
      ret.set(obj->as<String>()->replace(reg_exp, replacement));
    }
  });

  method("replaceAll", [](Context &ctx, Object *obj, Value &ret) {
    Str *pattern, *replacement;
    if (!ctx.arguments(2, &pattern, &replacement)) return;
    ret.set(obj->as<String>()->replace(pattern, replacement, true));
  });

  method("search", [](Context &ctx, Object *obj, Value &ret) {
    RegExp *pattern;
    if (!ctx.arguments(1, &pattern)) return;
    ret.set(obj->as<String>()->search(pattern));
  });

  method("slice", [](Context &ctx, Object *obj, Value &ret) {
    int start;
    int end = obj->as<String>()->str()->length();
    if (!ctx.arguments(1, &start, &end)) return;
    ret.set(obj->as<String>()->slice(start, end));
  });

  method("split", [](Context &ctx, Object *obj, Value &ret) {
    Str *separator = nullptr;
    int limit = Array::MAX_SIZE;
    if (!ctx.arguments(0, &separator, &limit)) return;
    ret.set(obj->as<String>()->split(separator, limit));
  });

  method("startsWith", [](Context &ctx, Object *obj, Value &ret) {
    Str *search;
    int position = 0;
    if (!ctx.arguments(1, &search, &position)) return;
    ret.set(obj->as<String>()->startsWith(search, position));
  });

  method("substring", [](Context &ctx, Object *obj, Value &ret) {
    int start;
    int end = obj->as<String>()->str()->length();
    if (!ctx.arguments(1, &start, &end)) return;
    ret.set(obj->as<String>()->substring(start, end));
  });

  method("toLowerCase", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<String>()->toLowerCase());
  });

  method("toUpperCase", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<String>()->toUpperCase());
  });

  method("trim", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<String>()->trim());
  });

  method("trimEnd", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<String>()->trimEnd());
  });

  method("trimStart", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<String>()->trimStart());
  });
}

template<> void ClassDef<Constructor<String>>::init() {
  super<Function>();
  ctor();

  method("fromCharCode", [](Context &ctx, Object*, Value &ret) {
    auto n = ctx.argc();
    uint32_t codes[n];
    for (int i = 0; i < n; i++) {
      codes[i] = std::max(0, int(ctx.arg(i).to_number()));
    }
    ret.set(Str::make(codes, n));
  });

  method("fromCodePoint", [](Context &ctx, Object*, Value &ret) {
    auto n = ctx.argc();
    uint32_t codes[n];
    for (int i = 0; i < n; i++) {
      codes[i] = std::max(0, int(ctx.arg(i).to_number()));
    }
    ret.set(Str::make(codes, n));
  });
}

void String::value_of(Value &out) {
  out.set(m_s);
}

auto String::to_string() const -> std::string {
  return m_s->str();
}

auto String::charAt(int i) -> Str* {
  auto c = m_s->chr_at(i);
  if (c < 0) return Str::empty;
  uint32_t code = c;
  return Str::make(&code, 1);
}

auto String::charCodeAt(int i) -> int {
  return m_s->chr_at(i);
}

bool String::endsWith(Str *search) {
  return endsWith(search, m_s->length());
}

bool String::endsWith(Str *search, int length) {
  if (length < 0) length = 0;
  if (length > m_s->length()) length = m_s->length();
  int tail = m_s->chr_to_pos(length);
  int size = search->size();
  if (size == 0) return true;
  if (size > tail) return false;
  return std::strncmp(m_s->c_str() + (tail - size), search->c_str(), size) == 0;
}

bool String::includes(Str *search, int position) {
  if (!search->size()) return true;
  if (position >= m_s->length()) return false;
  if (position < 0) position = 0;
  auto p = m_s->str().find(search->str(), m_s->chr_to_pos(position));
  return p != std::string::npos;
}

auto String::indexOf(Str *search, int position) -> int {
  if (!search->size()) return std::max(0, std::min(m_s->length(), position));
  if (position >= m_s->length()) return -1;
  if (position < 0) position = 0;
  auto p = m_s->str().find(search->str(), m_s->chr_to_pos(position));
  if (p == std::string::npos) return -1;
  return m_s->pos_to_chr(p);
}

auto String::lastIndexOf(Str *search, int position) -> int {
  if (!search->size()) return std::max(0, std::min(m_s->length(), position));
  if (position >= m_s->length()) position = m_s->length() - 1;
  if (position < 0) position = 0;
  auto p = m_s->str().rfind(search->str(), m_s->chr_to_pos(position));
  if (p == std::string::npos) return -1;
  return m_s->pos_to_chr(p);
}

auto String::lastIndexOf(Str *search) -> int {
  return lastIndexOf(search, m_s->length());
}

auto String::padEnd(int length, Str *padding) -> Str* {
  if (m_s->length() >= length) return m_s;
  auto &buf = s_shared_str_tmp_buf;
  std::memcpy(buf, m_s->c_str(), m_s->size());
  auto n = fill(
    buf + m_s->size(),
    sizeof(buf) - m_s->size(),
    padding,
    length - m_s->length()
  );
  return Str::make(buf, n + m_s->size());
}

auto String::padStart(int length, Str *padding) -> Str* {
  if (m_s->length() >= length) return m_s;
  auto &buf = s_shared_str_tmp_buf;
  auto n = fill(
    buf,
    sizeof(buf) - m_s->size(),
    padding,
    length - m_s->length()
  );
  std::memcpy(buf + n, m_s->c_str(), m_s->size());
  return Str::make(buf, n + m_s->size());
}

auto String::repeat(int count) -> Str* {
  if (count <= 0) return Str::empty;
  auto &buf = s_shared_str_tmp_buf;
  auto size = fill(buf, sizeof(buf), m_s, m_s->length() * count);
  return Str::make(buf, size);
}

auto String::replace(Str *pattern, Str *replacement, bool all) -> Str* {
  std::string result;
  auto &s = m_s->str();
  auto &t = pattern->str();
  size_t i = 0;
  for (;;) {
    auto j = s.find(t, i);
    if (j == std::string::npos) {
      result += s.substr(i);
      break;
    }
    auto prefix = s.substr(i, j - i);
    result += prefix;
    i = j + t.length();
    bool dollar = false;
    for (auto c : replacement->str()) {
      if (dollar) {
        switch (c) {
          case '$' : result += '$'; break;
          case '&' : result += t; break;
          case '`' : result += prefix; break;
          case '\'': result += s.substr(i); break;
          default: result += '$'; result += c; break;
        }
        dollar = false;
      } else if (c == '$') {
        dollar = true;
      } else {
        result += c;
      }
    }
    if (dollar) result += '$';
    if (!all) {
      result += s.substr(i);
      break;
    }
  }
  return Str::make(result);
}

auto String::replace(RegExp *pattern, Str *replacement) -> Str* {
  auto &s = m_s->str();
  auto &fmt = replacement->str();
  auto &re = pattern->regex();
  return Str::make(std::regex_replace(s, re, fmt));
}

auto String::search(RegExp *pattern) -> int {
  std::smatch sm;
  std::regex_search(m_s->str(), sm, pattern->regex());
  if (sm.empty()) return -1;
  auto p = sm.position(0);
  return m_s->pos_to_chr(p);
}

auto String::slice(int start) -> Str* {
  return slice(start, m_s->length());
}

auto String::slice(int start, int end) -> Str* {
  if (start < 0) start = m_s->length() + start;
  if (start < 0) start = 0;
  if (start >= m_s->length()) return Str::empty;
  if (end < 0) end = m_s->length() + end;
  if (end <= start) return Str::empty;
  if (end > m_s->length()) end = m_s->length();
  return Str::make(m_s->substring(start, end));
}

auto String::split(Str *separator) -> Array* {
  return split(separator, Array::MAX_SIZE);
}

auto String::split(Str *separator, int limit) -> Array* {
  if (limit < 0) limit = 0;
  if (limit > Array::MAX_SIZE) limit = Array::MAX_SIZE;
  if (separator == Str::empty) {
    const char *s = m_s->c_str();
    int m = m_s->size();
    int n = m_s->length(), i = 0;
    if (n > limit) n = limit;
    auto arr = Array::make(n);
    if (!limit) return arr;
    Utf8Decoder decoder(
      [&](int c) {
        uint32_t code = c;
        arr->set(i++, Str::make(&code, 1));
      }
    );
    for (int i = 0, p = 0; i < n && p < m; p++) {
      decoder.input(s[p]);
    }
    return arr;
  } else {
    auto arr = Array::make();
    if (!limit) return arr;
    if (!separator) {
      arr->push(m_s.get());
      return arr;
    } else {
      const auto &str = m_s->str();
      const auto &sep = separator->str();
      size_t a = 0, i = 0;
      while (i < str.length()) {
        auto p = str.find(sep, i);
        if (p != std::string::npos) {
          arr->push(Str::make(&str[a], p - a));
          if (arr->length() >= limit) return arr;
          i = p + sep.length();
          a = i;
        } else {
          i = str.length();
        }
      }
      arr->push(Str::make(&str[a], i - a));
      return arr;
    }
  }
}

bool String::startsWith(Str *search, int position) {
  int size = search->size();
  if (size == 0) return true;
  if (size > m_s->size()) return false;
  if (position < 0) position = 0;
  if (position > m_s->length()) return false;
  int head = m_s->chr_to_pos(position);
  if (head + size > m_s->size()) return false;
  return std::strncmp(m_s->c_str() + head, search->c_str(), size) == 0;
}

auto String::substring(int start) -> Str* {
  int len = m_s->length();
  if (start >= len) return Str::empty;
  if (start < 0) start = 0;
  return Str::make(m_s->substring(start, len));
}

auto String::substring(int start, int end) -> Str* {
  int len = m_s->size();
  if (start < 0) start = 0;
  if (start > len) start = len;
  if (end < 0) end = 0;
  if (end > len) end = len;
  if (start == end) return Str::empty;
  if (start < end) {
    return Str::make(m_s->substring(start, end));
  } else {
    return Str::make(m_s->substring(end, start));
  }
}

auto String::toLowerCase() -> Str* {
  auto s = m_s->str();
  for (auto &c : s) {
    c = std::tolower(c);
  }
  return Str::make(std::move(s));
}

auto String::toUpperCase() -> Str* {
  auto s = m_s->str();
  for (auto &c : s) {
    c = std::toupper(c);
  }
  return Str::make(std::move(s));
}

auto String::trim() -> Str* {
  const char *s = m_s->c_str();
  int a = 0, b = m_s->size() - 1;
  while (a <= b && s[a] <= 0x20) a++;
  while (b >= 0 && s[b] <= 0x20) b--;
  if (a > b) return Str::empty;
  return Str::make(s + a, b - a + 1);
}

auto String::trimEnd() -> Str* {
  const char *s = m_s->c_str();
  int a = m_s->size() - 1;
  while (a >= 0 && s[a] <= 0x20) a--;
  if (a <= 0) return Str::empty;
  return Str::make(s, a + 1);
}

auto String::trimStart() -> Str* {
  const char *s = m_s->c_str();
  int a = 0, n = m_s->size();
  while (a < n && s[a] <= 0x20) a++;
  if (a >= n) return Str::empty;
  return Str::make(s + a, n - a);
}

auto String::fill(char *buf, size_t size, Str *str, int n) -> size_t {
  auto l = str->length();
  auto s = str->size();
  size_t p = 0;
  while (p + s <= size) {
    if (n >= l) {
      std::memcpy(buf + p, str->c_str(), s);
      n -= l;
      p += s;
    } else {
      s = str->chr_to_pos(n);
      std::memcpy(buf + p, str->c_str(), s);
      p += s;
      break;
    }
  }
  return p;
}

//
// Function
//

template<> void ClassDef<Function>::init()
{
}

auto Function::to_string() const -> std::string {
  char s[256];
  std::sprintf(s, "[Function: %s]", m_method->name()->c_str());
  return s;
}

//
// Error
//

template<> void ClassDef<Error>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *message = nullptr;
    Error *cause = nullptr;
    if (!ctx.arguments(0, &message, &cause)) return nullptr;
    return Error::make(message, cause);
  });

  accessor("name", [](Object *obj, Value &val) { val.set(obj->as<Error>()->name()); });
  accessor("message", [](Object *obj, Value &val) { val.set(obj->as<Error>()->message()); });
  accessor("cause", [](Object *obj, Value &val) { val.set(obj->as<Error>()->cause()); });
  accessor("stack", [](Object *obj, Value &val) { val.set(obj->as<Error>()->stack()); });
}

template<> void ClassDef<Constructor<Error>>::init() {
  super<Function>();
  ctor();
}

Error::Error(const Context::Error &error) {
  std::string str;
  for (const auto &l : error.backtrace) {
    str += "In ";
    str += l.name;
    if (l.line && l.column) {
      char s[100];
      std::sprintf(
        s, " at line %d column %d in %s\n",
        l.line, l.column, l.source->filename.c_str()
      );
      str += s;
    } else {
      str += '\n';
    }
  }
  m_stack = Str::make(std::move(str));
  m_message = Str::make(error.message);
}

auto Error::name() const -> Str* {
  thread_local static ConstStr s_error("Error");
  return s_error;
}

//
// Promise
//

thread_local Promise* Promise::s_settled_queue_head = nullptr;
thread_local Promise* Promise::s_settled_queue_tail = nullptr;

bool Promise::run() {
  auto p = s_settled_queue_head;
  s_settled_queue_head = nullptr;
  s_settled_queue_tail = nullptr;
  while (p) {
    auto promise = p; p = p->m_next;
    promise->dequeue();
  }
  return s_settled_queue_head;
}

auto Promise::resolve(const Value &value) -> Promise* {
  auto p = Promise::make();
  p->settle(RESOLVED, value);
  return p;
}

auto Promise::reject(const Value &error) -> Promise* {
  auto p = Promise::make();
  p->settle(REJECTED, error);
  return p;
}

auto Promise::all(Array *promises) -> Promise* {
  auto n = promises->length();
  if (!n) return resolve(Array::make());
  auto p = Promise::make();
  new Aggregator(Aggregator::ALL, Settler::make(p), promises);
  return p;
}

auto Promise::all_settled(Array *promises) -> Promise* {
  auto n = promises->length();
  if (!n) return resolve(Array::make());
  auto p = Promise::make();
  new Aggregator(Aggregator::ALL_SETTLED, Settler::make(p), promises);
  return p;
}

auto Promise::any(Array *promises) -> Promise* {
  auto n = promises->length();
  if (!n) return reject(Array::make());
  auto p = Promise::make();
  new Aggregator(Aggregator::ANY, Settler::make(p), promises);
  return p;
}

auto Promise::race(Array *promises) -> Promise* {
  auto p = Promise::make();
  auto n = promises->length();
  if (!n) return p;
  new Aggregator(Aggregator::ANY, Settler::make(p), promises);
  return p;
}

auto Promise::then(
  Context *context,
  const Value &resolved_value,
  const Value &rejected_value
) -> Promise* {
  auto t = new Then(context, resolved_value, rejected_value);
  add_then(t);
  return t->m_promise;
}

auto Promise::then(
  Context *context,
  Function *on_resolved,
  Function *on_rejected,
  Function *on_finally
) -> Promise* {
  auto t = new Then(context, on_resolved, on_rejected, on_finally);
  add_then(t);
  return t->m_promise;
}

void Promise::add_then(Then *then) {
  if (m_thens_tail) {
    m_thens_tail->m_next = then;
    m_thens_tail = then;
  } else {
    m_thens_head = then;
    m_thens_tail = then;
  }
  if (m_state != PENDING) enqueue();
}

void Promise::clear_thens() {
  auto p = m_thens_head;
  while (p) {
    auto then = p; p = p->m_next;
    delete then;
  }
  m_thens_head = nullptr;
  m_thens_tail = nullptr;
}

void Promise::settle(State state, const Value &result) {
  if (m_state == PENDING) {
    m_state = state;
    m_result = result;
    enqueue();
    if (m_dependent) {
      m_dependent->settle(state, result);
      m_dependent = nullptr;
    }
  }
}

void Promise::cancel() {
  if (m_state == PENDING) {
    clear_thens();
    m_state = CANCELED;
  }
}

void Promise::enqueue() {
  if (!m_queued) {
    if (s_settled_queue_tail) {
      s_settled_queue_tail->m_next = this;
      s_settled_queue_tail = this;
    } else {
      s_settled_queue_head = this;
      s_settled_queue_tail = this;
    }
    m_queued = true;
    retain();
  }
}

void Promise::dequeue() {
  if (m_queued) {
    auto p = m_thens_head;
    m_thens_head = nullptr;
    m_thens_tail = nullptr;
    while (p) {
      auto then = p; p = p->m_next;
      then->execute(m_state, m_result);
      delete then;
    }
    m_next = nullptr;
    m_queued = false;
    release();
  }
}

template<> void ClassDef<Promise>::init() {
  thread_local static const auto s_field_res = static_cast<Method*>(ClassDef<Promise::Settler>::field("resolve"));
  thread_local static const auto s_field_rej = static_cast<Method*>(ClassDef<Promise::Settler>::field("reject"));

  ctor([](Context &ctx) -> Object* {
    Function *executor;
    if (!ctx.arguments(1, &executor)) return nullptr;
    auto promise = Promise::make();
    auto settler = Promise::Settler::make(promise);
    {
      promise->retain();
      Value args[2], ret;
      args[0].set(Function::make(s_field_res, settler));
      args[1].set(Function::make(s_field_rej, settler));
      (*executor)(ctx, 2, args, ret);
    }
    if (!ctx.ok()) return nullptr;
    return promise->pass();
  });

  method("then", [](Context &ctx, Object *obj, Value &ret) {
    Value on_resolved;
    Value on_rejected;
    if (!ctx.arguments(1, &on_resolved, &on_rejected)) return;
    ret.set(obj->as<Promise>()->then(ctx.root(), on_resolved, on_rejected));
  });

  method("catch", [](Context &ctx, Object *obj, Value &ret) {
    Function *on_rejected;
    if (!ctx.arguments(1, &on_rejected)) return;
    ret.set(obj->as<Promise>()->then(ctx.root(), nullptr, on_rejected));
  });

  method("finally", [](Context &ctx, Object *obj, Value &ret) {
    Function *on_finally;
    if (!ctx.arguments(1, &on_finally)) return;
    ret.set(obj->as<Promise>()->then(ctx.root(), nullptr, nullptr, on_finally));
  });
}

template<> void ClassDef<Constructor<Promise>>::init() {
  super<Function>();
  ctor();

  method("resolve", [](Context &ctx, Object *obj, Value &ret) {
    Value value; ctx.get(0, value);
    ret.set(Promise::resolve(value));
  });

  method("reject", [](Context &ctx, Object *obj, Value &ret) {
    Value error; ctx.get(0, error);
    ret.set(Promise::reject(error));
  });

  method("all", [](Context &ctx, Object *obj, Value &ret) {
    Array *promises;
    if (!ctx.arguments(1, &promises)) return;
    if (!promises) { ctx.error_argument_type(0, "an array"); return; }
    ret.set(Promise::all(promises));
  });

  method("allSettled", [](Context &ctx, Object *obj, Value &ret) {
    Array *promises;
    if (!ctx.arguments(1, &promises)) return;
    if (!promises) { ctx.error_argument_type(0, "an array"); return; }
    ret.set(Promise::all_settled(promises));
  });

  method("any", [](Context &ctx, Object *obj, Value &ret) {
    Array *promises;
    if (!ctx.arguments(1, &promises)) return;
    if (!promises) { ctx.error_argument_type(0, "an array"); return; }
    ret.set(Promise::any(promises));
  });

  method("race", [](Context &ctx, Object *obj, Value &ret) {
    Array *promises;
    if (!ctx.arguments(1, &promises)) return;
    if (!promises) { ctx.error_argument_type(0, "an array"); return; }
    ret.set(Promise::race(promises));
  });
}

//
// Promise::Callback
//

auto Promise::Callback::resolved() -> Function* {
  thread_local static Method* s_method = ClassDef<Promise::Callback>::method("on_resolved");
  return Function::make(s_method, this);
}

auto Promise::Callback::rejected() -> Function* {
  thread_local static Method* s_method = ClassDef<Promise::Callback>::method("on_rejected");
  return Function::make(s_method, this);
}

template<> void ClassDef<Promise::Callback>::init() {
  method("on_resolved", [](Context &ctx, Object *obj, Value &ret) {
    Value value; ctx.get(0, value);
    obj->as<Promise::Callback>()->on_resolved(value);
  });
  method("on_rejected", [](Context &ctx, Object *obj, Value &ret) {
    Value error; ctx.get(0, error);
    obj->as<Promise::Callback>()->on_rejected(error);
  });
}

//
// Promise::Then
//

Promise::Then::Then(
  Context *context,
  Function *on_resolved,
  Function *on_rejected,
  Function *on_finally
) : m_context(context)
  , m_on_resolved(on_resolved)
  , m_on_rejected(on_rejected)
  , m_on_finally(on_finally)
  , m_promise(Promise::make())
{
}

Promise::Then::Then(
  Context *context,
  const Value &resolved_value,
  const Value &rejected_value
) : m_context(context)
  , m_promise(Promise::make())
  , m_resolved_value(resolved_value)
  , m_rejected_value(rejected_value)
{
  if (resolved_value.is_function()) m_on_resolved = resolved_value.f();
  if (rejected_value.is_function()) m_on_rejected = rejected_value.f();
}

void Promise::Then::execute(State state, const Value &result) {
  if (m_context) {
    execute(m_context, state, result);
  } else {
    Context ctx(nullptr);
    execute(&ctx, state, result);
  }
}

void Promise::Then::execute(Context *ctx, State state, const Value &result) {
  Value arg(result), ret;
  if (state == RESOLVED) {
    if (m_on_resolved) {
      (*m_on_resolved)(*ctx, 1, &arg, ret);
    } else {
      ret = m_resolved_value;
    }
  } else {
    if (m_on_rejected) {
      (*m_on_rejected)(*ctx, 1, &arg, ret);
    } else {
      ret = m_rejected_value;
    }
  }

  if (!ctx->ok()) {
    m_promise->settle(REJECTED, Error::make(ctx->error()));
    return;
  }

  if (ret.is<Promise>()) {
    auto promise = ret.as<Promise>();
    switch (promise->m_state) {
      case PENDING: promise->m_dependent = m_promise; break;
      case RESOLVED: m_promise->settle(RESOLVED, promise->m_result); break;
      case REJECTED: m_promise->settle(REJECTED, promise->m_result); break;
      case CANCELED: break;
    }
    return;
  }

  m_promise->settle(RESOLVED, ret);
}

//
// Promise::Settler
//

template<> void ClassDef<Promise::Settler>::init() {
  method("resolve", [](Context &ctx, Object *obj, Value &) {
    Value value; ctx.get(0, value);
    obj->as<Promise::Settler>()->resolve(value);
  });
  method("reject", [](Context &ctx, Object *obj, Value &) {
    Value error; ctx.get(0, error);
    obj->as<Promise::Settler>()->reject(error);
  });
}

//
// Promise::Result
//

template<> void ClassDef<Promise::Result>::init() {
  field<Value>("status", [](Promise::Result *obj) { return &obj->status; });
  field<Value>("value", [](Promise::Result *obj) { return &obj->value; });
  field<Value>("reason", [](Promise::Result *obj) { return &obj->reason; });
}

//
// Promise::Aggregator
//

Promise::Aggregator::Aggregator(Type type, Settler *settler, Array *promises)
  : m_type(type)
  , m_settler(settler)
{
  auto n = promises->length();
  auto d = PooledArray<Ref<Dependency>>::make(n);
  m_dependencies = d;
  for (int i = 0; i < n; i++) {
    Value v; promises->get(i, v);
    auto p = v.is_promise() ? v.as<Promise>() : Promise::resolve(v);
    d->at(i) = Dependency::make(this, p);
  }
  for (int i = 0; i < n; i++) {
    d->at(i)->init();
  }
}

Promise::Aggregator::~Aggregator() {
  if (m_dependencies) {
    m_dependencies->free();
  }
}

void Promise::Aggregator::settle(Dependency *dep) {
  thread_local static const ConstStr s_fulfilled("fulfilled");
  thread_local static const ConstStr s_rejected("rejected");

  switch (m_type) {
    case ALL:
      if (dep->state() == REJECTED) {
        m_settler->reject(dep->result());
      } else if (++m_counter == m_dependencies->size()) {
        auto n = m_dependencies->size();
        auto a = Array::make(n);
        for (int i = 0; i < n; i++) a->set(i, m_dependencies->at(i)->result());
        m_settler->resolve(a);
      }
      break;
    case ALL_SETTLED:
      if (++m_counter == m_dependencies->size()) {
        auto n = m_dependencies->size();
        auto a = Array::make(n);
        for (int i = 0; i < n; i++) {
          auto d = m_dependencies->at(i).get();
          auto r = Result::make();
          if (d->state() == RESOLVED) {
            r->status = s_fulfilled.get();
            r->value = d->result();
          } else {
            r->status = s_rejected.get();
            r->reason = d->result();
          }
          a->set(i, d);
        }
        m_settler->resolve(a);
      }
      break;
    case ANY:
      if (dep->state() == RESOLVED) {
        m_settler->resolve(dep->result());
      } else if (++m_counter == m_dependencies->size()) {
        auto n = m_dependencies->size();
        auto a = Array::make(n);
        for (int i = 0; i < n; i++) a->set(i, m_dependencies->at(i)->result());
        m_settler->reject(a);
      }
      break;
    case RACE:
      if (dep->state() == RESOLVED) {
        m_settler->resolve(dep->result());
      } else {
        m_settler->reject(dep->result());
      }
      break;
  }
}

//
// Promise::Aggregator::Dependency
//

void Promise::Aggregator::Dependency::init() {
  Promise::WeakPtr::Watcher::watch(m_promise->weak_ptr());
  m_state = m_promise->m_state;
  m_result = m_promise->m_result;
  m_promise->then(nullptr, Callback::resolved(), Callback::rejected());
}

void Promise::Aggregator::Dependency::on_resolved(const Value &value) {
  m_state = RESOLVED;
  m_result = value;
  m_aggregator->settle(this);
}

void Promise::Aggregator::Dependency::on_rejected(const Value &error) {
  m_state = REJECTED;
  m_result = error;
  m_aggregator->settle(this);
}

void Promise::Aggregator::Dependency::on_weak_ptr_gone() {
  retain();
  m_aggregator = nullptr;
  release();
}

template<> void ClassDef<Promise::Aggregator::Dependency>::init() {
  super<Promise::Callback>();
}

//
// Array
//

template<> void ClassDef<Array>::init() {
  ctor([](Context &ctx) -> Object* {
    int size = 0;
    if (!ctx.arguments(0, &size)) return nullptr;
    if (size < 0) {
      ctx.error("invalid array length");
      return nullptr;
    }
    auto a = Array::make();
    auto d = a->elements();
    for (int i = 0; i < d->size(); i++) {
      d->at(i) = Value::empty;
    }
    a->length(size);
    return a;
  });

  geti([](Object *obj, int i, Value &val) {
    obj->as<Array>()->get(i, val);
  });

  seti([](Object *obj, int i, const Value &val) {
    if (val.is_empty()) {
      obj->as<Array>()->clear(i);
    } else {
      obj->as<Array>()->set(i, val);
    }
  });

  accessor("length",
    [](Object *obj, Value &val) { val.set(int(obj->as<Array>()->length())); },
    [](Object *obj, const Value &val) { obj->as<Array>()->length(val.to_number()); }
  );

  method("concat", [](Context &ctx, Object *obj, Value &ret) {
    auto a = obj->as<Array>();
    int size = a->length(), p = size, n = ctx.argc();
    Array *arrays[n];
    for (int i = 0; i < n; i++) {
      const auto &arg = ctx.arg(i);
      if (arg.is_array()) {
        auto a = arg.as<Array>();
        arrays[i] = a;
        size += a->length();
      } else {
        arrays[i] = nullptr;
        size++;
      }
    }
    auto all = Array::make(size);
    a->iterate_all(
      [&](Value &v, int i) {
        all->set(i, v);
      }
    );
    for (int i = 0; i < n; i++) {
      if (auto a = arrays[i]) {
        a->iterate_all(
          [&](Value &v, int i) {
            all->set(p + i, v);
          }
        );
        p += a->length();
      } else {
        all->set(p++, ctx.arg(i));
      }
    }
    ret.set(all);
  });

  method("copyWithin", [](Context &ctx, Object *obj, Value &ret) {
    auto a = obj->as<Array>();
    int target;
    int start = 0;
    int end = a->length();
    if (!ctx.arguments(1, &target, &start, &end)) return;
    a->copyWithin(target, start, end);
    ret.set(a);
  });

  method("every", [](Context &ctx, Object *obj, Value &ret) {
    Function *callback;
    if (!ctx.arguments(1, &callback)) return;
    bool found = false;
    obj->as<Array>()->iterate_while(
      [&](Value &v, int i) -> bool {
        Value argv[3], ret;
        argv[0] = v;
        argv[1].set(i);
        argv[2].set(obj);
        (*callback)(ctx, 3, argv, ret);
        if (!ctx.ok()) return false;
        if (!ret.to_boolean()) {
          found = true;
          return false;
        }
        return true;
      }
    );
    ret.set(!found);
  });

  method("fill", [](Context &ctx, Object *obj, Value &ret) {
    Value v;
    int start = 0, end;
    if (ctx.try_arguments(3, &v, &start, &end)) {
      obj->as<Array>()->fill(v, start, end);
      ret.set(obj);
    } else if (ctx.arguments(0, &v, &start)) {
      obj->as<Array>()->fill(v, start);
      ret.set(obj);
    }
  });

  method("filter", [](Context &ctx, Object *obj, Value &ret) {
    Function *callback;
    if (!ctx.arguments(1, &callback)) return;
    auto a = Array::make();
    ret.set(a);
    obj->as<Array>()->iterate_while([&](Value &v, int i) -> bool {
      Value argv[3], ret;
      argv[0] = v;
      argv[1].set(i);
      argv[2].set(obj);
      (*callback)(ctx, 3, argv, ret);
      if (!ctx.ok()) return false;
      if (ret.to_boolean()) a->push(v);
      return true;
    });
  });

  method("find", [](Context &ctx, Object *obj, Value &ret) {
    Function *callback;
    if (!ctx.arguments(1, &callback)) return;
    obj->as<Array>()->find([&](Value &v, int i) -> bool {
      Value argv[3], ret;
      argv[0] = v;
      argv[1].set(i);
      argv[2].set(obj);
      (*callback)(ctx, 3, argv, ret);
      if (!ctx.ok()) return true;
      return ret.to_boolean();
    }, ret);
  });

  method("findIndex", [](Context &ctx, Object *obj, Value &ret) {
    Function *callback;
    if (!ctx.arguments(1, &callback)) return;
    ret.set(obj->as<Array>()->findIndex([&](Value &v, int i) -> bool {
      Value argv[3], ret;
      argv[0] = v;
      argv[1].set(i);
      argv[2].set(obj);
      (*callback)(ctx, 3, argv, ret);
      if (!ctx.ok()) return true;
      return ret.to_boolean();
    }));
  });

  method("flat", [](Context &ctx, Object *obj, Value &ret) {
    int depth = 1;
    if (!ctx.arguments(0, &depth)) return;
    ret.set(obj->as<Array>()->flat(depth));
  });

  method("flatMap", [](Context &ctx, Object *obj, Value &ret) {
    Function *f;
    if (!ctx.arguments(1, &f)) return;
    ret.set(obj->as<Array>()->flatMap([&](Value &v, int i, Value &ret) -> bool {
      Value argv[3];
      argv[0] = v;
      argv[1].set(i);
      argv[2].set(obj);
      (*f)(ctx, 3, argv, ret);
      return ctx.ok();
    }));
  });

  method("forEach", [](Context &ctx, Object *obj, Value &ret) {
    Function *callback;
    if (!ctx.arguments(1, &callback)) return;
    obj->as<Array>()->forEach([&](Value &v, int i) -> bool {
      Value argv[3], ret;
      argv[0] = v;
      argv[1].set(i);
      argv[2].set(obj);
      (*callback)(ctx, 3, argv, ret);
      return ctx.ok();
    });
  });

  method("includes", [](Context &ctx, Object *obj, Value &ret) {
    Value value;
    int start = 0;
    if (!ctx.arguments(1, &value, &start)) return;
    ret.set(obj->as<Array>()->indexOf(value, start) >= 0);
  });

  method("indexOf", [](Context &ctx, Object *obj, Value &ret) {
    Value value;
    int start = 0;
    if (!ctx.arguments(1, &value, &start)) return;
    ret.set(obj->as<Array>()->indexOf(value, start));
  });

  method("join", [](Context &ctx, Object *obj, Value &ret) {
    Str *separator = nullptr;
    if (!ctx.arguments(0, &separator)) return;
    ret.set(obj->as<Array>()->join(separator));
  });

  method("lastIndexOf", [](Context &ctx, Object *obj, Value &ret) {
    Value value;
    int start = 0;
    if (!ctx.arguments(1, &value, &start)) return;
    ret.set(obj->as<Array>()->lastIndexOf(value, start));
  });

  method("map", [](Context &ctx, Object *obj, Value &ret) {
    Function *f;
    if (!ctx.arguments(1, &f)) return;
    ret.set(obj->as<Array>()->map(
      [&](Value &v, int i, Value &ret) -> bool {
        Value argv[3];
        argv[0] = v;
        argv[1].set(i);
        argv[2].set(obj);
        (*f)(ctx, 3, argv, ret);
        return ctx.ok();
      }
    ));
  });

  method("pop", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Array>()->pop(ret);
  });

  method("push", [](Context &ctx, Object *obj, Value &ret) {
    auto a = obj->as<Array>();
    for (int i = 0, n = ctx.argc(); i < n; i++) a->push(ctx.arg(i));
    ret.set(int(a->length()));
  });

  method("reduce", [](Context &ctx, Object *obj, Value &ret) {
    Function *callback;
    Value initial, argv[4];
    if (ctx.argc() > 1) {
      if (!ctx.arguments(2, &callback, &initial)) return;
      obj->as<Array>()->reduce([&](Value &ret, Value &v, int i) -> bool {
        argv[0] = ret;
        argv[1] = v;
        argv[2].set(i);
        argv[3].set(obj);
        (*callback)(ctx, 4, argv, ret);
        return ctx.ok();
      }, initial, ret);
    } else {
      if (!ctx.arguments(1, &callback)) return;
      obj->as<Array>()->reduce([&](Value &ret, Value &v, int i) -> bool {
        argv[0] = ret;
        argv[1] = v;
        argv[2].set(i);
        argv[3].set(obj);
        (*callback)(ctx, 4, argv, ret);
        return ctx.ok();
      }, ret);
    }
  });

  method("reduceRight", [](Context &ctx, Object *obj, Value &ret) {
    Function *callback;
    Value initial, argv[4];
    if (ctx.argc() > 1) {
      if (!ctx.arguments(2, &callback, &initial)) return;
      obj->as<Array>()->reduceRight([&](Value &ret, Value &v, int i) -> bool {
        argv[0] = ret;
        argv[1] = v;
        argv[2].set(i);
        argv[3].set(obj);
        (*callback)(ctx, 4, argv, ret);
        return ctx.ok();
      }, initial, ret);
    } else {
      if (!ctx.arguments(1, &callback)) return;
      obj->as<Array>()->reduceRight([&](Value &ret, Value &v, int i) -> bool {
        argv[0] = ret;
        argv[1] = v;
        argv[2].set(i);
        argv[3].set(obj);
        (*callback)(ctx, 4, argv, ret);
        return ctx.ok();
      }, ret);
    }
  });

  method("reverse", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<Array>()->reverse());
  });

  method("shift", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Array>()->shift(ret);
  });

  method("slice", [](Context &ctx, Object *obj, Value &ret) {
    int start = 0;
    int end = obj->as<Array>()->length();
    if (!ctx.arguments(0, &start, &end)) return;
    ret.set(obj->as<Array>()->slice(start, end));
  });

  method("some", [](Context &ctx, Object *obj, Value &ret) {
    Function *callback;
    if (!ctx.arguments(1, &callback)) return;
    bool found = false;
    obj->as<Array>()->iterate_while(
      [&](Value &v, int i) -> bool {
        Value argv[3], ret;
        argv[0] = v;
        argv[1].set(i);
        argv[2].set(obj);
        (*callback)(ctx, 3, argv, ret);
        if (!ctx.ok()) return false;
        if (ret.to_boolean()) {
          found = true;
          return false;
        }
        return true;
      }
    );
    ret.set(found);
  });

  method("sort", [](Context &ctx, Object *obj, Value &ret) {
    Function *comparator = nullptr;
    if (!ctx.arguments(0, &comparator)) return;
    if (comparator) {
      bool has_error = false;
      obj->as<Array>()->sort(
        [&](const Value &a, const Value &b) -> bool {
          if (has_error) return false;
          if (&a == &b) return false;
          if (b.is_empty() || b.is_undefined()) return true;
          if (a.is_empty() || a.is_undefined()) return false;
          Value argv[2], ret;
          argv[0] = a;
          argv[1] = b;
          (*comparator)(ctx, 2, argv, ret);
          if (!ctx.ok()) has_error = true;
          return ret.is_number() && ret.n() <= 0;
        }
      );
    } else {
      obj->as<Array>()->sort();
    }
    ret.set(obj);
  });

  method("splice", [](Context &ctx, Object *obj, Value &ret) {
    auto a = obj->as<Array>();
    int start, delete_count = a->length();
    if (!ctx.arguments(1, &start, &delete_count)) return;
    int n = ctx.argc() - 2;
    if (n > 0) {
      ret.set(a->splice(start, delete_count, &ctx.arg(2), n));
    } else {
      ret.set(a->splice(start, delete_count, nullptr, 0));
    }
  });

  method("unshift", [](Context &ctx, Object *obj, Value &ret) {
    auto a = obj->as<Array>();
    a->unshift(&ctx.arg(0), ctx.argc());
    ret.set(int(a->length()));
  });
}

template<> void ClassDef<Constructor<Array>>::init() {
  super<Function>();
  ctor();
}

void Array::copyWithin(int target, int start, int end) {
  auto n = length();
  if (target < 0) target = n + target;
  if (target < 0) target = 0;
  if (start < 0) start = n + start;
  if (start < 0) start = 0;
  if (end < 0) end = n + end;
  if (end < 0) end = 0;
  if (target >= n || start >= n || target == start) return;
  Value v;
  auto off = target - start;
  if (end + off > n) end = n - off;
  if (target < start) {
    for (int i = start; i < end; i++) {
      get(i, v);
      if (!v.is_empty()) set(i + off, v);
    }
  } else {
    for (int i = end - 1; i >= start; i--) {
      get(i, v);
      if (!v.is_empty()) set(i + off, v);
    }
  }
}

void Array::fill(const Value &v, int start) {
  if (start < 0) start = m_size + start;
  if (start < 0) start = 0;
  for (int i = m_size - 1; i >= start; i--) set(i, v);
}

void Array::fill(const Value &v, int start, int end) {
  if (start < 0) start = m_size + start;
  if (start < 0) start = 0;
  if (end < 0) end = m_size + end;
  if (end < 0) end = 0;
  for (int i = end - 1; i >= start; i--) set(i, v);
}

auto Array::filter(std::function<bool(Value&, int)> callback) -> Array* {
  auto out = make();
  iterate_all([&](Value &v, int i) {
    auto ret = callback(v, i);
    if (ret) out->push(v);
  });
  return out;
}

void Array::find(std::function<bool(Value&, int)> callback, Value &result) {
  result = Value::undefined;
  iterate_while([&](Value &v, int i) -> bool {
    auto ret = callback(v, i);
    if (ret) result = v;
    return !ret;
  });
}

auto Array::findIndex(std::function<bool(Value&, int)> callback) -> int {
  int found = -1;
  iterate_while([&](Value &v, int i) -> bool {
    auto ret = callback(v, i);
    if (ret) found = i;
    return !ret;
  });
  return found;
}

auto Array::flat(int depth) -> Array* {
  auto out = make();
  std::function<void(Value&, int)> expand;
  expand = [&](Value &v, int d) {
    if (v.is_array() && d <= depth) {
      v.as<Array>()->iterate_all([&](Value &v, int) {
        expand(v, d + 1);
      });
    } else {
      out->push(v);
    }
  };
  iterate_all([&](Value &v, int) {
    expand(v, 1);
  });
  return out;
}

auto Array::flatMap(std::function<bool(Value&, int, Value&)> callback) -> Array* {
  auto out = make();
  iterate_while([&](Value &v, int i) -> bool {
    Value ret;
    if (!callback(v, i, ret)) return false;
    if (ret.is_array()) {
      ret.as<Array>()->iterate_all([&](Value &v, int) {
        out->push(v);
      });
    } else {
      out->push(ret);
    }
    return true;
  });
  return out;
}

void Array::forEach(std::function<bool(Value&, int)> callback) {
  iterate_while(callback);
}

auto Array::indexOf(const Value &value, int start) -> int {
  auto n = std::min(m_size, (int)m_data->size());
  if (start < 0) start = m_size + start;
  if (start < 0) start = 0;
  auto values = m_data->elements();
  for (int i = start; i < n; i++) {
    const auto &v = values[i];
    if (!v.is_empty() && Value::is_identical(v, value)) return i;
  }
  return -1;
}

auto Array::join(Str *separator) -> Str* {
  std::string str;
  bool first = true;
  iterate_all([&](Value &v, int i) {
    if (first) first = false; else {
      str += separator ? separator->str() : ",";
    }
    auto s = v.to_string();
    str += s->str();
    s->release();
  });
  return Str::make(str);
}

auto Array::lastIndexOf(const Value &value, int start) -> int {
  auto n = std::min(m_size, (int)m_data->size()) - 1;
  if (start < 0) start = m_size + start;
  if (start > n) start = n;
  auto values = m_data->elements();
  for (int i = start; i >= 0; i--) {
    const auto &v = values[i];
    if (!v.is_empty() && Value::is_identical(v, value)) return i;
  }
  return -1;
}

auto Array::map(std::function<bool(Value&, int, Value&)> callback) -> Array* {
  auto out = make(length());
  iterate_while([&](Value &v, int i) -> bool {
    Value ret;
    if (!callback(v, i, ret)) return false;
    out->set(i, ret);
    return true;
  });
  return out;
}

void Array::pop(Value &result) {
  if (m_size > 0) {
    auto i = m_size - 1;
    get(i, result);
    clear(i);
    m_size = i;
  } else {
    result = Value::undefined;
  }
}

void Array::reduce(std::function<bool(Value&, Value&, int)> callback, Value &result) {
  bool first = true;
  iterate_while([&](Value &v, int i) -> bool {
    if (first) {
      result = v;
      first = false;
      return true;
    } else {
      return callback(result, v, i);
    }
  });
}

void Array::reduce(std::function<bool(Value&, Value&, int)> callback, Value &initial, Value &result) {
  result = initial;
  iterate_while([&](Value &v, int i) -> bool {
    return callback(result, v, i);
  });
}

void Array::reduceRight(std::function<bool(Value&, Value&, int)> callback, Value &result) {
  bool first = true;
  iterate_backward_while([&](Value &v, int i) -> bool {
    if (first) {
      result = v;
      first = false;
      return true;
    } else {
      return callback(result, v, i);
    }
  });
}

void Array::reduceRight(std::function<bool(Value&, Value&, int)> callback, Value &initial, Value &result) {
  result = initial;
  iterate_backward_while([&](Value &v, int i) -> bool {
    return callback(result, v, i);
  });
}

auto Array::reverse() -> Array* {
  for (int i = 0, n = m_size / 2; i < n; i++) {
    int j = m_size - i - 1;
    if (i != j) {
      Value a, b;
      get(i, a);
      get(j, b);
      set(i, b);
      set(j, a);
    }
  }
  return this;
}

void Array::shift(Value &result) {
  if (m_size > 0) {
    get(0, result);
    if (auto size = m_data->size()) {
      auto *values = m_data->elements();
      values[0].~Value();
      std::memmove(values, values + 1, (size - 1) * sizeof(Value));
      new (values + (size - 1)) Value(Value::empty);
    }
    m_size--;
  } else {
    result = Value::undefined;
  }
}

auto Array::slice(int start, int end) -> Array* {
  if (start < 0) start = m_size - start;
  if (end < 0) end = m_size - end;
  if (start < 0) start = 0;
  if (end > m_size) end = m_size;
  int n = end - start;
  if (n <= 0) return Array::make();
  auto a = Array::make(n);
  for (int i = 0; i < n; i++) {
    Value v;
    get(start + i, v);
    a->set(i, v);
  }
  return a;
}

void Array::sort() {
  auto size = std::min(m_size, int(m_data->size()));
  std::sort(
    m_data->elements(),
    m_data->elements() + size,
    [](const Value &a, const Value &b) -> bool {
      if (b.is_empty() || b.is_undefined()) return true;
      if (a.is_empty() || a.is_undefined()) return false;
      auto sa = a.to_string();
      auto sb = b.to_string();
      auto less = sa->str() < sb->str();
      sa->release();
      sb->release();
      return less;
    }
  );
}

void Array::sort(const std::function<bool(const Value&, const Value&)> &comparator) {
  // TODO: Re-write sorting algorithm to tolerate unstable comparators
  auto size = std::min(m_size, int(m_data->size()));
  std::sort(
    m_data->elements(),
    m_data->elements() + size,
    comparator
  );
}

auto Array::splice(int start, int delete_count, const Value *values, int count) -> Array* {
  if (start < 0) start = m_size + start;
  if (start < 0) start = 0;
  if (start + delete_count > m_size) delete_count = m_size - start;
  if (delete_count < 0) delete_count = 0;

  Value *old_values = m_data->elements();
  Value *new_values = old_values;

  auto ret = Array::make(delete_count);
  for (int i = 0; i < delete_count; i++) {
    auto &v = old_values[start + i];
    if (!v.is_empty()) ret->set(i, v);
  }

  if (delete_count != count) {
    int n = std::min(m_size, (int)m_data->size());
    if (delete_count > count) {
      auto max = n - delete_count;
      for (int i = start; i < max; i++) {
        new_values[i + count] = std::move(old_values[i + delete_count]);
      }
    } else {
      Data *new_data = nullptr;
      n += count - delete_count;
      if (n > m_data->size()) {
        auto new_size = 1 << power(n);
        if (new_size > MAX_SIZE) return ret; // TODO: report error
        new_data = Data::make(new_size);
        new_values = new_data->elements();
      }
      auto max = n - count;
      for (int i = max - 1; i >= start; i--) {
        new_values[i + count] = std::move(old_values[i + delete_count]);
      }
      if (new_data) {
        m_data->free();
        m_data = new_data;
      }
    }
  }

  for (int i = 0; i < count; i++) {
    new_values[start + i] = values[i];
  }

  m_size += count;
  m_size -= delete_count;
  return ret;
}

void Array::unshift(const Value *values, int count) {
  if (count > 0) {
    int n = std::min(m_size, (int)m_data->size()) + count;
    Value *old_values = m_data->elements();
    Value *new_values = old_values;
    Data *new_data = nullptr;
    if (n > m_data->size()) {
      auto new_size = 1 << power(n);
      if (new_size > MAX_SIZE) return; // TODO: report error
      new_data = Data::make(new_size);
      new_values = new_data->elements();
    }
    for (int i = n - 1; i >= count; i--) {
      new_values[i] = std::move(old_values[i - count]);
    }
    for (int i = 0; i < count; i++) {
      new_values[i] = values[i];
    }
    if (new_data) {
      m_data->free();
      m_data = new_data;
    }
    m_size += count;
  }
}

//
// RegExp
//

template<> void ClassDef<RegExp>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *pattern, *flags = nullptr;
    if (!ctx.arguments(1, &pattern, &flags)) return nullptr;
    try {
      return RegExp::make(pattern, flags);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("exec", [](Context &ctx, Object *obj, Value &ret) {
    Str *str;
    if (!ctx.arguments(1, &str)) return;
    ret.set(obj->as<RegExp>()->exec(str));
  });

  method("test", [](Context &ctx, Object *obj, Value &ret) {
    Str *str;
    if (!ctx.arguments(1, &str)) return;
    ret.set(obj->as<RegExp>()->test(str));
  });

  accessor("source",      [](Object *obj, Value &ret) { ret.set(obj->as<RegExp>()->source()); });
  accessor("global",      [](Object *obj, Value &ret) { ret.set(obj->as<RegExp>()->global()); });
  accessor("ignoreCase",  [](Object *obj, Value &ret) { ret.set(obj->as<RegExp>()->ignore_case()); });
  accessor("lastIndex",   [](Object *obj, Value &ret) { ret.set(obj->as<RegExp>()->last_index()); });
}

template<> void ClassDef<Constructor<RegExp>>::init() {
  super<Function>();
  ctor();
}

RegExp::RegExp(Str *pattern)
  : m_source(pattern)
  , m_regex(pattern->str(), chars_to_flags(nullptr, m_global))
{
}

RegExp::RegExp(Str *pattern, Str *flags)
  : m_source(pattern)
  , m_regex(pattern->str(), chars_to_flags(flags, m_global))
{
}

auto RegExp::exec(Str *str) -> Array* {
  std::smatch sm;
  auto &match = m_global ? m_match : sm;

  std::regex_search(str->str(), match, m_regex);
  if (match.empty()) return nullptr;

  auto result = Array::make(match.size());
  for (size_t i = 0; i < match.size(); i++) {
    auto &sm = match[i];
    result->set(i, Str::make(sm.str()));
  }

  if (m_global) {
    auto p = match[0].second - str->str().begin();
    m_last_index = str->pos_to_chr(p);
  }

  return result;
}

bool RegExp::test(Str *str) {
  std::smatch match;
  std::regex_search(str->str(), match, m_regex);
  return !match.empty();
}

auto RegExp::chars_to_flags(Str *chars, bool &global) -> std::regex::flag_type {
  std::regex::flag_type flags = std::regex::ECMAScript | std::regex::optimize;
  global = false;

  if (chars) {
    for (auto c : chars->str()) {
      switch (c) {
        case 'i': flags |= std::regex::icase; break;
        case 'g': global = true; break;
        default: throw std::runtime_error(std::string("invalid RegExp flags: ") + chars->str());
      }
    }
  }

  return flags;
}

//
// Utf8Decoder
//

size_t Utf8Decoder::encode(uint32_t code, char *output, size_t size) {
  if (code <= 0x7f) {
    if (size < 1) return 0;
    output[0] = code;
    return 1;
  } else if (code <= 0x7ff) {
    if (size < 2) return 0;
    output[0] = 0xc0 | (0x1f & (code >> 6));
    output[1] = 0x80 | (0x3f & (code >> 0));
    return 2;
  } else if (code <= 0xffff) {
    if (size < 3) return 0;
    output[0] = 0xe0 | (0x0f & (code >> 12));
    output[1] = 0x80 | (0x3f & (code >>  6));
    output[2] = 0x80 | (0x3f & (code >>  0));
    return 3;
  } else {
    if (size < 4) return 0;
    output[0] = 0xf0 | (0x07 & (code >> 18));
    output[1] = 0x80 | (0x3f & (code >> 12));
    output[2] = 0x80 | (0x3f & (code >>  6));
    output[3] = 0x80 | (0x3f & (code >>  0));
    return 4;
  }
}

void Utf8Decoder::reset() {
  m_codepoint = 0;
  m_shift = 0;
}

bool Utf8Decoder::input(char c) {
  if (!m_shift) {
    if (c & 0x80) {
      if ((c & 0xe0) == 0xc0) { m_codepoint = c & 0x1f; m_shift = 1; } else
      if ((c & 0xf0) == 0xe0) { m_codepoint = c & 0x0f; m_shift = 2; } else
      if ((c & 0xf8) == 0xf0) { m_codepoint = c & 0x07; m_shift = 3; } else return false;
    } else {
      m_output(c);
    }
  } else {
    if ((c & 0xc0) != 0x80) {
      return false;
    }
    m_codepoint = (m_codepoint << 6) | (c & 0x3f);
    if (!--m_shift) m_output(m_codepoint);
  }
  return true;
}

bool Utf8Decoder::end() {
  return !m_shift;
}

} // namespace pjs
