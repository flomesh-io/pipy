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
#include "inbound.hpp"
#include "pipeline.hpp"
#include "logging.hpp"

#include <set>

namespace pipy {

using tcp = asio::ip::tcp;

std::map<int, Listener*> Listener::s_all_listeners;

Listener::Listener(const std::string &ip, int port, bool reuse)
  : m_ip(ip)
  , m_port(port)
  , m_reuse(reuse)
  , m_acceptor(Net::service())
  , m_ssl_context(asio::ssl::context::sslv23)
  , m_ssl(false)
{
  s_all_listeners[port] = this;
}

Listener::Listener(const std::string &ip, int port, bool reuse, asio::ssl::context &&ssl_context)
  : m_ip(ip)
  , m_port(port)
  , m_reuse(reuse)
  , m_acceptor(Net::service())
  , m_ssl_context(std::move(ssl_context))
  , m_ssl(true)
{
  s_all_listeners[port] = this;
}

Listener::~Listener() {
  auto i = s_all_listeners.find(m_port);
  if (i != s_all_listeners.end() && i->second == this) {
    s_all_listeners.erase(i);
  }
}

void Listener::open(Pipeline *pipeline) {
  if (m_pipeline) {
    m_pipeline = pipeline;
  } else {
    m_pipeline = pipeline;
    start();
  }
}

void Listener::close() {
  m_acceptor.close();
  m_pipeline = nullptr;
}

void Listener::start() {
  tcp::resolver resolver(Net::service());
  tcp::resolver::query query(m_ip, std::to_string(m_port));
  tcp::endpoint endpoint = *resolver.resolve(query);

  m_acceptor.open(endpoint.protocol());
  m_acceptor.set_option(asio::socket_base::reuse_address(true));

  auto sock = m_acceptor.native_handle();
  int enabled = 1;

#ifdef __linux__
  setsockopt(sock, SOL_IP, IP_TRANSPARENT, &enabled, sizeof(enabled));
#endif

  if (m_reuse) {
#ifdef __FreeBSD__
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT_LB, &enabled, sizeof(enabled));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled));
#endif
  }

  m_acceptor.bind(endpoint);
  m_acceptor.listen(asio::socket_base::max_connections);

  accept();

  Log::info("[listener] Listening on %s:%d", m_ip.c_str(), m_port);
}

void Listener::accept() {
  auto inbound = m_ssl ? Inbound::make(m_ssl_context) : Inbound::make();
  inbound->accept(
    this, m_acceptor,
    [=](const std::error_code &ec) {
      if (ec != asio::error::operation_aborted) {
        accept();
      }
    }
  );
}

} // namespace pipy