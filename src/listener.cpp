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

namespace pipy {

using tcp = asio::ip::tcp;

//
// Listener::Options
//

Listener::Options::Options(pjs::Object *options) {
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

std::list<Listener*> Listener::s_all_listeners;
bool Listener::s_reuse_port = false;

void Listener::set_reuse_port(bool reuse) {
  s_reuse_port = reuse;
}

Listener::Listener(const std::string &ip, int port)
  : m_ip(ip)
  , m_port(port)
{
  m_address = asio::ip::make_address(m_ip);
  m_ip = m_address.to_string();
  s_all_listeners.push_back(this);
}

Listener::~Listener() {
  delete m_acceptor;
  s_all_listeners.remove(this);
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
  m_acceptor->close();
  Log::info("[listener] Stopped listening on port %d at %s", m_port, m_ip.c_str());
}

void Listener::set_options(const Options &options) {
  m_options = options;
  if (m_pipeline_layout) {
    int n = m_options.max_connections;
    if (n >= 0 && m_acceptor->count() >= n) {
      pause();
    } else {
      resume();
    }
  }
}

void Listener::for_each_inbound(const std::function<void(Inbound*)> &cb) {
  m_acceptor->for_each_inbound(cb);
}

void Listener::start() {
  try {
    auto *acceptor = new AcceptorTCP(this);
    m_acceptor = acceptor;

    tcp::endpoint endpoint(m_address, m_port);
    acceptor->start(endpoint);

    Log::info("[listener] Listening on port %d at %s", m_port, m_ip.c_str());

  } catch (std::runtime_error &err) {
    char msg[200];
    std::snprintf(
      msg, sizeof(msg),
      "[listener] Cannot start listening on port %d at %s: ",
      m_port, m_ip.c_str()
    );
    delete m_acceptor;
    m_acceptor = nullptr;
    throw std::runtime_error(std::string(msg) + err.what());
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

void Listener::open(InboundTCP *inbound) {
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

void Listener::close(InboundTCP *inbound) {
  m_acceptor->close(inbound);
  auto n = m_acceptor->count();
  int max = m_options.max_connections;
  if (max < 0 || n < max) {
    resume();
  }
}

auto Listener::find(const std::string &ip, int port) -> Listener* {
  auto addr = asio::ip::make_address(ip);
  for (auto *l : s_all_listeners) {
    if (l->m_port == port && l->m_address == addr) {
      return l;
    }
  }
  return nullptr;
}

//
// Listener::Acceptor
//

Listener::AcceptorTCP::AcceptorTCP(Listener *listener)
  : m_listener(listener)
  , m_acceptor(Net::context())
{
}

void Listener::AcceptorTCP::start(const asio::ip::tcp::endpoint &endpoint) {
  const auto &options = m_listener->m_options;

  m_acceptor.open(endpoint.protocol());
  m_acceptor.set_option(asio::socket_base::reuse_address(true));

  auto sock = m_acceptor.native_handle();

#ifdef __linux__
  if (options.transparent) {
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

  m_acceptor.bind(endpoint);
  m_acceptor.listen(asio::socket_base::max_connections);

  if (options.max_connections < 0 || m_inbounds.size() < options.max_connections) {
    accept();
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

} // namespace pipy
