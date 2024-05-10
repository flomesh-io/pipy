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
    Function *reviver = nullptr;
    if (!ctx.arguments(1, &str, &reviver)) return;
    std::function<bool(pjs::Object*, const pjs::Value&, Value&)> rev;
    if (reviver) {
      rev = [&](pjs::Object *obj, const pjs::Value &key, Value &val) -> bool {
        Value args[3];
        args[0] = key;
        args[1] = val;
        args[2].set(obj);
        (*reviver)(ctx, 3, args, val);
        return ctx.ok();
      };
    }
    std::string err;
    if (!JSON::parse(str->str(), rev, ret, err)) {
      ctx.error(err);
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
    Function *reviver = nullptr;
    if (!ctx.arguments(1, &data, &reviver)) return;
    std::function<bool(pjs::Object*, const pjs::Value&, Value&)> rev;
    if (reviver) {
      rev = [&](pjs::Object *obj, const pjs::Value &key, Value &val) -> bool {
        Value args[3];
        args[0] = key;
        args[1] = val;
        args[2].set(obj);
        (*reviver)(ctx, 3, args, val);
        return ctx.ok();
      };
    }
    std::string err;
    if (!data || !JSON::decode(*data, rev, ret, err)) {
      ctx.error(err);
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

static Data::Producer s_dp("JSON");

//
// JSONVisitor
//

class JSONVisitor {
public:
  JSONVisitor(JSON::Visitor *visitor) : m_parser(yajl_alloc(&s_callbacks, nullptr, visitor)) {}
  ~JSONVisitor() { yajl_free(m_parser); }

  bool visit(const std::string &str, std::string &err) {
    if (yajl_status_ok == yajl_parse(m_parser, (const unsigned char*)str.c_str(), str.length()) &&
        yajl_status_ok == yajl_complete_parse(m_parser)
    ) return true;
    get_error(0, err);
    return false;
  }

  bool visit(const Data &data, std::string &err) {
    size_t pos = 0;
    for (const auto c : data.chunks()) {
      auto ptr = std::get<0>(c);
      auto len = std::get<1>(c);
      auto ret = yajl_parse(m_parser, (const unsigned char*)ptr, len);
      if (ret != yajl_status_ok) {
        get_error(pos, err);
        return false;
      }
      pos += len;
    }
    if (yajl_status_ok != yajl_complete_parse(m_parser)) {
      get_error(pos, err);
      return false;
    }
    return true;
  }

private:
  yajl_handle m_parser;

  void get_error(size_t base_position, std::string &err) {
    auto err_str = yajl_get_error(m_parser, false, nullptr, 0);
    char str_buf[1000];
    std::snprintf(
      str_buf, sizeof(str_buf),
      "In JSON at position %d: %s",
      int(base_position + yajl_get_bytes_consumed(m_parser)),
      err_str
    );
    err.assign(str_buf);
    yajl_free_error(m_parser, err_str);
  }

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
  JSONParser(const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver)
    : JSONVisitor(this)
    , m_reviver(reviver) {}

  ~JSONParser() {
    auto *l = m_stack;
    while (l) {
      auto level = l; l = l->back;
      delete level;
    }
  }

  bool parse(const std::string &str, pjs::Value &val, std::string &err) {
    if (!visit(str, err)) return false;
    val = m_root;
    return true;
  }

  bool parse(const Data &data, pjs::Value &val, std::string &err) {
    if (!visit(data, err)) return false;
    val = m_root;
    return true;
  }

private:
  struct Level : public pjs::Pooled<Level> {
    Level* back;
    pjs::Ref<pjs::Object> container;
    pjs::Ref<pjs::Str> key;
  };

  Level* m_stack = nullptr;
  pjs::Value m_root;
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)>& m_reviver;
  bool m_aborted = false;

  void null() { value(pjs::Value::null); }
  void boolean(bool b) { value(b); }
  void integer(int64_t i) { value(double(i)); }
  void number(double n) { value(n); }
  void string(const char *s, size_t len) { value(std::string(s, len)); }

  void map_start() {
    if (!m_aborted) {
      auto l = new Level;
      l->back = m_stack;
      l->container = pjs::Object::make();
      m_stack = l;
    }
  }

  void map_key(const char *s, size_t len) {
    if (!m_aborted) {
      if (auto l = m_stack) {
        l->key = pjs::Str::make(s, len);
      }
    }
  }

  void map_end() {
    if (!m_aborted) {
      if (auto l = m_stack) {
        pjs::Value v(l->container.get());
        m_stack = l->back;
        delete l;
        value(v);
      }
    }
  }

  void array_start() {
    if (!m_aborted) {
      auto l = new Level;
      l->back = m_stack;
      l->container = pjs::Array::make();
      m_stack = l;
    }
  }

  void array_end() {
    map_end();
  }

  void value(const pjs::Value &value) {
    if (!m_aborted) {
      pjs::Value v(value);
      pjs::Value k;
      pjs::Object *obj = m_stack ? m_stack->container.get() : nullptr;

      if (m_reviver) {
        if (obj) {
          if (obj->is<pjs::Array>()) {
            k.set(pjs::Str::make(obj->as<pjs::Array>()->length()));
          } else {
            k.set(m_stack->key.get());
          }
        } else {
          k.set(pjs::Str::empty);
        }
        if (!m_reviver(obj, k, v)) {
          m_aborted = true;
          return;
        }
      }

      if (obj) {
        if (obj->is<pjs::Array>()) {
          obj->as<pjs::Array>()->push(v);
        } else {
          obj->set(m_stack->key, v);
        }
      } else {
        m_root = v;
      }
    }
  }
};

bool JSON::visit(const std::string &str, Visitor *visitor) {
  std::string err;
  JSONVisitor v(visitor);
  return v.visit(str, err);
}

bool JSON::visit(const std::string &str, Visitor *visitor, std::string &err) {
  JSONVisitor v(visitor);
  return v.visit(str, err);
}

bool JSON::visit(const Data &data, Visitor *visitor) {
  std::string err;
  JSONVisitor v(visitor);
  return v.visit(data, err);
}

bool JSON::visit(const Data &data, Visitor *visitor, std::string &err) {
  JSONVisitor v(visitor);
  return v.visit(data, err);
}

bool JSON::parse(
  const std::string &str,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
  pjs::Value &val
) {
  std::string err;
  JSONParser parser(reviver);
  return parser.parse(str, val, err);
}

bool JSON::parse(
  const std::string &str,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
  pjs::Value &val,
  std::string &err
) {
  JSONParser parser(reviver);
  return parser.parse(str, val, err);
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

bool JSON::decode(
  const Data &data,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
  pjs::Value &val
) {
  std::string err;
  JSONParser parser(reviver);
  return parser.parse(data, val, err);
}

bool JSON::decode(
  const Data &data,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
  pjs::Value &val,
  std::string &err
) {
  JSONParser parser(reviver);
  return parser.parse(data, val, err);
}

bool JSON::encode(
  const pjs::Value &val,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer,
  int space,
  Data &data
) {
  Data::Builder db(data, &s_dp);
  auto ret = encode(val, replacer, space, db);
  db.flush();
  return ret;
}

bool JSON::encode(
  const pjs::Value &val,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer,
  int space,
  Data::Builder &db
) {
  static const std::string s_null("null");
  static const std::string s_true("true");
  static const std::string s_false("false");

  if (space < 0) space = 0;
  if (space > 10) space = 10;

  pjs::Value v(val);
  std::function<bool(pjs::Value&, int)> write;

  if (replacer && !replacer(nullptr, pjs::Value::undefined, v)) {
    return false;
  }

  pjs::Object* objs[100];
  int obj_level = 0;

  auto push_indent = [&](int n) {
    for (int i = 0; i < n; i++) {
      db.push(' ');
    }
  };

  write = [&](pjs::Value &v, int l) -> bool {
    if (v.is_undefined() || v.is_null()) {
      db.push(s_null);
    } else if (v.is_boolean()) {
      db.push(v.b() ? s_true : s_false);
    } else if (v.is_number()) {
      auto n = v.n();
      if (std::isnan(n) || std::isinf(n)) {
        db.push(s_null);
      } else {
        char buf[100];
        auto l = pjs::Number::to_string(buf, sizeof(buf), n);
        db.push(buf, l);
      }
    } else if (v.is_string()) {
      db.push('"');
      utils::escape(
        v.s()->str(),
        [&](char c) { db.push(c); }
      );
      db.push('"');
    } else if (v.is_object()) {
      if (obj_level == sizeof(objs) / sizeof(objs[0])) {
        db.push(s_null);
        return true;
      }
      auto o = v.o();
      for (int i = 0; i < obj_level; i++) {
        if (objs[i] == o) {
          db.push(s_null);
          return true;
        }
      }
      objs[obj_level++] = o;
      if (o->is_array()) {
        bool first = true;
        db.push('[');
        if (space) db.push('\n');
        auto a = v.as<pjs::Array>();
        auto n = a->iterate_while([&](pjs::Value &v, int i) -> bool {
          pjs::Value v2(v);
          if (replacer && !replacer(a, i, v2)) return false;
          if (v2.is_undefined() || v2.is_function()) v2 = pjs::Value::null;
          if (first) {
            first = false;
          } else {
            db.push(',');
            if (space) db.push('\n');
          }
          if (space) push_indent(space * l + space);
          return write(v2, l + 1);
        });
        if (n < a->length()) return false;
        if (space) {
          db.push('\n');
          push_indent(space * l);
        }
        db.push(']');
      } else {
        bool first = true;
        db.push('{');
        if (space) db.push('\n');
        auto done = o->iterate_while([&](pjs::Str *k, pjs::Value &v) {
          pjs::Value v2(v);
          if (replacer && !replacer(o, k, v2)) return false;
          if (v2.is_undefined() || v2.is_function()) return true;
          if (first) {
            first = false;
          } else {
            db.push(',');
            if (space) db.push('\n');
          }
          if (space) push_indent(space * l + space);
          db.push('"');
          utils::escape(
            k->str(),
            [&](char c) { db.push(c); }
          );
          db.push('"');
          db.push(':');
          if (space) db.push(' ');
          return write(v2, l + 1);
        });
        if (!done) return false;
        if (space) {
          db.push('\n');
          push_indent(space * l);
        }
        db.push('}');
      }
      obj_level--;
    }
    return true;
  };

  auto ret = write(v, 0);
  return ret;
}

} // namespace pipy
