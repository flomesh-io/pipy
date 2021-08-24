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

#include "worker.hpp"
#include "module.hpp"
#include "task.hpp"
#include "event.hpp"
#include "message.hpp"
#include "session.hpp"
#include "api/algo.hpp"
#include "api/configuration.hpp"
#include "api/console.hpp"
#include "api/crypto.hpp"
#include "api/hessian.hpp"
#include "api/http.hpp"
#include "api/json.hpp"
#include "api/netmask.hpp"
#include "api/os.hpp"
#include "api/pipy.hpp"
#include "api/url.hpp"
#include "api/xml.hpp"
#include "logging.hpp"

#include <array>
#include <stdexcept>

extern void main_trigger_reload();

//
// Global
//

namespace pipy {

class Global : public pjs::ObjectTemplate<Global>
{
};

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Global>::init() {
  ctor();

  // Object
  variable("Object", class_of<Constructor<Object>>());

  // Boolean
  variable("Boolean", class_of<Constructor<Boolean>>());

  // Number
  variable("Number", class_of<Constructor<Number>>());

  // String
  variable("String", class_of<Constructor<String>>());

  // Array
  variable("Array", class_of<Constructor<Array>>());

  // Date
  variable("Date", class_of<Constructor<Date>>());

  // RegExp
  variable("RegExp", class_of<Constructor<RegExp>>());

  // JSON
  variable("JSON", class_of<JSON>());

  // XML
  variable("XML", class_of<XML>());

  // Hessian
  variable("Hessian", class_of<Hessian>());

  // console
  variable("console", class_of<Console>());

  // os
  variable("os", class_of<OS>());

  // URL
  variable("URL", class_of<Constructor<URL>>());

  // Netmask
  variable("Netmask", class_of<Constructor<Netmask>>());

  // Session
  variable("Session", class_of<Constructor<Session>>());

  // Data
  variable("Data", class_of<Constructor<pipy::Data>>());

  // Message
  variable("Message", class_of<Constructor<Message>>());

  // MessageStart
  variable("MessageStart", class_of<Constructor<MessageStart>>());

  // MessageEnd
  variable("MessageEnd", class_of<Constructor<MessageEnd>>());

  // SessionEnd
  variable("SessionEnd", class_of<Constructor<SessionEnd>>());

  // http
  variable("http", class_of<http::Http>());

  // crypto
  variable("crypto", class_of<crypto::Crypto>());

  // algo
  variable("algo", class_of<algo::Algo>());

  // pipy
  variable("pipy", class_of<Pipy>());

  // repeat
  method("repeat", [](Context &ctx, Object *obj, Value &ret) {
    int count;
    Function *f;
    if (ctx.try_arguments(1, &f)) {
      Value idx;
      for (int i = 0;; i++) {
        idx.set(i);
        (*f)(ctx, 1, &idx, ret);
        if (!ctx.ok()) break;
        if (!ret.to_boolean()) break;
      }
    } else if (ctx.try_arguments(2, &count, &f)) {
      Value idx;
      for (int i = 0; i < count; i++) {
        idx.set(i);
        (*f)(ctx, 1, &idx, ret);
        if (!ctx.ok()) break;
        if (!ret.to_boolean()) break;
      }
    } else {
      ctx.error_argument_type(0, "a function");
    }
  });
}

} // namespace pjs

//
// Worker
//

namespace pipy {

Worker* Worker::s_current = nullptr;
std::set<Worker*> Worker::s_all_workers;

Worker::Worker()
  : m_global_object(Global::make())
{
  s_all_workers.insert(this);
}

Worker::~Worker() {
  if (s_current == this) s_current = nullptr;
  s_all_workers.erase(this);
}

auto Worker::get_module(pjs::Str *filename) -> Module* {
  auto i = m_module_name_map.find(filename);
  if (i == m_module_name_map.end()) return nullptr;
  return i->second;
}

auto Worker::find_module(const std::string &path) -> Module* {
  auto i = m_module_map.find(path);
  if (i == m_module_map.end()) return nullptr;
  return i->second;
}

auto Worker::load_module(const std::string &path) -> Module* {
  auto i = m_module_map.find(path);
  if (i != m_module_map.end()) return i->second;
  auto l = m_modules.size();
  auto mod = new Module(this, l);
  m_module_map[path] = mod;
  m_module_name_map[pjs::Str::make(path)] = mod;
  m_modules.push_back(mod);
  if (!m_root) m_root = mod;
  if (!mod->load(path)) return nullptr;
  return mod;
}

void Worker::add_export(pjs::Str *ns, pjs::Str *name, Module *module) {
  auto &names = m_namespaces[ns];
  auto i = names.find(name);
  if (i != names.end()) {
    std::string msg("duplicated variable exporting name ");
    msg += name->str();
    msg += " from ";
    msg += module->path();
    throw std::runtime_error(msg);
  }
  names[name] = module;
}

auto Worker::get_export(pjs::Str *ns, pjs::Str *name) -> Module* {
  auto i = m_namespaces.find(ns);
  if (i == m_namespaces.end()) return nullptr;
  auto j = i->second.find(name);
  if (j == i->second.end()) return nullptr;
  return j->second;
}

auto Worker::new_loading_context() -> Context* {
  return new Context(nullptr, this, m_global_object);
}

auto Worker::new_runtime_context(Context *base) -> Context* {
  auto data = ContextData::make(m_modules.size());
  for (size_t i = 0; i < m_modules.size(); i++) {
    pjs::Object *proto = nullptr;
    if (base) proto = base->m_data->at(i);
    data->at(i) = m_modules[i]->new_context_data(proto);
  }
  auto ctx = new Context(
    base ? base->group() : nullptr,
    this, m_global_object, data
  );
  if (base) ctx->m_inbound = base->m_inbound;
  return ctx;
}

bool Worker::start() {
  try {
    for (auto i : m_modules) i->bind_exports();
    for (auto i : m_modules) i->bind_imports();
    for (auto i : m_modules) i->make_pipelines();
    for (auto i : m_modules) i->bind_pipelines();
    s_current = this;
  } catch (std::runtime_error &err) {
    Log::error("%s", err.what());
    return false;
  }

  for (auto *task : m_tasks) {
    if (!task->start()) {
      return false;
    }
  }

  return true;
}

void Worker::stop() {
  for (auto *task : m_tasks) {
    task->stop();
  }
  m_tasks.clear();
}

} // namespace pipy