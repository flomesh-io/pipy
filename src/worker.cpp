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
#include "listener.hpp"
#include "inbound.hpp"
#include "event.hpp"
#include "message.hpp"
#include "pipeline.hpp"
#include "codebase.hpp"
#include "status.hpp"
#include "api/algo.hpp"
#include "api/bgp.hpp"
#include "api/bpf.hpp"
#include "api/codebase-api.hpp"
#include "api/console.hpp"
#include "api/crypto.hpp"
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

  // Codebase
  variable("Codebase", class_of<Constructor<CodebaseWrapper>>());

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

Worker::Worker(pjs::Promise::Period *period)
  : pjs::Instance(Global::make(this))
  , m_period(period)
  , m_root_fiber(new_fiber())
{
  Log::debug(Log::ALLOC, "[worker   %p] ++", this);
}

Worker::~Worker() {
  Log::debug(Log::ALLOC, "[worker   %p] --", this);
}

auto Worker::new_context(Context *base) -> Context* {
  auto fiber = (base && base->fiber() ? base->fiber()->clone() : m_root_fiber->clone());
  return Context::make(this, fiber, base);
}

auto Worker::load_module(pjs::Module *referer, const std::string &path, pjs::Value &result) -> pjs::Module* {
  std::string name;

  if (path.empty() && !referer) {
    name = path;
  } else if (path[0] == '/') {
    name = utils::path_normalize(path);
  } else if (path[0] == '.' && path[1] == '/') {
    auto base = referer ? utils::path_dirname(referer->name()) : std::string("/");
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

  pjs::Ref<Context> ctx = new_context();
  mod->execute(*ctx, nullptr, result);
  if (!ctx->ok()) {
    Log::pjs_error(ctx->error());
    return nullptr;
  }

  return mod;
}

auto Worker::load_module(pjs::Module *referer, const std::string &path) -> pjs::Module* {
  pjs::Value result;
  return load_module(referer, path, result);
}

auto Worker::load_module(const std::string &path, pjs::Value &result) -> pjs::Module* {
  return load_module(nullptr, path, result);
}

void Worker::add_listener(Listener *listener) {
  m_listeners.insert(listener);
}

void Worker::remove_listener(Listener *listener) {
  m_listeners.erase(listener);
}

void Worker::stop(bool force) {
  if (force || !Pipy::has_exit_callbacks()) {
    stop_all();
  } else if (!m_waiting_for_exit_callbacks) {
    if (Pipy::has_exit_callbacks()) {
      pjs::Ref<Context> ctx = new_context();
      m_waiting_for_exit_callbacks = Pipy::start_exiting(*ctx, [this]() {
        m_waiting_for_exit_callbacks = false;
        stop_all();
      });
    }
    if (!m_waiting_for_exit_callbacks) {
      stop_all();
    }
  }
}

void Worker::stop_all() {
  m_period->end();
  for (auto *pt : m_pipeline_templates) pt->shutdown();
  if (s_current == this) s_current = nullptr;
}

void Worker::append_pipeline_template(PipelineLayout *pt) {
  m_pipeline_templates.insert(pt);
}

void Worker::remove_pipeline_template(PipelineLayout *pt) {
  m_pipeline_templates.erase(pt);
}

} // namespace pipy
