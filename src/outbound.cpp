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

#include "outbound.hpp"
#include "listener.hpp"
#include "constants.hpp"
#include "logging.hpp"

NS_BEGIN

using tcp = asio::ip::tcp;

Outbound::Outbound()
  : m_resolver(g_io_service)
  , m_socket(g_io_service)
  , m_ssl_context(asio::ssl::context::sslv3)
  , m_ssl_socket(g_io_service, m_ssl_context)
{
}

Outbound::Outbound(asio::ssl::context &&ssl_context)
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

Outbound::~Outbound() {
}

void Outbound::connect(
  const std::string &host, int port,
  Object::Receiver on_output
) {
  m_host = host;
  m_port = port;
  m_output = on_output;

  connect(0);
}

void Outbound::send(std::unique_ptr<Data> data) {
  if (!m_writing_ended) {
    if (data->size() > 0) {
      if (!m_overflowed) {
        if (m_buffer_limit > 0 && m_buffer.size() >= m_buffer_limit) {
          Log::error(
            "Outbound: %p to upstream %s:%d buffer overflow, size = %d",
            this, m_host.c_str(), m_port, m_buffer.size());
          m_overflowed = true;
        }
      }
      if (!m_overflowed) {
        m_buffer.push(*data);
        if (m_buffer.size() >= SEND_BUFFER_FLUSH_SIZE) pump();
      }
    } else {
      pump();
    }
  }
}

void Outbound::flush() {
  if (!m_writing_ended) {
    pump();
  }
}

void Outbound::end() {
  if (!m_writing_ended) {
    m_writing_ended = true;
    m_output = [](std::unique_ptr<Object>) {};
    pump();
    close();
    free();
  }
}

void Outbound::connect(double delay) {

  auto start_session = [=]() {
    Log::debug("Outbound: %p, connected to upstream %s", this, m_host.c_str());
    m_connected = true;
    m_output(make_object<SessionStart>());
    if (m_writing_ended && m_buffer.empty()) {
      m_reading_ended = true;
      close();
      free();
    } else {
      pump();
      receive();
    }
  };

  auto on_connected = [=](const std::error_code &ec) {
    if (ec) {
      Log::error(
        "Outbound: %p, cannot connect to %s:%d, %s",
        this, m_host.c_str(), m_port, ec.message().c_str());
      m_output(make_object<SessionEnd>(
        ec.message(),
        SessionEnd::CONNECTION_REFUSED));
      reconnect();
      return;
    }

    if (!m_ssl) {
      start_session();
      return;
    }

    m_ssl_socket.async_handshake(asio::ssl::stream_base::client, [=](const std::error_code &ec) {
      if (ec) {
        Log::error(
          "Outbound: %p, handshake failed to %s:%d, %s",
          this, m_host.c_str(), m_port, ec.message().c_str());
        m_output(make_object<SessionEnd>(
          ec.message(),
          SessionEnd::CONNECTION_REFUSED));
        reconnect();
      } else {
        start_session();
      }
    });
  };

  auto on_resolved = [=](
    const std::error_code &ec,
    tcp::resolver::results_type result
  ) {
    if (ec) {
      Log::error(
        "Outbound: %p, failed to resolve hostname %s, %s",
        this, m_host.c_str(), ec.message().c_str());
      m_output(make_object<SessionEnd>(
        ec.message(),
        SessionEnd::CANNOT_RESOLVE));
      reconnect();
      return;
    }

    if (m_ssl) {
      m_ssl_socket.lowest_layer().async_connect(*result, on_connected);
    } else {
      m_socket.async_connect(*result, on_connected);
    }
  };

  auto resolve = [=]() {
    m_resolver.async_resolve(
      tcp::resolver::query(m_host, std::to_string(m_port)),
      on_resolved);
  };

  if (delay > 0) {
    Listener::set_timeout(delay, resolve);
  } else {
    resolve();
  }
}

void Outbound::reconnect() {
  if (m_retry_count >= 0 && m_retries >= m_retry_count) {
    m_reading_ended = true;
    free();
  } else {
    std::error_code ec;
    m_socket.close(ec);
    m_ssl_socket.lowest_layer().close(ec);
    m_retries++;
    connect(m_retry_delay);
  }
}

void Outbound::receive() {
  auto buffer = new Data(RECEIVE_BUFFER_SIZE);

  auto on_received = [=](const std::error_code &ec, size_t n) {
    if (n > 0) {
      buffer->pop(buffer->size() - n);
      m_output(std::unique_ptr<Object>(buffer));
      m_output(make_object<Data>()); // flush
    } else {
      delete buffer;
    }

    if (ec) {
      if (ec == asio::error::eof) {
        Log::debug(
          "Outbound: %p, connection closed by upstream %s:%d",
          this, m_host.c_str(), m_port);
        m_output(make_object<SessionEnd>(SessionEnd::NO_ERROR));
      } else if (ec != asio::error::operation_aborted) {
        auto msg = ec.message();
        Log::warn(
          "Outbound: %p, error reading from upstream %s:%d, %s",
          this, m_host.c_str(), m_port, msg.c_str());
        m_output(make_object<SessionEnd>(SessionEnd::READ_ERROR));
      }
      reconnect();

    } else {
      receive();
    }
  };

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

void Outbound::pump() {
  if (!m_connected) return;
  if (m_pumping) return;
  if (m_buffer.empty()) return;

  auto on_sent = [=](const std::error_code &ec, std::size_t n) {
    m_buffer.shift(n);
    m_pumping = false;

    if (ec) {
      auto msg = ec.message();
      Log::warn(
        "Outbound: %p, error writing to upstream %s:%d, %s",
        this, m_host.c_str(), m_port, msg.c_str());
      m_buffer.clear();
      m_writing_ended = true;

    } else if (m_buffer.size() > 0) {
      pump();
    }

    if (m_overflowed && m_buffer.size() < m_buffer_limit) {
      m_overflowed = false;
    }

    if (m_writing_ended) {
      close();
      free();
    }
  };

  if (m_ssl) {
    m_ssl_socket.async_write_some(
      DataChunks(m_buffer.chunks()),
      on_sent
    );
  } else {
    m_socket.async_write_some(
      DataChunks(m_buffer.chunks()),
      on_sent
    );
  }

  m_pumping = true;
}

void Outbound::close() {
  if (!m_connected) return;
  if (m_pumping) return;

  std::error_code ec;
  if (m_ssl) {
    m_ssl_socket.lowest_layer().close(ec);
  } else {
    m_socket.close(ec);
  }
  if (ec) {
    Log::error("Outbound: %p, error closing socket to %s:%d, %s", this, m_host.c_str(), m_port, ec.message().c_str());
  } else {
    Log::debug("Outbound: %p, connection closed to %s:%d", this, m_host.c_str(), m_port);
  }
}

void Outbound::free() {
  if (m_reading_ended && m_writing_ended && !m_pumping && m_buffer.empty()) {
    delete this;
  }
}

NS_END