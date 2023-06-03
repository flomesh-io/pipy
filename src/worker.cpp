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
#include "worker-thread.hpp"
#include "thread.hpp"
#include "module.hpp"
#include "listener.hpp"
#include "inbound.hpp"
#include "task.hpp"
#include "watch.hpp"
#include "event.hpp"
#include "message.hpp"
#include "pipeline.hpp"
#include "codebase.hpp"
#include "status.hpp"
#include "api/algo.hpp"
#include "api/bgp.hpp"
#include "api/bpf.hpp"
#include "api/configuration.hpp"
#include "api/console.hpp"
#include "api/crypto.hpp"
#include "api/dns.hpp"
#include "api/hessian.hpp"
#include "api/http.hpp"
#include "api/json.hpp"
#include "api/logging.hpp"
#include "api/netmask.hpp"
#include "api/os.hpp"
#include "api/pipy.hpp"
#include "api/protobuf.hpp"
#include "api/resp.hpp"
#include "api/stats.hpp"
#include "api/swap.hpp"
#include "api/timeout.hpp"
#include "api/url.hpp"
#include "api/xml.hpp"
#include "log.hpp"

#include <array>
#include <limits>
#include <stdexcept>

//
// Global
//

namespace pipy {

class Global : public pjs::ObjectTemplate<Global, pjs::Global> {
public:
  auto worker() const -> Worker* { return m_worker; }

private:
  Global(Worker *worker) : m_worker(worker) {}

  Worker* m_worker;

  friend class pjs::ObjectTemplate<Global, pjs::Global>;
};

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<pipy::Global>::init() {
  super<pjs::Global>();

  // JSON
  variable("JSON", class_of<JSON>());

  // XML
  variable("XML", class_of<XML>());

  // Protobuf
  variable("protobuf", class_of<Protobuf>());

  // BGP
  variable("BGP", class_of<BGP>());

  // DNS
  variable("DNS", class_of<DNS>());

  // Hessian
  variable("Hessian", class_of<Hessian>());

  // RESP
  variable("RESP", class_of<RESP>());

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

  // ListenerArray
  variable("ListenerArray", class_of<Constructor<ListenerArray>>());

  // Swap
  variable("Swap", class_of<Constructor<Swap>>());

  // Timeout
  variable("Timeout", class_of<Constructor<Timeout>>());

  // logging
  variable("logging", class_of<logging::Logging>());

  // stats
  variable("stats", class_of<stats::Stats>());

  // http
  variable("http", class_of<http::Http>());

  // crypto
  variable("crypto", class_of<crypto::Crypto>());

  // algo
  variable("algo", class_of<algo::Algo>());

  // bpf
  variable("bpf", class_of<bpf::BPF>());

  // pipy
  variable("pipy", class_of<Pipy>());

  // __thread
  accessor("__thread", [](Object *obj, Value &ret) {
    ret.set(obj->as<pipy::Global>()->worker()->thread());
  });

}

} // namespace pjs

//
// Worker
//

namespace pipy {

thread_local pjs::Ref<Worker> Worker::s_current;

Worker::Worker(bool is_graph_enabled)
  : m_thread(Thread::make(WorkerThread::current()))
  , m_global_object(Global::make(this))
  , m_graph_enabled(is_graph_enabled)
{
  Log::debug(Log::ALLOC, "[worker   %p] ++", this);
}

Worker::~Worker() {
  Log::debug(Log::ALLOC, "[worker   %p] --", this);
}

bool Worker::handling_signal(int signal) {
  for (auto task : m_tasks) {
    if (task->type() == Task::SIGNAL && task->signal() == signal) {
      return true;
    }
  }
  return false;
}

auto Worker::find_js_module(const std::string &path) -> JSModule* {
  auto i = m_module_map.find(path);
  if (i == m_module_map.end()) return nullptr;
  return i->second;
}

auto Worker::load_js_module(const std::string &path) -> JSModule* {
  auto i = m_module_map.find(path);
  if (i != m_module_map.end()) return i->second;
  auto m = new JSModule(this, new_module_index());
  add_module(m);
  m_module_map[path] = m;
  if (!m_root) m_root = m;
  if (!m->load(path)) return nullptr;
  return m;
}

auto Worker::load_native_module(const std::string &path) -> nmi::NativeModule* {
  auto i = m_native_module_map.find(path);
  if (i != m_native_module_map.end()) return i->second;
  auto m = nmi::NativeModule::find(path);
  if (!m) m = nmi::NativeModule::load(path, new_module_index());
  add_module(m);
  m_native_module_map[path] = m;
  return m;
}

void Worker::add_listener(Listener *listener, PipelineLayout *layout, const Listener::Options &options) {
  auto &p = m_listeners[listener];
  p.pipeline_layout = layout;
  p.options = options;
}

void Worker::remove_listener(Listener *listener) {
  m_listeners.erase(listener);
}

bool Worker::update_listeners(bool force) {

  // Open new ports
  std::set<Listener*> new_open;
  for (const auto &i : m_listeners) {
    auto l = i.first;
    if (!l->is_open()) {
      new_open.insert(l);
      l->set_options(i.second.options);
      if (!l->pipeline_layout(i.second.pipeline_layout)) {
        if (force) continue;
        for (auto *l : new_open) {
          l->pipeline_layout(nullptr);
        }
        return false;
      }
    }
  }

  // Update existing ports
  for (const auto &i : m_listeners) {
    auto l = i.first;
    if (!new_open.count(l)) {
      l->set_options(i.second.options);
      l->pipeline_layout(i.second.pipeline_layout);
    }
  }

  // Close old ports
  Listener::for_each(
    [&](Listener *l) {
      if (l->reserved()) return;
      if (m_listeners.find(l) == m_listeners.end()) {
        l->pipeline_layout(nullptr);
      }
    }
  );

  return true;
}

void Worker::add_task(Task *task) {
  m_tasks.insert(task);
}

void Worker::add_watch(Watch *watch) {
  m_watches.insert(watch);
}

void Worker::add_export(pjs::Str *ns, pjs::Str *name, Module *module) {
  auto &names = m_namespaces[ns];
  auto i = names.find(name);
  if (i != names.end()) {
    std::string msg("duplicated variable exporting name ");
    msg += name->str();
    msg += " from ";
    msg += module->filename()->str();
    throw std::runtime_error(msg);
  }
  names[name] = module;
}

auto Worker::get_export(pjs::Str *ns, pjs::Str *name) -> int {
  auto i = m_namespaces.find(ns);
  if (i == m_namespaces.end()) return -1;
  auto j = i->second.find(name);
  if (j == i->second.end()) return -1;
  return j->second->m_index;
}

auto Worker::new_loading_context() -> Context* {
  return Context::make(nullptr, this, m_global_object);
}

auto Worker::new_runtime_context(Context *base) -> Context* {
  auto data = ContextData::make(m_modules.size());
  for (size_t i = 0; i < m_modules.size(); i++) {
    if (auto mod = m_modules[i]) {
      pjs::Object *proto = nullptr;
      if (base) proto = base->data(i);
      data->at(i) = mod->new_context_data(proto);
    }
  }
  return Context::make(base, this, m_global_object, data);
}

bool Worker::solve(pjs::Context &ctx, pjs::Str *filename, pjs::Value &result) {
  auto i = m_solved_files.find(filename);
  if (i != m_solved_files.end()) {
    auto &f = i->second;
    if (f.solving) {
      std::string msg("recursive sovling file: ");
      ctx.error(msg + filename->str());
      return false;
    } else {
      result = f.result;
      return true;
    }
  }

  auto sd = Codebase::current()->get(filename->str());
  if (!sd) {
    std::string msg("Cannot open script to solve: ");
    ctx.error(msg + filename->str());
    return false;
  }

  Data data(*sd);
  sd->release();
  auto &f = m_solved_files[filename];
  f.source.filename = filename->str();
  f.source.content = data.to_string();
  std::string error;
  char error_msg[1000];
  int error_line, error_column;
  auto expr = pjs::Parser::parse(&f.source, error, error_line, error_column);
  if (!expr) {
    std::snprintf(
      error_msg, sizeof(error_msg), "Syntax error: %s at line %d column %d in %s",
      error.c_str(),
      error_line,
      error_column,
      filename->c_str()
    );
    Log::pjs_location(f.source.content, filename->str(), error_line, error_column);
    Log::error("[pjs] %s", error_msg);
    std::snprintf(error_msg, sizeof(error_msg), "Cannot solve script: %s", filename->c_str());
    ctx.error(error_msg);
    m_solved_files.erase(filename);
    return false;
  }

  f.index = m_solved_files.size();
  f.filename = filename;
  f.expr = std::unique_ptr<pjs::Expr>(expr);
  f.solving = true;
  expr->resolve(ctx, -f.index);
  auto ret = expr->eval(ctx, result);
  if (!ctx.ok()) {
    Log::pjs_error(ctx.error());
    std::snprintf(error_msg, sizeof(error_msg), "Cannot solve script: %s", filename->c_str());
    ctx.reset();
    ctx.error(error_msg);
  }
  f.result = result;
  f.solving = false;

  return ret;
}

bool Worker::bind() {
  try {
    for (auto i : m_modules) if (i) i->bind_exports(this);
    for (auto i : m_modules) if (i) i->bind_imports(this);
    for (auto i : m_modules) if (i) i->make_pipelines();
    for (auto i : m_modules) if (i) i->bind_pipelines();
  } catch (std::runtime_error &err) {
    Log::error("%s", err.what());
    return false;
  }
  return true;
}

bool Worker::start(bool force) {

  // Update listening ports
  if (!update_listeners(force)) {
    return false;
  }

  // Start tasks
  for (auto *task : m_tasks) {
    task->start();
  }

  // Start watches
  for (auto *watch : m_watches) {
    watch->start();
  }

  s_current = this;
  return true;
}

void Worker::stop() {
  for (auto *task : m_tasks) task->end();
  for (auto *watch : m_watches) watch->end();
  for (auto *mod : m_modules) if (mod) mod->unload();
  if (s_current == this) s_current = nullptr;
}

auto Worker::new_module_index() -> int {
  int index = 0;
  while (index < m_modules.size() && m_modules[index]) index++;
  return index;
}

void Worker::add_module(Module *m) {
  auto i = m->index();
  if (i >= m_modules.size()) {
    m_modules.resize(i + 1);
  }
  m_modules[i] = m;
}

void Worker::remove_module(int i) {
  auto mod = m_modules[i];
  m_modules[i] = nullptr;
  m_module_map.erase(mod->filename()->str());
}

} // namespace pipy
