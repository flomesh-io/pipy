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
#include "listener.hpp"
#include "task.hpp"
#include "event.hpp"
#include "message.hpp"
#include "pipeline.hpp"
#include "codebase.hpp"
#include "status.hpp"
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

  // URLSearchParams
  variable("URLSearchParams", class_of<Constructor<URLSearchParams>>());

  // Netmask
  variable("Netmask", class_of<Constructor<Netmask>>());

  // Data
  variable("Data", class_of<Constructor<pipy::Data>>());

  // Message
  variable("Message", class_of<Constructor<Message>>());

  // MessageStart
  variable("MessageStart", class_of<Constructor<MessageStart>>());

  // MessageEnd
  variable("MessageEnd", class_of<Constructor<MessageEnd>>());

  // StreamEnd
  variable("StreamEnd", class_of<Constructor<StreamEnd>>());

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

pjs::Ref<Worker> Worker::s_current;

void Worker::restart() {
  pjs::Ref<Worker> current_worker = current();
  if (!current_worker) {
    Log::error("[restart] No program running");
    return;
  }

  auto codebase = Codebase::current();
  if (!codebase) {
    Log::error("[restart] No codebase");
    return;
  }

  auto &entry = codebase->entry();
  if (entry.empty()) {
    Log::error("[restart] Codebase has no entry point");
    return;
  }

  Log::info("[restart] Reloading codebase...");

  pjs::Ref<Worker> worker = make();
  if (worker->load_module(entry) && worker->start()) {
    current_worker->stop();
    Status::local.version = codebase->version();
    Status::local.update_modules();
    Log::info("[restart] Codebase reloaded");
  } else {
    worker->stop();
    Log::error("[restart] Failed reloading codebase");
  }
}

static bool s_has_exited = false;
static int s_exit_code = 0;

void Worker::exit(int exit_code) {
  static Timer s_timer;
  static bool has_stopped = false;

  if (has_stopped) return;

  if (s_has_exited) {
    Log::info("[shutdown] Forcing to shut down...");
    Net::stop();
    Log::info("[shutdown] Stopped.");
    has_stopped = true;
    return;
  }

  s_has_exited = true;
  s_exit_code = exit_code;

  Log::info("[shutdown] Shutting down...");
  if (auto worker = current()) worker->stop();

  static std::function<void()> check;
  check = []() {
    int n = 0;
    PipelineDef::for_each(
      [&](PipelineDef *def) {
        n += def->active();
      }
    );
    if (n > 0) {
      Log::info("[shutdown] Waiting for remaining %d pipelines...", n);
      s_timer.schedule(1, check);
    } else {
      Net::stop();
      Log::info("Stopped.");
      has_stopped = true;
    }
  };

  check();
}

bool Worker::exited() {
  return s_has_exited;
}

auto Worker::exit_code() -> int {
  return s_exit_code;
}

Worker::Worker()
  : m_global_object(Global::make())
{
  Log::debug("[worker   %p] ++", this);
}

Worker::~Worker() {
  Log::debug("[worker   %p] --", this);
}

bool Worker::handling_signal(int signal) {
  for (auto task : m_tasks) {
    if (task->type() == Task::SIGNAL && task->signal() == signal) {
      return true;
    }
  }
  return false;
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
  m_modules.push_back(mod);
  if (!m_root) m_root = mod;
  if (!mod->load(path)) return nullptr;
  return mod;
}

void Worker::add_listener(Listener *listener, PipelineDef *pipeline_def, const Listener::Options &options) {
  auto &p = m_listeners[listener];
  p.pipeline_def = pipeline_def;
  p.options = options;
}

void Worker::add_task(Task *task) {
  m_tasks.insert(task);
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
    if (auto mod = m_modules[i]) {
      pjs::Object *proto = nullptr;
      if (base) proto = base->m_data->at(i);
      data->at(i) = mod->new_context_data(proto);
    }
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
  } catch (std::runtime_error &err) {
    Log::error("%s", err.what());
    return false;
  }

  // Open new ports
  std::set<Listener*> new_open;
  try {
    for (const auto &i : m_listeners) {
      auto l = i.first;
      if (!l->open()) {
        l->pipeline_def(i.second.pipeline_def);
        l->set_options(i.second.options);
        new_open.insert(l);
      }
    }
  } catch (std::runtime_error &err) {
    for (auto *l : new_open) {
      l->pipeline_def(nullptr);
    }
    Log::error("%s", err.what());
    return false;
  }

  // Update existing ports
  for (const auto &i : m_listeners) {
    auto l = i.first;
    if (!new_open.count(l)) {
      l->pipeline_def(i.second.pipeline_def);
      l->set_options(i.second.options);
    }
  }

  // Close old ports
  Listener::for_each(
    [&](Listener *l) {
      if (l->reserved()) return;
      if (m_listeners.find(l) == m_listeners.end()) {
        l->pipeline_def(nullptr);
      }
    }
  );

  // Start tasks
  for (auto *task : m_tasks) {
    task->start();
  }

  s_current = this;
  return true;
}

void Worker::stop() {
  for (auto *task : m_tasks) delete task;
  for (auto *mod : m_modules) if (mod) mod->unload();
  if (s_current == this) s_current = nullptr;
}

void Worker::remove_module(int i) {
  auto mod = m_modules[i];
  m_modules[i] = nullptr;
  m_module_map.erase(mod->path());
}

} // namespace pipy
