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

#include "api/pipeline-api.hpp"
#include "input.hpp"
#include "message.hpp"
#include "worker.hpp"

// all filters
#include "filters/bgp.hpp"
#include "filters/connect.hpp"
#include "filters/compress.hpp"
#include "filters/decompress.hpp"
#include "filters/deframe.hpp"
#include "filters/demux.hpp"
#include "filters/detect-protocol.hpp"
#include "filters/dubbo.hpp"
#include "filters/dummy.hpp"
#include "filters/dump.hpp"
#include "filters/exec.hpp"
#include "filters/fcgi.hpp"
#include "filters/fork.hpp"
#include "filters/http.hpp"
#include "filters/insert.hpp"
#include "filters/loop.hpp"
#include "filters/mime.hpp"
#include "filters/mqtt.hpp"
#include "filters/mux.hpp"
#include "filters/netlink.hpp"
#include "filters/on-body.hpp"
#include "filters/on-event.hpp"
#include "filters/on-message.hpp"
#include "filters/on-start.hpp"
#include "filters/pipe.hpp"
#include "filters/print.hpp"
#include "filters/proxy-protocol.hpp"
#include "filters/repeat.hpp"
#include "filters/replace-body.hpp"
#include "filters/replace-event.hpp"
#include "filters/replace-message.hpp"
#include "filters/replace-start.hpp"
#include "filters/resp.hpp"
#include "filters/socks.hpp"
#include "filters/split.hpp"
#include "filters/swap.hpp"
#include "filters/tee.hpp"
#include "filters/thrift.hpp"
#include "filters/throttle.hpp"
#include "filters/tls.hpp"
#include "filters/wait.hpp"
#include "filters/websocket.hpp"

namespace pipy {

//
// PipelineDesigner
//

auto PipelineDesigner::make_pipeline_layout(pjs::Context &ctx, pjs::Function *builder) -> PipelineLayout* {
  auto worker = static_cast<Worker*>(ctx.instance());
  auto pl = PipelineLayout::make(worker);
  auto pd = PipelineDesigner::make(pl);
  pjs::Value arg(pd), ret;
  (*builder)(ctx, 1, &arg, ret);
  pd->close();
  if (ctx.ok()) return pl;
  pl->retain();
  pl->release();
  return nullptr;
}

auto PipelineDesigner::trace_location(pjs::Context &ctx) -> PipelineDesigner* {
  if (auto caller = ctx.caller()) {
    m_current_location = caller->call_site();
  }
  return this;
}

void PipelineDesigner::on_start(pjs::Object *starting_events) {
  if (!m_layout) throw std::runtime_error("pipeline layout is already built");
  if (m_current_filter) throw std::runtime_error("onStart() is only allowed prior to all filters");
  if (m_has_on_start) throw std::runtime_error("duplicate onStart()");
  m_layout->on_start(starting_events);
  m_layout->on_start_location(m_current_location);
  m_has_on_start = true;
}

void PipelineDesigner::on_end(pjs::Function *handler) {
  if (!m_layout) throw std::runtime_error("pipeline layout is already built");
  if (m_has_on_end) throw std::runtime_error("duplicate onEnd()");
  m_layout->on_end(handler);
  m_has_on_end = true;
}

void PipelineDesigner::to(pjs::Str *name) {
  if (!m_current_joint_filter) {
    throw std::runtime_error("calling to() without a joint-filter");
  }
  m_current_joint_filter->add_sub_pipeline(name);
  m_current_joint_filter = nullptr;
}

void PipelineDesigner::to(PipelineLayout *layout) {
  if (!m_current_joint_filter) {
    throw std::runtime_error("calling to() without a joint-filter");
  }
  m_current_joint_filter->add_sub_pipeline(layout);
  m_current_joint_filter = nullptr;
}

void PipelineDesigner::close() {
  m_current_filter = nullptr;
  m_current_joint_filter = nullptr;
  m_layout = nullptr;
}

void PipelineDesigner::accept_http_tunnel(pjs::Function *handler) {
  require_sub_pipeline(append_filter(new http::TunnelServer(handler)));
}

void PipelineDesigner::accept_proxy_protocol(pjs::Function *handler) {
  require_sub_pipeline(append_filter(new proxy_protocol::Server(handler)));
}

void PipelineDesigner::accept_socks(pjs::Function *on_connect) {
  require_sub_pipeline(append_filter(new socks::Server(on_connect)));
}

void PipelineDesigner::accept_tls(pjs::Object *options) {
  require_sub_pipeline(append_filter(new tls::Server(options)));
}

void PipelineDesigner::compress(const pjs::Value &algorithm) {
  append_filter(new Compress(algorithm));
}

void PipelineDesigner::compress_http(const pjs::Value &algorithm) {
  append_filter(new CompressHTTP(algorithm));
}

void PipelineDesigner::connect(const pjs::Value &target, pjs::Object *options) {
  if (options && options->is_function()) {
    append_filter(new Connect(target, options->as<pjs::Function>()));
  } else {
    append_filter(new Connect(target, options));
  }
}

void PipelineDesigner::connect_http_tunnel(pjs::Object *handshake) {
  require_sub_pipeline(append_filter(new http::TunnelClient(handshake)));
}

void PipelineDesigner::connect_proxy_protocol(const pjs::Value &address) {
  require_sub_pipeline(append_filter(new proxy_protocol::Client(address)));
}

void PipelineDesigner::connect_socks(const pjs::Value &address) {
  require_sub_pipeline(append_filter(new socks::Client(address)));
}

void PipelineDesigner::connect_tls(pjs::Object *options) {
  require_sub_pipeline(append_filter(new tls::Client(options)));
}

void PipelineDesigner::decode_bgp(pjs::Object *options) {
  append_filter(new bgp::Decoder(options));
}

void PipelineDesigner::decode_dubbo() {
  append_filter(new dubbo::Decoder());
}

void PipelineDesigner::decode_http_request(pjs::Function *handler) {
  append_filter(new http::RequestDecoder(handler));
}

void PipelineDesigner::decode_http_response(pjs::Function *handler) {
  append_filter(new http::ResponseDecoder(handler));
}

void PipelineDesigner::decode_mqtt() {
  append_filter(new mqtt::Decoder());
}

void PipelineDesigner::decode_multipart() {
  append_filter(new mime::MultipartDecoder());
}

void PipelineDesigner::decode_netlink() {
  append_filter(new netlink::Decoder());
}

void PipelineDesigner::decode_resp() {
  append_filter(new resp::Decoder());
}

void PipelineDesigner::decode_thrift() {
  append_filter(new thrift::Decoder());
}

void PipelineDesigner::decode_websocket() {
  append_filter(new websocket::Decoder());
}

void PipelineDesigner::decompress(const pjs::Value &algorithm) {
  append_filter(new Decompress(algorithm));
}

void PipelineDesigner::decompress_http() {
  append_filter(new DecompressHTTP());
}

void PipelineDesigner::deframe(pjs::Object *states) {
  append_filter(new Deframe(states));
}

void PipelineDesigner::demux(pjs::Object *options) {
  require_sub_pipeline(append_filter(new Demux(options)));
}

void PipelineDesigner::demux_fcgi() {
  require_sub_pipeline(append_filter(new fcgi::Demux()));
}

void PipelineDesigner::demux_http(pjs::Object *options) {
  require_sub_pipeline(append_filter(new http::Demux(options)));
}

void PipelineDesigner::detect_protocol(pjs::Function *handler) {
  append_filter(new ProtocolDetector(handler));
}

void PipelineDesigner::dummy() {
  append_filter(new Dummy());
}

void PipelineDesigner::dump(const pjs::Value &tag) {
  append_filter(new Dump(tag));
}

void PipelineDesigner::encode_bgp(pjs::Object *options) {
  append_filter(new bgp::Encoder(options));
}

void PipelineDesigner::encode_dubbo() {
  append_filter(new dubbo::Encoder());
}

void PipelineDesigner::encode_http_request(pjs::Object *options, pjs::Function *handler) {
  append_filter(new http::RequestEncoder(options, handler));
}

void PipelineDesigner::encode_http_response(pjs::Object *options, pjs::Function *handler) {
  append_filter(new http::ResponseEncoder(options, handler));
}

void PipelineDesigner::encode_mqtt() {
  append_filter(new mqtt::Encoder());
}

void PipelineDesigner::encode_netlink() {
  append_filter(new netlink::Encoder());
}

void PipelineDesigner::encode_resp() {
  append_filter(new resp::Encoder());
}

void PipelineDesigner::encode_thrift() {
  append_filter(new thrift::Encoder());
}

void PipelineDesigner::encode_websocket() {
  append_filter(new websocket::Encoder());
}

void PipelineDesigner::exec(const pjs::Value &command, pjs::Object *options) {
  append_filter(new Exec(command, options));
}

void PipelineDesigner::fork(const pjs::Value &init_args) {
  require_sub_pipeline(append_filter(new Fork(init_args)));
}

void PipelineDesigner::fork_join(pjs::Object *init_args) {
  require_sub_pipeline(append_filter(new Fork(Fork::JOIN, init_args)));
}

void PipelineDesigner::fork_race(pjs::Object *init_args) {
  require_sub_pipeline(append_filter(new Fork(Fork::RACE, init_args)));
}

void PipelineDesigner::handle(Event::Type type, pjs::Function *callback) {
  append_filter(new OnEvent(type, callback));
}

void PipelineDesigner::handle_body(pjs::Function *callback, pjs::Object *options) {
  append_filter(new OnBody(callback, options));
}

void PipelineDesigner::handle_message(pjs::Function *callback, pjs::Object *options) {
  append_filter(new OnMessage(callback, options));
}

void PipelineDesigner::handle_start(pjs::Function *callback) {
  append_filter(new OnStart(callback));
}

void PipelineDesigner::handle_tls_client_hello(pjs::Function *callback) {
  append_filter(new tls::OnClientHello(callback));
}

void PipelineDesigner::insert(pjs::Object *events) {
  append_filter(new Insert(events));
}

void PipelineDesigner::loop() {
  require_sub_pipeline(append_filter(new Loop()));
}

void PipelineDesigner::mux(pjs::Function *session_selector, pjs::Object *options) {
  if (options && options->is_function()) {
    require_sub_pipeline(append_filter(new Mux(session_selector, options->as<pjs::Function>())));
  } else {
    require_sub_pipeline(append_filter(new Mux(session_selector, options)));
  }
}

void PipelineDesigner::mux_fcgi(pjs::Function *session_selector, pjs::Object *options) {
  if (options && options->is_function()) {
    require_sub_pipeline(append_filter(new fcgi::Mux(session_selector, options->as<pjs::Function>())));
  } else {
    require_sub_pipeline(append_filter(new fcgi::Mux(session_selector, options)));
  }
}

void PipelineDesigner::mux_http(pjs::Function *session_selector, pjs::Object *options) {
  if (options && options->is_function()) {
    require_sub_pipeline(append_filter(new http::Mux(session_selector, options->as<pjs::Function>())));
  } else {
    require_sub_pipeline(append_filter(new http::Mux(session_selector, options)));
  }
}

void PipelineDesigner::repeat(pjs::Function *condition) {
  require_sub_pipeline(append_filter(new Repeat(condition)));
}

void PipelineDesigner::replace(Event::Type type, pjs::Object *replacement) {
  append_filter(new ReplaceEvent(type, replacement));
}

void PipelineDesigner::replace_body(pjs::Object *replacement, pjs::Object *options) {
  append_filter(new ReplaceBody(replacement, options));
}

void PipelineDesigner::replace_message(pjs::Object *replacement, pjs::Object *options) {
  append_filter(new ReplaceMessage(replacement, options));
}

void PipelineDesigner::replace_start(pjs::Object *replacement) {
  append_filter(new ReplaceStart(replacement));
}

void PipelineDesigner::pipe(const pjs::Value &target, pjs::Object *target_map, pjs::Object *init_args) {
  append_filter(new Pipe(target, target_map, init_args));
}

void PipelineDesigner::pipe_next() {
  append_filter(new PipeNext());
}

void PipelineDesigner::print() {
  append_filter(new Print());
}

void PipelineDesigner::serve_http(pjs::Object *handler, pjs::Object *options) {
  append_filter(new http::Server(handler, options));
}

void PipelineDesigner::split(const pjs::Value &separator) {
  append_filter(new Split(separator));
}

void PipelineDesigner::swap(const pjs::Value &hub) {
  append_filter(new Swap(hub));
}

void PipelineDesigner::tee(const pjs::Value &filename, pjs::Object *options) {
  append_filter(new Tee(filename, options));
}

void PipelineDesigner::throttle_concurrency(pjs::Object *quota, pjs::Object *options) {
  append_filter(new ThrottleConcurrency(quota, options));
}

void PipelineDesigner::throttle_data_rate(pjs::Object *quota, pjs::Object *options) {
  append_filter(new ThrottleDataRate(quota, options));
}

void PipelineDesigner::throttle_message_rate(pjs::Object *quota, pjs::Object *options) {
  append_filter(new ThrottleMessageRate(quota, options));
}

void PipelineDesigner::wait(pjs::Function *condition, pjs::Object *options) {
  append_filter(new Wait(condition, options));
}

void PipelineDesigner::check_integrity() {
  if (m_current_joint_filter) {
    throw std::runtime_error("missing .to(...) for the last filter");
  }
}

auto PipelineDesigner::append_filter(Filter *filter) -> Filter* {
  if (!m_layout) {
    delete filter;
    throw std::runtime_error("pipeline layout is already built");
  }
  check_integrity();
  filter->set_location(m_current_location);
  m_layout->append(filter);
  m_current_filter = filter;
  return filter;
}

void PipelineDesigner::require_sub_pipeline(Filter *filter) {
  m_current_joint_filter = filter;
}

//
// PipelineLayoutWrapper
//

auto PipelineLayoutWrapper::spawn(Context *ctx) -> Pipeline* {
  return Pipeline::make(m_layout, ctx);
}

//
// PipelineLayoutWrapper::Constructor
//

void PipelineLayoutWrapper::Constructor::operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret) {
  pjs::Function *f;
  if (!ctx.arguments(1, &f)) return;
  auto pl = PipelineDesigner::make_pipeline_layout(ctx, f);
  if (!pl) return;
  ret.set(PipelineLayoutWrapper::make(pl));
}

//
// PipelineWrapper
//

auto PipelineWrapper::start(int argc, pjs::Value argv[]) -> pjs::Promise* {
  retain();
  auto p = m_pipeline.get();
  auto promise = pjs::Promise::make();
  m_settler = pjs::Promise::Settler::make(promise);
  p->on_end(this);
  p->chain(EventTarget::input());
  p->start(argc, argv);
  return promise;
}

void PipelineWrapper::on_event(Event *evt) {
  if (evt->is<StreamEnd>()) {
    m_pipeline = nullptr;
  }
}

void PipelineWrapper::on_pipeline_result(Pipeline *p, pjs::Value &result) {
  m_settler->resolve(result);
  release();
}

//
// Hub
//

void Hub::join(EventTarget::Input *party) {
  if (m_broadcasting) { m_changing_parties.push_back({ true, party }); return; }
  if (m_pair[0] == party || m_pair[1] == party) return;
  if (m_parties.count(party) > 0) return;
  if (!m_pair[0]) { m_pair[0] = party; return; }
  if (!m_pair[1]) { m_pair[1] = party; return; }
  m_parties.insert(party);
}

void Hub::exit(EventTarget::Input *party) {
  if (m_broadcasting) { m_changing_parties.push_back({ false, party }); return; }
  if (m_pair[0] == party) { m_pair[0] = nullptr; return; }
  if (m_pair[1] == party) { m_pair[1] = nullptr; return; }
  m_parties.erase(party);
}

void Hub::broadcast(Event *evt, EventTarget::Input *from) {
  if (!m_broadcasting) {
    m_broadcasting = true;
    if (m_pair[0] && m_pair[0] != from) m_pair[0]->input(evt);
    if (m_pair[1] && m_pair[1] != from) m_pair[1]->input(evt);
    for (const auto &p : m_parties) if (p != from) p->input(evt);
    m_broadcasting = false;
    for (const auto &cp : m_changing_parties) {
      if (cp.join) {
        join(cp.party);
      } else {
        exit(cp.party);
      }
    }
    m_changing_parties.clear();
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<PipelineDesigner>::init() {

  // PipelineDesigner.onStart
  method("onStart", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    try {
      Object *starting_events = nullptr;
      if (!ctx.arguments(1, &starting_events)) return;
      if (!starting_events || (
          !starting_events->is<Function>() &&
          !Message::is_events(starting_events)
      )) {
        ctx.error_argument_type(0, "an Event, a Message, a function or an array");
        return;
      }
      config->on_start(starting_events);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.onEnd
  method("onEnd", [](Context &ctx, Object *thiz, Value &result) {
    auto config = thiz->as<PipelineDesigner>()->trace_location(ctx);
    try {
      Function *handler;
      if (!ctx.arguments(1, &handler)) return;
      config->on_end(handler);
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner.to
  method("to", [](Context &ctx, Object *thiz, Value &result) {
    try {
      Function *builder;
      PipelineLayoutWrapper *wrapper;
      if (ctx.get(0, builder) && builder) {
        if (auto pl = PipelineDesigner::make_pipeline_layout(ctx, builder)) {
          thiz->as<PipelineDesigner>()->to(pl);
        }
      } else if (ctx.get(0, wrapper) && wrapper) {
        thiz->as<PipelineDesigner>()->to(wrapper->get());
      } else {
        ctx.error_argument_type(0, "a function or a pipeline");
        return;
      }
      result.set(thiz);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  // PipelineDesigner[filterName]
  auto filter = [](
    const char *name,
    const std::function<void(Context &ctx, PipelineDesigner *obj)> &f
  ) {
    method(name, [=](Context &ctx, Object *thiz, Value &ret) {
      auto obj = thiz->as<PipelineDesigner>()->trace_location(ctx);
      try {
        f(ctx, obj);
        ret.set(obj);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
    });
  };

  // PipelineDesigner.acceptHTTPTunnel
  filter("acceptHTTPTunnel", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->accept_http_tunnel(handler);
  });

  // PipelineDesigner.acceptProxyProtocol
  filter("acceptProxyProtocol", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->accept_proxy_protocol(handler);
  });

  // PipelineDesigner.acceptSOCKS
  filter("acceptSOCKS", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->accept_socks(handler);
  });

  // PipelineDesigner.acceptTLS
  filter("acceptTLS", [](Context &ctx, PipelineDesigner *obj) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    obj->accept_tls(options);
  });

  // PipelineDesigner.compress
  filter("compress", [](Context &ctx, PipelineDesigner *obj) {
    Value algorithm;
    if (!ctx.arguments(1, &algorithm)) return;
    obj->compress(algorithm);
  });

  // PipelineDesigner.compressHTTP
  filter("compressHTTP", [](Context &ctx, PipelineDesigner *obj) {
    Value algorithm;
    if (!ctx.arguments(1, &algorithm)) return;
    obj->compress_http(algorithm);
  });

  // PipelineDesigner.connect
  filter("connect", [](Context &ctx, PipelineDesigner *obj) {
    Value target;
    Object *options = nullptr;
    if (!ctx.arguments(1, &target, &options)) return;
    obj->connect(target, options);
  });

  // PipelineDesigner.connectHTTPTunnel
  filter("connectHTTPTunnel", [](Context &ctx, PipelineDesigner *obj) {
    Object *handshake;
    if (!ctx.arguments(1, &handshake)) return;
    obj->connect_http_tunnel(handshake);
  });

  // PipelineDesigner.connectProxyProtocol
  filter("connectProxyProtocol", [](Context &ctx, PipelineDesigner *obj) {
    Value target;
    if (!ctx.arguments(1, &target)) return;
    obj->connect_proxy_protocol(target);
  });

  // PipelineDesigner.connectSOCKS
  filter("connectSOCKS", [](Context &ctx, PipelineDesigner *obj) {
    Value address;
    if (!ctx.arguments(1, &address)) return;
    obj->connect_socks(address);
  });

  // PipelineDesigner.connectTLS
  filter("connectTLS", [](Context &ctx, PipelineDesigner *obj) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    obj->connect_tls(options);
  });

  // PipelineDesigner.decodeBGP
  filter("decodeBGP", [](Context &ctx, PipelineDesigner *obj) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    obj->decode_bgp(options);
  });

  // PipelineDesigner.decodeDubbo
  filter("decodeDubbo", [](Context &ctx, PipelineDesigner *obj) {
    obj->decode_dubbo();
  });

  // PipelineDesigner.decodeHTTPRequest
  filter("decodeHTTPRequest", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler = nullptr;
    if (!ctx.arguments(0, &handler)) return;
    obj->decode_http_request(handler);
  });

  // PipelineDesigner.decodeHTTPResponse
  filter("decodeHTTPResponse", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler = nullptr;
    if (!ctx.arguments(0, &handler)) return;
    obj->decode_http_response(handler);
  });

  // PipelineDesigner.decodeMQTT
  filter("decodeMQTT", [](Context &ctx, PipelineDesigner *obj) {
    obj->decode_mqtt();
  });

  // PipelineDesigner.decodeMultipart
  filter("decodeMultipart", [](Context &ctx, PipelineDesigner *obj) {
    obj->decode_multipart();
  });

  // PipelineDesigner.decodeNetlink
  filter("decodeNetlink", [](Context &ctx, PipelineDesigner *obj) {
    obj->decode_netlink();
  });

  // PipelineDesigner.decodeRESP
  filter("decodeRESP", [](Context &ctx, PipelineDesigner *obj) {
    obj->decode_resp();
  });

  // PipelineDesigner.decodeThrift
  filter("decodeThrift", [](Context &ctx, PipelineDesigner *obj) {
    obj->decode_thrift();
  });

  // PipelineDesigner.decodeWebSocket
  filter("decodeWebSocket", [](Context &ctx, PipelineDesigner *obj) {
    obj->decode_websocket();
  });

  // PipelineDesigner.decompress
  filter("decompress", [](Context &ctx, PipelineDesigner *obj) {
    Value algorithm;
    if (!ctx.arguments(1, &algorithm)) return;
    obj->decompress(algorithm);
  });

  // PipelineDesigner.decompressHTTP
  filter("decompressHTTP", [](Context &ctx, PipelineDesigner *obj) {
    obj->decompress_http();
  });

  // PipelineDesigner.deframe
  filter("deframe", [](Context &ctx, PipelineDesigner *obj) {
    Object *states;
    if (!ctx.arguments(1, &states)) return;
    obj->deframe(states);
  });

  // PipelineDesigner.demux
  filter("demux", [](Context &ctx, PipelineDesigner *obj) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    obj->demux(options);
  });

  // PipelineDesigner.demuxFastCGI
  filter("demuxFastCGI", [](Context &ctx, PipelineDesigner *obj) {
    obj->demux_fcgi();
  });

  // PipelineDesigner.demuxHTTP
  filter("demuxHTTP", [](Context &ctx, PipelineDesigner *obj) {
    Object *options = nullptr;
    if (!ctx.arguments(0, &options)) return;
    obj->demux_http(options);
  });

  // PipelineDesigner.detectProtocol
  filter("detectProtocol", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->detect_protocol(handler);
  });

  // PipelineDesigner.dummy
  filter("dummy", [](Context &ctx, PipelineDesigner *obj) {
    obj->dummy();
  });

  // PipelineDesigner.dump
  filter("dump", [](Context &ctx, PipelineDesigner *obj) {
    Value tag;
    if (!ctx.arguments(0, &tag)) return;
    obj->dump(tag);
  });

  // PipelineDesigner.encodeBGP
  filter("encodeBGP", [](Context &ctx, PipelineDesigner *obj) {
    Object *options = nullptr;
    if (!ctx.arguments(1, &options)) return;
    obj->encode_bgp(options);
  });

  // PipelineDesigner.encodeDubbo
  filter("encodeDubbo", [](Context &ctx, PipelineDesigner *obj) {
    obj->encode_dubbo();
  });

  // PipelineDesigner.encodeHTTPRequest
  filter("encodeHTTPRequest", [](Context &ctx, PipelineDesigner *obj) {
    Object *options = nullptr;
    Function *handler = nullptr;
    if (ctx.is_function(0)) {
      if (!ctx.arguments(1, &handler, &options)) return;
    } else {
      if (!ctx.arguments(0, &options)) return;
    }
    obj->encode_http_request(options, handler);
  });

  // PipelineDesigner.encodeHTTPResponse
  filter("encodeHTTPResponse", [](Context &ctx, PipelineDesigner *obj) {
    Object *options = nullptr;
    Function *handler = nullptr;
    if (ctx.is_function(0)) {
      if (!ctx.arguments(1, &handler, &options)) return;
    } else {
      if (!ctx.arguments(0, &options)) return;
    }
    obj->encode_http_response(options, handler);
  });

  // PipelineDesigner.encodeMQTT
  filter("encodeMQTT", [](Context &ctx, PipelineDesigner *obj) {
    obj->encode_mqtt();
  });

  // PipelineDesigner.encodeNetlink
  filter("encodeNetlink", [](Context &ctx, PipelineDesigner *obj) {
    obj->encode_netlink();
  });

  // PipelineDesigner.encodeRESP
  filter("encodeRESP", [](Context &ctx, PipelineDesigner *obj) {
    obj->encode_resp();
  });

  // PipelineDesigner.encodeThrift
  filter("encodeThrift", [](Context &ctx, PipelineDesigner *obj) {
    obj->encode_thrift();
  });

  // PipelineDesigner.encodeWebSocket
  filter("encodeWebSocket", [](Context &ctx, PipelineDesigner *obj) {
    obj->encode_websocket();
  });

  // PipelineDesigner.exec
  filter("exec", [](Context &ctx, PipelineDesigner *obj) {
    Value command;
    Object *options = nullptr;
    if (!ctx.arguments(1, &command, &options)) return;
    obj->exec(command, options);
  });

  // PipelineDesigner.fork
  filter("fork", [](Context &ctx, PipelineDesigner *obj) {
    Value init_args;
    if (!ctx.arguments(0, &init_args)) return;
    obj->fork(init_args);
  });

  // PipelineDesigner.forkJoin
  filter("forkJoin", [](Context &ctx, PipelineDesigner *obj) {
    Array *init_args;
    Function *init_args_f;
    if (ctx.get(0, init_args) && init_args) {
      obj->fork_join(init_args);
    } else if (ctx.get(0, init_args_f) && init_args_f) {
      obj->fork_join(init_args_f);
    } else {
      ctx.error_argument_type(0, "an array or a function");
    }
  });

  // PipelineDesigner.forkRace
  filter("forkRace", [](Context &ctx, PipelineDesigner *obj) {
    Array *init_args;
    Function *init_args_f;
    if (ctx.get(0, init_args) && init_args) {
      obj->fork_race(init_args);
    } else if (ctx.get(0, init_args_f) && init_args_f) {
      obj->fork_race(init_args_f);
    } else {
      ctx.error_argument_type(0, "an array or a function");
    }
  });

  // PipelineDesigner.handle
  filter("handle", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->handle(Event::Type(-1), handler);
  });

  // PipelineDesigner.handleData
  filter("handleData", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->handle(Event::Type::Data, handler);
  });

  // PipelineDesigner.handleMessage
  filter("handleMessage", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    Object *options = nullptr;
    if (!ctx.arguments(1, &handler, &options)) return;
    obj->handle_message(handler, options);
  });

  // PipelineDesigner.handleMessageBody
  filter("handleMessageBody", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    Object *options = nullptr;
    if (!ctx.arguments(1, &handler, &options)) return;
    obj->handle_body(handler, options);
  });

  // PipelineDesigner.handleMessageEnd
  filter("handleMessageEnd", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->handle(Event::Type::MessageEnd, handler);
  });

  // PipelineDesigner.handleMessageStart
  filter("handleMessageStart", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->handle(Event::Type::MessageStart, handler);
  });

  // PipelineDesigner.handleStreamEnd
  filter("handleStreamEnd", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->handle(Event::Type::StreamEnd, handler);
  });

  // PipelineDesigner.handleStreamStart
  filter("handleStreamStart", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->handle_start(handler);
  });

  // PipelineDesigner.handleTLSClientHello
  filter("handleTLSClientHello", [](Context &ctx, PipelineDesigner *obj) {
    Function *handler;
    if (!ctx.arguments(1, &handler)) return;
    obj->handle_tls_client_hello(handler);
  });

  // PipelineDesigner.insert
  filter("insert", [](Context &ctx, PipelineDesigner *obj) {
    Object *events = nullptr;
    if (!ctx.arguments(1, &events)) return;
    obj->insert(events);
  });

  // PipelineDesigner.loop
  filter("loop", [](Context &ctx, PipelineDesigner *obj) {
    Function *builder;
    if (ctx.get(0, builder) && builder) {
      if (auto pl = PipelineDesigner::make_pipeline_layout(ctx, builder)) {
        obj->loop();
        obj->to(pl);
      }
    } else {
      ctx.error_argument_type(0, "a function");
    }
  });

  // PipelineDesigner.mux
  filter("mux", [](Context &ctx, PipelineDesigner *obj) {
    Function *session_selector = nullptr;
    Object *options = nullptr;
    if (
      ctx.try_arguments(0, &session_selector, &options) ||
      ctx.try_arguments(0, &options)
    ) {
      obj->mux(session_selector, options);
    } else {
      ctx.error_argument_type(0, "a function or an object");
    }
  });

  // PipelineDesigner.muxFastCGI
  filter("muxFastCGI", [](Context &ctx, PipelineDesigner *obj) {
    Function *session_selector = nullptr;
    Object *options = nullptr;
    if (
      ctx.try_arguments(0, &session_selector, &options) ||
      ctx.try_arguments(0, &options)
    ) {
      obj->mux_fcgi(session_selector, options);
    } else {
      ctx.error_argument_type(0, "a function or an object");
    }
  });

  // PipelineDesigner.muxHTTP
  filter("muxHTTP", [](Context &ctx, PipelineDesigner *obj) {
    Function *session_selector = nullptr;
    Object *options = nullptr;
    if (
      ctx.try_arguments(0, &session_selector, &options) ||
      ctx.try_arguments(0, &options)
    ) {
      obj->mux_http(session_selector, options);
    } else {
      ctx.error_argument_type(0, "a function or an object");
    }
  });

  // PipelineDesigner.pipe
  filter("pipe", [](Context &ctx, PipelineDesigner *obj) {
    Value target;
    Object *target_map = nullptr;
    Array *init_args = nullptr;
    Function *init_args_f = nullptr;
    if (!ctx.get(0, target)) return ctx.error_argument_count(1);
    if (!ctx.get(1, init_args) && !ctx.get(1, init_args_f)) {
      if (!ctx.get(1, target_map, (Object *)nullptr)) return ctx.error_argument_type(1, "an object, an array or a function");
      if (!ctx.get(2, init_args, (Array *)nullptr) && !ctx.get(2, init_args_f, (Function *)nullptr)) return ctx.error_argument_type(2, "an array or a function");
    }
    if (target_map) {
      target_map->iterate_while(
        [&](Str *k, Value &v) {
          if (v.is<PipelineLayoutWrapper>()) return true;
          if (v.is_function()) {
            auto pl = PipelineDesigner::make_pipeline_layout(ctx, v.f());
            if (!pl) return false;
            v.set(PipelineLayoutWrapper::make(pl));
            return true;
          }
          ctx.error("map entry '" + k->str() + "' doesn't contain a valid pipeline");
          return false;
        }
      );
      if (!ctx.ok()) return;
    }
    if (init_args_f) {
      obj->pipe(target, target_map, init_args_f);
    } else {
      obj->pipe(target, target_map, init_args);
    }
  });

  // PipelineDesigner.pipeNext
  filter("pipeNext", [](Context &ctx, PipelineDesigner *obj) {
    obj->pipe_next();
  });

  // PipelineDesigner.print
  filter("print", [](Context &ctx, PipelineDesigner *obj) {
    obj->print();
  });

  // PipelineDesigner.replace
  filter("repeat", [](Context &ctx, PipelineDesigner *obj) {
    Function *condition = nullptr;
    if (!ctx.arguments(1, &condition)) return;
    obj->repeat(condition);
  });

  // PipelineDesigner.replace
  filter("replace", [](Context &ctx, PipelineDesigner *obj) {
    Object *replacement = nullptr;
    if (!ctx.arguments(0, &replacement)) return;
    obj->replace(Event::Type(-1), replacement);
  });

  // PipelineDesigner.replaceData
  filter("replaceData", [](Context &ctx, PipelineDesigner *obj) {
    Object *replacement = nullptr;
    if (!ctx.arguments(0, &replacement)) return;
    obj->replace(Event::Type::Data, replacement);
  });

  // PipelineDesigner.replaceMessage
  filter("replaceMessage", [](Context &ctx, PipelineDesigner *obj) {
    Object *replacement = nullptr;
    Object *options = nullptr;
    if (!ctx.arguments(0, &replacement, &options)) return;
    obj->replace_message(replacement, options);
  });

  // PipelineDesigner.replaceMessageBody
  filter("replaceMessageBody", [](Context &ctx, PipelineDesigner *obj) {
    Object *replacement = nullptr;
    Object *options = nullptr;
    if (!ctx.arguments(0, &replacement, &options)) return;
    obj->replace_body(replacement, options);
  });

  // PipelineDesigner.replaceMessageEnd
  filter("replaceMessageEnd", [](Context &ctx, PipelineDesigner *obj) {
    Object *replacement = nullptr;
    if (!ctx.arguments(0, &replacement)) return;
    obj->replace(Event::Type::MessageEnd, replacement);
  });

  // PipelineDesigner.replaceMessageStart
  filter("replaceMessageStart", [](Context &ctx, PipelineDesigner *obj) {
    Object *replacement = nullptr;
    if (!ctx.arguments(0, &replacement)) return;
    obj->replace(Event::Type::MessageStart, replacement);
  });

  // PipelineDesigner.replaceStreamEnd
  filter("replaceStreamEnd", [](Context &ctx, PipelineDesigner *obj) {
    Object *replacement = nullptr;
    if (!ctx.arguments(0, &replacement)) return;
    obj->replace(Event::Type::StreamEnd, replacement);
  });

  // PipelineDesigner.replaceStreamStart
  filter("replaceStreamStart", [](Context &ctx, PipelineDesigner *obj) {
    Object *replacement = nullptr;
    if (!ctx.arguments(0, &replacement)) return;
    obj->replace_start(replacement);
  });

  // PipelineDesigner.serveHTTP
  filter("serveHTTP", [](Context &ctx, PipelineDesigner *obj) {
    Object *handler;
    Object *options = nullptr;
    if (!ctx.arguments(1, &handler, &options)) return;
    obj->serve_http(handler, options);
  });

  // PipelineDesigner.split
  filter("split", [](Context &ctx, PipelineDesigner *obj) {
    Value separator;
    if (!ctx.arguments(1, &separator)) return;
    obj->split(separator);
  });

  // PipelineDesigner.swap
  filter("swap", [](Context &ctx, PipelineDesigner *obj) {
    Hub *hub = nullptr;
    Function *hub_f = nullptr;
    if (!ctx.get(0, hub) && !ctx.get(0, hub_f)) {
      ctx.error_argument_type(0, "a Hub or a function");
      return;
    }
    if (hub_f) obj->swap(hub_f); else obj->swap(hub);
  });

  // PipelineDesigner.tee
  filter("tee", [](Context &ctx, PipelineDesigner *obj) {
    Value filename;
    Object *options = nullptr;
    if (!ctx.arguments(1, &filename, &options)) return;
    obj->tee(filename, options);
  });

  // PipelineDesigner.throttleConcurrency
  filter("throttleConcurrency", [](Context &ctx, PipelineDesigner *obj) {
    pjs::Object *quota;
    pjs::Object *options = nullptr;
    if (!ctx.arguments(1, &quota, &options)) return;
    obj->throttle_concurrency(quota, options);
  });

  // PipelineDesigner.throttleDataRate
  filter("throttleDataRate", [](Context &ctx, PipelineDesigner *obj) {
    pjs::Object *quota;
    pjs::Object *options = nullptr;
    if (!ctx.arguments(1, &quota, &options)) return;
    obj->throttle_data_rate(quota, options);
  });

  // PipelineDesigner.throttleMessageRate
  filter("throttleMessageRate", [](Context &ctx, PipelineDesigner *obj) {
    pjs::Object *quota;
    pjs::Object *options = nullptr;
    if (!ctx.arguments(1, &quota, &options)) return;
    obj->throttle_message_rate(quota, options);
  });

  // PipelineDesigner.wait
  filter("wait", [](Context &ctx, PipelineDesigner *obj) {
    Function *condition;
    Object *options = nullptr;
    if (!ctx.arguments(1, &condition, &options)) return;
    obj->wait(condition, options);
  });
}

template<> void ClassDef<PipelineLayoutWrapper>::init() {
  method("spawn", [](Context &ctx, Object *thiz, Value &ret) {
    auto worker = static_cast<Worker*>(ctx.instance());
    auto context = worker->new_context();
    auto p = thiz->as<PipelineLayoutWrapper>()->spawn(context);
    auto pw = new PipelineWrapper(p);
    ret.set(pw->start(ctx.argc(), ctx.argv()));
  });
}

template<> void ClassDef<PipelineLayoutWrapper::Constructor>::init() {
  super<Function>();
  ctor();
  variable("Hub", class_of<Constructor<Hub>>());
}

template<> void ClassDef<Hub>::init() {
  ctor();
}

template<> void ClassDef<Constructor<Hub>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs
