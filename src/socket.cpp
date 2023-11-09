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
using udp = asio::ip::udp;

//
// SocketBase
//

void SocketBase::log_debug(const char *msg) {
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

void SocketBase::log_warn(const char *msg, const std::error_code &ec) {
  if (Log::is_enabled(Log::WARN)) {
    char desc[1000];
    on_socket_describe(desc, sizeof(desc));
    Log::warn("%s %s: %s", desc, msg, ec.message().c_str());
  }
}

void SocketBase::log_error(const char *msg, const std::error_code &ec) {
  if (Log::is_enabled(Log::ERROR)) {
    char desc[1000];
    on_socket_describe(desc, sizeof(desc));
    Log::error("%s %s: %s", desc, msg, ec.message().c_str());
  }
}

void SocketBase::log_error(const char *msg) {
  if (Log::is_enabled(Log::ERROR)) {
    char desc[1000];
    on_socket_describe(desc, sizeof(desc));
    Log::error("%s %s", desc, msg);
  }
}

//
// SocketTCP
//

thread_local Data::Producer SocketTCP::s_dp("TCP Socket");

SocketTCP::~SocketTCP() {
  Ticker::get()->unwatch(this);
}

void SocketTCP::open() {
  m_socket.set_option(asio::socket_base::keep_alive(m_options.keep_alive));
  m_socket.set_option(tcp::no_delay(m_options.no_delay));

  auto t = Ticker::get()->tick();
  m_tick_read = t;
  m_tick_write = t;
  m_state = OPEN;
  m_opened = true;

  if (m_eos) {
    send();
    if (m_state == CLOSED) return;
  }

  if (!m_buffer_send.empty()) {
    FlushTarget::need_flush();
  }

  receive();
  Ticker::get()->watch(this);
}

void SocketTCP::output(Event *evt) {
  if (m_state == CLOSED) return;
  if (m_state == HALF_CLOSED_LOCAL) return;

  if (auto data = evt->as<Data>()) {
    if (data->size() > 0) {
      auto limit = m_options.buffer_limit;
      if (limit > 0 && m_buffer_send.size() >= limit) {
        log_error("buffer overflow");
        on_socket_input(StreamEnd::make(StreamEnd::BUFFER_OVERFLOW));
        close();
      } else {
        m_buffer_send.push(*data);
        auto limit = m_options.congestion_limit;
        if (limit > 0 && m_buffer_send.size() >= limit) {
          m_congestion.begin();
        }
        if (m_state != IDLE) FlushTarget::need_flush();
      }
    }
  } else if (auto eos = evt->as<StreamEnd>()) {
    if (!m_eos) m_eos = eos;
    if (m_state != IDLE) FlushTarget::need_flush();
  }
}

void SocketTCP::close() {
  m_state = CLOSED;
  close_socket();
  close_async();
}

void SocketTCP::receive() {
  if (m_state != OPEN && m_state != HALF_CLOSED_LOCAL) return;
  if (m_receiving) return;
  if (m_paused) return;

  m_buffer_receive.push(Data(RECEIVE_BUFFER_SIZE, &s_dp));
  m_socket.async_read_some(
    DataChunks(m_buffer_receive.chunks()),
    ReceiveHandler(this)
  );

  m_receiving = true;
}

void SocketTCP::send() {
  if (m_state != OPEN && m_state != HALF_CLOSED_REMOTE) return;
  if (m_sending) return;

  if (m_buffer_send.empty()) {
    if (m_eos) {
      if (m_eos->error_code() == StreamEnd::NO_ERROR) {
        shutdown_socket();
        if (m_state == OPEN) {
          m_state = HALF_CLOSED_LOCAL;
        } else {
          close();
        }
      } else {
        close();
      }
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
}

void SocketTCP::shutdown_socket() {
  if (m_socket.is_open()) {
    std::error_code ec;
    m_socket.shutdown(tcp::socket::shutdown_send, ec);
    if (ec) {
      log_warn("error when socket shutdown", ec);
    } else {
      log_debug("socket shutdown");
    }
  }
}

void SocketTCP::close_socket() {
  if (m_socket.is_open()) {
    std::error_code ec;
    m_socket.close(ec);
    if (ec) {
      log_warn("error closing socket", ec);
    } else {
      log_debug("socket closed");
    }
  }
}

void SocketTCP::close_async() {
  if (m_closed) return;
  if (m_receiving) return;
  if (m_sending) return;
  if (m_state != CLOSED) return;
  m_closed = true;
  if (m_opened) on_socket_close();
}

void SocketTCP::on_tap_open() {
  m_paused = false;
  receive();
}

void SocketTCP::on_tap_close() {
  m_paused = true;
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
      close();
      return;
    }
  }

  if (m_options.read_timeout > 0) {
    if (r >= m_options.read_timeout) {
      on_socket_input(StreamEnd::make(StreamEnd::READ_TIMEOUT));
      close();
      return;
    }
  }

  if (m_options.write_timeout > 0) {
    if (r >= m_options.write_timeout) {
      on_socket_input(StreamEnd::make(StreamEnd::WRITE_TIMEOUT));
      close();
      return;
    }
  }
}

void SocketTCP::on_receive(const std::error_code &ec, std::size_t n) {
  InputContext ic(this);

  m_receiving = false;
  m_tick_read = Ticker::get()->tick();

  if (ec != asio::error::operation_aborted && m_state != CLOSED) {
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
        log_debug("EOF from peer");
        on_socket_input(StreamEnd::make());
        if (m_state == OPEN) {
          m_state = HALF_CLOSED_REMOTE;
        } else if (m_state == HALF_CLOSED_LOCAL) {
          m_state = CLOSED;
          close_socket();
        }
      } else if (ec == asio::error::connection_reset) {
        log_warn("connection reset by peer", ec);
        on_socket_input(StreamEnd::make(StreamEnd::CONNECTION_RESET));
        m_state = CLOSED;
        close_socket();
      } else {
        log_warn("error reading from peer", ec);
        on_socket_input(StreamEnd::make(StreamEnd::READ_ERROR));
        m_state = CLOSED;
        close_socket();
      }

    } else {
      receive();
    }
  }

  close_async();
}

void SocketTCP::on_send(const std::error_code &ec, std::size_t n) {
  m_sending = false;
  m_tick_write = Ticker::get()->tick();

  if (ec != asio::error::operation_aborted && m_state != CLOSED) {
    m_buffer_send.shift(n);
    m_traffic_write += n;

    auto limit = m_options.congestion_limit;
    if (limit > 0 && m_buffer_send.size() < limit) {
      m_congestion.end();
    }

    if (ec) {
      log_warn("error writing to peer", ec);
      m_state = CLOSED;
      close_socket();

    } else if (m_buffer_send.empty()) {
      if (m_eos) {
        if (m_eos->error_code() != StreamEnd::NO_ERROR) {
          m_state = CLOSED;
          close_socket();
        } else {
          shutdown_socket();
          if (m_state == OPEN) {
            m_state = HALF_CLOSED_LOCAL;
          } else if (m_state == HALF_CLOSED_REMOTE) {
            m_state = CLOSED;
            close_socket();
          }
        }
      }

    } else {
      send();
    }
  }

  close_async();
}

//
// SocketUDP
//

thread_local Data::Producer SocketUDP::s_dp("UDP Socket");

SocketUDP::~SocketUDP() {
  Ticker::get()->unwatch(this);
}

void SocketUDP::open() {
  m_endpoint = m_socket.local_endpoint();
  m_opened = true;

  if (!m_buffer.empty()) {
    m_buffer.flush(
      [this](Event *evt) {
        if (auto data = evt->as<Data>()) {
          send(data);
        }
      }
    );
  }

  receive();
  Ticker::get()->watch(this);
}

void SocketUDP::close() {
  m_closing = true;
  close_peers();
  close_socket();
  close_async();
}

void SocketUDP::output(Event *evt) {
  if (auto data = evt->as<Data>()) {
    if (!data->empty()) {
      if (m_opened) {
        send(data);
      } else {
        m_buffer.push(data);
      }
    }
  }
}

void SocketUDP::output(Event *evt, Peer *peer) {
  if (auto data = evt->as<Data>()) {
    if (!data->empty()) {
      peer->m_tick_write = Ticker::get()->tick();
      send(data, peer->m_endpoint);
    }
  } else if (evt->is<StreamEnd>()) {
    m_peers.erase(peer->m_endpoint);
    peer->m_socket = nullptr;
    peer->on_peer_close();
  }
}

void SocketUDP::receive() {
  if (m_closing) return;
  if (m_receiving) return;
  if (m_paused) return;

  auto *buf = Data::make(RECEIVE_BUFFER_SIZE, &s_dp);
  buf->retain();

  m_socket.async_receive_from(
    DataChunks(buf->chunks()),
    m_from,
    ReceiveHandler(this, buf)
  );

  m_receiving = true;
}

void SocketUDP::send(Data *data) {
  if (m_closing) return;

  data->retain();
  m_sending_size += data->size();
  m_sending_count++;

  if (Log::is_enabled(Log::UDP)) {
    std::cerr << Log::format_elapsed_time();
    std::cerr << (m_is_inbound ? " udp <<<< send " : " udp send >>>> ");
    std::cerr << data->size() << std::endl;
  }

  m_socket.async_send(
    DataChunks(data->chunks()),
    SendHandler(this, data)
  );
}

void SocketUDP::send(Data *data, const asio::ip::udp::endpoint &endpoint) {
  if (m_closed) return;

  data->retain();
  m_sending_size += data->size();
  m_sending_count++;

  if (Log::is_enabled(Log::UDP)) {
    std::cerr << Log::format_elapsed_time();
    std::cerr << (m_is_inbound ? " udp <<<< send " : " udp send >>>> ");
    std::cerr << data->size() << std::endl;
  }

  m_socket.async_send_to(
    DataChunks(data->chunks()),
    endpoint,
    SendHandler(this, data)
  );
}

void SocketUDP::close_peers(StreamEnd::Error err) {
  InputContext ic;
  std::map<asio::ip::udp::endpoint, Peer*> peers(std::move(m_peers));
  for (const auto &pair : peers) {
    auto p = pair.second;
    p->m_socket = nullptr;
    p->on_peer_input(StreamEnd::make(err));
    p->on_peer_close();
  }
}

void SocketUDP::close_socket() {
  if (m_socket.is_open()) {
    std::error_code ec;
    m_socket.close(ec);
    if (ec) {
      log_warn("error closing socket", ec);
    } else {
      log_debug("socket closed");
    }
  }
}

void SocketUDP::close_async() {
  if (m_closed) return;
  if (m_receiving) return;
  if (m_sending_count > 0) return;
  if (m_closing) {
    m_closed = true;
    if (m_opened) on_socket_close();
  }
}

void SocketUDP::on_tap_open() {
  m_paused = false;
  receive();
}

void SocketUDP::on_tap_close() {
  m_paused = true;
}

void SocketUDP::on_tick(double tick) {
  auto i = m_peers.begin();
  while (i != m_peers.end()) {
    auto p = i->second; i++;
    p->tick(tick);
  }
}

void SocketUDP::on_receive(Data *data, const std::error_code &ec, std::size_t n) {
  InputContext ic(this);

  m_receiving = false;

  if (ec != asio::error::operation_aborted && !m_closing) {
    if (n > 0) {
      data->pop(data->size() - n);
      auto size = data->size();
      m_traffic_read += size;

      if (Log::is_enabled(Log::UDP)) {
        std::cerr << Log::format_elapsed_time();
        std::cerr << (m_is_inbound ? " udp >>>> recv " : " udp recv <<<< ");
        std::cerr << size << std::endl;
      }

      Peer *peer = nullptr;
      auto i = m_peers.find(m_from);
      if (i == m_peers.end()) {
        peer = on_socket_new_peer();
        if (peer) {
          peer->m_socket = this;
          peer->m_endpoint = m_from;
          peer->m_tick_write = Ticker::get()->tick();
          m_peers[m_from] = peer;
          peer->on_peer_open();
        }
      } else {
        peer = i->second;
      }

      if (peer) {
        peer->m_tick_read = Ticker::get()->tick();
        peer->on_peer_input(data);
      } else {
        on_socket_input(data);
      }
    }

    if (ec) {
      log_warn("error reading from peers", ec);
      m_closing = true;
      close_peers(StreamEnd::READ_ERROR);
      close_socket();

    } else {
      receive();
    }
  }

  data->release();
  close_async();
}

void SocketUDP::on_send(Data *data, const std::error_code &ec, std::size_t n) {
  m_sending_count--;

  if (ec != asio::error::operation_aborted && !m_closing) {
    m_sending_size -= data->size();
    m_traffic_write += n;

    auto limit = m_options.congestion_limit;
    if (limit > 0 && m_sending_size < limit) {
      m_congestion.end();
    }

    if (ec) {
      log_warn("error writing to peers", ec);
      m_closing = true;
      close_peers(StreamEnd::WRITE_ERROR);
      close_socket();
    }
  }

  data->release();
  close_async();
}

//
// SocketUDP::Peer
//

void SocketUDP::Peer::tick(double t) {
  const auto &options = m_socket->m_options;

  auto r = t - m_tick_read;
  auto w = t - m_tick_write;

  if (options.idle_timeout > 0) {
    auto t = options.idle_timeout;
    if (r >= t && w >= t) {
      m_socket->m_peers.erase(m_endpoint);
      m_socket = nullptr;
      on_peer_input(StreamEnd::make(StreamEnd::IDLE_TIMEOUT));
      on_peer_close();
      return;
    }
  }

  if (options.read_timeout > 0) {
    if (r >= options.read_timeout) {
      m_socket->m_peers.erase(m_endpoint);
      m_socket = nullptr;
      on_peer_input(StreamEnd::make(StreamEnd::READ_TIMEOUT));
      on_peer_close();
      return;
    }
  }

  if (options.write_timeout > 0) {
    if (r >= options.write_timeout) {
      m_socket->m_peers.erase(m_endpoint);
      m_socket = nullptr;
      on_peer_input(StreamEnd::make(StreamEnd::WRITE_TIMEOUT));
      on_peer_close();
      return;
    }
  }
}

} // namespace pipy
