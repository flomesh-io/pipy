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

#include "configuration.hpp"
#include "pipeline.hpp"
#include "module.hpp"
#include "listener.hpp"
#include "reader.hpp"
#include "task.hpp"
#include "context.hpp"
#include "worker.hpp"
#include "graph.hpp"
#include "utils.hpp"
#include "logging.hpp"

// all filters
#include "filters/connect.hpp"
#include "filters/compress-message.hpp"
#include "filters/decompress-message.hpp"
#include "filters/deframe.hpp"
#include "filters/demux.hpp"
#include "filters/deposit-message.hpp"
#include "filters/detect-protocol.hpp"
#include "filters/dubbo.hpp"
#include "filters/dummy.hpp"
#include "filters/dump.hpp"
#include "filters/exec.hpp"
#include "filters/fork.hpp"
#include "filters/http.hpp"
#include "filters/link.hpp"
#include "filters/merge.hpp"
#include "filters/mqtt.hpp"
#include "filters/mux.hpp"
#include "filters/on-body.hpp"
#include "filters/on-event.hpp"
#include "filters/on-message.hpp"
#include "filters/on-start.hpp"
#include "filters/pack.hpp"
#include "filters/print.hpp"
#include "filters/replace-body.hpp"
#include "filters/replace-event.hpp"
#include "filters/replace-message.hpp"
#include "filters/replace-start.hpp"
#include "filters/socks.hpp"
#include "filters/split.hpp"
#include "filters/tee.hpp"
#include "filters/throttle.hpp"
#include "filters/tls.hpp"
#include "filters/use.hpp"
#include "filters/wait.hpp"
#include "filters/websocket.hpp"

#include <stdexcept>
#include <sstream>

namespace pipy {

Configuration::Configuration(pjs::Object *context_prototype)
  : m_context_prototype(context_prototype)
{
  if (!m_context_prototype) {
    m_context_prototype = pjs::Object::make();
  }
}

void Configuration::add_export(pjs::Str *ns, pjs::Object *variables) {
  if (ns->str().empty()) throw std::runtime_error("namespace cannot be empty");
  if (!variables) throw std::runtime_error("variable list cannot be null");
  variables->iterate_all(
    [&](pjs::Str *k, pjs::Value &v) {
      if (k->str().empty()) throw std::runtime_error("variable name cannot be empty");
      m_exports.emplace_back();
      auto &imp = m_exports.back();
      imp.ns = ns;
      imp.name = k;
      imp.value = v;
    }
  );
}

void Configuration::add_import(pjs::Object *variables) {
  if (!variables) throw std::runtime_error("variable list cannot be null");
  variables->iterate_all(
    [&](pjs::Str *k, pjs::Value &v) {
      if (k->str().empty()) throw std::runtime_error("variable name cannot be empty");
      if (v.is_string()) {
        if (v.s()->str().empty()) throw std::runtime_error("namespace cannot be empty");
        m_imports.emplace_back();
        auto &imp = m_imports.back();
        imp.ns = v.s();
        imp.name = k;
        imp.original_name = k;
      } else {
        std::string msg("namespace expected for import: ");
        throw std::runtime_error(msg + k->str());
      }
    }
  );
}

void Configuration::listen(int port, pjs::Object *options) {
  Listener::Options opt;
  if (options) get_listen_options(options, opt);
  m_listens.push_back({ "0.0.0.0", port, opt });
  m_current_filters = &m_listens.back().filters;
}

void Configuration::listen(const std::string &port, pjs::Object *options) {
  std::string addr;
  int port_num;
  if (!utils::get_host_port(port, addr, port_num)) {
    std::string msg("invalid 'ip:port' form: ");
    throw std::runtime_error(msg + port);
  }

  uint8_t ip[16];
  if (!utils::get_ip_v4(addr, ip) && !utils::get_ip_v6(addr, ip)) {
    std::string msg("invalid IP address: ");
    throw std::runtime_error(msg + addr);
  }

  Listener::Options opt;
  if (options) get_listen_options(options, opt);
  m_listens.push_back({ addr, port_num, opt });
  m_current_filters = &m_listens.back().filters;
}

void Configuration::read(const std::string &pathname) {
  m_readers.push_back({ pathname });
  m_current_filters = &m_readers.back().filters;
}

void Configuration::task(const std::string &when) {
  std::string name("Task #");
  name += std::to_string(m_tasks.size() + 1);
  m_tasks.push_back({ name, when });
  m_current_filters = &m_tasks.back().filters;
}

void Configuration::pipeline(const std::string &name) {
  if (name.empty()) throw std::runtime_error("pipeline name cannot be empty");
  m_named_pipelines.push_back({ name });
  m_current_filters = &m_named_pipelines.back().filters;
}

void Configuration::accept_http_tunnel(pjs::Str *target, pjs::Function *handler) {
  auto *filter = new http::TunnelServer(handler);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::accept_socks(pjs::Str *target, pjs::Function *on_connect) {
  auto *filter = new socks::Server(on_connect);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::accept_tls(pjs::Str *target, pjs::Object *options) {
  auto *filter = new tls::Server(options);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::compress_message(pjs::Object *options) {
  append_filter(new CompressMessage(CompressMessageBase::Options::parse(options)));
}

void Configuration::compress_http(pjs::Object *options) {
  append_filter(new CompressHTTP(CompressMessageBase::Options::parse(options)));
}

void Configuration::connect(const pjs::Value &target, pjs::Object *options) {
  append_filter(new Connect(target, options));
}

void Configuration::connect_http_tunnel(pjs::Str *target, const pjs::Value &address) {
  auto *filter = new http::TunnelClient(address);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::connect_socks(pjs::Str *target, const pjs::Value &address) {
  auto *filter = new socks::Client(address);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::connect_tls(pjs::Str *target, pjs::Object *options) {
  auto *filter = new tls::Client(options);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::decode_dubbo() {
  append_filter(new dubbo::Decoder());
}

void Configuration::decode_http_request() {
  append_filter(new http::RequestDecoder());
}

void Configuration::decode_http_response(pjs::Object *options) {
  append_filter(new http::ResponseDecoder(options));
}

void Configuration::decode_mqtt(pjs::Object *options) {
  mqtt::Decoder::Options opts;
  if (options) {
    options->get("protocolLevel", opts.protocol_level);
    if (!opts.protocol_level.is_undefined() &&
        !opts.protocol_level.is_number() &&
        !opts.protocol_level.is_function()
    ) {
      throw std::runtime_error("options.protocolLevel expects a number or a function");
    }
  }
  append_filter(new mqtt::Decoder(opts));
}

void Configuration::decode_websocket() {
  append_filter(new websocket::Decoder());
}

void Configuration::decompress_http(pjs::Function *enable) {
  append_filter(new DecompressHTTP(enable));
}

void Configuration::decompress_message(const pjs::Value &algorithm) {
  append_filter(new DecompressMessage(algorithm));
}

void Configuration::deframe(pjs::Object *states) {
  append_filter(new Deframe(states));
}

void Configuration::demux(pjs::Str *target) {
  auto *filter = new Demux();
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::demux_queue(pjs::Str *target) {
  auto *filter = new DemuxQueue();
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::demux_http(pjs::Str *target, pjs::Object *options) {
  auto *filter = new http::Demux(options);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::deposit_message(const pjs::Value &filename, pjs::Object *options) {
  DepositMessage::Options opts;
  if (options) {
    pjs::Value threshold, keep;
    options->get("threshold", threshold);
    options->get("keep", keep);
    if (!utils::get_byte_size(threshold, opts.threshold)) throw std::runtime_error("options.threshold expects a number or a string");
    if (!keep.is_boolean()) throw std::runtime_error("options.keep expects a boolean");
    opts.keep = keep.b();
  }
  append_filter(new DepositMessage(filename, opts));
}

void Configuration::detect_protocol(pjs::Function *callback) {
  append_filter(new ProtocolDetector(callback));
}

void Configuration::dummy() {
  append_filter(new Dummy());
}

void Configuration::dump(const pjs::Value &tag) {
  append_filter(new Dump(tag));
}

void Configuration::encode_dubbo(pjs::Object *message_obj) {
  append_filter(new dubbo::Encoder(message_obj));
}

void Configuration::encode_http_request(pjs::Object *options) {
  append_filter(new http::RequestEncoder(options));
}

void Configuration::encode_http_response(pjs::Object *response_obj) {
  append_filter(new http::ResponseEncoder(response_obj));
}

void Configuration::encode_mqtt() {
  append_filter(new mqtt::Encoder());
}

void Configuration::encode_websocket() {
  append_filter(new websocket::Encoder());
}

void Configuration::exec(const pjs::Value &command) {
  append_filter(new Exec(command));
}

void Configuration::fetch_http(pjs::Str *layout) {
  auto *filter = new http::Client();
  filter->add_sub_pipeline(layout);
  append_filter(filter);
}

void Configuration::fork(pjs::Str *target, pjs::Object *initializers) {
  auto *filter = new Fork(initializers);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::link(size_t count, pjs::Str **targets, pjs::Function **conditions) {
  auto *filter = new Link();
  for (size_t i = 0; i < count; i++) {
    filter->add_sub_pipeline(targets[i]);
    filter->add_condition(conditions[i]);
  }
  append_filter(filter);
}

void Configuration::merge(pjs::Str *target, const pjs::Value &key, pjs::Object *options) {
  auto *filter = new Merge(key, options);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::mux_queue(pjs::Str *target, const pjs::Value &key, pjs::Object *options) {
  auto *filter = new Mux(key, options);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::mux_http(pjs::Str *target, const pjs::Value &key, pjs::Object *options) {
  auto *filter = new http::Mux(key, options);
  filter->add_sub_pipeline(target);
  append_filter(filter);
}

void Configuration::on_body(pjs::Function *callback, int size_limit) {
  append_filter(new OnBody(callback, size_limit));
}

void Configuration::on_event(Event::Type type, pjs::Function *callback) {
  append_filter(new OnEvent(type, callback));
}

void Configuration::on_message(pjs::Function *callback, int size_limit) {
  append_filter(new OnMessage(callback, size_limit));
}

void Configuration::on_start(pjs::Function *callback) {
  append_filter(new OnStart(callback));
}

void Configuration::on_tls_client_hello(pjs::Function *callback) {
  append_filter(new tls::OnClientHello(callback));
}

void Configuration::pack(int batch_size, pjs::Object *options) {
  append_filter(new Pack(batch_size, options));
}

void Configuration::print() {
  append_filter(new Print());
}

void Configuration::replace_body(const pjs::Value &replacement, int size_limit) {
  append_filter(new ReplaceBody(replacement, size_limit));
}

void Configuration::replace_event(Event::Type type, const pjs::Value &replacement) {
  append_filter(new ReplaceEvent(type, replacement));
}

void Configuration::replace_message(const pjs::Value &replacement, int size_limit) {
  append_filter(new ReplaceMessage(replacement, size_limit));
}

void Configuration::replace_start(const pjs::Value &replacement) {
  append_filter(new ReplaceStart(replacement));
}

void Configuration::serve_http(pjs::Object *handler) {
  append_filter(new http::Server(handler));
}

void Configuration::split(pjs::Function *callback) {
  append_filter(new Split(callback));
}

void Configuration::tee(const pjs::Value &filename) {
  append_filter(new Tee(filename));
}

void Configuration::throttle_concurrency(const pjs::Value &quota, const pjs::Value &account) {
  append_filter(new ThrottleConcurrency(quota, account));
}

void Configuration::throttle_data_rate(const pjs::Value &quota, const pjs::Value &account) {
  append_filter(new ThrottleDataRate(quota, account));
}

void Configuration::throttle_message_rate(const pjs::Value &quota, const pjs::Value &account) {
  append_filter(new ThrottleMessageRate(quota, account));
}

void Configuration::use(Module *module, pjs::Str *pipeline) {
  append_filter(new Use(module, pipeline));
}

void Configuration::use(const std::list<Module*> modules, pjs::Str *pipeline, pjs::Function *when) {
  append_filter(new Use(modules, pipeline, when));
}

void Configuration::use(const std::list<Module*> modules, pjs::Str *pipeline, pjs::Str *pipeline_down, pjs::Function *when) {
  append_filter(new Use(modules, pipeline, pipeline_down, when));
}

void Configuration::wait(pjs::Function *condition, pjs::Object *options) {
  Wait::Options opts;
  if (options) {
    pjs::Value timeout;
    options->get("timeout", timeout);
    if (!utils::get_seconds(timeout, opts.timeout)) throw std::runtime_error("options.timeout expects a number or a string");
  }
  append_filter(new Wait(condition, opts));
}

void Configuration::bind_exports(Worker *worker, Module *module) {
  for (const auto &exp : m_exports) {
    if (m_context_prototype->has(exp.name)) {
      std::string msg("duplicated variable name ");
      msg += exp.name->str();
      throw std::runtime_error(msg);
    }
    m_context_prototype->set(exp.name, exp.value);
    worker->add_export(exp.ns, exp.name, module);
  }
}

void Configuration::bind_imports(Worker *worker, Module *module, pjs::Expr::Imports *imports) {
  for (const auto &imp : m_imports) {
    auto m = worker->get_export(imp.ns, imp.original_name);
    if (!m) {
      std::string msg("cannot import variable ");
      msg += imp.name->str();
      msg += " in ";
      msg += module->path();
      throw std::runtime_error(msg);
    }
    imports->add(imp.name, m->index(), imp.original_name);
  }
}

void Configuration::apply(Module *mod) {
  std::list<pjs::Field*> fields;
  m_context_prototype->iterate_all([&](pjs::Str *key, pjs::Value &val) {
    fields.push_back(
      pjs::Variable::make(
        key->str(), val,
        pjs::Field::Enumerable | pjs::Field::Writable
      )
    );
  });

  m_context_class = pjs::Class::make(
    "ContextData",
    pjs::class_of<ContextDataBase>(),
    fields
  );

  mod->m_context_class = m_context_class;

  auto make_pipeline = [&](
    PipelineDef::Type type,
    const std::string &name,
    std::list<std::unique_ptr<Filter>> &filters
  ) -> PipelineDef*
  {
    auto pipeline_def = PipelineDef::make(mod, type, name);
    for (auto &f : filters) {
      pipeline_def->append(f.release());
    }
    mod->m_pipelines.push_back(pipeline_def);
    return pipeline_def;
  };

  for (auto &i : m_named_pipelines) {
    auto s = pjs::Str::make(i.name);
    auto p = make_pipeline(PipelineDef::NAMED, i.name, i.filters);
    mod->m_named_pipelines[s] = p;
  }

  Worker *worker = mod->worker();

  for (auto &i : m_listens) {
    if (!i.port) continue;
    auto name = std::to_string(i.port) + '@' + i.ip;
    auto p = make_pipeline(PipelineDef::LISTEN, name, i.filters);
    auto listener = Listener::get(i.ip, i.port);
    if (listener->reserved()) {
      std::string msg("Port reserved: ");
      throw std::runtime_error(msg + std::to_string(i.port));
    }
#ifndef __linux__
    if (i.options.transparent) {
      Log::error("Trying to listen on %d in transparent mode, which is not supported on this platform", i.port);
    }
#endif
    worker->add_listener(listener, p, i.options);
  }

  for (auto &i : m_readers) {
    auto p = make_pipeline(PipelineDef::READ, i.pathname, i.filters);
    auto r = Reader::make(i.pathname, p);
    worker->add_reader(r);
  }

  for (auto &i : m_tasks) {
    auto p = make_pipeline(PipelineDef::TASK, i.name, i.filters);
    auto t = Task::make(i.when, p);
    worker->add_task(t);
  }
}

void Configuration::draw(Graph &g) {
  auto add_filters = [](Graph::Pipeline &gp, const std::list<std::unique_ptr<Filter>> &filters) {
    for (const auto &f : filters) {
      Graph::Filter gf;
      std::stringstream ss;
      f->dump(ss);
      gf.name = ss.str();
      gf.fork = (gf.name == "fork" || gf.name == "merge");
      for (int i = 0; i < f->num_sub_pipelines(); i++) {
        gf.links.push_back(f->get_sub_pipeline_name(i));
      }
      gp.filters.emplace_back(std::move(gf));
    }
  };

  for (const auto &i : m_named_pipelines) {
    Graph::Pipeline p;
    p.name = i.name;
    add_filters(p, i.filters);
    g.add_named_pipeline(std::move(p));
  }

  for (const auto &i : m_listens) {
    Graph::Pipeline p;
    p.name = "Listen on ";
    p.name += std::to_string(i.port);
    p.name += " at ";
    p.name += i.ip;
    add_filters(p, i.filters);
    g.add_root_pipeline(std::move(p));
  }

  for (const auto &i : m_readers) {
    Graph::Pipeline p;
    p.name = "Read ";
    p.name += i.pathname;
    add_filters(p, i.filters);
    g.add_root_pipeline(std::move(p));
  }

  for (const auto &i : m_tasks) {
    Graph::Pipeline p;
    p.name = i.name;
    p.name += " (";
    p.name += i.when;
    p.name += ')';
    add_filters(p, i.filters);
    g.add_root_pipeline(std::move(p));
  }
}

void Configuration::get_listen_options(pjs::Object *obj, Listener::Options &opt) {
  pjs::Value max_conn, read_timeout, write_timeout, transparent;
  obj->get("maxConnections", max_conn);
  obj->get("readTimeout", read_timeout);
  obj->get("writeTimeout", write_timeout);
  obj->get("transparent", transparent);

  if (!max_conn.is_undefined()) {
    if (!max_conn.is_number()) throw std::runtime_error("options.maxConnections expects a number");
    opt.max_connections = max_conn.n();
  }

  if (!read_timeout.is_undefined()) {
    if (read_timeout.is_string()) {
      opt.read_timeout = utils::get_seconds(read_timeout.s()->str());
    } else {
      opt.read_timeout = read_timeout.to_number();
    }
  }

  if (!write_timeout.is_undefined()) {
    if (write_timeout.is_string()) {
      opt.write_timeout = utils::get_seconds(write_timeout.s()->str());
    } else {
      opt.write_timeout = write_timeout.to_number();
    }
  }

  if (!transparent.is_undefined()) {
    if (!transparent.is_boolean()) throw std::runtime_error("options.transparent expects a boolean");
    opt.transparent = transparent.b();
  }
}

void Configuration::append_filter(Filter *filter) {
  if (!m_current_filters) {
    delete filter;
    throw std::runtime_error("no pipeline found");
  }
  m_current_filters->emplace_back(filter);
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Configuration>::init() {

  // Configuration.export
  method("export", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Str *ns;
    pjs::Object *variables;
    if (!ctx.arguments(2, &ns, &variables)) return;
    try {
      thiz->as<Configuration>()->add_export(ns, variables);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.import
  method("import", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Object *variables;
    if (!ctx.arguments(1, &variables)) return;
    try {
      thiz->as<Configuration>()->add_import(variables);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.listen
  method("listen", [](Context &ctx, Object *thiz, Value &result) {
    int port;
    Str *port_str;
    pjs::Object *options = nullptr;
    try {
      if (ctx.try_arguments(1, &port_str, &options)) {
        thiz->as<Configuration>()->listen(port_str->str(), options);
        result.set(thiz);
      } else if (ctx.try_arguments(1, &port, &options)) {
        thiz->as<Configuration>()->listen(port, options);
        result.set(thiz);
      } else {
        ctx.error_argument_type(0, "a number of string");
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.pipeline
  method("pipeline", [](Context &ctx, Object *thiz, Value &result) {
    std::string name;
    if (!ctx.arguments(1, &name)) return;
    try {
      thiz->as<Configuration>()->pipeline(name);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.read
  method("read", [](Context &ctx, Object *thiz, Value &result) {
    std::string pathname;
    try {
      if (!ctx.arguments(1, &pathname)) return;
      thiz->as<Configuration>()->read(pathname);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.task
  method("task", [](Context &ctx, Object *thiz, Value &result) {
    std::string when;
    try {
      if (!ctx.arguments(0, &when)) return;
      thiz->as<Configuration>()->task(when);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.acceptHTTPTunnel
  method("acceptHTTPTunnel", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Function *handler;
    if (!ctx.arguments(2, &target, &handler)) return;
    try {
      thiz->as<Configuration>()->accept_http_tunnel(target, handler);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.acceptSOCKS
  method("acceptSOCKS", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Function *on_connect;
    if (!ctx.arguments(2, &target, &on_connect)) return;
    try {
      thiz->as<Configuration>()->accept_socks(target, on_connect);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.acceptTLS
  method("acceptTLS", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Object *options;
    if (!ctx.arguments(2, &target, &options)) return;
    if (!options) {
      ctx.error_argument_type(1, "a non-null object");
      return;
    }
    try {
      thiz->as<Configuration>()->accept_tls(target, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.compressHTTP
  method("compressHTTP", [](Context &ctx, Object *thiz, Value &result) {
      Object *options = nullptr;
      if (!ctx.arguments(0, &options)) return;
      try {
        thiz->as<Configuration>()->compress_http(options);
        result.set(thiz);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
  });

  // Configuration.compressMessage
  method("compressMessage", [](Context &ctx, Object *thiz, Value &result) {
      Object *options = nullptr;
      if (!ctx.arguments(0, &options)) return;
      try {
        thiz->as<Configuration>()->compress_message(options);
        result.set(thiz);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
  });

  // Configuration.connect
  method("connect", [](Context &ctx, Object *thiz, Value &result) {
    Value target;
    Object *options = nullptr;
    if (!ctx.arguments(1, &target, &options)) return;
    try {
      thiz->as<Configuration>()->connect(target, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.connectHTTPTunnel
  method("connectHTTPTunnel", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Value address;
    if (!ctx.arguments(2, &target, &address)) return;
    try {
      thiz->as<Configuration>()->connect_http_tunnel(target, address);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.connectSOCKS
  method("connectSOCKS", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Value address;
    if (!ctx.arguments(2, &target, &address)) return;
    try {
      thiz->as<Configuration>()->connect_socks(target, address);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.connectTLS
  method("connectTLS", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Object *options = nullptr;
    if (!ctx.arguments(1, &target, &options)) return;
    try {
      thiz->as<Configuration>()->connect_tls(target, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.deframe
  method("deframe", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Object *states;
    if (!ctx.arguments(1, &states)) return;
    try {
      thiz->as<Configuration>()->deframe(states);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.demux
  method("demux", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    if (!ctx.arguments(1, &target)) return;
    try {
      thiz->as<Configuration>()->demux(target);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.demuxQueue
  method("demuxQueue", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    if (!ctx.arguments(1, &target)) return;
    try {
      thiz->as<Configuration>()->demux_queue(target);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.demuxHTTP
  method("demuxHTTP", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Object *options = nullptr;
    if (!ctx.arguments(1, &target, &options)) return;
    try {
      thiz->as<Configuration>()->demux_http(target, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decodeDubbo
  method("decodeDubbo", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->decode_dubbo();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decodeHTTPRequest
  method("decodeHTTPRequest", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->decode_http_request();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decodeHTTPResponse
  method("decodeHTTPResponse", [](Context &ctx, Object *thiz, Value &result) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    try {
      thiz->as<Configuration>()->decode_http_response(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decodeMQTT
  method("decodeMQTT", [](Context &ctx, Object *thiz, Value &result) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    try {
      thiz->as<Configuration>()->decode_mqtt(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decodeWebSocket
  method("decodeWebSocket", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->decode_websocket();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decompressHTTP
  method("decompressHTTP", [](Context &ctx, Object *thiz, Value &result) {
    Function *enable = nullptr;
    if (!ctx.arguments(0, &enable)) return;
    try {
      thiz->as<Configuration>()->decompress_http(enable);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decompressMessage
  method("decompressMessage", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Value algorithm;
    if (!ctx.arguments(1, &algorithm)) return;
    try {
      thiz->as<Configuration>()->decompress_message(algorithm);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.depositMessage
  method("depositMessage", [](Context &ctx, Object *thiz, Value &result) {
    Value filename;
    Object *options = nullptr;
    if (!ctx.arguments(1, &filename, &options)) return;
    try {
      thiz->as<Configuration>()->deposit_message(filename, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.detectProtocol
  method("detectProtocol", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->detect_protocol(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.dummy
  method("dummy", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->dummy();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.dump
  method("dump", [](Context &ctx, Object *thiz, Value &result) {
    Value tag;
    if (!ctx.arguments(0, &tag)) return;
    try {
      thiz->as<Configuration>()->dump(tag);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.encodeDubbo
  method("encodeDubbo", [](Context &ctx, Object *thiz, Value &result) {
    Object *message_obj = nullptr;
    if (!ctx.arguments(0, &message_obj)) return;
    try {
      thiz->as<Configuration>()->encode_dubbo(message_obj);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.encodeHTTPRequest
  method("encodeHTTPRequest", [](Context &ctx, Object *thiz, Value &result) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    try {
      thiz->as<Configuration>()->encode_http_request(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.encodeHTTPResponse
  method("encodeHTTPResponse", [](Context &ctx, Object *thiz, Value &result) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    try {
      thiz->as<Configuration>()->encode_http_response(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.encodeMQTT
  method("encodeMQTT", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->encode_mqtt();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.encodeWebSocket
  method("encodeWebSocket", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->encode_websocket();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.exec
  method("exec", [](Context &ctx, Object *thiz, Value &result) {
    Value command;
    if (!ctx.arguments(1, &command)) return;
    try {
      thiz->as<Configuration>()->exec(command);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.fetchHTTP
  method("fetchHTTP", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Str *layout;
    if (!ctx.arguments(1, &layout)) return;
    try {
      thiz->as<Configuration>()->fetch_http(layout);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.fork
  method("fork", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Object *initializers = nullptr;
    if (!ctx.arguments(1, &target, &initializers)) return;
    try {
      thiz->as<Configuration>()->fork(target, initializers);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.handleStreamStart
  method("handleStreamStart", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_start(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.handleTLSClientHello
  method("handleTLSClientHello", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_tls_client_hello(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.handleData
  method("handleData", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_event(Event::Data, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.handleMessage
  method("handleMessage", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    int size_limit = -1;
    std::string size_limit_str;
    if (ctx.try_arguments(2, &size_limit_str, &callback)) {
      size_limit = utils::get_byte_size(size_limit_str);
    } else if (
      !ctx.try_arguments(2, &size_limit, &callback) &&
      !ctx.arguments(1, &callback)
    ) return;
    try {
      thiz->as<Configuration>()->on_message(callback, size_limit);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.handleMessageStart
  method("handleMessageStart", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_event(Event::MessageStart, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.handleMessageBody
  method("handleMessageBody", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    int size_limit = -1;
    std::string size_limit_str;
    if (ctx.try_arguments(2, &size_limit_str, &callback)) {
      size_limit = utils::get_byte_size(size_limit_str);
    } else if (
      !ctx.try_arguments(2, &size_limit, &callback) &&
      !ctx.arguments(1, &callback)
    ) return;
    try {
      thiz->as<Configuration>()->on_body(callback, size_limit);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.handleMessageEnd
  method("handleMessageEnd", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_event(Event::MessageEnd, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.handleStreamEnd
  method("handleStreamEnd", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_event(Event::StreamEnd, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.link
  method("link", [](Context &ctx, Object *thiz, Value &result) {
    int n = (ctx.argc() + 1) >> 1;
    Str *targets[n];
    Function *conditions[n];
    for (int i = 0; i < n; i++) {
      int a = (i << 1);
      int b = (i << 1) + 1;
      if (ctx.arg(a).is_string()) {
        targets[i] = ctx.arg(a).s();
      } else {
        ctx.error_argument_type(a, "a string");
        return;
      }
      if (b >= ctx.argc()) {
        conditions[i] = nullptr;
      } else if (!ctx.arg(b).is_function()) {
        ctx.error_argument_type(b, "a function");
        return;
      } else {
        conditions[i] = ctx.arg(b).f();
      }
    }
    try {
      thiz->as<Configuration>()->link(n, targets, conditions);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.merge
  method("merge", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Value key;
    Object *options = nullptr;
    if (!ctx.arguments(2, &target, &key, &options)) return;
    try {
      thiz->as<Configuration>()->merge(target, key, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.muxQueue
  method("muxQueue", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Value key;
    Object *options = nullptr;
    if (!ctx.arguments(2, &target, &key, &options)) return;
    try {
      thiz->as<Configuration>()->mux_queue(target, key, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.muxHTTP
  method("muxHTTP", [](Context &ctx, Object *thiz, Value &result) {
    Str *target;
    Value key;
    Object *options = nullptr;
    if (!ctx.arguments(2, &target, &key, &options)) return;
    try {
      thiz->as<Configuration>()->mux_http(target, key, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.pack
  method("pack", [](Context &ctx, Object *thiz, Value &result) {
    int batch_size = 1;
    Object *options = nullptr;
    if (!ctx.arguments(0, &batch_size, &options)) return;
    try {
      thiz->as<Configuration>()->pack(batch_size, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.print
  method("print", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->print();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.replaceStreamStart
  method("replaceStreamStart", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<Configuration>()->replace_start(replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.replaceData
  method("replaceData", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<Configuration>()->replace_event(Event::Data, replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.replaceMessage
  method("replaceMessage", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    int size_limit = -1;
    std::string size_limit_str;
    if (ctx.try_arguments(1, &size_limit_str, &replacement)) {
      size_limit = utils::get_byte_size(size_limit_str);
    } else if (
      !ctx.try_arguments(1, &size_limit, &replacement) &&
      !ctx.arguments(0, &replacement)
    ) return;
    try {
      thiz->as<Configuration>()->replace_message(replacement, size_limit);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.replaceMessageStart
  method("replaceMessageStart", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<Configuration>()->replace_event(Event::MessageStart, replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.replaceMessageBody
  method("replaceMessageBody", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    int size_limit = -1;
    std::string size_limit_str;
    if (ctx.try_arguments(1, &size_limit_str, &replacement)) {
      size_limit = utils::get_byte_size(size_limit_str);
    } else if (
      !ctx.try_arguments(1, &size_limit, &replacement) &&
      !ctx.arguments(0, &replacement)
    ) return;
    try {
      thiz->as<Configuration>()->replace_body(replacement, size_limit);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.replaceMessageEnd
  method("replaceMessageEnd", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<Configuration>()->replace_event(Event::MessageEnd, replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.replaceStreamEnd
  method("replaceStreamEnd", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<Configuration>()->replace_event(Event::StreamEnd, replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.serveHTTP
  method("serveHTTP", [](Context &ctx, Object *thiz, Value &result) {
    Object *handler;
    if (!ctx.arguments(1, &handler)) return;
    try {
      thiz->as<Configuration>()->serve_http(handler);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.split
  method("split", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->split(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.throttleConcurrency
  method("throttleConcurrency", [](Context &ctx, Object *thiz, Value &result) {
    Value quota, account;
    if (!ctx.arguments(1, &quota, &account)) return;
    try {
      thiz->as<Configuration>()->throttle_concurrency(quota, account);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.throttleDataRate
  method("throttleDataRate", [](Context &ctx, Object *thiz, Value &result) {
    Value quota, account;
    if (!ctx.arguments(1, &quota, &account)) return;
    try {
      thiz->as<Configuration>()->throttle_data_rate(quota, account);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.tee
  method("tee", [](Context &ctx, Object *thiz, Value &result) {
    Value filename;
    if (!ctx.arguments(1, &filename)) return;
    try {
      thiz->as<Configuration>()->tee(filename);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.throttleMessageRate
  method("throttleMessageRate", [](Context &ctx, Object *thiz, Value &result) {
    Value quota, account;
    if (!ctx.arguments(1, &quota, &account)) return;
    try {
      thiz->as<Configuration>()->throttle_message_rate(quota, account);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.use
  method("use", [](Context &ctx, Object *thiz, Value &result) {
    std::string module;
    Array *modules;
    Str *pipeline;
    Str *pipeline_down = nullptr;
    Function *when = nullptr;
    auto root = static_cast<pipy::Context*>(ctx.root());
    auto worker = root->worker();
    if (
      ctx.try_arguments(3, &modules, &pipeline, &pipeline_down, &when) ||
      ctx.try_arguments(2, &modules, &pipeline, &when)
    ) {
      std::list<Module*> mods;
      modules->iterate_while(
        [&](pjs::Value &v, int) {
          auto s = v.to_string();
          auto path = utils::path_normalize(s->str());
          s->release();
          auto mod = worker->load_module(path);
          if (!mod) {
            std::string msg("[pjs] Cannot load module: ");
            msg += module;
            ctx.error(msg);
            return false;
          }
          mods.push_back(mod);
          return true;
        }
      );
      if (mods.size() == modules->length()) {
        try {
          thiz->as<Configuration>()->use(mods, pipeline, pipeline_down, when);
          result.set(thiz);
        } catch (std::runtime_error &err) {
          ctx.error(err);
        }
      }
    } else if (ctx.arguments(2, &module, &pipeline)) {
      auto path = utils::path_normalize(module);
      auto mod = worker->load_module(path);
      if (!mod) {
        std::string msg("[pjs] Cannot load module: ");
        msg += module;
        ctx.error(msg);
        return;
      }
      try {
        thiz->as<Configuration>()->use(mod, pipeline);
        result.set(thiz);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
    }
  });

  // Configuration.wait
  method("wait", [](Context &ctx, Object *thiz, Value &result) {
    Function *condition;
    Object *options = nullptr;
    if (!ctx.arguments(1, &condition, &options)) return;
    try {
      thiz->as<Configuration>()->wait(condition, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

}

} // namespace pjs
