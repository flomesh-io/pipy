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

#include "yaml.hpp"
#include "utils.hpp"
#include "yaml.h"

#include <stack>

namespace pjs {

using namespace pipy;

//
// YAML
//

template<> void ClassDef<YAML>::init() {
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
    try {
      YAML::parse(str->str(), rev, ret);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("stringify", [](Context &ctx, Object *obj, Value &ret) {
    Value val;
    Function *replacer = nullptr;
    if (!ctx.arguments(1, &val, &replacer)) return;
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
    ret.set(YAML::stringify(val, rep));
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
    try {
      if (data) {
        YAML::decode(*data, rev, ret);
      } else {
        ret = Value::null;
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Value val;
    Function *replacer = nullptr;
    if (!ctx.arguments(1, &val, &replacer)) return;
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
    YAML::encode(val, rep, *data);
    ret.set(data);
  });
}

} // namespace pjs

namespace pipy {

thread_local static Data::Producer s_dp("YAML");

static int yaml_read(void *ext, unsigned char *buffer, size_t size, size_t *length) {
  Data::Reader *dr = (Data::Reader*)ext;
  *length = dr->read(size, buffer);
  return 1;
}

static void yaml_parse(
  yaml_parser_t &p,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
  pjs::Value &val
) {
  std::stack<pjs::Value> stack;
  std::map<std::string, pjs::Value> anchors;
  pjs::Ref<pjs::Str> key;

  auto scalar = [&](const pjs::Value &v, const yaml_char_t *anchor) {
    if (stack.empty()) {
      val = v;
    } else {
      auto parent = stack.top().o();
      if (parent->is_array()) {
        parent->as<pjs::Array>()->push(v);
      } else if (key) {
        parent->set(key, v);
        key = nullptr;
      } else {
        auto s = v.to_string();
        key = s;
        s->release();
      }
    }
    if (anchor) {
      anchors[(const char *)anchor] = v;
    }
  };

  for (;;) {
    yaml_event_t e;
    if (!yaml_parser_parse(&p, &e)) {
      throw std::runtime_error(p.problem);
    }
    switch (e.type) {
      case YAML_STREAM_START_EVENT:
        break;
      case YAML_STREAM_END_EVENT:
        return;
      case YAML_DOCUMENT_START_EVENT:
        break;
      case YAML_DOCUMENT_END_EVENT:
        break;
      case YAML_ALIAS_EVENT:
        scalar(anchors[(const char *)e.data.alias.anchor], nullptr);
        break;
      case YAML_SCALAR_EVENT:
        if (e.data.scalar.tag) printf("tag: %s\n", e.data.scalar.tag);
        scalar(
          pjs::Str::make((const char *)e.data.scalar.value, e.data.scalar.length),
          e.data.scalar.anchor
        );
        break;
      case YAML_SEQUENCE_START_EVENT: {
        auto array = pjs::Array::make();
        scalar(array, e.data.sequence_start.anchor);
        stack.push(array);
        break;
      }
      case YAML_SEQUENCE_END_EVENT:
        stack.pop();
        break;
      case YAML_MAPPING_START_EVENT: {
        auto object = pjs::Object::make();
        scalar(object, e.data.mapping_start.anchor);
        stack.push(object);
        break;
      }
      case YAML_MAPPING_END_EVENT:
        stack.pop();
        break;
      default: break;
    }
  }
}

void YAML::parse(
  const std::string &str,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
  pjs::Value &val
) {
  yaml_parser_t p;
  yaml_parser_initialize(&p);
  yaml_parser_set_input_string(&p, (const unsigned char *)str.c_str(), str.length());
  yaml_parse(p, reviver, val);
  yaml_parser_delete(&p);
}

auto YAML::stringify(
  const pjs::Value &val,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer
) -> std::string {
  return std::string();
}

void YAML::decode(
  const Data &data,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &reviver,
  pjs::Value &val
) {
  Data::Reader dr(data);
  yaml_parser_t p;
  yaml_parser_initialize(&p);
  yaml_parser_set_input(&p, yaml_read, &dr);
  yaml_parse(p, reviver, val);
  yaml_parser_delete(&p);
}

bool YAML::encode(
  const pjs::Value &val,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer,
  Data &data
) {
  return false;
}

bool YAML::encode(
  const pjs::Value &val,
  const std::function<bool(pjs::Object*, const pjs::Value&, pjs::Value&)> &replacer,
  int space,
  Data::Builder &db
) {
  return false;
}

} // namespace pipy
