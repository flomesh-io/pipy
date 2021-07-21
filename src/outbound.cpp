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
#include "utils.hpp"
#include "logging.hpp"

namespace pipy {

using tcp = asio::ip::tcp;

List<Outbound> Outbound::s_all_outbounds;

Outbound::Outbound()
  : m_resolver(Net::service())
  , m_socket(Net::service())
{
  Log::debug("Outbound: %p, allocated", this);
  s_all_outbounds.push(this);
}

Outbound::~Outbound() {
  Log::debug("Outbound: %p, freed", this);
  if (m_on_delete) {
    m_on_delete();
  }
  s_all_outbounds.remove(this);
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
    m_connection_time += utils::now() - m_start_time;
    m_connected = true;
    m_address = m_socket.remote_endpoint().address().to_string();
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
        pjs::Ref<SessionEnd> evt(SessionEnd::make(
          ec.message(),
          SessionEnd::CONNECTION_REFUSED));
        if (output(evt)) return;
        cancel_connecting();
      }
      return;
    }

    start_session();
  };

  auto on_resolved = [=](
    const std::error_code &ec,
    tcp::resolver::results_type result
  ) {
    if (ec) {
      Log::error(
        "Outbound: %p, failed to resolve hostname %s, %s",
        this, m_host.c_str(), ec.message().c_str());
      pjs::Ref<SessionEnd> evt(SessionEnd::make(
        ec.message(),
        SessionEnd::CANNOT_RESOLVE));
      if (output(evt)) return;
      cancel_connecting();
      return;
    }

    m_socket.async_connect(*result, on_connected);
  };

  auto resolve = [=]() {
    m_start_time = utils::now();
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
  m_retries++;
  connect(m_retry_delay);
}

void Outbound::receive() {
  static Data::Producer s_data_producer("Outbound");

  pjs::Ref<Data> buffer(Data::make(RECEIVE_BUFFER_SIZE, &s_data_producer));

  auto on_received = [=](const std::error_code &ec, size_t n) {
    if (n > 0) {
      buffer->pop(buffer->size() - n);
      if (output(buffer)) return;
      if (output(Data::flush())) return;
    }

    if (ec) {
      if (ec == asio::error::eof || ec == asio::error::operation_aborted) {
        Log::debug(
          ec == asio::error::eof
            ? "Outbound: %p, connection closed by upstream %s:%d"
            : "Outbound: %p, closed connection to upstream %s:%d",
          this, m_host.c_str(), m_port);
        pjs::Ref<SessionEnd> evt(SessionEnd::make(SessionEnd::NO_ERROR));
        if (output(evt)) return;
      } else {
        auto msg = ec.message();
        Log::warn(
          "Outbound: %p, error reading from upstream %s:%d, %s",
          this, m_host.c_str(), m_port, msg.c_str());
        pjs::Ref<SessionEnd> evt(SessionEnd::make(SessionEnd::READ_ERROR));
        if (output(evt)) return;
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

  m_socket.async_read_some(
    DataChunks(buffer->chunks()),
    on_received);
}

bool Outbound::output(Event *evt) {
  if (m_receiver) {
    m_outputing = true;
    m_receiver(evt);
    m_outputing = false;
    if (m_freed) {
      delete this;
      return true;
    }
  }
  return false;
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

  m_socket.async_write_some(
    DataChunks(m_buffer.chunks()),
    on_sent
  );

  m_pumping = true;
}

void Outbound::close() {
  if (!m_connected) return;
  if (m_pumping) return;

  std::error_code ec;
  m_socket.close(ec);
  if (ec) {
    Log::error("Outbound: %p, error closing socket to %s:%d, %s", this, m_host.c_str(), m_port, ec.message().c_str());
  } else {
    Log::debug("Outbound: %p, connection closed to %s:%d", this, m_host.c_str(), m_port);
  }
}

void Outbound::free() {
  if (!m_pumping && m_reading_ended && m_writing_ended) {
    if (m_outputing) {
      m_freed = true;
    } else {
      delete this;
    }
  }
}

} // namespace pipy