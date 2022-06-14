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

#include "json.hpp"
#include "data.hpp"
#include "utils.hpp"
#include "yajl/yajl_parse.h"

#include <stack>

namespace pjs {

using namespace pipy;

//
// JSON
//

template<> void ClassDef<JSON>::init() {
  ctor();

  method("parse", [](Context &ctx, Object *obj, Value &ret) {
    Str *str;
    if (!ctx.arguments(1, &str)) return;
    if (!JSON::parse(str->str(), ret)) {
      ret = Value::undefined;
    }
  });

  method("stringify", [](Context &ctx, Object *obj, Value &ret) {
    Value val;
    Function *replacer = nullptr;
    int space = 0;
    if (!ctx.arguments(1, &val, &replacer, &space)) return;
    if (val.is_undefined()) {
      ret = Value::undefined;
      return;
    }
    std::function<bool(pjs::Object*, const pjs::Value&, Value&)> rep;
    if (replacer) {
      rep = [&](pjs::Object *obj, const pjs::Value &key, Value &val) -> bool {
        Value args[3];
        args[0] = key;
        args[1] = val;
        args[2].set(obj);
        (*replacer)(ctx, 3, args, val);
        return ctx.ok();
      };
    }
    ret.set(JSON::stringify(val, rep, space));
  });

  method("decode", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    if (!data || !JSON::decode(*data, ret)) {
      ret = Value::undefined;
    }
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Value val;
    Function *replacer = nullptr;
    int space = 0;
    if (!ctx.arguments(1, &val, &replacer, &space)) return;
    if (val.is_undefined()) {
      ret = Value::undefined;
      return;
    }
    std::function<bool(pjs::Object*, const pjs::Value&, Value&)> rep;
    if (replacer) {
      rep = [&](pjs::Object *obj, const pjs::Value &key, Value &val) -> bool {
        Value args[3];
        args[0] = key;
        args[1] = val;
        args[2].set(obj);
        (*replacer)(ctx, 3, args, val);
        return ctx.ok();
      };
    }
    auto *data = pipy::Data::make();
    JSON::encode(val, rep, space, *data);
    ret.set(data);
  });
}

} // namespace pjs

namespace pipy {

//
// JSONVisitor
//

class JSONVisitor {
public:
  JSONVisitor(JSON::Visitor *visitor) : m_parser(yajl_alloc(&s_callbacks, nullptr, visitor)) {}
  ~JSONVisitor() { yajl_free(m_parser); }

  bool visit(const std::string &str) {
    if (yajl_status_ok != yajl_parse(m_parser, (const unsigned char*)str.c_str(), str.length())) return false;
    if (yajl_status_ok != yajl_complete_parse(m_parser)) return false;
    return true;
  }

  bool visit(const Data &data) {
    for (const auto c : data.chunks()) {
      auto ret = yajl_parse(m_parser, (const unsigned char*)std::get<0>(c), std::get<1>(c));
      if (ret != yajl_status_ok) return false;
    }
    if (yajl_status_ok != yajl_complete_parse(m_parser)) return false;
    return true;
  }

private:
  yajl_handle m_parser;

  static yajl_callbacks s_callbacks;

  static int yajl_null(void *ctx) { static_cast<JSON::Visitor*>(ctx)->null(); return 1; }
  static int yajl_boolean(void *ctx, int val) { static_cast<JSON::Visitor*>(ctx)->boolean(val); return 1; }
  static int yajl_integer(void *ctx, long long val) { static_cast<JSON::Visitor*>(ctx)->integer(val); return 1; }
  static int yajl_double(void *ctx, double val) { static_cast<JSON::Visitor*>(ctx)->number(val); return 1; }
  static int yajl_string(void *ctx, const unsigned char *val, size_t len) { static_cast<JSON::Visitor*>(ctx)->string((const char *)val, len); return 1; }
  static int yajl_start_map(void *ctx) { static_cast<JSON::Visitor*>(ctx)->map_start(); return 1; }
  static int yajl_map_key(void *ctx, const unsigned char *key, size_t len) { static_cast<JSON::Visitor*>(ctx)->map_key((const char *)key, len); return 1; }
  static int yajl_end_map(void *ctx) { static_cast<JSON::Visitor*>(ctx)->map_end(); return 1; }
  static int yajl_start_array(void *ctx) { static_cast<JSON::Visitor*>(ctx)->array_start(); return 1; }
  static int yajl_end_array(void *ctx) { static_cast<JSON::Visitor*>(ctx)->array_end(); return 1; }
};

yajl_callbacks JSONVisitor::s_callbacks = {
  &JSONVisitor::yajl_null,
  &JSONVisitor::yajl_boolean,
  &JSONVisitor::yajl_integer,
  &JSONVisitor::yajl_double,
  nullptr,
  &JSONVisitor::yajl_string,
  &JSONVisitor::yajl_start_map,
  &JSONVisitor::yajl_map_key,
  &JSONVisitor::yajl_end_map,
  &JSONVisitor::yajl_start_array,
  &JSONVisitor::yajl_end_array,
};

//
// JSONParser
//

class JSONParser : public JSONVisitor, public JSON::Visitor {
public:
  JSONParser() : JSONVisitor(this) {}

  bool parse(const std::string &str, pjs::Value &val) {
    if (!visit(str)) return false;
    val = m_root;
    return true;
  }

  bool parse(const Data &data, pjs::Value &val) {
    if (!visit(data)) return false;
    val = m_root;
    return true;
  }

private:
  std::stack<pjs::Value> m_stack;
  pjs::Value m_root;
  pjs::Ref<pjs::Str> m_current_key;

  void null() { value(pjs::Value::null); }
  void boolean(bool b) { value(b); }
  void integer(int64_t i) { value(double(i)); }
  void number(double n) { value(n); }
  void string(const char *s, size_t len) { value(std::string(s, len)); }
  void map_start() { pjs::Value v(pjs::Object::make()); value(v); m_stack.push(v); }
  void map_key(const char *s, size_t len) { m_current_key = pjs::Str::make(std::string(s, len)); }
  void map_end() { m_stack.pop(); }
  void array_start() { pjs::Value v(pjs::Array::make()); value(v); m_stack.push(v); }
  void array_end() { m_stack.pop(); }

  void value(const pjs::Value &v) {
    if (m_stack.empty()) {
      m_stack.push(v);
      m_root = v;
    } else {
      auto &top = m_stack.top();
      if (top.is_array()) {
        top.as<pjs::Array>()->push(v);
      } else {
        top.as<pjs::Object>()->ht_set(m_current_key, v);
      }
    }
  }
};

bool JSON::visit(const std::string &str, Visitor *visitor) {
  JSONVisitor v(visitor);
  return v.visit(str);
}

bool JSON::visit(const Data &data, Visitor *visitor) {
  JSONVisitor v(visitor);
  return v.visit(data);
}

bool JSON::parse(const std::string &str, pjs::Value &val) {
  JSONParser parser;
  return parser.parse(str, val);
}

auto JSON::stringify(
  const pjs::Value &val,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer,
  int space
) -> std::string {
  Data data;
  if (!encode(val, replacer, space, data)) return "";
  return data.to_string();
}

bool JSON::decode(const Data &data, pjs::Value &val) {
  JSONParser parser;
  return parser.parse(data, val);
}

bool JSON::encode(
  const pjs::Value &val,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer,
  int space,
  Data &data
) {
  static Data::Producer s_dp("JSON");

  static std::string s_null("null");
  static std::string s_true("true");
  static std::string s_false("false");

  if (space < 0) space = 0;
  if (space > 10) space = 10;

  pjs::Value v(val);
  std::function<bool(pjs::Value&, int)> write;

  if (replacer && !replacer(nullptr, pjs::Value::undefined, v)) {
    return false;
  }

  Data::Builder b(data, &s_dp);
  pjs::Object* objs[100];
  int obj_level = 0;

  auto push_indent = [&](int n) {
    for (int i = 0; i < n; i++) {
      b.push(' ');
    }
  };

  write = [&](pjs::Value &v, int l) -> bool {
    if (v.is_undefined() || v.is_null()) {
      b.push(s_null);
    } else if (v.is_boolean()) {
      b.push(v.b() ? s_true : s_false);
    } else if (v.is_number()) {
      auto n = v.n();
      if (std::isnan(n) || std::isinf(n)) {
        b.push(s_null);
      } else {
        char buf[100];
        auto l = pjs::Number::to_string(buf, sizeof(buf), n);
        b.push(buf, l);
      }
    } else if (v.is_string()) {
      b.push('"');
      utils::escape(
        v.s()->str(),
        [&](char c) { b.push(c); }
      );
      b.push('"');
    } else if (v.is_object()) {
      if (obj_level == sizeof(objs) / sizeof(objs[0])) {
        b.push(s_null);
        return true;
      }
      auto o = v.o();
      for (int i = 0; i < obj_level; i++) {
        if (objs[i] == o) {
          b.push(s_null);
          return true;
        }
      }
      objs[obj_level++] = o;
      if (o->is_array()) {
        bool first = true;
        b.push('[');
        if (space) b.push('\n');
        auto a = v.as<pjs::Array>();
        auto n = a->iterate_while([&](pjs::Value &v, int i) -> bool {
          pjs::Value v2(v);
          if (replacer && !replacer(a, i, v2)) return false;
          if (v2.is_undefined() || v2.is_function()) v2 = pjs::Value::null;
          if (first) {
            first = false;
          } else {
            b.push(',');
            if (space) b.push('\n');
          }
          if (space) push_indent(space * l + space);
          return write(v2, l + 1);
        });
        if (n < a->length()) return false;
        if (space) {
          b.push('\n');
          push_indent(space * l);
        }
        b.push(']');
      } else {
        bool first = true;
        b.push('{');
        if (space) b.push('\n');
        auto done = o->iterate_while([&](pjs::Str *k, pjs::Value &v) {
          pjs::Value v2(v);
          if (replacer && !replacer(o, k, v2)) return false;
          if (v2.is_undefined() || v2.is_function()) return true;
          if (first) {
            first = false;
          } else {
            b.push(',');
            if (space) b.push('\n');
          }
          if (space) push_indent(space * l + space);
          b.push('"');
          utils::escape(
            k->str(),
            [&](char c) { b.push(c); }
          );
          b.push('"');
          b.push(':');
          if (space) b.push(' ');
          return write(v2, l + 1);
        });
        if (!done) return false;
        if (space) {
          b.push('\n');
          push_indent(space * l);
        }
        b.push('}');
      }
      obj_level--;
    }
    return true;
  };

  auto ret = write(v, 0);
  b.flush();
  return ret;
}

} // namespace pipy
