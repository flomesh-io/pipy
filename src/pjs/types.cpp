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
  , m_retain_count(1)
  , m_return_list(nullptr)
  , m_allocated(0)
  , m_pooled(0)
{
  all()[name] = this;
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
  int status;
  auto cxx_name = abi::__cxa_demangle(c_name, 0, 0, &status);
  m_pool = new Pool(cxx_name ? cxx_name : c_name, size);
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
    auto c = codes[i];
    if (c <= 0x7f) {
      if (p + 1 > sizeof(buf)) break;
      buf[p++] = c;
    } else if (c <= 0x7ff) {
      if (p + 2 > sizeof(buf)) break;
      buf[p++] = 0xc0 | (0x1f & (c >> 6));
      buf[p++] = 0x80 | (0x3f & (c >> 0));
    } else if (c <= 0xffff) {
      if (p + 3 > sizeof(buf)) break;
      buf[p++] = 0xe0 | (0x0f & (c >> 12));
      buf[p++] = 0x80 | (0x3f & (c >>  6));
      buf[p++] = 0x80 | (0x3f & (c >>  0));
    } else {
      if (p + 4 > sizeof(buf)) break;
      buf[p++] = 0xf0 | (0x07 & (c >> 18));
      buf[p++] = 0x80 | (0x3f & (c >> 12));
      buf[p++] = 0x80 | (0x3f & (c >>  6));
      buf[p++] = 0x80 | (0x3f & (c >>  0));
    }
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

auto Str::parse_int() const -> double {
  const auto *p = m_char_data->c_str();
  while (*p && std::isblank(*p)) p++;
  if (*p == '-' || *p == '+') {
    auto next = *(p+1);
    if (!std::isdigit(next)) return NAN;
    return std::atoi(p);
  } else if (std::isdigit(*p)) {
    return std::atof(p);
  } else {
    return NAN;
  }
}

auto Str::parse_float() const -> double {
  const auto *p = m_char_data->c_str();
  while (*p && std::isblank(*p)) p++;
  if (*p == '-' || *p == '+') {
    auto next = *(p+1);
    if (next != '.' && !std::isdigit(next)) return NAN;
    return std::atof(p);
  } else if (*p == '.' || std::isdigit(*p)) {
    return std::atof(p);
  } else {
    return NAN;
  }
}

auto Str::substring(int start, int end) -> std::string {
  auto a = chr_to_pos(start);
  auto b = chr_to_pos(end);
  return m_char_data->str().substr(a, b - a);
}

//
// Str::CharData
//

Str::CharData::CharData(std::string &&str)
  : m_str(std::move(str))
  , m_refs(0)
{
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
// Class
//

thread_local std::map<std::string, Class*> Class::m_class_map;
thread_local std::vector<Class::ClassSlot> Class::m_class_slots(1);
thread_local size_t Class::m_class_slot_free = 0;

Class::Class(
  const std::string &name,
  Class *super,
  const std::list<Field*> &fields)
  : m_super(super)
  , m_name(pjs::Str::make(name))
{
  if (super) {
    m_field_map = super->m_field_map;
    m_fields = super->m_fields;
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
  }
  for (auto &p : m_field_map) p.first->retain();
  m_class_map[name] = this;
  if (auto id = m_class_slot_free) {
    m_id = id;
    m_class_slot_free = m_class_slots[id].next_slot;
    m_class_slots[id].class_ptr = this;
  } else {
    m_id = m_class_slots.size();
    m_class_slots.push_back({ this });
  }
}

Class::~Class() {
  m_class_map.erase(m_name->str());
  auto &slot = m_class_slots[m_id];
  slot.class_ptr = nullptr;
  slot.next_slot = m_class_slot_free;
  m_class_slot_free = m_id;
  for (auto &p : m_field_map) {
    p.first->release();
  }
}

//
// Object
//

template<> void ClassDef<Object>::init() {
  method("toString", [](Context &ctx, Object *obj, Value &ret) { ret.set(obj->to_string()); });
  method("valueOf", [](Context &ctx, Object *obj, Value &ret) { obj->value_of(ret); });
  m_c = Class::make("Object", nullptr, m_fields);
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
// Number
//

template<> void ClassDef<Number>::init() {
  ctor([](Context &ctx) -> Object* {
    return Number::make(ctx.argc() > 0 ? ctx.arg(0).to_number() : 0);
  });

  method("toExponential", [](Context &ctx, Object *obj, Value &ret) {
    auto n = obj->as<Number>()->value();
    Value digits;
    if (!ctx.arguments(0, &digits)) return;
    if (digits.is_undefined()) {
      char str[100];
      auto len = Number::to_exponential(str, sizeof(str), n);
      ret.set(Str::make(str, len));
    } else if (digits.is_number()) {
      char str[100];
      auto len = Number::to_exponential(str, sizeof(str), n, digits.n());
      ret.set(Str::make(str, len));
    } else {
      ctx.error_argument_type(0, "a number");
    }
  });

  method("toFixed", [](Context &ctx, Object *obj, Value &ret) {
    auto n = obj->as<Number>()->value();
    int digits = 0;
    if (!ctx.arguments(0, &digits)) return;
    char str[100];
    auto len = Number::to_fixed(str, sizeof(str), n, digits);
    ret.set(Str::make(str, len));
  });

  method("toPrecision", [](Context &ctx, Object *obj, Value &ret) {
    auto n = obj->as<Number>()->value();
    Value digits;
    if (!ctx.arguments(0, &digits)) return;
    if (digits.is_undefined()) {
      char str[100];
      auto len = Number::to_string(str, sizeof(str), n);
      ret.set(Str::make(str, len));
    } else if (digits.is_number()) {
      char str[100];
      auto len = Number::to_precision(str, sizeof(str), n, digits.n());
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

static size_t integral_number_to_string(char *str, size_t len, double n) {
  double i; std::modf(n, &i);
  if (std::modf(n, &i) == 0) {
    return std::snprintf(str, len, "%lld", (long long)i);
  } else {
    return 0;
  }
}

size_t Number::to_string(char *str, size_t len, double n) {
  if (auto l = special_number_to_string(str, len, n)) return l;
  if (auto l = integral_number_to_string(str, len, n)) return l;
  auto max = std::numeric_limits<double>::digits10 + 1;
  len = std::snprintf(str, len, "%.*f", max, n);
  while (len > 1 && str[len-1] == '0') len--;
  if (len > 1 && str[len-1] == '.') len--;
  return len;
}

size_t Number::to_precision(char *str, size_t len, double n, int precision) {
  if (auto l = special_number_to_string(str, len, n)) return l;
  if (auto l = integral_number_to_string(str, len, n)) return l;
  auto max = std::numeric_limits<double>::digits10 + 1;
  if (precision < 0) precision = 0;
  if (precision > max) precision = max;
  return std::snprintf(str, len, "%.*g", precision, n);
}

size_t Number::to_fixed(char *str, size_t len, double n, int digits) {
  if (auto l = special_number_to_string(str, len, n)) return l;
  if (auto l = integral_number_to_string(str, len, n)) return l;
  auto max = std::numeric_limits<double>::digits10 + 1;
  if (digits < 0) digits = 0;
  if (digits > max) digits = max;
  return std::snprintf(str, len, "%.*f", digits, n);
}

size_t Number::to_exponential(char *str, size_t len, double n) {
  if (auto l = special_number_to_string(str, len, n)) return l;
  if (auto l = integral_number_to_string(str, len, n)) return l;
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
  if (auto l = integral_number_to_string(str, len, n)) return l;
  auto max = std::numeric_limits<double>::digits10 + 1;
  if (digits < 0) digits = 0;
  if (digits > max) digits = max;
  return std::snprintf(str, len, "%.*e", digits, n);
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
  if (i < 0) return Str::empty;
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
  if (position >= m_s->length()) return -1;
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
}

template<> void ClassDef<Constructor<Error>>::init() {
  super<Function>();
  ctor();
}

auto Error::name() const -> Str* {
  thread_local static ConstStr s_error("Error");
  return s_error;
}

//
// Array
//

template<> void ClassDef<Array>::init() {
  ctor([](Context &ctx) -> Object* {
    int size;
    if (!ctx.arguments(0, &size)) return nullptr;
    if (size < 0) {
      ctx.error("invalid array length");
      return nullptr;
    }
    return Array::make(size);
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
  for (int i = start; i < m_size; i++) set(i, v);
}

void Array::fill(const Value &v, int start, int end) {
  if (start < 0) start = m_size + start;
  if (start < 0) start = 0;
  if (end < 0) end = m_size + end;
  if (end < 0) end = 0;
  for (int i = start; i < end; i++) set(i, v);
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
  for (int i = 0; i < m_size; i++) {
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
