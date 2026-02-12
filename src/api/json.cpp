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
#include "api/c-string.hpp"

#include "rapidjson/reader.h"
#include "rapidjson/error/en.h"

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
    Object *options = nullptr;
    if (!ctx.arguments(1, &data, &reviver, &options)) return;
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
    if (!data || !JSON::decode(*data, rev, ret, err, options)) {
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

using namespace rapidjson;

// 1. Define adapter: Forward RapidJSON events to JSON::Visitor
struct RapidJsonHandler : public BaseReaderHandler<UTF8<>, RapidJsonHandler> {
  JSON::Visitor *m_visitor;

  RapidJsonHandler(JSON::Visitor *v) : m_visitor(v) {}

  bool Null() { m_visitor->null(); return true; }
  bool Bool(bool b) { m_visitor->boolean(b); return true; }
  bool Int(int i) { m_visitor->integer(i); return true; }
  bool Uint(unsigned u) { m_visitor->integer(u); return true; }
  bool Int64(int64_t i) { m_visitor->integer(i); return true; }
  bool Uint64(uint64_t u) { m_visitor->number((double)u); return true; }
  bool Double(double d) { m_visitor->number(d); return true; }
  
  bool String(const char* str, SizeType length, bool copy) {
    m_visitor->string(str, length);
    return true;
  }
  
  bool StartObject() { m_visitor->map_start(); return true; }
  bool Key(const char* str, SizeType length, bool copy) {
    m_visitor->map_key(str, length);
    return true;
  }
  bool EndObject(SizeType memberCount) { m_visitor->map_end(); return true; }
  
  bool StartArray() { m_visitor->array_start(); return true; }
  bool EndArray(SizeType elementCount) { m_visitor->array_end(); return true; }
};
// 2. Custom input stream for traversing pipy::Data chunks
class PipyDataStream {
public:
  typedef char Ch;

  PipyDataStream(const Data &data) : m_chunks(data.chunks()), m_iter(m_chunks.begin()) {
    if (m_iter != m_chunks.end()) {
      auto chunk = *m_iter;
      m_ptr = std::get<0>(chunk);
      m_end = m_ptr + std::get<1>(chunk);
      // Handle empty chunk cases
      while (m_ptr == m_end) {
        if (++m_iter == m_chunks.end()) break;
        chunk = *m_iter;
        m_ptr = std::get<0>(chunk);
        m_end = m_ptr + std::get<1>(chunk);
      }
    } else {
      m_ptr = m_end = nullptr;
    }
  }

  // Interface required by RapidJSON Stream concept
  Ch Peek() const {
    if (m_ptr == m_end) return '\0';
    return *m_ptr;
  }

  Ch Take() {
    if (m_ptr == m_end) return '\0';
    Ch c = *m_ptr++;
    if (m_ptr == m_end) next_chunk();
    return c;
  }

  size_t Tell() const { return m_count; }

  // Output methods like Put are not needed here
  Ch* PutBegin() { return 0; }
  void Put(Ch) {}
  void Flush() {}
  size_t PutEnd(Ch*) { return 0; }

private:
  void next_chunk() {
    while (true) {
      if (++m_iter == m_chunks.end()) {
        m_ptr = m_end; // Mark end
        return;
      }
      auto chunk = *m_iter;
      auto len = std::get<1>(chunk);
      if (len > 0) {
        m_ptr = std::get<0>(chunk);
        m_end = m_ptr + len;
        return;
      }
    }
  }

  Data::Chunks m_chunks;
  Data::Chunks::Iterator m_iter;
  const char* m_ptr;
  const char* m_end;
  size_t m_count = 0;
};

static Data::Producer s_dp("JSON");

//
// JSONVisitor
//

class JSONVisitor {
public:
  JSONVisitor(JSON::Visitor *visitor) : m_handler(visitor) {}
  ~JSONVisitor() {  }

  bool visit(const std::string &str, std::string &err) {
    StringStream ss(str.c_str());
    return parse(ss, err);
  }

  bool visit(const Data &data, std::string &err) {
    PipyDataStream ds(data);
    return parse(ds, err);
  }

private:
  RapidJsonHandler m_handler;
  Reader m_reader;

  template <typename InputStream>
  bool parse(InputStream &is, std::string &err) {
    // kParseStopWhenDoneFlag ensures parsing stops after completing a JSON object, and reports an error if there is trailing garbage data
    // or use kParseDefaultFlags
    ParseResult result = m_reader.Parse<kParseStopWhenDoneFlag>(is, m_handler);
    
    if (!result) {
      char buf[100];
      std::snprintf(buf, sizeof(buf), 
        "In JSON at position %u: %s", 
        (unsigned)result.Offset(), 
        GetParseError_En(result.Code())
      );
      err = buf;
      return false;
    }
    return true;
  }
};

//
// JSON::DecodeOptions
//

JSON::DecodeOptions::DecodeOptions(pjs::Object *options) {
  Value(options, "maxStringSize")
    .get(max_string_size)
    .check_nullable();
}

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

  void set_max_string_size(int size) { m_max_string_size = size; }

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

  int m_max_string_size = -1;
  Level* m_stack = nullptr;
  pjs::Value m_root;
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)>& m_reviver;
  bool m_aborted = false;

  void null() { value(pjs::Value::null); }
  void boolean(bool b) { value(b); }
  void integer(int64_t i) { value(double(i)); }
  void number(double n) { value(n); }

  void string(const char *s, size_t len) {
    if (m_max_string_size >= 0 && len > m_max_string_size) {
      Data data(s, len, &s_dp);
      value(CString::make(data));
    } else {
      value(std::string(s, len));
    }
  }

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
  pjs::Value &val,
  const DecodeOptions &options
) {
  std::string err;
  JSONParser parser(reviver);
  parser.set_max_string_size(options.max_string_size);
  return parser.parse(data, val, err);
}

bool JSON::decode(
  const Data &data,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
  pjs::Value &val,
  std::string &err,
  const DecodeOptions &options
) {
  JSONParser parser(reviver);
  parser.set_max_string_size(options.max_string_size);
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
    } else if (v.is<CString>()) {
      db.push('"');
      auto data = v.as<CString>()->data();
      for (const auto chk : data->chunks()) {
        utils::escape(
          (const char *)std::get<0>(chk),
          std::get<1>(chk),
          [&](char c) { db.push(c); }
        );
      }
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
