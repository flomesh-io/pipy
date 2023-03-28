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
#include "task.hpp"
#include "watch.hpp"
#include "context.hpp"
#include "worker.hpp"
#include "graph.hpp"
#include "utils.hpp"
#include "log.hpp"

// all filters
#include "filters/bgp.hpp"
#include "filters/branch.hpp"
#include "filters/chain.hpp"
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
#include "filters/link-input.hpp"
#include "filters/link-output.hpp"
#include "filters/mime.hpp"
#include "filters/mqtt.hpp"
#include "filters/mux.hpp"
#include "filters/on-body.hpp"
#include "filters/on-event.hpp"
#include "filters/on-message.hpp"
#include "filters/on-start.hpp"
#include "filters/pack.hpp"
#include "filters/print.hpp"
#include "filters/proxy-protocol.hpp"
#include "filters/read.hpp"
#include "filters/replace-body.hpp"
#include "filters/replace-event.hpp"
#include "filters/replace-message.hpp"
#include "filters/replace-start.hpp"
#include "filters/replay.hpp"
#include "filters/resp.hpp"
#include "filters/socks.hpp"
#include "filters/split.hpp"
#include "filters/tee.hpp"
#include "filters/thrift.hpp"
#include "filters/throttle.hpp"
#include "filters/tls.hpp"
#include "filters/use.hpp"
#include "filters/wait.hpp"
#include "filters/websocket.hpp"

#include <stdexcept>
#include <sstream>

namespace pipy {

//
// FilterConfigurator
//

void FilterConfigurator::on_start(pjs::Object *handler) {
  if (!m_config) throw std::runtime_error("no pipeline found");
  if (m_current_filter) throw std::runtime_error("onStart() is only allowed prior to filters");
  if (m_config->on_start) throw std::runtime_error("duplicated onStart()");
  m_config->on_start = handler;
}

void FilterConfigurator::on_end(pjs::Function *handler) {
  if (!m_config) throw std::runtime_error("no pipeline found");
  if (m_current_filter) throw std::runtime_error("onEnd() is only allowed prior to filters");
  if (m_config->on_end) throw std::runtime_error("duplicated onEnd()");
  m_config->on_end = handler;
}

void FilterConfigurator::accept_http_tunnel(pjs::Function *handler) {
  require_sub_pipeline(append_filter(new http::TunnelServer(handler)));
}

void FilterConfigurator::accept_proxy_protocol(pjs::Function *handler) {
  require_sub_pipeline(append_filter(new proxy_protocol::Server(handler)));
}

void FilterConfigurator::accept_socks(pjs::Function *on_connect) {
  require_sub_pipeline(append_filter(new socks::Server(on_connect)));
}

void FilterConfigurator::accept_tls(pjs::Object *options) {
  require_sub_pipeline(append_filter(new tls::Server(options)));
}

void FilterConfigurator::branch(int count, pjs::Function **conds, const pjs::Value *layout) {
  append_filter(new Branch(count, conds, layout));
}

void FilterConfigurator::chain(const std::list<JSModule*> modules) {
  append_filter(new Chain(modules));
}

void FilterConfigurator::chain_next() {
  append_filter(new ChainNext());
}

void FilterConfigurator::compress_http(pjs::Object *options) {
  append_filter(new CompressHTTP(options));
}

void FilterConfigurator::compress_message(pjs::Object *options) {
  append_filter(new CompressMessage(options));
}

void FilterConfigurator::connect(const pjs::Value &target, pjs::Object *options) {
  append_filter(new Connect(target, options));
}

void FilterConfigurator::connect_http_tunnel(const pjs::Value &address) {
  require_sub_pipeline(append_filter(new http::TunnelClient(address)));
}

void FilterConfigurator::connect_proxy_protocol(const pjs::Value &address) {
  require_sub_pipeline(append_filter(new proxy_protocol::Client(address)));
}

void FilterConfigurator::connect_socks(const pjs::Value &address) {
  require_sub_pipeline(append_filter(new socks::Client(address)));
}

void FilterConfigurator::connect_tls(pjs::Object *options) {
  require_sub_pipeline(append_filter(new tls::Client(options)));
}

void FilterConfigurator::decode_bgp(pjs::Object *options) {
  append_filter(new bgp::Decoder(options));
}

void FilterConfigurator::decode_dubbo() {
  append_filter(new dubbo::Decoder());
}

void FilterConfigurator::decode_http_request() {
  append_filter(new http::RequestDecoder());
}

void FilterConfigurator::decode_http_response(pjs::Object *options) {
  append_filter(new http::ResponseDecoder(options));
}

void FilterConfigurator::decode_mqtt() {
  append_filter(new mqtt::Decoder());
}

void FilterConfigurator::decode_multipart() {
  append_filter(new mime::MultipartDecoder());
}

void FilterConfigurator::decode_resp() {
  append_filter(new resp::Decoder());
}

void FilterConfigurator::decode_thrift() {
  append_filter(new thrift::Decoder());
}

void FilterConfigurator::decode_websocket() {
  append_filter(new websocket::Decoder());
}

void FilterConfigurator::decompress_http(pjs::Function *enable) {
  append_filter(new DecompressHTTP(enable));
}

void FilterConfigurator::decompress_message(const pjs::Value &algorithm) {
  append_filter(new DecompressMessage(algorithm));
}

void FilterConfigurator::deframe(pjs::Object *states) {
  append_filter(new Deframe(states));
}

void FilterConfigurator::demux(pjs::Object *options) {
  require_sub_pipeline(append_filter(new Demux(options)));
}

void FilterConfigurator::demux_http(pjs::Object *options) {
  require_sub_pipeline(append_filter(new http::Demux(options)));
}

void FilterConfigurator::deposit_message(const pjs::Value &filename, pjs::Object *options) {
  append_filter(new DepositMessage(filename, options));
}

void FilterConfigurator::detect_protocol(pjs::Function *callback) {
  append_filter(new ProtocolDetector(callback));
}

void FilterConfigurator::dummy() {
  append_filter(new Dummy());
}

void FilterConfigurator::dump(const pjs::Value &tag) {
  append_filter(new Dump(tag));
}

void FilterConfigurator::encode_bgp(pjs::Object *options) {
  append_filter(new bgp::Encoder(options));
}

void FilterConfigurator::encode_dubbo() {
  append_filter(new dubbo::Encoder());
}

void FilterConfigurator::encode_http_request(pjs::Object *options) {
  append_filter(new http::RequestEncoder(options));
}

void FilterConfigurator::encode_http_response(pjs::Object *options) {
  append_filter(new http::ResponseEncoder(options));
}

void FilterConfigurator::encode_mqtt() {
  append_filter(new mqtt::Encoder());
}

void FilterConfigurator::encode_resp() {
  append_filter(new resp::Encoder());
}

void FilterConfigurator::encode_thrift() {
  append_filter(new thrift::Encoder());
}

void FilterConfigurator::encode_websocket() {
  append_filter(new websocket::Encoder());
}

void FilterConfigurator::exec(const pjs::Value &command) {
  append_filter(new Exec(command));
}

void FilterConfigurator::fork(const pjs::Value &init_arg) {
  require_sub_pipeline(append_filter(new Fork(init_arg)));
}

void FilterConfigurator::handle_body(pjs::Function *callback, int size_limit) {
  append_filter(new OnBody(callback, size_limit));
}

void FilterConfigurator::handle_event(Event::Type type, pjs::Function *callback) {
  append_filter(new OnEvent(type, callback));
}

void FilterConfigurator::handle_message(pjs::Function *callback, int size_limit) {
  append_filter(new OnMessage(callback, size_limit));
}

void FilterConfigurator::handle_start(pjs::Function *callback) {
  append_filter(new OnStart(callback));
}

void FilterConfigurator::handle_tls_client_hello(pjs::Function *callback) {
  append_filter(new tls::OnClientHello(callback));
}

void FilterConfigurator::input(pjs::Function *callback) {
  require_sub_pipeline(append_filter(new LinkInput(callback)));
}

void FilterConfigurator::link(size_t count, pjs::Str **layouts, pjs::Function **conditions) {
  auto *filter = new Link();
  for (size_t i = 0; i < count; i++) {
    filter->add_sub_pipeline(layouts[i]);
    filter->add_condition(conditions[i]);
  }
  append_filter(filter);
}

void FilterConfigurator::mux(pjs::Function *session_selector, pjs::Object *options) {
  if (options && options->is_function()) {
    require_sub_pipeline(append_filter(new Mux(session_selector, options->as<pjs::Function>())));
  } else {
    require_sub_pipeline(append_filter(new Mux(session_selector, options)));
  }
}

void FilterConfigurator::mux_http(pjs::Function *session_selector, pjs::Object *options) {
  if (options && options->is_function()) {
    require_sub_pipeline(append_filter(new http::Mux(session_selector, options->as<pjs::Function>())));
  } else {
    require_sub_pipeline(append_filter(new http::Mux(session_selector, options)));
  }
}

void FilterConfigurator::output(pjs::Function *output_f) {
  append_filter(new LinkOutput(output_f));
}

void FilterConfigurator::pack(int batch_size, pjs::Object *options) {
  append_filter(new Pack(batch_size, options));
}

void FilterConfigurator::print() {
  append_filter(new Print());
}

void FilterConfigurator::read(const pjs::Value &pathname) {
  append_filter(new Read(pathname));
}

void FilterConfigurator::replace_body(const pjs::Value &replacement, int size_limit) {
  append_filter(new ReplaceBody(replacement, size_limit));
}

void FilterConfigurator::replace_event(Event::Type type, const pjs::Value &replacement) {
  append_filter(new ReplaceEvent(type, replacement));
}

void FilterConfigurator::replace_message(const pjs::Value &replacement, int size_limit) {
  append_filter(new ReplaceMessage(replacement, size_limit));
}

void FilterConfigurator::replace_start(const pjs::Value &replacement) {
  append_filter(new ReplaceStart(replacement));
}

void FilterConfigurator::replay(pjs::Object *options) {
  require_sub_pipeline(append_filter(new Replay(options)));
}

void FilterConfigurator::serve_http(pjs::Object *handler) {
  append_filter(new http::Server(handler));
}

void FilterConfigurator::split(Data *separator) {
  append_filter(new Split(separator));
}

void FilterConfigurator::split(pjs::Str *separator) {
  append_filter(new Split(separator));
}

void FilterConfigurator::split(pjs::Function *callback) {
  append_filter(new Split(callback));
}

void FilterConfigurator::tee(const pjs::Value &filename) {
  append_filter(new Tee(filename));
}

void FilterConfigurator::throttle_concurrency(pjs::Object *quota) {
  append_filter(new ThrottleConcurrency(quota));
}

void FilterConfigurator::throttle_data_rate(pjs::Object *quota) {
  append_filter(new ThrottleDataRate(quota));
}

void FilterConfigurator::throttle_message_rate(pjs::Object *quota) {
  append_filter(new ThrottleMessageRate(quota));
}

void FilterConfigurator::use(JSModule *module, pjs::Str *pipeline) {
  append_filter(new Use(module, pipeline));
}

void FilterConfigurator::use(nmi::NativeModule *module, pjs::Str *pipeline) {
  append_filter(new Use(module, pipeline));
}

void FilterConfigurator::use(const std::list<JSModule*> modules, pjs::Str *pipeline, pjs::Function *when) {
  append_filter(new Use(modules, pipeline, when));
}

void FilterConfigurator::use(const std::list<JSModule*> modules, pjs::Str *pipeline, pjs::Str *pipeline_down, pjs::Function *when) {
  append_filter(new Use(modules, pipeline, pipeline_down, when));
}

void FilterConfigurator::wait(pjs::Function *condition, pjs::Object *options) {
  append_filter(new Wait(condition, options));
}

auto FilterConfigurator::trace_location(pjs::Context &ctx) -> FilterConfigurator* {
  if (auto caller = ctx.caller()) {
    m_current_location = caller->call_site();
  }
  return this;
}

void FilterConfigurator::to(pjs::Str *layout_name) {
  if (!m_current_joint_filter) {
    throw std::runtime_error("calling to() without a joint-filter");
  }
  m_current_joint_filter->add_sub_pipeline(layout_name);
  m_current_joint_filter = nullptr;
}

void FilterConfigurator::to(const std::string &name, const std::function<void(FilterConfigurator*)> &cb) {
  if (!m_current_joint_filter) {
    throw std::runtime_error("calling to() without a joint-filter");
  }
  int index = sub_pipeline(name, cb);
  m_current_joint_filter->add_sub_pipeline(index);
  m_current_joint_filter = nullptr;
}

auto FilterConfigurator::sub_pipeline(const std::string &name, const std::function<void(FilterConfigurator*)> &cb) -> int {
  int index = -1;
  pjs::Ref<FilterConfigurator> fc(m_configuration->new_indexed_pipeline(name, index));
  cb(fc);
  fc->check_integrity();
  return index;
}

void FilterConfigurator::check_integrity() {
  if (m_current_joint_filter) {
    throw std::runtime_error("missing .to(...) for the last filter");
  }
}

auto FilterConfigurator::append_filter(Filter *filter) -> Filter* {
  if (!m_config) {
    delete filter;
    throw std::runtime_error("no pipeline found");
  }
  check_integrity();
  filter->set_location(m_current_location);
  m_config->filters.emplace_back(filter);
  m_current_filter = filter;
  return filter;
}

void FilterConfigurator::require_sub_pipeline(Filter *filter) {
  m_current_joint_filter = filter;
}

//
// Configuration
//

Configuration::Configuration(pjs::Object *context_prototype)
  : pjs::ObjectTemplate<Configuration, FilterConfigurator>(this)
  , m_context_prototype(context_prototype)
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
  check_integrity();
  m_listens.emplace_back();
  auto &config = m_listens.back();
  config.index = next_pipeline_index();
  config.ip = "0.0.0.0";
  config.port = port;
  config.options = Listener::Options(options);
  FilterConfigurator::set_pipeline_config(&config);
}

void Configuration::listen(const std::string &port, pjs::Object *options) {
  check_integrity();
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

  m_listens.emplace_back();
  auto &config = m_listens.back();
  config.index = next_pipeline_index();
  config.ip = addr;
  config.port = port_num;
  config.options = Listener::Options(options);
  FilterConfigurator::set_pipeline_config(&config);
}

void Configuration::task(const std::string &when) {
  check_integrity();
  std::string name("Task #");
  name += std::to_string(m_tasks.size() + 1);
  m_tasks.emplace_back();
  auto &config = m_tasks.back();
  config.index = next_pipeline_index();
  config.name = name;
  config.when = when;
  FilterConfigurator::set_pipeline_config(&config);
}

void Configuration::watch(const std::string &filename) {
  check_integrity();
  if (filename.empty()) throw std::runtime_error("filename cannot be empty");
  m_watches.emplace_back();
  auto &config = m_watches.back();
  config.index = next_pipeline_index();
  config.filename = filename[0] == '/' ? filename : std::string("/") + filename;
  FilterConfigurator::set_pipeline_config(&config);
}

void Configuration::pipeline(const std::string &name) {
  check_integrity();
  if (name.empty()) throw std::runtime_error("pipeline name cannot be empty");
  m_named_pipelines.emplace_back();
  auto &config = m_named_pipelines.back();
  config.index = next_pipeline_index();
  config.name = name;
  FilterConfigurator::set_pipeline_config(&config);
}

void Configuration::pipeline() {
  check_integrity();
  if (m_entrance_pipeline) throw std::runtime_error("more than one entrance pipelines");
  m_entrance_pipeline = std::unique_ptr<PipelineConfig>(new PipelineConfig);
  FilterConfigurator::set_pipeline_config(m_entrance_pipeline.get());
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
    auto l = worker->get_export(imp.ns, imp.original_name);
    if (l < 0) {
      std::string msg("cannot import variable ");
      msg += imp.name->str();
      msg += " in ";
      msg += module->filename()->str();
      throw std::runtime_error(msg);
    }
    imports->add(imp.name, l, imp.original_name);
  }
}

void Configuration::apply(JSModule *mod) {
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
    int index,
    const std::string &name,
    const std::string &label,
    PipelineConfig &config
  ) -> PipelineLayout*
  {
    auto layout = PipelineLayout::make(mod, index, name, label);
    layout->on_start(config.on_start);
    layout->on_end(config.on_end);
    for (auto &f : config.filters) {
      layout->append(f.release());
    }
    return layout;
  };

  if (m_entrance_pipeline) {
    std::string name("Module Entrance: ");
    name += mod->filename()->str();
    auto p = make_pipeline(-1, "", name, *m_entrance_pipeline);
    mod->m_entrance_pipeline = p;
  }

  for (auto &i : m_named_pipelines) {
    auto s = pjs::Str::make(i.name);
    auto p = make_pipeline(i.index, i.name, "", i);
    mod->m_named_pipelines[s] = p;
  }

  for (auto &i : m_indexed_pipelines) {
    auto p = make_pipeline(i.second.index, "", i.second.name, i.second);
    mod->m_indexed_pipelines[p->index()] = p;
  }

  Worker *worker = mod->worker();

  for (auto &i : m_listens) {
    if (!i.port) continue;
    auto name = std::to_string(i.port) + '@' + i.ip;
    auto p = make_pipeline(i.index, "", name, i);
    auto listener = Listener::get(i.options.protocol, i.ip, i.port);
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

  for (auto &i : m_tasks) {
    auto p = make_pipeline(i.index, "", i.name, i);
    auto t = Task::make(i.when, p);
    worker->add_task(t);
  }

  for (auto &i : m_watches) {
    std::string name("Watch ");
    auto p = make_pipeline(i.index, "", name + i.filename, i);
    auto w = Watch::make(i.filename, p);
    worker->add_watch(w);
  }
}

void Configuration::draw(Graph &g) {
  auto add_filters = [](Graph::Pipeline &gp, const std::list<std::unique_ptr<Filter>> &filters) {
    for (const auto &f : filters) {
      Graph::Filter gf;
      f->dump(gf);
      gp.filters.emplace_back(std::move(gf));
    }
  };

  if (m_entrance_pipeline) {
    Graph::Pipeline p;
    p.index = -1;
    p.name = "Module Entrance";
    add_filters(p, m_entrance_pipeline->filters);
    g.add_pipeline(std::move(p));
  }

  for (const auto &i : m_named_pipelines) {
    Graph::Pipeline p;
    p.index = i.index;
    p.name = i.name;
    add_filters(p, i.filters);
    g.add_pipeline(std::move(p));
  }

  for (const auto &i : m_indexed_pipelines) {
    Graph::Pipeline p;
    p.index = i.second.index;
    p.label = i.second.name;
    add_filters(p, i.second.filters);
    g.add_pipeline(std::move(p));
  }

  for (const auto &i : m_listens) {
    Graph::Pipeline p;
    p.index = i.index;
    p.label = "Listen on ";
    p.label += std::to_string(i.port);
    p.label += " at ";
    p.label += i.ip;
    add_filters(p, i.filters);
    g.add_pipeline(std::move(p));
  }

  for (const auto &i : m_tasks) {
    Graph::Pipeline p;
    p.index = i.index;
    p.label = i.name;
    p.label += " (";
    p.label += i.when;
    p.label += ')';
    add_filters(p, i.filters);
    g.add_pipeline(std::move(p));
  }

  for (const auto &i : m_watches) {
    Graph::Pipeline p;
    p.index = i.index;
    p.label = "Watch ";
    p.label += i.filename;
    add_filters(p, i.filters);
    g.add_pipeline(std::move(p));
  }
}

auto Configuration::new_indexed_pipeline(const std::string &name, int &index) -> FilterConfigurator* {
  index = next_pipeline_index();
  auto &i = m_indexed_pipelines[index];
  i.index = index;
  i.name = name;
  return FilterConfigurator::make(this, &i);
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<FilterConfigurator>::init() {

  // FilterConfigurator.onStart
  method("onStart", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Object *starting_events = nullptr;
      if (!ctx.arguments(1, &starting_events)) return;
      if (!starting_events &&
          !starting_events->is<Event>() &&
          !starting_events->is<Message>() &&
          !starting_events->is<Array>() &&
          !starting_events->is<Function>()
      ) {
        ctx.error_argument_type(1, "an Event, a Message, a function or an array");
        return;
      }
      thiz->as<FilterConfigurator>()->on_start(starting_events);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.onEnd
  method("onEnd", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Function *handler;
      if (!ctx.arguments(1, &handler)) return;
      thiz->as<FilterConfigurator>()->on_end(handler);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.to
  method("to", [](Context &ctx, Object *thiz, Value &result) {
    try {
      pjs::Str *layout_name;
      pjs::Function *layout_builder;
      if (ctx.try_arguments(1, &layout_name)) {
        thiz->as<FilterConfigurator>()->to(layout_name);
      } else if (ctx.try_arguments(1, &layout_builder)) {
        thiz->as<FilterConfigurator>()->to(
          layout_builder->to_string(),
          [&](FilterConfigurator *fc) {
            pjs::Value arg(fc), ret;
            (*layout_builder)(ctx, 1, &arg, ret);
          }
        );
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.acceptHTTPTunnel
  method("acceptHTTPTunnel", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Function *handler;
      if (ctx.try_arguments(2, &layout, &handler)) {
        thiz->as<FilterConfigurator>()->accept_http_tunnel(handler);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(1, &handler)) {
        thiz->as<FilterConfigurator>()->accept_http_tunnel(handler);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.acceptProxyProtocol
  method("acceptProxyProtocol", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Function *handler;
      if (!ctx.arguments(1, &handler)) return;
      thiz->as<FilterConfigurator>()->accept_proxy_protocol(handler);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.acceptSOCKS
  method("acceptSOCKS", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Function *on_connect;
      if (ctx.try_arguments(2, &layout, &on_connect)) {
        thiz->as<FilterConfigurator>()->accept_socks(on_connect);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(1, &on_connect)) {
        thiz->as<FilterConfigurator>()->accept_socks(on_connect);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.acceptTLS
  method("acceptTLS", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Object *options = nullptr;
      if (ctx.try_arguments(1, &layout, &options)) {
        thiz->as<FilterConfigurator>()->accept_tls(options);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(0, &options)) {
        thiz->as<FilterConfigurator>()->accept_tls(options);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.branch
  method("branch", [](Context &ctx, Object *thiz, Value &result) {
    try {
      int n = ctx.argc();
      if (n < 2) throw std::runtime_error("requires at least 2 arguments");
      n = (n + 1) / 2;

      bool has_default = (ctx.argc() % 2 > 0);
      bool has_functions = false;
      for (int i = 0; i < n; i++) {
        if (ctx.arg(i*2).is_function()) {
          has_functions = true;
          break;
        }
      }

      // Dynamic branch
      if (has_functions) {
        for (int i = 0; i < n; i++) {
          auto p = i * 2;
          if (p + 1 < ctx.argc()) {
            if (!ctx.arg(p).is_function()) {
              ctx.error_argument_type(p, "a function");
              return;
            }
            p++;
          }
          if (!ctx.arg(p).is_string() && !ctx.arg(p).is_function()) {
            ctx.error_argument_type(p, "a string or a function");
            return;
          }
        }
        Function *conds[n];
        Value layouts[n];
        for (int i = 0; i < n; i++) {
          auto p = i * 2;
          auto &cond = ctx.arg(p);
          if (p + 1 < ctx.argc()) {
            conds[i] = cond.f();
            p++;
          } else {
            conds[i] = nullptr;
          }
          auto &layout = ctx.arg(p);
          if (layout.is_string()) {
            layouts[i].set(layout.s());
          } else {
            layouts[i].set(
              thiz->as<FilterConfigurator>()->sub_pipeline(
                layout.f()->to_string(),
                [&](FilterConfigurator *fc) {
                  Value arg(fc), ret;
                  (*layout.f())(ctx, 1, &arg, ret);
                }
              )
            );
            if (!ctx.ok()) return;
          }
        }
        thiz->as<FilterConfigurator>()->branch(n, conds, layouts);

      // Static branch
      } else {
        for (int i = 0; i < n; i++) {
          auto p = i * 2 + 1;
          if (!ctx.arg(p).is_function()) {
            ctx.error_argument_type(p, "a function");
            return;
          }
        }
        if (has_default) {
          auto p = ctx.argc() - 1;
          if (!ctx.arg(p).is_function()) {
            ctx.error_argument_type(p, "a function");
            return;
          }
        }
        int selected = -1;
        for (int i = 0; i < n; i++) {
          auto p = i * 2;
          if (ctx.arg(p).to_boolean()) {
            selected = p + 1;
            break;
          }
        }
        if (selected < 0 && has_default) selected = ctx.argc() - 1;
        if (selected > 0) {
          auto *f = ctx.arg(selected).f();
          pjs::Value arg(thiz), ret;
          (*f)(ctx, 1, &arg, ret);
        }
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.chain
  method("chain", [](Context &ctx, Object *thiz, Value &result) {
    Array *modules = nullptr;
    if (!ctx.arguments(0, &modules)) return;
    if (!modules) {
      try {
        thiz->as<FilterConfigurator>()->chain_next();
        result.set(thiz);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
    } else {
      auto root = static_cast<pipy::Context*>(ctx.root());
      auto worker = root->worker();
      std::list<JSModule*> mods;
      modules->iterate_while(
        [&](pjs::Value &v, int) {
          auto s = v.to_string();
          auto path = utils::path_normalize(s->str());
          s->release();
          auto mod = worker->load_js_module(path);
          if (!mod) {
            std::string msg("[chain] Cannot load module: ");
            msg += path;
            ctx.error(msg);
            return false;
          }
          mods.push_back(mod);
          return true;
        }
      );
      if (mods.size() == modules->length()) {
        try {
          thiz->as<FilterConfigurator>()->chain(mods);
          result.set(thiz);
        } catch (std::runtime_error &err) {
          ctx.error(err);
        }
      }
    }
  });

  // FilterConfigurator.compressHTTP
  method("compressHTTP", [](Context &ctx, Object *thiz, Value &result) {
      Object *options = nullptr;
      if (!ctx.arguments(0, &options)) return;
      try {
        thiz->as<FilterConfigurator>()->compress_http(options);
        result.set(thiz);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
  });

  // FilterConfigurator.compressMessage
  method("compressMessage", [](Context &ctx, Object *thiz, Value &result) {
      Object *options = nullptr;
      if (!ctx.arguments(0, &options)) return;
      try {
        thiz->as<FilterConfigurator>()->compress_message(options);
        result.set(thiz);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
  });

  // FilterConfigurator.connect
  method("connect", [](Context &ctx, Object *thiz, Value &result) {
    Value target;
    Object *options = nullptr;
    if (!ctx.arguments(1, &target, &options)) return;
    try {
      thiz->as<FilterConfigurator>()->trace_location(ctx)->connect(target, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.connectHTTPTunnel
  method("connectHTTPTunnel", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Value address;
      if (ctx.try_arguments(2, &layout, &address)) {
        thiz->as<FilterConfigurator>()->connect_http_tunnel(address);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(1, &address)) {
        thiz->as<FilterConfigurator>()->connect_http_tunnel(address);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.connectProxyProtocol
  method("connectProxyProtocol", [](Context &ctx, Object *thiz, Value &result) {
    Value target;
    if (!ctx.arguments(1, &target)) return;
    try {
      thiz->as<FilterConfigurator>()->connect_proxy_protocol(target);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.connectSOCKS
  method("connectSOCKS", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Value address;
      if (ctx.try_arguments(2, &layout, &address)) {
        thiz->as<FilterConfigurator>()->connect_socks(address);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(1, &address)) {
        thiz->as<FilterConfigurator>()->connect_socks(address);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.connectTLS
  method("connectTLS", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Object *options = nullptr;
      if (ctx.try_arguments(1, &layout, &options)) {
        thiz->as<FilterConfigurator>()->connect_tls(options);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(0, &options)) {
        thiz->as<FilterConfigurator>()->connect_tls(options);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decodeBGP
  method("decodeBGP", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Object *options = nullptr;
      if (!ctx.arguments(0, &options)) return;
      thiz->as<FilterConfigurator>()->decode_bgp(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decodeDubbo
  method("decodeDubbo", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->decode_dubbo();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decodeHTTPRequest
  method("decodeHTTPRequest", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->decode_http_request();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decodeHTTPResponse
  method("decodeHTTPResponse", [](Context &ctx, Object *thiz, Value &result) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    try {
      thiz->as<FilterConfigurator>()->decode_http_response(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decodeMQTT
  method("decodeMQTT", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->decode_mqtt();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decodeMultipart
  method("decodeMultipart", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->decode_multipart();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decodeRESP
  method("decodeRESP", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->decode_resp();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decodeThrift
  method("decodeThrift", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->decode_thrift();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decodeWebSocket
  method("decodeWebSocket", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->decode_websocket();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decompressHTTP
  method("decompressHTTP", [](Context &ctx, Object *thiz, Value &result) {
    Function *enable = nullptr;
    if (!ctx.arguments(0, &enable)) return;
    try {
      thiz->as<FilterConfigurator>()->decompress_http(enable);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.decompressMessage
  method("decompressMessage", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Value algorithm;
    if (!ctx.arguments(1, &algorithm)) return;
    try {
      thiz->as<FilterConfigurator>()->decompress_message(algorithm);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.deframe
  method("deframe", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Object *states;
    if (!ctx.arguments(1, &states)) return;
    try {
      thiz->as<FilterConfigurator>()->deframe(states);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.demux
  method("demux", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout = nullptr;
      pjs::Object *options = nullptr;
      if (ctx.try_arguments(1, &layout, &options)) {
        thiz->as<FilterConfigurator>()->demux(options);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(0, &options)) {
        thiz->as<FilterConfigurator>()->demux(options);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.demuxHTTP
  method("demuxHTTP", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Object *options = nullptr;
      if (ctx.try_arguments(1, &layout, &options)) {
        thiz->as<FilterConfigurator>()->demux_http(options);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(0, &options)) {
        thiz->as<FilterConfigurator>()->demux_http(options);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.depositMessage
  method("depositMessage", [](Context &ctx, Object *thiz, Value &result) {
    Value filename;
    Object *options = nullptr;
    if (!ctx.arguments(1, &filename, &options)) return;
    try {
      thiz->as<FilterConfigurator>()->deposit_message(filename, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.detectProtocol
  method("detectProtocol", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<FilterConfigurator>()->detect_protocol(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.dummy
  method("dummy", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->dummy();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.dump
  method("dump", [](Context &ctx, Object *thiz, Value &result) {
    Value tag;
    if (!ctx.arguments(0, &tag)) return;
    try {
      thiz->as<FilterConfigurator>()->dump(tag);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.encodeBGP
  method("encodeBGP", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Object *options = nullptr;
      if (!ctx.arguments(0, &options)) return;
      thiz->as<FilterConfigurator>()->encode_bgp(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.encodeDubbo
  method("encodeDubbo", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->encode_dubbo();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.encodeHTTPRequest
  method("encodeHTTPRequest", [](Context &ctx, Object *thiz, Value &result) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    try {
      thiz->as<FilterConfigurator>()->encode_http_request(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.encodeHTTPResponse
  method("encodeHTTPResponse", [](Context &ctx, Object *thiz, Value &result) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    try {
      thiz->as<FilterConfigurator>()->encode_http_response(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.encodeMQTT
  method("encodeMQTT", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->encode_mqtt();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.encodeRESP
  method("encodeRESP", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->encode_resp();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.encodeThrift
  method("encodeThrift", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->encode_thrift();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.encodeWebSocket
  method("encodeWebSocket", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->encode_websocket();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.exec
  method("exec", [](Context &ctx, Object *thiz, Value &result) {
    Value command;
    if (!ctx.arguments(1, &command)) return;
    try {
      thiz->as<FilterConfigurator>()->exec(command);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.fork
  method("fork", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Value init_arg;
      if (ctx.try_arguments(1, &layout, &init_arg)) {
        thiz->as<FilterConfigurator>()->fork(init_arg);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(0, &init_arg)) {
        thiz->as<FilterConfigurator>()->fork(init_arg);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.handleData
  method("handleData", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<FilterConfigurator>()->handle_event(Event::Type::Data, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.handleMessage
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
      thiz->as<FilterConfigurator>()->handle_message(callback, size_limit);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.handleMessageBody
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
      thiz->as<FilterConfigurator>()->handle_body(callback, size_limit);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.handleMessageEnd
  method("handleMessageEnd", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<FilterConfigurator>()->handle_event(Event::Type::MessageEnd, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.handleMessageStart
  method("handleMessageStart", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<FilterConfigurator>()->handle_event(Event::Type::MessageStart, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.handleStreamEnd
  method("handleStreamEnd", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<FilterConfigurator>()->handle_event(Event::Type::StreamEnd, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.handleStreamStart
  method("handleStreamStart", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<FilterConfigurator>()->handle_start(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.handleTLSClientHello
  method("handleTLSClientHello", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<FilterConfigurator>()->handle_tls_client_hello(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.input
  method("input", [](Context &ctx, Object *thiz, Value &result) {
    try {
      pjs::Str *layout;
      pjs::Function *callback = nullptr;
      if (ctx.try_arguments(1, &layout, &callback)) {
        thiz->as<FilterConfigurator>()->input(callback);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (ctx.arguments(0, &callback)) {
        thiz->as<FilterConfigurator>()->input(callback);
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.link
  method("link", [](Context &ctx, Object *thiz, Value &result) {
    int n = (ctx.argc() + 1) >> 1;
    Str *layouts[n];
    Function *conditions[n];
    for (int i = 0; i < n; i++) {
      int a = (i << 1);
      int b = (i << 1) + 1;
      if (ctx.arg(a).is_string()) {
        layouts[i] = ctx.arg(a).s();
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
      thiz->as<FilterConfigurator>()->link(n, layouts, conditions);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.mux
  method("mux", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Function *session_selector = nullptr;
      Object *options = nullptr;
      if (
        ctx.try_arguments(1, &layout, &session_selector, &options) ||
        ctx.try_arguments(1, &layout, &options)
      ) {
        thiz->as<FilterConfigurator>()->mux(session_selector, options);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (
        ctx.try_arguments(0, &session_selector, &options) ||
        ctx.try_arguments(0, &options)
      ) {
        thiz->as<FilterConfigurator>()->mux(session_selector, options);
      } else {
        ctx.error_argument_type(0, "a function");
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.muxHTTP
  method("muxHTTP", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Str *layout;
      Function *session_selector = nullptr;
      Object *options = nullptr;
      if (
        ctx.try_arguments(1, &layout, &session_selector, &options) ||
        ctx.try_arguments(1, &layout, &options)
      ) {
        thiz->as<FilterConfigurator>()->mux_http(session_selector, options);
        thiz->as<FilterConfigurator>()->to(layout);
      } else if (
        ctx.try_arguments(0, &session_selector, &options) ||
        ctx.try_arguments(0, &options)
      ) {
        thiz->as<FilterConfigurator>()->mux_http(session_selector, options);
      } else {
        ctx.error_argument_type(0, "a function");
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.output
  method("output", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Function *output_f = nullptr;
    if (!ctx.arguments(0, &output_f)) return;
    try {
      thiz->as<FilterConfigurator>()->output(output_f);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.pack
  method("pack", [](Context &ctx, Object *thiz, Value &result) {
    int batch_size = 1;
    Object *options = nullptr;
    if (!ctx.arguments(0, &batch_size, &options)) return;
    try {
      thiz->as<FilterConfigurator>()->pack(batch_size, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.print
  method("print", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<FilterConfigurator>()->print();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.read
  method("read", [](Context &ctx, Object *thiz, Value &result) {
    Value pathname;
    if (!ctx.arguments(1, &pathname)) return;
    try {
      thiz->as<FilterConfigurator>()->read(pathname);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.repeat
  method("repeat", [](Context &ctx, Object *thiz, Value &result) {
    int count;
    Array* array;
    Function *f;
    if (ctx.get(0, count)) {
      if (!ctx.check(1, f)) return;
      for (int i = 0; i < count; i++) {
        Value args[2], ret;
        args[0].set(thiz);
        args[1].set(i);
        (*f)(ctx, 2, args, ret);
        if (!ctx.ok()) return;
      }
    } else if (ctx.get(0, array)) {
      if (!ctx.check(1, f)) return;
      array->iterate_while(
        [&](Value &v, int i) {
          Value args[3], ret;
          args[0].set(thiz);
          args[1] = v;
          args[2].set(i);
          (*f)(ctx, 3, args, ret);
          return ctx.ok();
        }
      );
    } else {
      ctx.error_argument_type(0, "a number or an array");
    }
    result.set(thiz);
  });

  // FilterConfigurator.replaceData
  method("replaceData", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<FilterConfigurator>()->replace_event(Event::Type::Data, replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.replaceMessage
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
      thiz->as<FilterConfigurator>()->replace_message(replacement, size_limit);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.replaceMessageBody
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
      thiz->as<FilterConfigurator>()->replace_body(replacement, size_limit);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.replaceMessageEnd
  method("replaceMessageEnd", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<FilterConfigurator>()->replace_event(Event::Type::MessageEnd, replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.replaceMessageStart
  method("replaceMessageStart", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<FilterConfigurator>()->replace_event(Event::Type::MessageStart, replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.replaceStreamEnd
  method("replaceStreamEnd", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<FilterConfigurator>()->replace_event(Event::Type::StreamEnd, replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.replaceStreamStart
  method("replaceStreamStart", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<FilterConfigurator>()->replace_start(replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.replay
  method("replay", [](Context &ctx, Object *thiz, Value &result) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    try {
      thiz->as<FilterConfigurator>()->replay(options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.serveHTTP
  method("serveHTTP", [](Context &ctx, Object *thiz, Value &result) {
    Object *handler;
    if (!ctx.arguments(1, &handler)) return;
    try {
      thiz->as<FilterConfigurator>()->serve_http(handler);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.split
  method("split", [](Context &ctx, Object *thiz, Value &result) {
    pipy::Data *separator;
    Str *separator_str;
    Function *callback;
    try {
      if (ctx.try_arguments(1, &separator)) {
        thiz->as<FilterConfigurator>()->split(separator);
      } else if (ctx.try_arguments(1, &separator_str)) {
        thiz->as<FilterConfigurator>()->split(separator_str);
      } else if (ctx.try_arguments(1, &callback)) {
        thiz->as<FilterConfigurator>()->split(callback);
      } else {
        ctx.error_argument_type(0, "a string, Data or function");
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.tee
  method("tee", [](Context &ctx, Object *thiz, Value &result) {
    Value filename;
    if (!ctx.arguments(1, &filename)) return;
    try {
      thiz->as<FilterConfigurator>()->tee(filename);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.throttleConcurrency
  method("throttleConcurrency", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Object *quota;
    if (!ctx.arguments(1, &quota)) return;
    try {
      thiz->as<FilterConfigurator>()->throttle_concurrency(quota);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.throttleDataRate
  method("throttleDataRate", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Object *quota;
    if (!ctx.arguments(1, &quota)) return;
    try {
      thiz->as<FilterConfigurator>()->throttle_data_rate(quota);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.throttleMessageRate
  method("throttleMessageRate", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Object *quota;
    if (!ctx.arguments(1, &quota)) return;
    try {
      thiz->as<FilterConfigurator>()->throttle_message_rate(quota);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // FilterConfigurator.use
  method("use", [](Context &ctx, Object *thiz, Value &result) {
    static const std::string s_dot_so(".so");
    std::string module;
    Array *modules;
    Str *pipeline = nullptr;
    Str *pipeline_down = nullptr;
    Function *when = nullptr;
    auto root = static_cast<pipy::Context*>(ctx.root());
    auto worker = root->worker();
    if (
      ctx.try_arguments(3, &modules, &pipeline, &pipeline_down, &when) ||
      ctx.try_arguments(2, &modules, &pipeline, &when)
    ) {
      std::list<JSModule*> mods;
      modules->iterate_while(
        [&](pjs::Value &v, int) {
          auto s = v.to_string();
          auto path = utils::path_normalize(s->str());
          s->release();
          auto mod = worker->load_js_module(path);
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
          thiz->as<FilterConfigurator>()->use(mods, pipeline, pipeline_down, when);
          result.set(thiz);
        } catch (std::runtime_error &err) {
          ctx.error(err);
        }
      }
    } else if (ctx.arguments(1, &module, &pipeline)) {
      if (utils::ends_with(module, s_dot_so)) {
        try {
          auto mod = worker->load_native_module(module);
          thiz->as<FilterConfigurator>()->use(mod, pipeline);
          result.set(thiz);
        } catch (std::runtime_error &err) {
          ctx.error(err);
        }
      } else {
        auto path = utils::path_normalize(module);
        auto mod = worker->load_js_module(path);
        if (!mod) {
          std::string msg("[pjs] Cannot load module: ");
          msg += module;
          ctx.error(msg);
          return;
        }
        try {
          thiz->as<FilterConfigurator>()->use(mod, pipeline);
          result.set(thiz);
        } catch (std::runtime_error &err) {
          ctx.error(err);
        }
      }
    }
  });

  // FilterConfigurator.wait
  method("wait", [](Context &ctx, Object *thiz, Value &result) {
    Function *condition;
    Object *options = nullptr;
    if (!ctx.arguments(1, &condition, &options)) return;
    try {
      thiz->as<FilterConfigurator>()->wait(condition, options);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

}

template<> void ClassDef<Configuration>::init() {
  super<FilterConfigurator>();

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
    if (ctx.argc() == 0) {
      try {
        thiz->as<Configuration>()->pipeline();
        result.set(thiz);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
    } else {
      std::string name;
      if (!ctx.arguments(1, &name)) return;
      try {
        thiz->as<Configuration>()->pipeline(name);
        result.set(thiz);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
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

  // Configuration.watch
  method("watch", [](Context &ctx, Object *thiz, Value &result) {
    std::string filename;
    try {
      if (!ctx.arguments(1, &filename)) return;
      thiz->as<Configuration>()->watch(filename);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

} // namespace pjs
