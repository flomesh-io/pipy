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

#include "listener.hpp"
#include "pipeline.hpp"
#include "worker.hpp"
#include "log.hpp"

namespace pjs {

using namespace pipy;

template<>
void EnumDef<Listener::Protocol>::init() {
  define(Listener::Protocol::TCP, "tcp");
  define(Listener::Protocol::UDP, "udp");
}

} // namespace pjs

namespace pipy {

//
// Listener::Options
//

Listener::Options::Options(pjs::Object *options) {
  Value(options, "protocol")
    .get_enum(protocol)
    .check_nullable();
  Value(options, "maxConnections")
    .get(max_connections)
    .check_nullable();
  Value(options, "readTimeout")
    .get_seconds(read_timeout)
    .check_nullable();
  Value(options, "writeTimeout")
    .get_seconds(write_timeout)
    .check_nullable();
  Value(options, "idleTimeout")
    .get_seconds(idle_timeout)
    .check_nullable();
  Value(options, "congestionLimit")
    .get_binary_size(congestion_limit)
    .check_nullable();
  Value(options, "bufferLimit")
    .get_binary_size(buffer_limit)
    .check_nullable();
  Value(options, "keepAlive")
    .get(keep_alive)
    .check_nullable();
  Value(options, "noDelay")
    .get(no_delay)
    .check_nullable();
  Value(options, "transparent")
    .get(transparent)
    .check_nullable();
  Value(options, "masquerade")
    .get(masquerade)
    .check_nullable();
  Value(options, "peerStats")
    .get(peer_stats)
    .check_nullable();
}

//
// Listener
//

thread_local std::set<Listener*> Listener::s_listeners[int(Listener::Protocol::MAX)];
bool Listener::s_reuse_port = false;

void Listener::set_reuse_port(bool reuse) {
  s_reuse_port = reuse;
}

void Listener::commit_all() {
  for (int i = 0; i < int(Listener::Protocol::MAX); i++) {
    for (auto l : s_listeners[i]) {
      l->commit();
    }
  }
}

void Listener::rollback_all() {
  for (int i = 0; i < int(Listener::Protocol::MAX); i++) {
    for (auto l : s_listeners[i]) {
      l->rollback();
    }
  }
}

void Listener::delete_all() {
  for (int i = 0; i < int(Listener::Protocol::MAX); i++) {
    std::set<Listener*> all(std::move(s_listeners[i]));
    for (auto l : all) delete l;
  }
}

Listener::Listener(Protocol protocol, const std::string &ip, int port)
  : m_protocol(protocol)
  , m_ip(ip)
  , m_port(port)
{
  m_address = asio::ip::make_address(m_ip);
  m_ip = m_address.to_string();
  char label[100];
  const char *proto;
  switch (m_protocol) {
  case Protocol::TCP: proto = "TCP"; break;
  case Protocol::UDP: proto = "UDP"; break;
  default: proto = "?"; break;
  }
  std::snprintf(
    label, sizeof(label),
    "[%s]:%d/%s",
    m_ip.c_str(), port, proto
  );
  m_label = pjs::Str::make(label);
  s_listeners[int(protocol)].insert(this);
}

Listener::~Listener() {
  s_listeners[int(m_protocol)].erase(this);
}

bool Listener::pipeline_layout(PipelineLayout *layout) {
  if (m_pipeline_layout.get() != layout) {
    if (layout) {
      if (!m_pipeline_layout) {
        if (!start()) return false;
      }
    } else if (m_pipeline_layout) {
      stop();
    }
    m_pipeline_layout = layout;
  }
  return true;
}

void Listener::set_options(const Options &options) {
  m_options = options;
  m_options.protocol = m_protocol;
  if (m_acceptor) {
    int n = m_options.max_connections;
    if (n >= 0 && m_inbounds.size() >= n) {
      pause();
    } else {
      resume();
    }
  }
}

bool Listener::set_next_state(PipelineLayout *pipeline_layout, const Options &options) {
  m_new_listen = true;
  m_pipeline_layout_next = pipeline_layout;
  m_options_next = options;
  if (!m_acceptor) return start_listening();
  return true;
}

void Listener::commit() {
  if (m_new_listen) {
    m_pipeline_layout = m_pipeline_layout_next;
    m_pipeline_layout_next = nullptr;
    m_options = m_options_next;
    m_options_next = Options();
    if (m_pipeline_layout) {
      if (m_acceptor) {
        start_accepting();
      }
    } else {
      stop();
    }
  }
}

void Listener::rollback() {
  if (m_new_listen) {
    m_pipeline_layout_next = nullptr;
    m_options_next = Options();
    if (!m_pipeline_layout) stop();
  }
}

bool Listener::for_each_inbound(const std::function<bool(Inbound*)> &cb) {
  for (auto i = m_inbounds.head(); i; i = i->next()) {
    if (!cb(i)) return false;
  }
  return true;
}

bool Listener::start() {
  return (
    start_listening() &&
    start_accepting()
  );
}

bool Listener::start_listening() {
  char desc[200];
  describe(desc, sizeof(desc));

  try {
    switch (m_protocol) {
      case Protocol::TCP: {
        asio::ip::tcp::endpoint endpoint(m_address, m_port);
        auto *acceptor = new AcceptorTCP(this);
        m_acceptor = acceptor;
        acceptor->start(endpoint);
        break;
      }
      case Protocol::UDP: {
        asio::ip::udp::endpoint endpoint(m_address, m_port);
        auto *acceptor = new AcceptorUDP(this);
        m_acceptor = acceptor;
        acceptor->start(endpoint);
        break;
      }
      default: break;
    }
    return true;

  } catch (std::runtime_error &err) {
    m_acceptor = nullptr;
    Log::error("[listener] Cannot start listening on %s: %s", desc, err.what());
    return false;
  }
}

bool Listener::start_accepting() {
  char desc[200];
  describe(desc, sizeof(desc));

  try {
    if (m_options.max_connections < 0 || m_inbounds.size() < m_options.max_connections) {
      m_acceptor->accept();
      m_paused = false;
    } else {
      m_acceptor->cancel();
      m_paused = true;
    }

    Log::info("[listener] Listening on %s", desc);
    return true;

  } catch (std::runtime_error &err) {
    m_acceptor = nullptr;
    Log::error("[listener] Cannot start listening on %s: %s", desc, err.what());
    return false;
  }
}

void Listener::pause() {
  if (!m_paused) {
    m_acceptor->cancel();
    m_paused = true;
  }
}

void Listener::resume() {
  if (m_paused) {
    m_acceptor->accept();
    m_paused = false;
  }
}

void Listener::stop() {
  List<Inbound> inbounds(std::move(m_inbounds));
  for (auto i = inbounds.head(); i; i = i->next()) {
    i->dangle();
  }
  if (m_acceptor) {
    m_acceptor->stop();
    m_acceptor = nullptr;
    char desc[200];
    describe(desc, sizeof(desc));
    Log::info("[listener] Stopped listening on %s", desc);
  }
}

void Listener::open(Inbound *inbound) {
  m_inbounds.push(inbound);
  auto n = m_inbounds.size();
  m_peak_connections = std::max(m_peak_connections, int(n));
  int max = m_options.max_connections;
  if (max > 0 && n >= max) {
    pause();
  } else {
    m_acceptor->accept();
  }
}

void Listener::close(Inbound *inbound) {
  m_inbounds.remove(inbound);
  auto n = m_inbounds.size();
  int max = m_options.max_connections;
  if (max < 0 || n < max) {
    resume();
  }
}

void Listener::describe(char *buf, size_t len) {
  const char *proto = nullptr;
  switch (m_protocol) {
    case Protocol::TCP: proto = "TCP"; break;
    case Protocol::UDP: proto = "UDP"; break;
    default: proto = "?"; break;
  }
  std::snprintf(
    buf, len, "%s port %d at %s",
    proto, m_port, m_ip.c_str()
  );
}

void Listener::set_sock_opts(int sock) {
#ifdef __linux__
  if (m_options.transparent) {
    int enabled = 1;
    setsockopt(sock, SOL_IP, IP_TRANSPARENT, &enabled, sizeof(enabled));
    setsockopt(sock, SOL_IP, IP_ORIGDSTADDR, &enabled, sizeof(enabled));
  }
#endif

  if (s_reuse_port) {
    int enabled = 1;
#ifdef __FreeBSD__
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT_LB, &enabled, sizeof(enabled));
#elif _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&enabled, sizeof(enabled));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled));
#endif
  }
}

auto Listener::find(Protocol protocol, const std::string &ip, int port) -> Listener* {
  for (auto *l : s_listeners[int(protocol)]) {
    if (l->ip() == ip && l->port() == port) {
      return l;
    }
  }
  return nullptr;
}

//
// Listener::AcceptorTCP
//

Listener::AcceptorTCP::AcceptorTCP(Listener *listener)
  : m_listener(listener)
  , m_acceptor(Net::context())
{
}

Listener::AcceptorTCP::~AcceptorTCP() {
  stop();
}

void Listener::AcceptorTCP::start(const asio::ip::tcp::endpoint &endpoint) {
  m_acceptor.open(endpoint.protocol());
  m_acceptor.set_option(asio::socket_base::reuse_address(true));

  m_listener->set_sock_opts(m_acceptor.native_handle());

  m_acceptor.bind(endpoint);
  m_acceptor.listen(asio::socket_base::max_connections);
}

void Listener::AcceptorTCP::accept() {
  auto inbound = InboundTCP::make(m_listener, m_listener->m_options);
  inbound->accept(m_acceptor);
  m_accepting = inbound;
}

void Listener::AcceptorTCP::cancel() {
  m_acceptor.cancel();
  if (m_accepting) {
    m_accepting->cancel();
    m_accepting = nullptr;
  }
}

void Listener::AcceptorTCP::stop() {
  m_acceptor.close();
  if (m_accepting) {
    m_accepting->dangle();
    m_accepting = nullptr;
  }
}

//
// Listener::AcceptorUDP
//

Listener::AcceptorUDP::AcceptorUDP(Listener *listener)
  : SocketUDP(true, listener->m_options)
  , m_listener(listener)
{
}

Listener::AcceptorUDP::~AcceptorUDP() {
  if (m_socket) {
    m_socket->discard();
  }
  close();
}

void Listener::AcceptorUDP::start(const asio::ip::udp::endpoint &endpoint) {
  auto &s = SocketUDP::socket();
  s.open(endpoint.protocol());
  s.set_option(asio::socket_base::reuse_address(true));

  m_listener->set_sock_opts(s.native_handle());

  s.bind(endpoint);
  const auto &ep = s.local_endpoint();
  m_local_addr = ep.address().to_string();
  m_local_port = ep.port();

  retain();
  SocketUDP::open();

  m_socket = Socket::make(SocketUDP::socket().native_handle());
}

void Listener::AcceptorUDP::accept() {
  m_accepting = true;
}

void Listener::AcceptorUDP::cancel() {
  m_accepting = false;
}

void Listener::AcceptorUDP::stop() {
  SocketUDP::close();
}

auto Listener::AcceptorUDP::on_socket_new_peer() -> Peer* {
  if (m_accepting) {
    auto i = InboundUDP::make(m_listener, m_listener->m_options, m_socket);
    return i;
  } else {
    return nullptr;
  }
}

void Listener::AcceptorUDP::on_socket_describe(char *buf, size_t len) {
  std::snprintf(
    buf, len,
    "[acceptor %p] UDP -> [%s]:%d",
    this,
    m_local_addr.c_str(),
    m_local_port
  );
}

//
// ListenerArray
//

auto ListenerArray::add_listener(int port, pjs::Object *options) -> Listener* {
  std::string ip_port("0.0.0.0:");
  ip_port += std::to_string(port);
  return add_listener(pjs::Ref<pjs::Str>(pjs::Str::make(ip_port)), options);
}

auto ListenerArray::add_listener(pjs::Str *port, pjs::Object *options) -> Listener* {
  if (!options) options = m_default_options;
  Listener::Options opts(options);

  std::string ip;
  int port_num;
  get_ip_port(port->str(), ip, port_num);

  auto listener = Listener::get(opts.protocol, ip, port_num);
  m_listeners[listener] = opts;

  if (auto *w = m_worker) {
    w->add_listener(listener, m_pipeline_layout, opts);
    w->update_listeners(true);
  }

  return listener;
}

auto ListenerArray::remove_listener(int port, pjs::Object *options) -> Listener* {
  std::string ip_port("0.0.0.0:");
  ip_port += std::to_string(port);
  return remove_listener(pjs::Ref<pjs::Str>(pjs::Str::make(ip_port)), options);
}

auto ListenerArray::remove_listener(pjs::Str *port, pjs::Object *options) -> Listener* {
  if (!options) options = m_default_options;
  Listener::Options opts(options);

  std::string ip;
  int port_num;
  get_ip_port(port->str(), ip, port_num);

  auto listener = Listener::get(opts.protocol, ip, port_num);
  if (m_listeners.find(listener) != m_listeners.end()) {
    m_listeners.erase(listener);
  }

  if (auto *w = m_worker) {
    w->remove_listener(listener);
    w->update_listeners(true);
  }

  return listener;
}

void ListenerArray::set_listeners(pjs::Array *array) {
  std::map<Listener*, Listener::Options> listeners;
  if (array) {
    array->iterate_all(
      [&](pjs::Value &v, int i) {
        std::string ip;
        int port_num;
        if (v.is_number()) {
          auto l = Listener::get(Listener::Protocol::TCP, "0.0.0.0", v.to_int32());
          listeners[l] = m_default_options.get();
        } else if (v.is_string()) {
          get_ip_port(v.s()->str(), ip, port_num);
          auto l = Listener::get(Listener::Protocol::TCP, ip, port_num);
          listeners[l] = m_default_options.get();
        } else if (v.is_object() && v.o()) {
          pjs::Value port;
          v.o()->get("port", port);
          if (port.is_number()) {
            ip = "0.0.0.0";
            port_num = port.to_int32();
          } else if (port.is_string()) {
            get_ip_port(port.s()->str(), ip, port_num);
          } else {
            std::string err("invalid value type for the port property in element ");
            err += std::to_string(i);
            throw std::runtime_error(err);
          }
          Listener::Options opt(v.o());
          auto l = Listener::get(opt.protocol, ip, port_num);
          listeners[l] = opt;
        } else {
          std::string err("invalid value type for a listening port in element ");
          err += std::to_string(i);
          throw std::runtime_error(err);
        }
      }
    );
  }

  if (m_worker) {
    for (const auto &i : m_listeners) {
      auto l = i.first;
      if (listeners.find(l) == listeners.end()) {
        m_worker->remove_listener(l);
      }
    }

    for (const auto &i : listeners) {
      m_worker->add_listener(i.first, m_pipeline_layout, i.second);
    }

    m_worker->update_listeners(true);
  }

  m_listeners.swap(listeners);
}

void ListenerArray::apply(Worker *worker, PipelineLayout *layout) {
  if (m_worker) {
    throw std::runtime_error("ListenerArray is being listened already");
  }
  m_worker = worker;
  m_pipeline_layout = layout;
  for (const auto &p : m_listeners) {
    worker->add_listener(p.first, layout, p.second);
  }
}

void ListenerArray::get_ip_port(const std::string &ip_port, std::string &ip, int &port) {
  if (!utils::get_host_port(ip_port, ip, port)) {
    std::string msg("invalid 'ip:port' form: ");
    throw std::runtime_error(msg + ip_port);
  }

  uint8_t buf[16];
  if (!utils::get_ip_v4(ip, buf) && !utils::get_ip_v6(ip, buf)) {
    std::string msg("invalid IP address: ");
    throw std::runtime_error(msg + ip);
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<ListenerArray>::init() {
  ctor([](Context &ctx) -> Object* {
    Array *listeners = nullptr;
    Object *options = nullptr;
    if (!ctx.arguments(0, &listeners, &options)) return nullptr;
    auto la = ListenerArray::make(options);
    if (listeners) {
      try {
        la->set_listeners(listeners);
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
    }
    return la;
  });

  method("set", [](Context &ctx, Object *obj, Value &ret) {
    Array *listeners = nullptr;
    if (!ctx.arguments(0, &listeners)) return;
    try {
      obj->as<ListenerArray>()->set_listeners(listeners);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("add", [](Context &ctx, Object *obj, Value &ret) {
    try {
      int port;
      Str *port_str;
      Object *options = nullptr;
      if (ctx.try_arguments(1, &port, &options)) {
        obj->as<ListenerArray>()->add_listener(port, options);
      } else if (ctx.try_arguments(1, &port_str, &options)) {
        obj->as<ListenerArray>()->add_listener(port_str, options);
      } else {
        ctx.error_argument_type(0, "a number or string");
        return;
      }
      obj->as<ListenerArray>()->add_listener(port, options);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("remove", [](Context &ctx, Object *obj, Value &ret) {
    try {
      int port;
      Str *port_str;
      Object *options = nullptr;
      if (ctx.try_arguments(1, &port, &options)) {
        obj->as<ListenerArray>()->remove_listener(port, options);
      } else if (ctx.try_arguments(1, &port_str, &options)) {
        obj->as<ListenerArray>()->remove_listener(port_str, options);
      } else {
        ctx.error_argument_type(0, "a number or string");
        return;
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<Constructor<ListenerArray>>::init() {
  super<Function>();
  ctor();
}

} // namespace psj
