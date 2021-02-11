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

#include "upstream.hpp"
#include "session.hpp"
#include "data.hpp"
#include "logging.hpp"

NS_BEGIN

using tcp = asio::ip::tcp;

std::map<std::string, Listener*> Upstream::s_host_map;

void Upstream::add_host(Listener *listener) {
  s_host_map[listener->host()] = listener;
}

auto Upstream::find_host(const std::string &host) -> Listener* {
  auto p = s_host_map.find(host);
  if (p == s_host_map.end()) return nullptr;
  return p->second;
}

Upstream::Upstream()
  : m_resolver(g_io_service)
  , m_socket(g_io_service)
  , m_ssl_context(asio::ssl::context::sslv3)
  , m_ssl_socket(g_io_service, m_ssl_context)
  , m_ssl(false)
{
}

Upstream::Upstream(asio::ssl::context &&ssl_context)
  : m_resolver(g_io_service)
  , m_socket(g_io_service)
  , m_ssl_context(std::move(ssl_context))
  , m_ssl_socket(g_io_service, m_ssl_context)
  , m_ssl(true)
{
  m_ssl_socket.set_verify_mode(asio::ssl::verify_none);
  m_ssl_socket.set_verify_callback([](bool preverified, asio::ssl::verify_context &ctx) {
    return preverified;
  });
}

void Upstream::connect(
  const std::string &host,
  std::shared_ptr<Context> context,
  Object::Receiver receiver
) {
  if (host.empty() || !m_host.empty()) return;

  m_host = host;
  m_receiver = receiver;

  if (auto listener = find_host(host)) {
    m_connected = true;
    m_pipeline = listener->accept(
      context,
      [=](std::unique_ptr<Object> obj) { egress(std::move(obj)); }
    );
    egress(make_object<SessionStart>());
    for (auto &obj : m_blocked_objects) {
      m_pipeline->ingress(std::move(obj));
    }
    m_blocked_objects.clear();
    return;
  }

  std::string name, port;
  auto p = host.find_last_of(':');
  if (p == std::string::npos) {
    name = host;
    port = "80";
  } else {
    name = host.substr(0 , p);
    port = host.substr(p + 1);
  }

  auto start_session = [=]() {
    m_connected = true;
    Log::debug("Connected to upstream %s", m_host.c_str());
    egress(make_object<SessionStart>());
    for (auto &obj : m_blocked_objects) {
      ingress(std::move(obj));
    }
    m_blocked_objects.clear();
    receive();
  };

  auto on_connected = [=](const std::error_code &ec) {
    m_async_waiting = false;

    if (ec) {
      Log::error(
        "Connection failed to host %s, error: %s",
        m_host.c_str(), ec.message().c_str());
      egress(make_object<SessionEnd>(
        ec.message(),
        SessionEnd::CONNECTION_REFUSED));
      free();
      return;
    }

    if (!m_ssl) {
      start_session();
      return;
    }

    m_async_waiting = true;

    m_ssl_socket.async_handshake(asio::ssl::stream_base::client, [=](const std::error_code &ec) {
      m_async_waiting = false;

      if (ec) {
        Log::error(
          "Handshake failed to host %s, error: %s",
          m_host.c_str(), ec.message().c_str());
        egress(make_object<SessionEnd>(
          ec.message(),
          SessionEnd::CONNECTION_REFUSED));
        free();
      } else {
        start_session();
      }
    });
  };

  auto on_resolved = [=](
    const std::error_code &ec,
    tcp::resolver::results_type result
  ) {
    m_async_waiting = false;

    if (ec) {
      Log::error(
        "Failed to resolve hostname %s, error: %s",
        m_host.c_str(), ec.message().c_str());
      egress(make_object<SessionEnd>(
        ec.message(),
        SessionEnd::CANNOT_RESOLVE));
      free();
      return;
    }

    m_async_waiting = true;

    if (m_ssl) {
      m_ssl_socket.lowest_layer().async_connect(*result, on_connected);
    } else {
      m_socket.async_connect(*result, on_connected);
    }
  };

  m_async_waiting = true;
  m_resolver.async_resolve(
    tcp::resolver::query(name, port),
    on_resolved);
}

void Upstream::ingress(std::unique_ptr<Object> obj) {
  if (!m_connected) {
    m_blocked_objects.push_back(std::move(obj));
    return;
  }

  if (auto e = obj->as<SessionEnd>()) {
    if (!m_closed) {
      m_closed = true;
      if (m_pipeline) {
        m_pipeline->ingress(std::move(obj));
        free();
      } else if (m_buffer.empty()) {
        close();
      }
    }
    return;
  }

  if (m_pipeline) {
    m_pipeline->ingress(std::move(obj));
    return;
  }

  if (auto data = obj->as<Data>()) {
    send(std::unique_ptr<Data>(data));
    obj.release();
  }
}

void Upstream::egress(std::unique_ptr<Object> obj) {
  if (m_receiver) {
    m_receiver(std::move(obj));
  }
};

void Upstream::send(std::unique_ptr<Data> data) {
  if (!m_closed) {
    m_buffer.push_back(std::move(data));
    if (m_buffer.size() == 1) pump();
  }
}

void Upstream::pump() {
  auto on_sent = [=](const std::error_code &ec, std::size_t n) {
    const auto &data = m_buffer.front();
    data->shift(n);
    if (data->size() == 0) {
      m_buffer.pop_front();
      if (!ec && !m_buffer.empty()) pump();
    }

    if (ec) {
      auto msg = ec.message();
      Log::error(
        "Error writing to upstream %s: %s",
        m_host.c_str(), msg.c_str());
      m_buffer.clear();
      close();

    } else if (m_closed) {
      close();
    }
  };

  if (m_ssl) {
    m_ssl_socket.async_write_some(
      DataChunks(m_buffer.front()->chunks()),
      on_sent
    );
  } else {
    m_socket.async_write_some(
      DataChunks(m_buffer.front()->chunks()),
      on_sent
    );
  }
}

void Upstream::receive() {
  auto buffer = new Data(0x1000);

  auto on_received = [=](const std::error_code &ec, size_t n) {
    m_async_waiting = false;

    if (n > 0) {
      buffer->pop(buffer->size() - n);
      egress(std::unique_ptr<Object>(buffer));
    } else {
      delete buffer;
    }

    if (ec) {
      if (ec == asio::error::eof) {
        Log::debug(
          "Connection closed from upstream %s",
          m_host.c_str());
        egress(make_object<SessionEnd>(SessionEnd::NO_ERROR));
      } else {
        auto msg = ec.message();
        Log::error(
          "Error reading from upstream %s: %s",
          m_host.c_str(), msg.c_str());
        egress(make_object<SessionEnd>(SessionEnd::READ_ERROR));
      }
      free();

    } else {
      receive();
    }
  };

  m_async_waiting = true;

  if (m_ssl) {
    m_ssl_socket.async_read_some(
      DataChunks(buffer->chunks()),
      on_received);
  } else {
    m_socket.async_read_some(
      DataChunks(buffer->chunks()),
      on_received);
  }
}

void Upstream::close() {
  m_resolver.cancel();

  std::error_code ec;
  if (m_ssl) {
    m_ssl_socket.lowest_layer().shutdown(tcp::socket::shutdown_send, ec);
  } else {
    m_socket.shutdown(tcp::socket::shutdown_send, ec);
  }

  if (ec) {
    Log::error("Error closing socket: %s", this, ec.message().c_str());
  }

  free();
}

void Upstream::free() {
  if (!m_async_waiting && m_buffer.empty()) {
    delete this;
  }
}

NS_END
