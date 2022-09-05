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
#include "pipeline.hpp"
#include "utils.hpp"
#include "status.hpp"
#include "log.hpp"

namespace pipy {

using tcp = asio::ip::tcp;
using udp = asio::ip::udp;

List<Outbound> Outbound::s_all_outbounds;

//
// Outbound
//

Outbound::Outbound(EventTarget::Input *output, const Options &options)
  : m_options(options)
  , m_output(output)
{
  Log::debug("[outbound %p] ++", this);
  s_all_outbounds.push(this);
}

Outbound::~Outbound() {
  Log::debug("[outbound %p] --", this);
  s_all_outbounds.remove(this);
}

auto Outbound::protocol_name() const -> pjs::Str* {
  static pjs::ConstStr s_TCP("TCP");
  static pjs::ConstStr s_UDP("UDP");
  switch (m_options.protocol) {
    case Protocol::TCP: return s_TCP;
    case Protocol::UDP: return s_UDP;
  }
  return nullptr;
}

auto Outbound::address() -> pjs::Str* {
  if (!m_address) {
    std::string s("[");
    s += m_host;
    s += "]:";
    s += std::to_string(m_port);
    m_address = pjs::Str::make(std::move(s));
  }
  return m_address;
}

void Outbound::output(Event *evt) {
  m_output->input(evt);
}

void Outbound::describe(char *desc) {
  sprintf(
    desc, "[outbound %p] [%s]:%d -> [%s]:%d (%s)",
    this,
    m_local_addr.c_str(),
    m_local_port,
    m_remote_addr.c_str(),
    m_port,
    m_host.c_str()
  );
}

//
// OutboundTCP
//

OutboundTCP::OutboundTCP(EventTarget::Input *output, const Options &options)
  : Outbound(output, options)
  , FlushTarget(true)
  , m_resolver(Net::context())
  , m_socket(Net::context())
{
}

void OutboundTCP::connect(const std::string &host, int port) {
  m_host = host;
  m_port = port;
  m_connecting = true;

  pjs::Str *keys[2];
  keys[0] = protocol_name();
  keys[1] = address();
  m_metric_traffic_out = Status::metric_outbound_out->with_labels(keys, 2);
  m_metric_traffic_in = Status::metric_outbound_in->with_labels(keys, 2);
  m_metric_conn_time = Status::metric_outbound_conn_time->with_labels(keys, 2);

  start(0);
}

void OutboundTCP::send(Event *evt) {
  if (auto *data = evt->as<Data>()) {
    if (!m_ended) {
      if (data->size() > 0) {
        if (!m_overflowed) {
          if (m_options.buffer_limit > 0 && m_buffer.size() >= m_options.buffer_limit) {
            Log::error(
              "OutboundTCP: %p to host = %s port = %d buffer overflow, size = %d",
              this, m_host.c_str(), m_port, m_buffer.size());
            m_overflowed = true;
          }
        }
        if (!m_overflowed) {
          m_buffer.push(*data);
          need_flush();
        } else {
          m_discarded_data_size += data->size();
        }
      }
    }
  } else if (evt->is<StreamEnd>()) {
    if (!m_ended) {
      m_ended = true;
      pump();
    }
  }
}

void OutboundTCP::reset() {
  asio::error_code ec;

  if (m_connecting) {
    m_connecting = false;
    m_connect_timer.cancel();
    m_resolver.cancel();
    m_socket.cancel(ec);
  } else if (m_connected) {
    m_read_timer.cancel();
    m_write_timer.cancel();
  }

  m_discarded_data_size = 0;
  m_overflowed = false;
  m_ended = false;
  m_retries = 0;
  m_connected = false;
  m_buffer.clear();
  m_socket.shutdown(tcp::socket::shutdown_both, ec);
  m_socket.close(ec);
}

void OutboundTCP::on_flush() {
  if (!m_ended) {
    pump();
  }
}

void OutboundTCP::on_tap_open()
{
}

void OutboundTCP::on_tap_close()
{
}

void OutboundTCP::start(double delay) {
  if (delay > 0) {
    m_retry_timer.schedule(
      delay,
      [this]() {
        resolve();
      }
    );
  } else {
    resolve();
  }
}

void OutboundTCP::resolve() {
  auto host = m_host;
  if (host == "localhost") host = "127.0.0.1";

  m_resolver.async_resolve(
    tcp::resolver::query(host, std::to_string(m_port)),
    [this](
      const std::error_code &ec,
      tcp::resolver::results_type results
    ) {
      if (ec != asio::error::operation_aborted) {
        if (ec) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[200];
            describe(desc);
            Log::error("%s cannot resolve hostname: %s", desc, ec.message().c_str());
          }
          restart(StreamEnd::CANNOT_RESOLVE);

        } else {
          auto &result = *results;
          const auto &target = result.endpoint();
          m_remote_addr = target.address().to_string();
          connect(target);
        }
      }

      release();
    }
  );

  if (Log::is_enabled(Log::DEBUG)) {
    char desc[200];
    describe(desc);
    Log::debug("%s resolving hostname...", desc);
  }

  if (m_options.connect_timeout > 0) {
    m_connect_timer.schedule(
      m_options.connect_timeout,
      [this]() {
        m_resolver.cancel();
        m_socket.cancel();
        restart(StreamEnd::CONNECTION_TIMEOUT);
      }
    );
  }

  m_start_time = utils::now();

  if (m_retries > 0) {
    if (Log::is_enabled(Log::WARN)) {
      char desc[200];
      describe(desc);
      Log::warn("%s retry connecting... (retries = %d)", desc, m_retries);
    }
  }

  retain();
}

void OutboundTCP::connect(const asio::ip::tcp::endpoint &target) {
  m_socket.async_connect(
    target,
    [=](const std::error_code &ec) {
      if (m_options.connect_timeout > 0) {
        m_connect_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        if (ec) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[200];
            describe(desc);
            Log::error("%s cannot connect: %s", desc, ec.message().c_str());
          }
          restart(StreamEnd::CONNECTION_REFUSED);

        } else {
          if (Log::is_enabled(Log::DEBUG)) {
            char desc[200];
            describe(desc);
            Log::debug("%s connected", desc);
          }
          if (m_connecting) {
            const auto &ep = m_socket.local_endpoint();
            m_local_addr = ep.address().to_string();
            m_local_port = ep.port();
            auto conn_time = utils::now() - m_start_time;
            m_connection_time += conn_time;
            m_metric_conn_time->observe(conn_time);
            Status::metric_outbound_conn_time->observe(conn_time);
            m_connected = true;
            m_connecting = false;
            receive();
            pump();
          } else {
            close(StreamEnd::CONNECTION_CANCELED);
          }
        }
      }

      release();
    }
  );

  if (Log::is_enabled(Log::DEBUG)) {
    char desc[200];
    describe(desc);
    Log::debug("%s connecting...", desc);
  }

  retain();
}

void OutboundTCP::restart(StreamEnd::Error err) {
  if (m_options.retry_count >= 0 && m_retries >= m_options.retry_count) {
    m_connecting = false;
    InputContext ic(this);
    output(StreamEnd::make(err));
  } else {
    m_retries++;
    std::error_code ec;
    m_socket.shutdown(tcp::socket::shutdown_both, ec);
    m_socket.close(ec);
    start(m_options.retry_delay);
  }
}

void OutboundTCP::receive() {
  if (!m_socket.is_open()) return;

  static Data::Producer s_data_producer("OutboundTCP");
  pjs::Ref<Data> buffer(Data::make(RECEIVE_BUFFER_SIZE, &s_data_producer));

  m_socket.async_read_some(
    DataChunks(buffer->chunks()),
    [=](const std::error_code &ec, size_t n) {
      InputContext ic(this);

      if (m_options.read_timeout > 0){
        m_read_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        if (n > 0) {
          if (m_socket.is_open()) {
            buffer->pop(buffer->size() - n);
            if (auto more = m_socket.available()) {
              Data buf(more, &s_data_producer);
              auto n = m_socket.read_some(DataChunks(buf.chunks()));
              if (n < more) buf.pop(more - n);
              buffer->push(buf);
            }
          }
          m_metric_traffic_in->increase(buffer->size());
          Status::metric_outbound_in->increase(buffer->size());
          output(buffer);
        }

        if (ec) {
          if (ec == asio::error::eof) {
            if (Log::is_enabled(Log::DEBUG)) {
              char desc[200];
              describe(desc);
              Log::debug("%s connection closed by peer", desc);
            }
            close(StreamEnd::NO_ERROR);
          } else if (ec == asio::error::connection_reset) {
            if (Log::is_enabled(Log::WARN)) {
              char desc[200];
              describe(desc);
              Log::warn("%s connection reset by peer", desc);
            }
            close(StreamEnd::CONNECTION_RESET);
          } else {
            if (Log::is_enabled(Log::WARN)) {
              char desc[200];
              describe(desc);
              Log::warn("%s error reading from peer: %s", desc, ec.message().c_str());
            }
            close(StreamEnd::READ_ERROR);
          }

        } else {
          receive();
          wait();
        }
      }

      release();
    }
  );

  if (m_options.read_timeout > 0) {
    m_read_timer.schedule(
      m_options.read_timeout,
      [this]() {
        close(StreamEnd::READ_TIMEOUT);
      }
    );
  }

  retain();
}

void OutboundTCP::pump() {
  if (!m_socket.is_open()) return;
  if (m_pumping || !m_connected) return;

  if (m_buffer.empty()) {
    if (m_ended) {
      std::error_code ec;
      m_socket.shutdown(tcp::socket::shutdown_send, ec);
    }
    return;
  }

  m_socket.async_write_some(
    DataChunks(m_buffer.chunks()),
    [=](const std::error_code &ec, std::size_t n) {
      m_pumping = false;

      if (m_options.write_timeout > 0) {
        m_write_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        m_buffer.shift(n);
        m_metric_traffic_out->increase(n);
        Status::metric_outbound_out->increase(n);

        if (ec) {
          if (Log::is_enabled(Log::WARN)) {
            char desc[200];
            describe(desc);
            Log::warn("%s error writing to peer: %s", desc, ec.message().c_str());
          }
          close(StreamEnd::WRITE_ERROR);

        } else if (m_overflowed && m_buffer.empty()) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[200];
            describe(desc);
            Log::error("%s overflowed by %d bytes", desc, m_discarded_data_size);
          }
          close(StreamEnd::BUFFER_OVERFLOW);

        } else {
          pump();
        }
      }

      release();
    }
  );

  if (m_options.write_timeout > 0) {
    m_write_timer.schedule(
      m_options.write_timeout,
      [this]() {
        close(StreamEnd::WRITE_TIMEOUT);
      }
    );
  }

  wait();
  retain();

  m_pumping = true;
}

void OutboundTCP::wait() {
  if (!m_socket.is_open()) return;
  if (m_options.idle_timeout > 0) {
    m_idle_timer.cancel();
    m_idle_timer.schedule(
      m_options.idle_timeout,
      [this]() {
        close(StreamEnd::IDLE_TIMEOUT);
      }
    );
  }
}

void OutboundTCP::close(StreamEnd::Error err) {
  if (!m_connected) return;

  m_buffer.clear();
  m_discarded_data_size = 0;
  m_overflowed = false;
  m_ended = false;
  m_retries = 0;
  m_connected = false;

  if (m_socket.is_open()) {
    std::error_code ec;
    m_socket.shutdown(tcp::socket::shutdown_both, ec);
    m_socket.close(ec);

    if (ec) {
      if (Log::is_enabled(Log::ERROR)) {
        char desc[200];
        describe(desc);
        Log::error("%s error closing socket: %s", desc, ec.message().c_str());
      }
    } else {
      if (Log::is_enabled(Log::DEBUG)) {
        char desc[200];
        describe(desc);
        Log::debug("%s connection closed to peer", desc);
      }
    }
  }

  InputContext ic(this);
  output(StreamEnd::make(err));
}

//
// OutboundUDP
//

OutboundUDP::OutboundUDP(EventTarget::Input *output, const Options &options)
  : Outbound(output, options)
  , m_resolver(Net::context())
  , m_socket(Net::context())
{
}

void OutboundUDP::connect(const std::string &host, int port) {
  m_host = host;
  m_port = port;
  m_connecting = true;

  pjs::Str *keys[2];
  keys[0] = protocol_name();
  keys[1] = address();
  m_metric_traffic_out = Status::metric_outbound_out->with_labels(keys, 2);
  m_metric_traffic_in = Status::metric_outbound_in->with_labels(keys, 2);
  m_metric_conn_time = Status::metric_outbound_conn_time->with_labels(keys, 2);

  start(0);
}

void OutboundUDP::send(Event *evt) {
  if (evt->is<MessageStart>()) {
    if (!m_ended) {
      m_message_started = true;
      m_buffer.clear();
    }

  } else if (auto *data = evt->as<Data>()) {
    if (m_message_started) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_message_started) {
      m_pending_buffer.push(Data::make(std::move(m_buffer)));
      m_message_started = false;
      pump();
    }

  } else if (evt->is<StreamEnd>()) {
    if (!m_ended) {
      m_ended = true;
      m_message_started = false;
      pump();
    }
  }
}

void OutboundUDP::reset() {
  asio::error_code ec;

  if (m_connecting) {
    m_connecting = false;
    m_connect_timer.cancel();
    m_retry_timer.cancel();
    m_resolver.cancel();
    m_socket.cancel(ec);
  } else if (m_connected) {
    m_idle_timer.cancel();
  }

  m_message_started = false;
  m_ended = false;
  m_retries = 0;
  m_connected = false;
  m_buffer.clear();
  m_pending_buffer.clear();
  m_socket.shutdown(udp::socket::shutdown_both, ec);
  m_socket.close(ec);
}

void OutboundUDP::on_tap_open()
{
}

void OutboundUDP::on_tap_close()
{
}

void OutboundUDP::start(double delay) {
  if (delay > 0) {
    m_retry_timer.schedule(
      delay,
      [this]() {
        resolve();
      }
    );
  } else {
    resolve();
  }
}

void OutboundUDP::resolve() {
  auto host = m_host;
  if (host == "localhost") host = "127.0.0.1";

  m_resolver.async_resolve(
    udp::resolver::query(host, std::to_string(m_port)),
    [this](
      const std::error_code &ec,
      udp::resolver::results_type results
    ) {
      if (ec != asio::error::operation_aborted) {
        if (ec) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[200];
            describe(desc);
            Log::error("%s cannot resolve hostname: %s", desc, ec.message().c_str());
          }
          restart(StreamEnd::CANNOT_RESOLVE);

        } else {
          auto &result = *results;
          const auto &target = result.endpoint();
          m_remote_addr = target.address().to_string();
          connect(target);
        }
      }

      release();
    }
  );

  if (Log::is_enabled(Log::DEBUG)) {
    char desc[200];
    describe(desc);
    Log::debug("%s resolving hostname...", desc);
  }

  if (m_options.connect_timeout > 0) {
    m_connect_timer.schedule(
      m_options.connect_timeout,
      [this]() {
        m_resolver.cancel();
        m_socket.cancel();
        restart(StreamEnd::CONNECTION_TIMEOUT);
      }
    );
  }

  m_start_time = utils::now();

  if (m_retries > 0) {
    if (Log::is_enabled(Log::WARN)) {
      char desc[200];
      describe(desc);
      Log::warn("%s retry connecting... (retries = %d)", desc, m_retries);
    }
  }

  retain();
}

void OutboundUDP::connect(const asio::ip::udp::endpoint &target) {
  m_socket.async_connect(
    target,
    [=](const std::error_code &ec) {
      if (m_options.connect_timeout > 0) {
        m_connect_timer.cancel();
      }

      if (ec != asio::error::operation_aborted) {
        if (ec) {
          if (Log::is_enabled(Log::ERROR)) {
            char desc[200];
            describe(desc);
            Log::error("%s cannot connect: %s", desc, ec.message().c_str());
          }
          restart(StreamEnd::CONNECTION_REFUSED);

        } else {
          if (Log::is_enabled(Log::DEBUG)) {
            char desc[200];
            describe(desc);
            Log::debug("%s connected", desc);
          }
          if (m_connecting) {
            const auto &ep = m_socket.local_endpoint();
            m_local_addr = ep.address().to_string();
            m_local_port = ep.port();
            auto conn_time = utils::now() - m_start_time;
            m_connection_time += conn_time;
            m_metric_conn_time->observe(conn_time);
            Status::metric_outbound_conn_time->observe(conn_time);
            m_connected = true;
            m_connecting = false;
            receive();
            pump();
          } else {
            close(StreamEnd::CONNECTION_CANCELED);
          }
        }
      }

      release();
    }
  );

  if (Log::is_enabled(Log::DEBUG)) {
    char desc[200];
    describe(desc);
    Log::debug("%s connecting...", desc);
  }

  retain();
}

void OutboundUDP::restart(StreamEnd::Error err) {
  if (m_options.retry_count >= 0 && m_retries >= m_options.retry_count) {
    m_connecting = false;
    InputContext ic(this);
    output(StreamEnd::make(err));
  } else {
    m_retries++;
    std::error_code ec;
    m_socket.shutdown(udp::socket::shutdown_both, ec);
    m_socket.close(ec);
    start(m_options.retry_delay);
  }
}

void OutboundUDP::receive() {
  if (!m_socket.is_open()) return;

  static Data::Producer s_data_producer("OutboundUDP");
  pjs::Ref<Data> buffer(Data::make(m_options.max_packet_size, &s_data_producer));

  m_socket.async_receive(
    DataChunks(buffer->chunks()),
    [=](const std::error_code &ec, size_t n) {
      InputContext ic(this);

      if (ec != asio::error::operation_aborted) {
        if (n > 0) {
          if (m_socket.is_open()) {
            buffer->pop(buffer->size() - n);
          }
          m_metric_traffic_in->increase(buffer->size());
          Status::metric_outbound_in->increase(buffer->size());
          output(MessageStart::make());
          output(buffer);
          output(MessageEnd::make());
        }

        if (ec) {
          if (ec == asio::error::eof) {
            if (Log::is_enabled(Log::DEBUG)) {
              char desc[200];
              describe(desc);
              Log::debug("%s connection closed by peer", desc);
            }
            close(StreamEnd::NO_ERROR);
          } else if (ec == asio::error::connection_reset) {
            if (Log::is_enabled(Log::WARN)) {
              char desc[200];
              describe(desc);
              Log::warn("%s connection reset by peer", desc);
            }
            close(StreamEnd::CONNECTION_RESET);
          } else {
            if (Log::is_enabled(Log::WARN)) {
              char desc[200];
              describe(desc);
              Log::warn("%s error reading from peer: %s", desc, ec.message().c_str());
            }
            close(StreamEnd::READ_ERROR);
          }

        } else {
          receive();
          wait();
        }
      }

      release();
    }
  );

  retain();
}

void OutboundUDP::pump() {
  if (!m_socket.is_open()) return;
  if (!m_connected) return;

  while (!m_pending_buffer.empty()) {
    auto evt = m_pending_buffer.shift();

    if (auto data = evt->as<Data>()) {
      m_socket.async_send(
        DataChunks(data->chunks()),
        [=](const std::error_code &ec, std::size_t n) {
          if (ec != asio::error::operation_aborted) {
            m_metric_traffic_out->increase(n);
            Status::metric_outbound_out->increase(n);
            if (ec) {
              if (Log::is_enabled(Log::WARN)) {
                char desc[200];
                describe(desc);
                Log::warn("%s error writing to peer: %s", desc, ec.message().c_str());
              }
              close(StreamEnd::WRITE_ERROR);
            }
          }

          release();
        }
      );

      retain();
    }

    evt->release();
  }

  wait();
}

void OutboundUDP::wait() {
  if (!m_socket.is_open()) return;
  if (m_options.idle_timeout > 0) {
    m_idle_timer.cancel();
    m_idle_timer.schedule(
      m_options.idle_timeout,
      [this]() {
        close(StreamEnd::IDLE_TIMEOUT);
      }
    );
  }
}

void OutboundUDP::close(StreamEnd::Error err) {
  if (!m_connected) return;

  m_buffer.clear();
  m_pending_buffer.clear();
  m_ended = false;
  m_retries = 0;
  m_connected = false;

  if (m_socket.is_open()) {
    std::error_code ec;
    m_socket.shutdown(udp::socket::shutdown_both, ec);
    m_socket.close(ec);

    if (ec) {
      if (Log::is_enabled(Log::ERROR)) {
        char desc[200];
        describe(desc);
        Log::error("%s error closing socket: %s", desc, ec.message().c_str());
      }
    } else {
      if (Log::is_enabled(Log::DEBUG)) {
        char desc[200];
        describe(desc);
        Log::debug("%s connection closed to peer", desc);
      }
    }
  }

  InputContext ic(this);
  output(StreamEnd::make(err));
}

} // namespace pipy
