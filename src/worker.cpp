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
#include "pipeline-lb.hpp"
#include "codebase.hpp"
#include "status.hpp"
#include "api/algo.hpp"
#include "api/bgp.hpp"
#include "api/bpf.hpp"
#include "api/configuration.hpp"
#include "api/console.hpp"
#include "api/crypto.hpp"
#include "api/c-string.hpp"
#include "api/c-struct.hpp"
#include "api/dns.hpp"
#include "api/hessian.hpp"
#include "api/http.hpp"
#include "api/ip.hpp"
#include "api/json.hpp"
#include "api/logging.hpp"
#include "api/os.hpp"
#include "api/pipy.hpp"
#include "api/pipeline-api.hpp"
#include "api/print.hpp"
#include "api/protobuf.hpp"
#include "api/resp.hpp"
#include "api/sqlite.hpp"
#include "api/stats.hpp"
#include "api/swap.hpp"
#include "api/timeout.hpp"
#include "api/url.hpp"
#include "api/xml.hpp"
#include "api/yaml.hpp"
#include "api/zlib.hpp"
#include "log.hpp"
#include "utils.hpp"

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

  // CString
  variable("CString", class_of<Constructor<CString>>());

  // CStruct
  variable("CStruct", class_of<Constructor<CStruct>>());

  // CUnion
  variable("CUnion", class_of<Constructor<CUnion>>());

  // JSON
  variable("JSON", class_of<JSON>());

  // YAML
  variable("YAML", class_of<YAML>());

  // XML
  variable("XML", class_of<XML>());

  // zlib
  variable("zlib", class_of<ZLib>());

  // Protobuf
  variable("protobuf", class_of<Protobuf>());

  // IP
  variable("IP", class_of<Constructor<IP>>());

  // IPMask
  variable("IPMask", class_of<Constructor<IPMask>>());

  // IPEndpoint
  variable("IPEndpoint", class_of<Constructor<IPEndpoint>>());

  // Netmask
  variable("Netmask", class_of<Constructor<IPMask>>());

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
  variable("Swap", class_of<Constructor<LegacySwap>>());

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

  // sqlite
  variable("sqlite", class_of<sqlite::Sqlite>());

  // pipy
  variable("pipy", class_of<Pipy>());

  // print
  variable("print", class_of<PrintFunction>());

  // println
  variable("println", class_of<PrintlnFunction>());

  // pipeline
  variable("pipeline", class_of<PipelineLayoutWrapper::Constructor>());

  // __thread
  accessor("__thread", [](Object *obj, Value &ret) {
    ret.set(Thread::current());
  });

}

} // namespace pjs

//
// Worker
//

namespace pipy {

thread_local pjs::Ref<Worker> Worker::s_current;

Worker::Worker(pjs::Promise::Period *period, PipelineLoadBalancer *plb, bool is_graph_enabled)
  : pjs::Instance(Global::make(this))
  , m_period(period)
  , m_root_fiber(new_fiber())
  , m_pipeline_lb(plb)
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
  auto i = m_js_module_map.find(path);
  if (i == m_js_module_map.end()) return nullptr;
  return i->second;
}

auto Worker::load_js_module(const std::string &path) -> JSModule* {
  pjs::Value result;
  return load_js_module(path, result);
}

auto Worker::load_js_module(const std::string &path, pjs::Value &result) -> JSModule* {
  auto i = m_js_module_map.find(path);
  if (i != m_js_module_map.end()) return i->second;
  auto m = new JSModule(this, new_module_index());
  add_module(m);
  m_js_module_map[path] = m;
  if (!m_root) m_root = m;
  if (!m->load(path, result)) return nullptr;
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

auto Worker::load_module(pjs::Module *referer, const std::string &path) -> pjs::Module* {
  std::string name;

  if (path[0] == '/') {
    name = utils::path_normalize(path);
  } else if (path[0] == '.') {
    auto base = utils::path_dirname(referer->name());
    name = utils::path_normalize(utils::path_join(base, path));
  } else {
    return nullptr;
  }

  auto i = m_module_map.find(name);
  if (i != m_module_map.end()) return i->second.get();

  auto sd = Codebase::current()->get(name);
  if (!sd) {
    Log::warn("[pjs] Cannot open script %s", name.c_str());
    return nullptr;
  }

  Data data(*sd);
  auto source = data.to_string();
  sd->release();

  auto mod = new pjs::Module(this);
  m_module_map[name] = std::unique_ptr<pjs::Module>(mod);
  mod->load(name, source);

  std::string error;
  int error_line, error_column;
  if (!mod->compile(error, error_line, error_column)) {
    Log::pjs_location(source, name, error_line, error_column);
    Log::error(
      "[pjs] Syntax error: %s at line %d column %d in %s",
      error.c_str(), error_line, error_column, path.c_str()
    );
    return nullptr;
  }

  mod->resolve(
    [this](pjs::Module *referer, pjs::Str *path) {
      return load_module(referer, path->str());
    }
  );

  pjs::Ref<Context> ctx = new_loading_context();
  pjs::Value result;
  mod->execute(*ctx, -1, nullptr, result);
  if (!ctx->ok()) {
    Log::pjs_error(ctx->error());
    return nullptr;
  }

  return mod;
}

void Worker::add_listener_array(ListenerArray *la) {
  m_listener_arrays.push_back(la);
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
#ifndef __linux__
      if (l->options().transparent) {
        Log::error("Trying to listen on %d in transparent mode, which is not supported on this platform", l->port());
      }
#endif
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
      if (l->is_new_listen()) return true; // TODO: Remove this
      if (l->reserved()) return true;
      if (m_listeners.find(l) == m_listeners.end()) {
        l->pipeline_layout(nullptr);
      }
      return true;
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

void Worker::add_exit(PipelineLayout *layout) {
  m_exits.emplace_back();
  m_exits.back() = new Exit(this, layout);
}

void Worker::add_admin(const std::string &path, PipelineLayout *layout) {
  m_admins.emplace_back();
  m_admins.back() = new Admin(path, layout);
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
  return Context::make(this, m_root_fiber);
}

auto Worker::new_runtime_context(Context *base) -> Context* {
  auto data = ContextData::make(m_legacy_modules.size());
  for (size_t i = 0; i < m_legacy_modules.size(); i++) {
    if (auto mod = m_legacy_modules[i]) {
      pjs::Object *proto = nullptr;
      if (base) proto = base->data(i);
      data->at(i) = mod->new_context_data(proto);
    }
  }
  return Context::make(this, nullptr, base, data);
}

auto Worker::new_context(Context *base) -> Context* {
  auto fiber = (base && base->fiber() ? base->fiber()->clone() : m_root_fiber->clone());
  return Context::make(this, fiber, base);
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
  auto expr = pjs::Parser::parse_expr(&f.source, error, error_line, error_column);
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
  expr->resolve(nullptr, ctx, -f.index);
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
    for (auto i : m_legacy_modules) if (i) i->bind_exports(this);
    for (auto i : m_legacy_modules) if (i) i->bind_imports(this);
    for (auto i : m_legacy_modules) if (i) i->make_pipelines();
    for (auto i : m_legacy_modules) if (i) i->bind_pipelines();
  } catch (std::runtime_error &err) {
    Log::error("%s", err.what());
    return false;
  }
  return true;
}

bool Worker::start(bool force) {
  m_forced = force;

  // Register pipelines to the pipeline load balancer
  for (const auto &p : m_js_module_map) {
    p.second->setup_pipeline_lb(m_pipeline_lb);
  }

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

  m_started = true;
  s_current = this;

  return true;
}

void Worker::stop(bool force) {
  if (force || (!Pipy::has_exit_callbacks() && m_exits.empty())) {
    end_all();
  } else if (!m_exit_signal) {
    m_exit_signal = std::unique_ptr<Signal>(new Signal);
    if (Pipy::has_exit_callbacks()) {
      pjs::Ref<Context> ctx = new_context();
      m_waiting_for_exit_callbacks = Pipy::start_exiting(*ctx, [this]() {
        m_waiting_for_exit_callbacks = false;
        on_exit(nullptr);
      });
    }
    if (m_exits.size() > 0) {
      std::list<Exit*> exits = m_exits;
      for (auto *exit : exits) {
        exit->start();
      }
    } else if (!m_waiting_for_exit_callbacks) {
      m_exit_signal->fire();
      end_all();
    }
  }
}

bool Worker::admin(Message *request, const std::function<void(Message*)> &respond) {
  for (auto *admin : m_admins) {
    if (admin->handle(request, respond)) {
      return true;
    }
  }
  return false;
}

auto Worker::new_module_index() -> int {
  int index = 0;
  while (index < m_legacy_modules.size() && m_legacy_modules[index]) index++;
  return index;
}

void Worker::add_module(Module *m) {
  auto i = m->index();
  if (i >= m_legacy_modules.size()) {
    m_legacy_modules.resize(i + 1);
  }
  m_legacy_modules[i] = m;
}

void Worker::remove_module(int i) {
  auto mod = m_legacy_modules[i];
  m_legacy_modules[i] = nullptr;
  m_js_module_map.erase(mod->filename()->str());
}

void Worker::on_exit(Exit *exit) {
  bool done = true;
  for (auto *exit : m_exits) {
    if (!exit->done()) {
      done = false;
      break;
    }
  }
  if (done && !m_waiting_for_exit_callbacks) {
    if (m_exit_signal) m_exit_signal->fire();
    end_all();
  }
}

void Worker::end_all() {
  m_period->end();
  if (s_current == this) s_current = nullptr;

  for (auto *pt : m_pipeline_templates) pt->shutdown();
  for (auto *task : m_tasks) task->end();
  for (auto *watch : m_watches) watch->end();
  for (auto *exit : m_exits) exit->end();
  for (auto *admin : m_admins) admin->end();

  for (const auto &la : m_listener_arrays) la->close();
  m_listener_arrays.clear();
  m_pipeline_lb = nullptr;

  if (m_pipeline_templates.empty()) {
    for (auto *mod : m_legacy_modules) if (mod) mod->unload();
  } else {
    m_unloading = true;
  }
}

void Worker::append_pipeline_template(PipelineLayout *pt) {
  m_pipeline_templates.insert(pt);
}

void Worker::remove_pipeline_template(PipelineLayout *pt) {
  m_pipeline_templates.erase(pt);
  if (m_pipeline_templates.empty() && m_unloading) {
    m_unloading = false;
    for (auto *mod : m_legacy_modules) if (mod) mod->unload();
  }
}

//
// Worker::Exit
//

void Worker::Exit::start() {
  InputContext ic;
  m_stream_end = false;
  m_pipeline = Pipeline::make(
    m_pipeline_layout,
    m_pipeline_layout->new_context()
  );
  m_pipeline->chain(EventTarget::input());
  m_pipeline->start();
}

void Worker::Exit::end() {
  delete this;
}

void Worker::Exit::on_event(Event *evt) {
  if (evt->is<StreamEnd>()) {
    m_stream_end = true;
    m_worker->on_exit(this);
  }
}

//
// Worker::Admin
//

Worker::Admin::~Admin() {
  while (auto h = m_handlers.head()) {
    delete h;
  }
}

bool Worker::Admin::handle(Message *request, const std::function<void(Message*)> &respond) {
  pjs::Ref<http::RequestHead> head = pjs::coerce<http::RequestHead>(request->head());
  if (!utils::starts_with(head->path->str(), m_path)) return false;
  new Handler(this, request, respond);
  return true;
}

void Worker::Admin::end() {
  delete this;
}

//
// Worker::Admin::Handler
//

Worker::Admin::Handler::Handler(Admin *admin, Message *message, const std::function<void(Message*)> &respond)
  : m_admin(admin)
  , m_respond(respond)
{
  InputContext ic;
  admin->m_handlers.push(this);
  auto pl = admin->m_pipeline_layout.get();
  auto *p = Pipeline::make(pl, pl->new_context());
  m_pipeline = p;
  p->chain(EventTarget::input());
  p->start();
  message->write(p->input());
}

Worker::Admin::Handler::~Handler() {
  m_admin->m_handlers.remove(this);
}

void Worker::Admin::Handler::on_event(Event *evt) {
  if (auto m = m_response_reader.read(evt)) {
    if (m_respond) {
      m_respond(m);
    }
    m->release();
    delete this;
  }
}

} // namespace pipy
