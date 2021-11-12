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

#include "pipy.hpp"
#include "codebase.hpp"
#include "configuration.hpp"
#include "module.hpp"
#include "utils.hpp"

extern void main_trigger_reload();

namespace pipy {

void Pipy::operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret) {
  pjs::Value ret_obj;
  pjs::Object *context_prototype = nullptr;
  if (!ctx.arguments(0, &context_prototype)) return;
  if (context_prototype && context_prototype->is_function()) {
    auto *f = context_prototype->as<pjs::Function>();
    (*f)(ctx, 0, nullptr, ret_obj);
    if (!ctx.ok()) return;
    if (!ret_obj.is_object()) {
      ctx.error("function did not return an object");
      return;
    }
    context_prototype = ret_obj.o();
  }
  try {
    auto config = Configuration::make(context_prototype);
    ret.set(config);
  } catch (std::runtime_error &err) {
    ctx.error(err);
  }
}

void Pipy::Script::set(const std::string &path, const std::string &script) {
  Module::set_script(path, script);
}

void Pipy::Script::reset(const std::string &path) {
  Module::reset_script(path);
}

std::map<std::string, std::string> Pipy::Store::s_values;

void Pipy::Store::set(const std::string &key, const std::string &value) {
  s_values[key] = value;
}

bool Pipy::Store::get(const std::string &key, std::string &value) {
  auto i = s_values.find(key);
  if (i == s_values.end()) return false;
  value = i->second;
  return true;
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Pipy>::init() {
  super<Function>();
  ctor();

  variable("script", class_of<Pipy::Script>());
  variable("store", class_of<Pipy::Store>());

  method("load", [](Context &ctx, Object*, Value &ret) {
    std::string filename;
    if (!ctx.arguments(1, &filename)) return;
    auto path = utils::path_normalize(filename);
    ret.set(Codebase::current()->get(path));
  });

  method("restart", [](Context&, Object*, Value&) {
    Worker::restart();
  });

  method("exit", [](Context &ctx, Object*, Value&) {
    int exit_code = 0;
    if (!ctx.arguments(0, &exit_code)) return;
    Worker::exit(exit_code);
  });
}

template<> void ClassDef<Pipy::Script>::init() {
  ctor();

  method("set", [](Context &ctx, Object*, Value&) {
    Str *path;
    Str *content_str = nullptr;
    pipy::Data *content = nullptr;
    if (ctx.try_arguments(2, &path, &content_str)) {
      Pipy::Script::set(path->str(), content_str->str());
    } else if (ctx.arguments(2, &path, &content)) {
      if (!content) {
        ctx.error_argument_type(1, "a Data object");
        return;
      }
      Pipy::Script::set(path->str(), content->to_string());
    }
  });

  method("reset", [](Context &ctx, Object*, Value&) {
    Str *path;
    if (!ctx.arguments(1, &path)) return;
    Pipy::Script::reset(path->str());
  });
}

template<> void ClassDef<Pipy::Store>::init() {
  ctor();

  method("set", [](Context &ctx, Object*, Value&) {
    Str *key;
    Value val;
    if (!ctx.arguments(2, &key, &val)) return;
    auto *val_str = val.to_string();
    Pipy::Store::set(key->str(), val_str->str());
    val_str->release();
  });

  method("get", [](Context &ctx, Object*, Value &ret) {
    Str *key;
    if (!ctx.arguments(1, &key)) return;
    std::string val;
    if (Pipy::Store::get(key->str(), val)) {
      ret.set(val);
    }
  });
}

} // namespace pjs