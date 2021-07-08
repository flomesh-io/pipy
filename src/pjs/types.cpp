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
// Str
//

std::unordered_map<std::string, Str*> Str::s_ht;

const Ref<Str> Str::empty(Str::make(""));
const Ref<Str> Str::nan(Str::make("NaN"));
const Ref<Str> Str::pos_inf(Str::make("Infinity"));
const Ref<Str> Str::neg_inf(Str::make("-Infinity"));
const Ref<Str> Str::undefined(Str::make("undefined"));
const Ref<Str> Str::null(Str::make("null"));
const Ref<Str> Str::bool_true(Str::make("true"));
const Ref<Str> Str::bool_false(Str::make("false"));

auto Str::parse_int() const -> double {
  const auto *p = m_str.c_str();
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
  const auto *p = m_str.c_str();
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

//
// Value
//

const Value Value::empty((Value*)nullptr);
const Value Value::undefined;
const Value Value::null((Object*)nullptr);

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

void Context::backtrace(int line, int column) {
  Location l;
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

std::map<std::string, Class*> Class::m_class_map;

Class::Class(
  const std::string &name,
  Class *super,
  const std::list<Field*> &fields)
  : m_super(super)
  , m_name(name)
{
  if (super) {
    m_field_map = super->m_field_map;
    m_fields = super->m_fields;
  }
  for (const auto f : fields) {
    auto k = f->key();
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
}

Class::~Class() {
  m_class_map.erase(m_name);
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
    for (int i = 1; i < ctx.argc(); i++) {
      auto obj2 = ctx.arg(i).to_object();
      Object::assign(obj, obj2);
      obj2->release();
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
  std::sprintf(s, "[object %s]", m_class->name().c_str());
  return s;
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

  // "toExponential",
  // "toFixed",
  // "toPrecision",
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
  if (std::isinf(m_n)) return m_n > 0 ? Str::pos_inf->str() : Str::neg_inf->str();
  double i;
  if (std::modf(m_n, &i) == 0) return std::to_string(int64_t(i));
  return std::to_string(m_n);
}

//
// String
//

template<> void ClassDef<String>::init() {
  ctor([](Context &ctx) -> Object* {
    return String::make(ctx.argc() > 0 ? ctx.arg(0).to_string() : Str::empty.get());
  });

  accessor("length", [](Object *obj, Value &ret) { ret.set(obj->as<String>()->length()); });

  method("endsWith", [](Context &ctx, Object *obj, Value &ret) {
    Str *search;
    int length = -1;
    if (!ctx.arguments(1, &search, &length)) return;
    ret.set(obj->as<String>()->ends_with(search, length));
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

  method("split", [](Context &ctx, Object *obj, Value &ret) {
    Str *separator = nullptr;
    int limit = -1;
    if (!ctx.arguments(0, &separator, &limit)) return;
    ret.set(obj->as<String>()->split(separator, limit));
  });

  method("startsWith", [](Context &ctx, Object *obj, Value &ret) {
    Str *search;
    int position = 0;
    if (!ctx.arguments(1, &search, &position)) return;
    ret.set(obj->as<String>()->starts_with(search, position));
  });

  method("substring", [](Context &ctx, Object *obj, Value &ret) {
    int start, end;
    if (ctx.try_arguments(2, &start, &end)) {
      ret.set(obj->as<String>()->substring(start, end));
    } else if (ctx.try_arguments(1, &start)) {
      ret.set(obj->as<String>()->substring(start));
    } else {
      ctx.error_argument_type(0, "a number");
    }
  });

  method("toLowerCase", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<String>()->to_lower_case());
  });

  method("toUpperCase", [](Context &ctx, Object *obj, Value &ret) {
    ret.set(obj->as<String>()->to_upper_case());
  });
}

template<> void ClassDef<Constructor<String>>::init() {
  super<Function>();
  ctor();
}

void String::value_of(Value &out) {
  out.set(m_s);
}

auto String::to_string() const -> std::string {
  return m_s->str();
}

bool String::ends_with(Str *search, int length) {
  if (length < 0) length = m_s->length();
  int len = search->length();
  if (len == 0) return true;
  if (len > length) return false;
  // TODO: code point based positions
  return std::strncmp(m_s->c_str() + (length - len), search->c_str(), len) == 0;
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

auto String::split(Str *separator, int limit) -> Array* {
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
      if (!std::strncmp(&str[i], sep.c_str(), sep.length())) {
        arr->push(str.substr(a, i - a));
        if (limit > 0 && arr->length() >= limit) return arr;
        i += sep.length();
        a = i;
      } else {
        i += 1;
      }
    }
    arr->push(str.substr(a, i - a));
    return arr;
  }
}

bool String::starts_with(Str *search, int position) {
  int len = search->length();
  if (len == 0) return true;
  int max_len = m_s->length() - position;
  if (max_len < len) return false;
  // TODO: code point based positions
  return std::strncmp(m_s->c_str() + position, search->c_str(), len) == 0;
}

auto String::substring(int start) -> Str* {
  int max_len = m_s->length();
  if (start >= max_len) return Str::empty;
  if (start < 0) start = 0;
  return Str::make(m_s->str().substr(start, max_len - start));
}

auto String::substring(int start, int end) -> Str* {
  int max_len = m_s->length();
  if (start < 0) start = 0;
  if (end > max_len) end = max_len;
  if (end <= start) return Str::empty;
  return Str::make(m_s->str().substr(start, end - start));
}

auto String::to_lower_case() -> Str* {
  auto s = m_s->str();
  for (auto &c : s) {
    c = std::tolower(c);
  }
  return Str::make(s);
}

auto String::to_upper_case() -> Str* {
  auto s = m_s->str();
  for (auto &c : s) {
    c = std::toupper(c);
  }
  return Str::make(s);
}

//
// Function
//

template<> void ClassDef<Function>::init()
{
}

auto Function::to_string() const -> std::string {
  char s[256];
  std::sprintf(s, "[Function: %s]", m_method->name().c_str());
  return s;
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

  // "concat",
  // "copyWithin",
  // "entries",

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
    } else if (ctx.arguments(1, &v, &start)) {
      obj->as<Array>()->fill(v, start);
      ret.set(obj);
    }
  });

  // "filter",
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

  // "includes",
  // "indexOf",
  // "join",

  method("join", [](Context &ctx, Object *obj, Value &ret) {
    Str *separator = nullptr;
    if (!ctx.arguments(0, &separator)) return;
    ret.set(obj->as<Array>()->join(separator));
  });

  // "keys",
  // "lastIndexOf",
  // "map",

  method("map", [](Context &ctx, Object *obj, Value &ret) {
    Function *f;
    if (!ctx.arguments(1, &f)) return;
    ret.set(obj->as<Array>()->map([&](Value &v, int i, Value &ret) -> bool {
      Value argv[3];
      argv[0] = v;
      argv[1].set(i);
      argv[2].set(obj);
      (*f)(ctx, 3, argv, ret);
      return ctx.ok();
    }));
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

  // "reduceRight",
  // "reverse",

  method("shift", [](Context &ctx, Object *obj, Value &ret) {
    obj->as<Array>()->shift(ret);
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

  // "splice",
  // "unshift",
  // "values",
}

template<> void ClassDef<Constructor<Array>>::init() {
  super<Function>();
  ctor();
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

void Array::sort() {
  auto size = std::min(m_size, int(m_data->size()));
  std::sort(
    m_data->elements(),
    m_data->elements() + size,
    [](const Value &a, const Value &b) -> bool {
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
  auto size = std::min(m_size, int(m_data->size()));
  std::sort(
    m_data->elements(),
    m_data->elements() + size,
    comparator
  );
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
    result->set(i, sm.str());
  }

  if (m_global) {
    m_last_index = match[0].second - str->str().begin();
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

} // namespace pjs