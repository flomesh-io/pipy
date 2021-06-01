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
#include "context.hpp"
#include "worker.hpp"
#include "graph.hpp"
#include "utils.hpp"

// all filters
#include "filters/connect.hpp"
#include "filters/decompress-body.hpp"
#include "filters/demux.hpp"
#include "filters/dubbo.hpp"
#include "filters/dummy.hpp"
#include "filters/dump.hpp"
#include "filters/exec.hpp"
#include "filters/fork.hpp"
#include "filters/http.hpp"
#include "filters/link.hpp"
#include "filters/mux.hpp"
#include "filters/on-body.hpp"
#include "filters/on-event.hpp"
#include "filters/on-message.hpp"
#include "filters/on-start.hpp"
#include "filters/print.hpp"
#include "filters/replace-body.hpp"
#include "filters/replace-event.hpp"
#include "filters/replace-message.hpp"
#include "filters/replace-start.hpp"
#include "filters/tap.hpp"
#include "filters/use.hpp"
#include "filters/wait.hpp"

#include <stdexcept>
#include <sstream>

namespace pipy {

bool Configuration::s_reuse_port = false;

Configuration::Configuration(pjs::Object *context_prototype) {
  std::list<pjs::Field*> fields;
  if (context_prototype) {
    context_prototype->iterate_all([&](pjs::Str *key, pjs::Value &val) {
      fields.push_back(new pjs::Variable(key->str(), val, pjs::Field::Enumerable | pjs::Field::Writable));
    });
  }

  auto cls = pjs::Class::make(
    "ContextData",
    pjs::class_of<ContextDataBase>(),
    fields
  );

  m_context_class = cls;
}

void Configuration::listen(int port, pjs::Object *options) {
  bool reuse_port = s_reuse_port;
  bool ssl = false;
  std::string cert_pem, key_pem;

  if (options) {
    pjs::Value reuse, tls;
    options->get("reuse", reuse);
    options->get("tls", tls);

    if (!reuse.is_undefined()) {
      reuse_port = reuse.to_boolean();
    }

    if (!tls.is_undefined()) {
      if (!tls.is_object()) throw std::runtime_error("option tls must be an object");
      if (auto *o = tls.o()) {
        ssl = true;
        pjs::Value cert, key;
        o->get("cert", cert);
        o->get("key", key);
        if (!cert.is_undefined()) {
          auto *s = cert.to_string();
          cert_pem = s->str();
          s->release();
        }
        if (!key.is_undefined()) {
          auto *s = key.to_string();
          key_pem = s->str();
          s->release();
        }
      }
    }
  }

  asio::ssl::context ssl_context(asio::ssl::context::sslv23);

  if (ssl) {
    if (!cert_pem.empty()) ssl_context.use_certificate_chain(asio::const_buffer(cert_pem.c_str(), cert_pem.length()));
    if (!key_pem.empty()) ssl_context.use_private_key(asio::const_buffer(key_pem.c_str(), key_pem.length()), asio::ssl::context::pem);
  }

  m_listens.push_back({
    "0.0.0.0",
    port,
    reuse_port,
    ssl,
    std::move(ssl_context)
  });

  m_current_filters = &m_listens.back().filters;
}

void Configuration::task(double interval) {
  if (interval < 0.01 || interval > 24 * 60 * 60) throw std::runtime_error("time interval out of range");
  std::string name("Task #");
  name += std::to_string(m_tasks.size() + 1);
  m_tasks.push_back({ name, utils::to_string(interval) + 's' });
  m_current_filters = &m_tasks.back().filters;
}

void Configuration::task(const std::string &interval) {
  auto t = utils::get_seconds(interval);
  if (t < 0.01 || t > 24 * 60 * 60) throw std::runtime_error("time interval out of range");
  std::string name("Task #");
  name += std::to_string(m_tasks.size() + 1);
  m_tasks.push_back({ name, interval });
  m_current_filters = &m_tasks.back().filters;
}

void Configuration::pipeline(const std::string &name) {
  m_named_pipelines.push_back({ name });
  m_current_filters = &m_named_pipelines.back().filters;
}

void Configuration::connect(const pjs::Value &target, pjs::Object *options) {
  append_filter(new Connect(target, options));
}

void Configuration::decode_dubbo() {
  append_filter(new dubbo::Decoder());
}

void Configuration::decode_http_request() {
  append_filter(new http::RequestDecoder());
}

void Configuration::decode_http_response() {
  append_filter(new http::ResponseDecoder());
}

void Configuration::decompress_body(pjs::Str *algorithm) {
  auto algo = pjs::EnumDef<DecompressBody::Algorithm>::value(algorithm);
  if (int(algo) < 0) throw std::runtime_error("unknown decompression algorithm");
  append_filter(new DecompressBody(algo));
}

void Configuration::demux(pjs::Str *target) {
  append_filter(new Demux(target));
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

void Configuration::encode_http_request(pjs::Object *request_obj) {
  append_filter(new http::RequestEncoder(request_obj));
}

void Configuration::encode_http_response(pjs::Object *response_obj) {
  append_filter(new http::ResponseEncoder(response_obj));
}

void Configuration::exec(const pjs::Value &command) {
  append_filter(new Exec(command));
}

void Configuration::fork(pjs::Str *target, pjs::Object *session_data) {
  append_filter(new Fork(target, session_data));
}

void Configuration::link(size_t count, pjs::Str **targets, pjs::Function **conditions) {
  std::list<Link::Route> routes;
  for (size_t i = 0; i < count; i++) {
    routes.emplace_back(targets[i], conditions[i]);
  }
  append_filter(new Link(routes));
}

void Configuration::mux(pjs::Str *target, pjs::Function *selector) {
  append_filter(new Mux(target, selector));
}

void Configuration::on_body(pjs::Function *callback) {
  append_filter(new OnBody(callback));
}

void Configuration::on_event(Event::Type type, pjs::Function *callback) {
  append_filter(new OnEvent(type, callback));
}

void Configuration::on_message(pjs::Function *callback) {
  append_filter(new OnMessage(callback));
}

void Configuration::on_start(pjs::Function *callback) {
  append_filter(new OnStart(callback));
}

void Configuration::print() {
  append_filter(new Print());
}

void Configuration::replace_body(const pjs::Value &replacement) {
  append_filter(new ReplaceBody(replacement));
}

void Configuration::replace_event(Event::Type type, const pjs::Value &replacement) {
  append_filter(new ReplaceEvent(type, replacement));
}

void Configuration::replace_message(const pjs::Value &replacement) {
  append_filter(new ReplaceMessage(replacement));
}

void Configuration::replace_start(const pjs::Value &replacement) {
  append_filter(new ReplaceStart(replacement));
}

void Configuration::tap(const pjs::Value &quota, const pjs::Value &account) {
  append_filter(new Tap(quota, account));
}

void Configuration::use(Module *module, pjs::Str *pipeline, pjs::Object *argv) {
  append_filter(new Use(module, pipeline, argv));
}

void Configuration::wait(pjs::Function *condition) {
  append_filter(new Wait(condition));
}

void Configuration::apply(Module *mod) {
  mod->m_context_class = m_context_class;

  auto make_pipeline = [&](
    Pipeline::Type type,
    const std::string &name,
    std::list<std::unique_ptr<Filter>> &filters
  ) -> Pipeline*
  {
    auto pipeline = Pipeline::make(mod, type, name);
    for (auto &f : filters) {
      pipeline->append(f.release());
    }
    return pipeline;
  };

  for (auto &i : m_named_pipelines) {
    auto s = pjs::Str::make(i.name);
    auto p = make_pipeline(Pipeline::NAMED, i.name, i.filters);
    mod->m_named_pipelines[s] = p;
  }

  for (auto &i : m_listens) {
    auto name = i.ip + ':' + std::to_string(i.port);
    auto p = make_pipeline(Pipeline::LISTEN, name, i.filters);
    auto listener = Listener::get(i.port);
    if (!listener) {
      listener = i.ssl
        ? Listener::make(i.ip, i.port, i.reuse, std::move(i.ssl_context))
        : Listener::make(i.ip, i.port, i.reuse);
    }
    listener->open(p);
  }

  for (auto &i : m_tasks) {
    auto p = make_pipeline(Pipeline::TASK, i.name, i.filters);
    Task::make(i.interval, p)->start();
  }
}

void Configuration::draw(Graph &g) {
  auto add_filters = [](Graph::Pipeline &gp, const std::list<std::unique_ptr<Filter>> &filters) {
    for (const auto &f : filters) {
      Graph::Filter gf;
      gf.name = f->draw(gf.links, gf.fork);
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
    p.name += i.ip;
    p.name += ':';
    p.name += std::to_string(i.port);
    add_filters(p, i.filters);
    g.add_root_pipeline(std::move(p));
  }

  for (const auto &i : m_tasks) {
    Graph::Pipeline p;
    p.name = i.name;
    p.name += " (every ";
    p.name += i.interval;
    p.name += ')';
    add_filters(p, i.filters);
    g.add_root_pipeline(std::move(p));
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

  // Configuration.listen
  method("listen", [](Context &ctx, Object *thiz, Value &result) {
    int port;
    Str *port_str;
    pjs::Object *options = nullptr;
    if (ctx.try_arguments(1, &port_str, &options)) {
      port = std::atoi(port_str->c_str());
    } else if (!ctx.arguments(1, &port, &options)) {
      return;
    }
    try {
      thiz->as<Configuration>()->listen(port, options);
      result.set(thiz);
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

  // Configuration.task
  method("task", [](Context &ctx, Object *thiz, Value &result) {
    double interval;
    std::string interval_str;
    try {
      if (ctx.try_arguments(1, &interval)) {
        thiz->as<Configuration>()->task(interval);
      } else if (ctx.try_arguments(1, &interval_str)) {
        thiz->as<Configuration>()->task(interval_str);
      } else {
        ctx.error_argument_type(0, "a number or a string");
        return;
      }
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

  // Configuration.decodeDubbo
  method("decodeDubbo", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->decode_dubbo();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decodeHttpRequest
  method("decodeHttpRequest", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->decode_http_request();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decodeHttpResponse
  method("decodeHttpResponse", [](Context &ctx, Object *thiz, Value &result) {
    try {
      thiz->as<Configuration>()->decode_http_response();
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.decompressMessageBody
  method("decompressMessageBody", [](Context &ctx, Object *thiz, Value &result) {
    Str *algorithm;
    if (!ctx.arguments(1, &algorithm)) return;
    try {
      thiz->as<Configuration>()->decompress_body(algorithm);
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

  // Configuration.encodeHttpRequest
  method("encodeHttpRequest", [](Context &ctx, Object *thiz, Value &result) {
    Object *request_obj = nullptr;
    if (!ctx.arguments(0, &request_obj)) return;
    try {
      thiz->as<Configuration>()->encode_http_request(request_obj);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.encodeHttpResponse
  method("encodeHttpResponse", [](Context &ctx, Object *thiz, Value &result) {
    Object *response_obj = nullptr;
    if (!ctx.arguments(0, &response_obj)) return;
    try {
      thiz->as<Configuration>()->encode_http_response(response_obj);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.exec
  method("exec", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Value command;
    if (!ctx.arguments(1, &command)) return;
    try {
      thiz->as<Configuration>()->exec(command);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.fork
  method("fork", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Str *target;
    pjs::Object *session_data = nullptr;
    if (!ctx.arguments(1, &target, &session_data)) return;
    try {
      thiz->as<Configuration>()->fork(target, session_data);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.link
  method("link", [](Context &ctx, Object *thiz, Value &result) {
    int n = (ctx.argc() + 1) >> 1;
    pjs::Str *targets[n];
    pjs::Function *conditions[n];
    for (int i = 0; i < n; i++) {
      int a = (i << 1);
      int b = (i << 1) + 1;
      if (!ctx.arg(a).is_string()) {
        ctx.error_argument_type(a, "a string");
        return;
      }
      targets[i] = ctx.arg(a).s();
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

  // Configuration.mux
  method("mux", [](Context &ctx, Object *thiz, Value &result) {
    pjs::Str *target;
    pjs::Function *selector = nullptr;
    if (!ctx.arguments(1, &target, &selector)) return;
    try {
      thiz->as<Configuration>()->mux(target, selector);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.onSessionStart
  method("onSessionStart", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_start(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.onData
  method("onData", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_event(Event::Data, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.onMessage
  method("onMessage", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_message(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.onMessageStart
  method("onMessageStart", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_event(Event::MessageStart, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.onMessageBody
  method("onMessageBody", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_body(callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.onMessageEnd
  method("onMessageEnd", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_event(Event::MessageEnd, callback);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.onSessionEnd
  method("onSessionEnd", [](Context &ctx, Object *thiz, Value &result) {
    Function *callback = nullptr;
    if (!ctx.arguments(1, &callback)) return;
    try {
      thiz->as<Configuration>()->on_event(Event::SessionEnd, callback);
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

  // Configuration.replaceSessionStart
  method("replaceSessionStart", [](Context &ctx, Object *thiz, Value &result) {
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
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<Configuration>()->replace_message(replacement);
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
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<Configuration>()->replace_body(replacement);
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

  // Configuration.replaceSessionEnd
  method("replaceSessionEnd", [](Context &ctx, Object *thiz, Value &result) {
    Value replacement;
    if (!ctx.arguments(0, &replacement)) return;
    try {
      thiz->as<Configuration>()->replace_event(Event::SessionEnd, replacement);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.tap
  method("tap", [](Context &ctx, Object *thiz, Value &result) {
    Value quota, account;
    if (!ctx.arguments(1, &quota, &account)) return;
    try {
      thiz->as<Configuration>()->tap(quota, account);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.use
  method("use", [](Context &ctx, Object *thiz, Value &result) {
    std::string module;
    Str *pipeline;
    Object* argv = nullptr;
    if (!ctx.arguments(2, &module, &pipeline)) return;
    if (ctx.argc() == 3) {
      auto &arg3 = ctx.arg(2);
      if (arg3.is_array() || arg3.is_function()) {
        argv = arg3.o();
      }
    } else if (ctx.argc() > 2) {
      auto a = Array::make(ctx.argc() - 2);
      for (int i = 2; i < ctx.argc(); i++) {
        a->set(i - 2, ctx.arg(i));
      }
      argv = a;
    }
    auto path = utils::path_normalize(module);
    auto root = static_cast<pipy::Context*>(ctx.root());
    auto worker = root->worker();
    auto mod = worker->load_module(path);
    if (!mod) {
      std::string msg("[pjs] Cannot load module: ");
      msg += module;
      ctx.error(msg);
      return;
    }
    try {
      thiz->as<Configuration>()->use(mod, pipeline, argv);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // Configuration.wait
  method("wait", [](Context &ctx, Object *thiz, Value &result) {
    Function *condition;
    if (!ctx.arguments(1, &condition)) return;
    try {
      thiz->as<Configuration>()->wait(condition);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

}

} // namespace pjs
