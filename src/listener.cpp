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

#include <set>

namespace pipy {

using tcp = asio::ip::tcp;

bool Listener::s_reuse_port = false;

std::map<int, Listener*> Listener::s_all_listeners;

void Listener::set_reuse_port(bool reuse) {
  s_reuse_port = reuse;
}

Listener::Listener(int port)
  : m_ip("::")
  , m_port(port)
  , m_acceptor(Net::service())
{
  s_all_listeners[port] = this;
}

Listener::~Listener() {
  auto i = s_all_listeners.find(m_port);
  if (i != s_all_listeners.end() && i->second == this) {
    s_all_listeners.erase(i);
  }
}

void Listener::pipeline_def(PipelineDef *pipeline_def) {
  if (m_pipeline_def.get() != pipeline_def) {
    if (pipeline_def) {
      if (!m_pipeline_def) {
        try {
          start();
        } catch (std::runtime_error &err) {
          m_acceptor.close();
          throw err;
        }
      }
    } else if (m_pipeline_def) {
      close();
    }
    m_pipeline_def = pipeline_def;
  }
}

void Listener::close() {
  m_acceptor.close();
  Log::info("[listener] Stopped listening on port %d at %s", m_port, m_ip.c_str());
}

void Listener::set_reserved(bool reserved) {
  m_reserved = reserved;
}

void Listener::set_max_connections(int n) {
  m_max_connections = n;
  if (m_pipeline_def) {
    if (n >= 0 && m_inbounds.size() >= n) {
      pause();
    } else {
      resume();
    }
  }
}

void Listener::start() {
  auto addr = asio::ip::address::from_string(m_ip);
  tcp::endpoint endpoint(addr, m_port);
  m_acceptor.open(endpoint.protocol());
  m_acceptor.set_option(asio::socket_base::reuse_address(true));

  auto sock = m_acceptor.native_handle();
  int enabled = 1;

#ifdef __linux__
  setsockopt(sock, SOL_IP, IP_TRANSPARENT, &enabled, sizeof(enabled));
#endif

  if (s_reuse_port) {
#ifdef __FreeBSD__
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT_LB, &enabled, sizeof(enabled));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled));
#endif
  }

  m_acceptor.bind(endpoint);
  m_acceptor.listen(asio::socket_base::max_connections);

  if (m_max_connections < 0 || m_inbounds.size() < m_max_connections) {
    accept();
  }

  Log::info("[listener] Listening on port %d at %s", m_port, m_ip.c_str());
}

void Listener::close(Inbound *inbound) {
  m_inbounds.remove(inbound);
  if (m_max_connections < 0 || m_inbounds.size() < m_max_connections) {
    resume();
  }
}

void Listener::accept() {
  auto inbound = Inbound::make(this);
  inbound->accept(
    this, m_acceptor,
    [=](const std::error_code &ec) {
      if (!ec) {
        m_inbounds.push(inbound);
        m_peak_connections = std::max(m_peak_connections, int(m_inbounds.size()));
        if (m_max_connections > 0 && m_inbounds.size() >= m_max_connections) {
          pause();
        } else {
          accept();
        }
      }
    }
  );
}

void Listener::pause() {
  if (!m_paused) {
    m_acceptor.cancel();
    m_paused = true;
  }
}

void Listener::resume() {
  if (m_paused) {
    accept();
    m_paused = false;
  }
}

} // namespace pipy