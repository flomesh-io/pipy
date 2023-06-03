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

#include "socket.hpp"
#include "log.hpp"

namespace pipy {

using tcp = asio::ip::tcp;

//
// SocketTCP
//

thread_local Data::Producer SocketTCP::s_dp("TCP Socket");

SocketTCP::~SocketTCP() {
  Ticker::get()->unwatch(this);
}

void SocketTCP::start() {
  m_socket.set_option(asio::socket_base::keep_alive(m_options.keep_alive));
  m_socket.set_option(tcp::no_delay(m_options.no_delay));

  auto t = Ticker::get()->tick();
  m_tick_read = t;
  m_tick_write = t;
  m_started = true;

  on_socket_start();

  receive();

  if (m_sending_end) {
    send();
  } else if (!m_buffer_send.empty()) {
    FlushTarget::need_flush();
  }

  Ticker::get()->watch(this);
}

void SocketTCP::output(Event *evt) {
  if (!m_sending_end) {
    if (auto data = evt->as<Data>()) {
      if (data->size() > 0) {
        auto size = m_buffer_send.size();
        auto limit = m_options.buffer_limit;
        if (limit > 0 && size >= limit) {
          auto excess = size + data->size() - limit;
          char msg[200];
          std::snprintf(msg, sizeof(msg), "buffer overflow by %d bytes over the limit of %d", (int)excess, (int)limit);
          Log::error(msg);
          on_socket_overflow(excess);
          close(false);
        } else {
          m_buffer_send.push(*data);
          auto limit = m_options.congestion_limit;
          if (limit > 0 && m_buffer_send.size() >= limit) {
            m_congestion.begin();
          }
          if (m_started) {
            FlushTarget::need_flush();
          }
        }
      }
    } else if (auto eos = evt->as<StreamEnd>()) {
      m_sending_end = true;
      m_eos = (eos->error_code() == StreamEnd::NO_ERROR);
      if (m_started) {
        send();
      }
    }
  }
}

void SocketTCP::close() {
  close(false);
}

void SocketTCP::receive() {
  if (m_closed) return;
  if (m_receiving) return;
  if (m_paused) return;

  m_buffer_receive.push(Data(RECEIVE_BUFFER_SIZE, &s_dp));
  m_socket.async_read_some(
    DataChunks(m_buffer_receive.chunks()),
    ReceiveHandler(this)
  );

  m_receiving = true;
  handler_retain();
}

void SocketTCP::send() {
  if (m_closed) return;
  if (m_sending) return;

  if (m_buffer_send.empty()) {
    if (m_sending_end) {
      close_send();
    }
    return;
  }

  if (Log::is_enabled(Log::TCP)) {
    std::cerr << Log::format_elapsed_time();
    std::cerr << (m_is_inbound ? " tcp <<<< send " : " tcp send >>>> ");
    std::cerr << m_buffer_send.size() << std::endl;
  }

  m_socket.async_write_some(
    DataChunks(m_buffer_send.chunks()),
    SendHandler(this)
  );

  m_sending = true;
  handler_retain();
}

void SocketTCP::close_receive() {
  if (!m_sending_end) {
    handler_retain();
  }
}

void SocketTCP::close_send() {
  if (m_receiving_end) {
    close(true);
    handler_release();
  } else if (m_eos) {
    std::error_code ec;
    m_socket.shutdown(tcp::socket::shutdown_send, ec);
  } else {
    close(false);
  }
}

void SocketTCP::close(bool shutdown) {
  if (m_started && !m_closed) {
    if (m_socket.is_open()) {
      std::error_code ec;
      if (shutdown && m_eos) {
        m_socket.shutdown(tcp::socket::shutdown_both, ec);
      }
      m_socket.close(ec);

      if (ec) {
        log_error("error closing socket", ec);
      } else {
        log_debug("socket closed");
      }
    }

    m_sending_end = true;
    m_closed = true;

    if (m_paused) {
      m_paused = false;
      handler_release();
    }
  }
}

void SocketTCP::on_tap_open() {
  if (m_paused) {
    m_paused = false;
    receive();
    handler_release();
  }
}

void SocketTCP::on_tap_close() {
  if (!m_paused) {
    m_paused = true;
    handler_retain();
  }
}

void SocketTCP::on_flush() {
  send();
}

void SocketTCP::on_tick(double tick) {
  auto r = tick - m_tick_read;
  auto w = tick - m_tick_write;

  if (m_options.idle_timeout > 0) {
    auto t = m_options.idle_timeout;
    if (r >= t && w >= t) {
      on_socket_input(StreamEnd::make(StreamEnd::IDLE_TIMEOUT));
      close(StreamEnd::IDLE_TIMEOUT);
      return;
    }
  }

  if (m_options.read_timeout > 0) {
    if (r >= m_options.read_timeout) {
      on_socket_input(StreamEnd::make(StreamEnd::READ_TIMEOUT));
      close(StreamEnd::READ_TIMEOUT);
      return;
    }
  }

  if (m_options.write_timeout > 0) {
    if (r >= m_options.write_timeout) {
      on_socket_input(StreamEnd::make(StreamEnd::WRITE_TIMEOUT));
      close(StreamEnd::WRITE_TIMEOUT);
      return;
    }
  }
}

void SocketTCP::on_receive(const std::error_code &ec, std::size_t n) {
  InputContext ic(this);

  m_receiving = false;
  m_tick_read = Ticker::get()->tick();

  if (ec != asio::error::operation_aborted && !m_closed) {
    if (n > 0) {
      m_buffer_receive.pop(m_buffer_receive.size() - n);
      auto size = m_buffer_receive.size();
      m_traffic_read += size;

      if (Log::is_enabled(Log::TCP)) {
        std::cerr << Log::format_elapsed_time();
        std::cerr << (m_is_inbound ? " tcp >>>> recv " : " tcp recv <<<< ");
        std::cerr << size << std::endl;
      }

      on_socket_input(Data::make(std::move(m_buffer_receive)));
    }

    if (ec) {
      if (ec == asio::error::eof) {
        m_receiving_end = true;
        log_debug("EOF from peer");
        on_socket_input(StreamEnd::make());
        close_receive();
      } else if (ec == asio::error::connection_reset) {
        log_warn("connection reset by peer", ec);
        on_socket_input(StreamEnd::make(StreamEnd::CONNECTION_RESET));
        close(false);
      } else {
        log_warn("error reading from peer", ec);
        on_socket_input(StreamEnd::make(StreamEnd::READ_ERROR));
        close(false);
      }

    } else {
      receive();
    }
  }

  handler_release();
}

void SocketTCP::on_send(const std::error_code &ec, std::size_t n) {
  m_sending = false;
  m_tick_write = Ticker::get()->tick();

  if (ec != asio::error::operation_aborted && !m_closed) {
    m_buffer_send.shift(n);
    m_traffic_write += n;

    auto limit = m_options.congestion_limit;
    if (limit > 0 && m_buffer_send.size() < limit) {
      m_congestion.end();
    }

    if (ec) {
      log_warn("error writing to peer", ec);
      close(false);

    } else if (m_sending_end && m_buffer_send.empty()) {
      close_send();

    } else {
      send();
    }
  }

  handler_release();
}

void SocketTCP::log_debug(const char *msg) {
  if (m_is_inbound) {
    if (Log::is_enabled(Log::INBOUND)) {
      char desc[1000];
      on_socket_describe(desc, sizeof(desc));
      Log::debug(Log::INBOUND, "%s %s", desc, msg);
    }
  } else {
    if (Log::is_enabled(Log::OUTBOUND)) {
      char desc[1000];
      on_socket_describe(desc, sizeof(desc));
      Log::debug(Log::OUTBOUND, "%s %s", desc, msg);
    }
  }
}

void SocketTCP::log_warn(const char *msg, const std::error_code &ec) {
  if (Log::is_enabled(Log::WARN)) {
    char desc[1000];
    on_socket_describe(desc, sizeof(desc));
    Log::warn("%s %s: %s", desc, msg, ec.message().c_str());
  }
}

void SocketTCP::log_error(const char *msg, const std::error_code &ec) {
  if (Log::is_enabled(Log::ERROR)) {
    char desc[1000];
    on_socket_describe(desc, sizeof(desc));
    Log::error("%s %s: %s", desc, msg, ec.message().c_str());
  }
}

void SocketTCP::log_error(const char *msg) {
  if (Log::is_enabled(Log::ERROR)) {
    char desc[1000];
    on_socket_describe(desc, sizeof(desc));
    Log::error("%s %s", desc, msg);
  }
}

} // namespace pipy
