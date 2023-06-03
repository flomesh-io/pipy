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

using tcp = asio::ip::tcp;

thread_local static Data::Producer s_dp_udp("InboundUDP");

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

Listener::Listener(Protocol protocol, const std::string &ip, int port)
  : m_protocol(protocol)
  , m_ip(ip)
  , m_port(port)
{
  m_address = asio::ip::make_address(m_ip);
  m_ip = m_address.to_string();
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
      close();
    }
    m_pipeline_layout = layout;
  }
  return true;
}

void Listener::close() {
  if (m_acceptor) {
    m_acceptor->close();
    m_acceptor = nullptr;
    char desc[200];
    describe(desc, sizeof(desc));
    Log::info("[listener] Stopped listening on %s", desc);
  }
}

void Listener::set_options(const Options &options) {
  m_options = options;
  m_options.protocol = m_protocol;
  if (m_acceptor) {
    int n = m_options.max_connections;
    if (n >= 0 && m_acceptor->count() >= n) {
      pause();
    } else {
      resume();
    }
  }
}

void Listener::for_each_inbound(const std::function<void(Inbound*)> &cb) {
  if (m_acceptor) {
    m_acceptor->for_each_inbound(cb);
  }
}

bool Listener::start() {
  char desc[200];
  describe(desc, sizeof(desc));

  try {
    switch (m_protocol) {
      case Protocol::TCP: {
        asio::ip::tcp::endpoint endpoint(m_address, m_port);
        auto *acceptor = new AcceptorTCP(this);
        m_paused = !acceptor->start(endpoint);
        m_acceptor = acceptor;
        break;
      }
      case Protocol::UDP: {
        asio::ip::udp::endpoint endpoint(m_address, m_port);
        auto *acceptor = new AcceptorUDP(
          this,
          m_options.transparent,
          m_options.masquerade
        );
        acceptor->start(endpoint);
        m_paused = false;
        m_acceptor = acceptor;
        break;
      }
      default: break;
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

void Listener::open(Inbound *inbound) {
  m_acceptor->open(inbound);
  auto n = m_acceptor->count();
  m_peak_connections = std::max(m_peak_connections, int(n));
  int max = m_options.max_connections;
  if (max > 0 && n >= max) {
    pause();
  } else {
    m_acceptor->accept();
  }
}

void Listener::close(Inbound *inbound) {
  m_acceptor->close(inbound);
  auto n = m_acceptor->count();
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
  close();
}

bool Listener::AcceptorTCP::start(const asio::ip::tcp::endpoint &endpoint) {
  const auto &options = m_listener->m_options;

  m_acceptor.open(endpoint.protocol());
  m_acceptor.set_option(asio::socket_base::reuse_address(true));

  m_listener->set_sock_opts(m_acceptor.native_handle());

  m_acceptor.bind(endpoint);
  m_acceptor.listen(asio::socket_base::max_connections);

  if (options.max_connections < 0 || m_inbounds.size() < options.max_connections) {
    accept();
    return true;
  } else {
    return false;
  }
}

auto Listener::AcceptorTCP::count() -> size_t const {
  return m_inbounds.size();
}

void Listener::AcceptorTCP::accept() {
  auto inbound = InboundTCP::make(m_listener, m_listener->m_options);
  inbound->accept(m_acceptor);
  m_accepting = inbound;
}

void Listener::AcceptorTCP::cancel() {
  m_acceptor.cancel();
  m_accepting = nullptr;
}

void Listener::AcceptorTCP::open(Inbound *inbound) {
  m_inbounds.push(static_cast<InboundTCP*>(inbound));
}

void Listener::AcceptorTCP::close(Inbound *inbound) {
  m_inbounds.remove(static_cast<InboundTCP*>(inbound));
}

void Listener::AcceptorTCP::close() {
  m_acceptor.close();
  if (m_accepting) m_accepting->dangle();
  while (auto *ib = m_inbounds.head()) {
    ib->dangle();
    m_inbounds.remove(ib);
  }
}

void Listener::AcceptorTCP::for_each_inbound(const std::function<void(Inbound*)> &cb) {
  for (auto p = m_inbounds.head(); p; p = p->List<InboundTCP>::Item::next()) {
    cb(p);
  }
}

//
// Listener::AcceptorUDP
//

Listener::AcceptorUDP::AcceptorUDP(Listener *listener, bool transparent, bool masquerade)
  : m_listener(listener)
  , m_socket(Net::context())
  , m_socket_raw(Net::context())
  , m_transparent(transparent)
  , m_masquerade(masquerade)
{
}

Listener::AcceptorUDP::~AcceptorUDP() {
  close();
}

void Listener::AcceptorUDP::start(const asio::ip::udp::endpoint &endpoint) {
  m_socket.open(endpoint.protocol());
  m_socket.set_option(asio::socket_base::reuse_address(true));
  m_listener->set_sock_opts(m_socket.native_handle());
  m_socket.bind(endpoint);
  m_local = m_socket.local_endpoint();
  if (m_masquerade) {
    m_socket_raw.open(asio::generic::raw_protocol(AF_INET, IPPROTO_RAW));
  }
  receive();
}

auto Listener::AcceptorUDP::inbound(
  const asio::ip::udp::endpoint &src,
  const asio::ip::udp::endpoint &dst,
  bool create
) -> InboundUDP* {
  auto i = m_inbound_map.find(dst);
  if (i != m_inbound_map.end()) {
    auto &peers = i->second;
    auto j = peers.find(src);
    if (j != peers.end()) return j->second;
  }
  if (create) {
    auto *inbound = InboundUDP::make(
      m_listener,
      m_listener->m_options,
      m_socket,
      m_socket_raw,
      m_local, src, dst
    );
    return inbound;
  }
  return nullptr;
}

auto Listener::AcceptorUDP::count() -> size_t const {
  return m_inbounds.size();
}

void Listener::AcceptorUDP::receive() {
  m_socket.async_receive_from(
    asio::null_buffers(),
    m_peer,
    [=](const std::error_code &ec, std::size_t n) {
      InputContext ic;

      if (ec != asio::error::operation_aborted) {
        if (!ec) {
          auto max_size = m_listener->m_options.max_packet_size;
          auto iov_size = (max_size + DATA_CHUNK_SIZE - 1) / DATA_CHUNK_SIZE;
          Data buf(max_size, &s_dp_udp);

          auto s = m_socket.native_handle();

          struct sockaddr_in addr;
          struct msghdr msg;
          struct iovec iov[iov_size];

          int i = 0;
          for (auto c : buf.chunks()) {
            auto buf = std::get<0>(c);
            auto len = std::get<1>(c);
            iov[i].iov_base = buf;
            iov[i].iov_len = len;
            i++;
          }

          msg.msg_name = &addr;
          msg.msg_namelen = sizeof(addr);
          msg.msg_iov = iov;
          msg.msg_iovlen = iov_size;
          msg.msg_flags = 0;

          char control_data[1000];
          if (m_transparent) {
            msg.msg_control = control_data;
            msg.msg_controllen = sizeof(control_data);
          } else {
            msg.msg_control = nullptr;
            msg.msg_controllen = 0;
          }

          auto n = recvmsg(s, &msg, 0);
          if (n > 0) {
            buf.pop(buf.size() - n);
            m_peer.address(asio::ip::make_address_v4(ntohl(addr.sin_addr.s_addr)));
            m_peer.port(ntohs(addr.sin_port));
            asio::ip::udp::endpoint destination;

#ifdef __linux__
            if (m_transparent) {
              for (auto *cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_ORIGDSTADDR) {
                  auto *addr = (struct sockaddr_in *)CMSG_DATA(cmsg);
                  destination.address(asio::ip::make_address_v4(ntohl(addr->sin_addr.s_addr)));
                  destination.port(ntohs(addr->sin_port));
                  break;
                }
              }
            }
#endif // __linux__

            InboundUDP *inb = inbound(m_peer, destination, !m_paused);
            if (inb && inb->is_receiving()) {
              inb->receive(Data::make(std::move(buf)));
            }
          }

        } else {
          if (Log::is_enabled(Log::WARN)) {
            char desc[200];
            m_listener->describe(desc, sizeof(desc));
            Log::warn(
              "[listener] error receiving on %s: %s",
              desc, ec.message().c_str()
            );
          }
        }

        receive();
      }

      release();
    }
  );

  retain();
}

void Listener::AcceptorUDP::accept() {
  m_paused = false;
}

void Listener::AcceptorUDP::cancel() {
  m_paused = true;
}

void Listener::AcceptorUDP::open(Inbound *inbound) {
  auto *inb = static_cast<InboundUDP*>(inbound);
  m_inbounds.push(static_cast<InboundUDP*>(inbound));
  auto &peers = m_inbound_map[inb->destination()];
  peers[inb->peer()] = inb;
}

void Listener::AcceptorUDP::close(Inbound *inbound) {
  auto *inb = static_cast<InboundUDP*>(inbound);
  m_inbounds.remove(inb);
  auto i = m_inbound_map.find(inb->destination());
  if (i != m_inbound_map.end()) {
    i->second.erase(inb->peer());
    if (i->second.empty()) m_inbound_map.erase(i);
  }
}

void Listener::AcceptorUDP::close() {
  m_socket.close();
  for (auto *p = m_inbounds.head(); p; ) {
    auto *i = p; p = p->List<InboundUDP>::Item::next();
    i->release();
  }
}

void Listener::AcceptorUDP::for_each_inbound(const std::function<void(Inbound*)> &cb) {
  for (auto p = m_inbounds.head(); p; p = p->List<InboundUDP>::Item::next()) {
    cb(p);
  }
}

//
// ListenerArray
//

auto ListenerArray::add_listener(int port, const Listener::Options &options) -> Listener* {
  std::string ip_port("0.0.0.0:");
  ip_port += std::to_string(port);
  return add_listener(pjs::Str::make(ip_port), options);
}

auto ListenerArray::add_listener(pjs::Str *port, const Listener::Options &options) -> Listener* {
  std::string ip;
  int port_num;
  get_ip_port(port->str(), ip, port_num);

  auto listener = Listener::get(options.protocol, ip, port_num);
  if (listener->reserved()) {
    std::string msg("Port reserved: ");
    throw std::runtime_error(msg + std::to_string(port_num));
  }
#ifndef __linux__
  if (options.transparent) {
    Log::error("Trying to listen on %d in transparent mode, which is not supported on this platform", port_num);
  }
#endif

  m_listeners[listener] = options;

  if (auto *w = m_worker) {
    w->add_listener(listener, m_pipeline_layout, options);
    w->update_listeners(true);
  }

  return listener;
}

auto ListenerArray::remove_listener(int port, const Listener::Options &options) -> Listener* {
  std::string ip_port("0.0.0.0:");
  ip_port += std::to_string(port);
  return remove_listener(pjs::Str::make(ip_port), options);
}

auto ListenerArray::remove_listener(pjs::Str *port, const Listener::Options &options) -> Listener* {
  std::string ip;
  int port_num;
  get_ip_port(port->str(), ip, port_num);

  auto listener = Listener::get(options.protocol, ip, port_num);
  if (m_listeners.find(listener) != m_listeners.end()) {
    m_listeners.erase(listener);
  }

  if (auto *w = m_worker) {
    w->remove_listener(listener);
    w->update_listeners(true);
  }

  return listener;
}

bool ListenerArray::apply(Worker *worker, PipelineLayout *layout) {
  if (m_worker) return false;
  m_worker = worker;
  m_pipeline_layout = layout;
  for (const auto &p : m_listeners) {
    worker->add_listener(p.first, layout, p.second);
  }
  return true;
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
  ctor();

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
