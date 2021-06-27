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
#include "constants.hpp"
#include "logging.hpp"

namespace pipy {

using tcp = asio::ip::tcp;

static asio::ssl::context s_default_ssl_context(asio::ssl::context::sslv23_client);

Outbound::Outbound(int buffer_limit)
  : m_resolver(Net::service())
  , m_socket(Net::service())
  , m_ssl_socket(Net::service(), s_default_ssl_context)
  , m_buffer_limit(buffer_limit)
{
}

Outbound::Outbound(asio::ssl::context &ssl_context)
  : m_resolver(Net::service())
  , m_socket(Net::service())
  , m_ssl_socket(Net::service(), ssl_context)
  , m_ssl(true)
  , m_buffer_limit(0)
{
  m_ssl_socket.set_verify_mode(asio::ssl::verify_none);
  m_ssl_socket.set_verify_callback([](bool preverified, asio::ssl::verify_context &ctx) {
    return preverified;
  });
}

Outbound::~Outbound() {
  if (m_on_delete) {
    m_on_delete();
  }
}

void Outbound::connect(const std::string &host, int port) {
  m_host = host;
  m_port = port;
  connect(0);
}

void Outbound::send(const pjs::Ref<Data> &data) {
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
      } else {
        m_discarded_data_size += data->size();
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
    pump();
    close();
    free();
  }
}

void Outbound::connect(double delay) {

  auto start_session = [=]() {
    Log::debug("Outbound: %p, connected to upstream %s", this, m_host.c_str());
    m_connected = true;
    m_address = (m_ssl ? m_ssl_socket.lowest_layer() : m_socket).remote_endpoint().address().to_string();
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
      if (should_reconnect()) {
        reconnect();
      } else {
        if (m_receiver) {
          pjs::Ref<SessionEnd> evt(SessionEnd::make(
            ec.message(),
            SessionEnd::CONNECTION_REFUSED));
          m_receiver(evt);
        }
        cancel_connecting();
      }
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
        if (m_receiver) {
          pjs::Ref<SessionEnd> evt(SessionEnd::make(
            ec.message(),
            SessionEnd::CONNECTION_REFUSED));
          m_receiver(evt);
        }
        cancel_connecting();
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
      if (m_receiver) {
        pjs::Ref<SessionEnd> evt(SessionEnd::make(
          ec.message(),
          SessionEnd::CANNOT_RESOLVE));
        m_receiver(evt);
      }
      cancel_connecting();
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
    m_timer.schedule(delay, resolve);
  } else {
    resolve();
  }
}

bool Outbound::should_reconnect() {
  if (m_retry_count >= 0 && m_retries >= m_retry_count) {
    return false;
  } else {
    return true;
  }
}

void Outbound::cancel_connecting() {
  m_reading_ended = true;
  free();
}

void Outbound::reconnect() {
  std::error_code ec;
  m_socket.close(ec);
  m_ssl_socket.lowest_layer().close(ec);
  m_retries++;
  connect(m_retry_delay);
}

void Outbound::receive() {
  pjs::Ref<Data> buffer(Data::make(RECEIVE_BUFFER_SIZE));

  auto on_received = [=](const std::error_code &ec, size_t n) {
    if (n > 0) {
      buffer->pop(buffer->size() - n);
      if (m_receiver) m_receiver(buffer);
      if (m_receiver) m_receiver(Data::flush());
    }

    if (ec) {
      if (ec == asio::error::eof || ec == asio::error::operation_aborted) {
        Log::debug(
          ec == asio::error::eof
            ? "Outbound: %p, connection closed by upstream %s:%d"
            : "Outbound: %p, closed connection to upstream %s:%d",
          this, m_host.c_str(), m_port);
        if (m_receiver) {
          pjs::Ref<SessionEnd> evt(SessionEnd::make(SessionEnd::NO_ERROR));
          m_receiver(evt);
        }
      } else {
        auto msg = ec.message();
        Log::warn(
          "Outbound: %p, error reading from upstream %s:%d, %s",
          this, m_host.c_str(), m_port, msg.c_str());
        if (m_receiver) {
          pjs::Ref<SessionEnd> evt(SessionEnd::make(SessionEnd::READ_ERROR));
          m_receiver(evt);
        }
      }

      if (should_reconnect()) {
        reconnect();
      } else {
        m_reading_ended = true;
        free();
      }

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

    } else {
      pump();
    }

    if (m_overflowed && m_buffer.size() == 0) {
      Log::error(
        "Outbound: %p, %d bytes sending to upstream %s:%d were discared due to overflow",
        this, m_discarded_data_size, m_host.c_str(), m_port
      );
      m_writing_ended = true;
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
  if (!m_pumping && m_reading_ended && m_writing_ended) {
    delete this;
  }
}

} // namespace pipy