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
#include "logging.hpp"

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
  Value(options, "transparent")
    .get(transparent)
    .check_nullable();
  Value(options, "closeEOF")
    .get(close_eof)
    .check_nullable();
}

//
// Listener
//

std::map<Listener::Protocol, std::map<int, Listener*>> Listener::s_listeners;
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
  s_listeners[protocol][port] = this;
}

Listener::~Listener() {
  s_listeners[m_protocol].erase(m_port);
}

void Listener::pipeline_layout(PipelineLayout *layout) {
  if (m_pipeline_layout.get() != layout) {
    if (layout) {
      if (!m_pipeline_layout) {
        start();
      }
    } else if (m_pipeline_layout) {
      close();
    }
    m_pipeline_layout = layout;
  }
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

void Listener::start() {
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
        auto *acceptor = new AcceptorUDP(this);
        acceptor->start(endpoint);
        m_paused = false;
        m_acceptor = acceptor;
        break;
      }
    }

    Log::info("[listener] Listening on %s", desc);

  } catch (std::runtime_error &err) {
    m_acceptor = nullptr;
    std::string msg("[listener] Cannot start listening on ");
    msg += desc;
    msg += ": ";
    msg += err.what();
    throw std::runtime_error(msg);
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

auto Listener::find(int port, Protocol protocol) -> Listener* {
  auto i = s_listeners.find(protocol);
  if (i != s_listeners.end()) {
    const auto &m = i->second;
    auto i = m.find(port);
    if (i != m.end()) return i->second;
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
  for (
    auto *p = m_inbounds.head();
    p; p = p->List<InboundTCP>::Item::next()
  ) {
    p->dangle();
  }
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
}

void Listener::AcceptorTCP::cancel() {
  m_acceptor.cancel();
}

void Listener::AcceptorTCP::open(Inbound *inbound) {
  m_inbounds.push(static_cast<InboundTCP*>(inbound));
}

void Listener::AcceptorTCP::close(Inbound *inbound) {
  m_inbounds.remove(static_cast<InboundTCP*>(inbound));
}

void Listener::AcceptorTCP::close() {
  m_acceptor.close();
}

void Listener::AcceptorTCP::for_each_inbound(const std::function<void(Inbound*)> &cb) {
  for (auto p = m_inbounds.head(); p; p = p->List<InboundTCP>::Item::next()) {
    cb(p);
  }
}

//
// Listener::AcceptorUDP
//

Listener::AcceptorUDP::AcceptorUDP(Listener *listener)
  : m_listener(listener)
  , m_socket(Net::context())
{
}

Listener::AcceptorUDP::~AcceptorUDP() {
  for (auto *p = m_inbounds.head(); p; ) {
    auto *i = p;
    p = p->List<InboundUDP>::Item::next();
    i->dangle();
    i->stop();
  }
}

void Listener::AcceptorUDP::start(const asio::ip::udp::endpoint &endpoint) {
  m_socket.open(endpoint.protocol());
  m_socket.set_option(asio::socket_base::reuse_address(true));
  m_listener->set_sock_opts(m_socket.native_handle());
  m_socket.bind(endpoint);
  receive();
}

auto Listener::AcceptorUDP::inbound(const asio::ip::udp::endpoint &peer, bool create) -> InboundUDP* {
  auto i = m_inbound_map.find(peer);
  if (i != m_inbound_map.end()) return i->second;
  if (create) {
    auto *inbound = InboundUDP::make(
      m_listener,
      m_listener->m_options,
      m_socket, peer
    );
    inbound->start();
    return inbound;
  }
  return nullptr;
}

auto Listener::AcceptorUDP::count() -> size_t const {
  return m_inbounds.size();
}

void Listener::AcceptorUDP::receive() {
  static Data::Producer s_data_producer("InboundUDP");
  pjs::Ref<Data> buffer(Data::make(m_listener->m_options.max_packet_size, &s_data_producer));

  m_socket.async_receive_from(
    DataChunks(buffer->chunks()),
    m_peer,
    [=](const std::error_code &ec, std::size_t n) {
      if (ec != asio::error::operation_aborted) {
        if (!ec) {
          InboundUDP *inb = inbound(m_peer, !m_paused);
          if (inb && inb->is_receiving()) {
            if (n > 0) buffer->pop(buffer->size() - n);
            inb->receive(buffer);
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
  m_inbound_map[inb->peer()] = inb;
}

void Listener::AcceptorUDP::close(Inbound *inbound) {
  auto *inb = static_cast<InboundUDP*>(inbound);
  m_inbounds.remove(inb);
  m_inbound_map.erase(inb->peer());
}

void Listener::AcceptorUDP::close() {
  m_socket.close();
}

void Listener::AcceptorUDP::for_each_inbound(const std::function<void(Inbound*)> &cb) {
  for (auto p = m_inbounds.head(); p; p = p->List<InboundUDP>::Item::next()) {
    cb(p);
  }
}

} // namespace pipy
