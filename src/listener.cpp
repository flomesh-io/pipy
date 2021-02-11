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
#include "session.hpp"
#include "logging.hpp"

#include <memory>

NS_BEGIN

using tcp = asio::ip::tcp;

asio::io_service g_io_service;

static std::list<asio::steady_timer*> s_timer_pool;
static bool s_reuse_port = false;

void Listener::run() {
  g_io_service.run();
}

void Listener::stop() {
  g_io_service.stop();
}

auto Listener::listen(const std::string &ip, int port, Pipeline *pipeline) -> Listener* {
  return new Listener(ip, port, pipeline);
}

void Listener::set_timeout(double duration, std::function<void()> handler) {
  asio::steady_timer *timer = nullptr;

  if (s_timer_pool.size() > 0) {
    timer = s_timer_pool.back();
    s_timer_pool.pop_back();
  } else {
    timer = new asio::steady_timer(g_io_service);
  }

  timer->expires_after(std::chrono::milliseconds((long long)(duration * 1000)));
  timer->async_wait([=](const asio::error_code &ec) {
    s_timer_pool.push_back(timer);
    handler();
  });
}

void Listener::set_reuse_port(bool enabled) {
  s_reuse_port = enabled;
}

Listener::Listener(const std::string &ip, int port, Pipeline *pipeline)
  : m_ip(ip)
  , m_port(port)
  , m_acceptor(g_io_service)
  , m_pipeline(pipeline)
{
  tcp::resolver resolver(g_io_service);
  tcp::resolver::query query(ip, std::to_string(port));
  tcp::endpoint endpoint = *resolver.resolve(query);

  m_acceptor.open(endpoint.protocol());
  m_acceptor.set_option(asio::socket_base::reuse_address(true));

  auto sock = m_acceptor.native_handle();
  int enabled = 1;

#ifdef __linux__
  setsockopt(sock, SOL_IP, IP_TRANSPARENT, &enabled, sizeof(enabled));
#endif

  if (s_reuse_port) {
#ifdef __linux__
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled));
#elif defined(__FreeBSD__)
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT_LB, &enabled, sizeof(enabled));
#endif
  }

  m_acceptor.bind(endpoint);
  m_acceptor.listen(asio::socket_base::max_connections);

  accept();

  Log::info("Listening on %s:%d", ip.c_str(), port);
}

void Listener::close() {
  m_acceptor.close();
  delete this;
}

void Listener::accept() {
  auto inbound = new Inbound;
  inbound->accept(
    m_pipeline,
    m_acceptor,
    [=](const std::error_code &ec) {
      if (ec != asio::error::operation_aborted) {
        accept();
      }
    }
  );
}

NS_END