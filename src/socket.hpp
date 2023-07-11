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

#ifndef SOCKET_HPP
#define SOCKET_HPP

#include "net.hpp"
#include "input.hpp"
#include "data.hpp"
#include "timer.hpp"

namespace pipy {

//
// SocketBase
//

class SocketBase {
public:
  struct Options {
    size_t congestion_limit = 1024*1024;
    size_t buffer_limit = 0;
    double read_timeout = 0;
    double write_timeout = 0;
    double idle_timeout = 60;
    bool keep_alive = true;
    bool no_delay = true;
  };

protected:
  SocketBase(bool is_inbound, const Options &options)
    : m_is_inbound(is_inbound)
    , m_options(options) {}

  void log_debug(const char *msg);
  void log_warn(const char *msg, const std::error_code &ec);
  void log_error(const char *msg, const std::error_code &ec);
  void log_error(const char *msg);

  bool m_is_inbound;
  const Options& m_options;
  size_t m_traffic_read = 0;
  size_t m_traffic_write = 0;

  virtual void on_socket_input(Event *evt) = 0;
  virtual void on_socket_close() = 0;
  virtual void on_socket_describe(char *buf, size_t len) = 0;
};

//
// SocketTCP
//

class SocketTCP :
  public SocketBase,
  public InputSource,
  public FlushTarget,
  public Ticker::Watcher
{
protected:
  SocketTCP(bool is_inbound, const Options &options)
    : SocketBase(is_inbound, options)
    , FlushTarget(true)
    , m_socket(Net::context()) {}

  ~SocketTCP();

  auto socket() -> asio::ip::tcp::socket& { return m_socket; }
  auto buffered() const -> size_t { return m_buffer_send.size(); }

  void open();
  void output(Event *evt);
  void close();

private:
  enum State {
    IDLE,
    OPEN,
    HALF_CLOSED_REMOTE,
    HALF_CLOSED_LOCAL,
    CLOSED,
  };

  asio::ip::tcp::socket m_socket;
  Data m_buffer_receive;
  Data m_buffer_send;
  pjs::Ref<StreamEnd> m_eos;
  Congestion m_congestion;
  double m_tick_read;
  double m_tick_write;
  State m_state = IDLE;
  bool m_receiving = false;
  bool m_sending = false;
  bool m_paused = false;
  bool m_closed = false;

  void receive();
  void send();
  void shutdown_socket();
  void close_socket();
  void close_async();

  virtual void on_tap_open() override;
  virtual void on_tap_close() override;
  virtual void on_flush() override;
  virtual void on_tick(double tick) override;

  void on_receive(const std::error_code &ec, std::size_t n);
  void on_send(const std::error_code &ec, std::size_t n);

  struct ReceiveHandler : public SelfHandler<SocketTCP> {
    using SelfHandler::SelfHandler;
    ReceiveHandler(const ReceiveHandler &r) : SelfHandler(r) {}
    void operator()(const std::error_code &ec, std::size_t n) { self->on_receive(ec, n); }
  };

  struct SendHandler : public SelfHandler<SocketTCP> {
    using SelfHandler::SelfHandler;
    SendHandler(const SendHandler &r) : SelfHandler(r) {}
    void operator()(const std::error_code &ec, std::size_t n) { self->on_send(ec, n); }
  };

  thread_local static Data::Producer s_dp;
};

//
// SocketUDP
//

class SocketUDP :
  public SocketBase,
  public InputSource,
  public Ticker::Watcher
{
public:

  //
  // SocketUDP::Peer
  //

  class Peer {
  public:
    Peer() {}
    ~Peer() { if (auto s = m_socket) s->m_peers.erase(m_endpoint); }

  protected:
    void output(Event *evt) { if (auto s = m_socket) s->output(evt, this); }
    auto local() const -> const asio::ip::udp::endpoint& { return m_socket->m_endpoint; }
    auto peer() const -> const asio::ip::udp::endpoint& { return m_endpoint; }

  private:
    void tick(double t);

    SocketUDP* m_socket = nullptr;
    asio::ip::udp::endpoint m_endpoint;
    double m_tick_read;
    double m_tick_write;

    virtual void on_peer_input(Event *evt) = 0;
    virtual void on_peer_close() = 0;

    friend class SocketUDP;
  };

protected:
  SocketUDP(bool is_inbound, const Options &options)
    : SocketBase(is_inbound, options)
    , m_socket(Net::context()) {}

  ~SocketUDP();

  auto socket() -> asio::ip::udp::socket& { return m_socket; }
  auto buffered() const -> size_t { return m_sending_size; }

  void open();
  void close();
  void output(Event *evt);

private:
  virtual auto on_socket_new_peer() -> Peer* = 0;

  asio::ip::udp::socket m_socket;
  asio::ip::udp::endpoint m_endpoint;
  asio::ip::udp::endpoint m_from;
  std::map<asio::ip::udp::endpoint, Peer*> m_peers;
  EventBuffer m_buffer;
  Congestion m_congestion;
  int m_sending_size = 0;
  bool m_receiving = false;
  bool m_open = false;
  bool m_paused = false;
  bool m_closing = false;
  bool m_closed = false;

  void output(Event *evt, Peer *peer);
  void receive();
  void send(Data *data);
  void send(Data *data, const asio::ip::udp::endpoint &endpoint);
  void close_peers(StreamEnd::Error err = StreamEnd::Error::NO_ERROR);
  void close_socket();
  void close_async();

  virtual void on_tap_open() override;
  virtual void on_tap_close() override;
  virtual void on_tick(double tick) override;

  void on_receive(Data *data, const std::error_code &ec, std::size_t n);
  void on_send(Data *data, const std::error_code &ec, std::size_t n);

  struct ReceiveHandler : public SelfDataHandler<SocketUDP, Data> {
    using SelfDataHandler::SelfDataHandler;
    ReceiveHandler(const ReceiveHandler &r) : SelfDataHandler(r) {}
    void operator()(const std::error_code &ec, std::size_t n) { self->on_receive(data, ec, n); }
  };

  struct SendHandler : public SelfDataHandler<SocketUDP, Data> {
    using SelfDataHandler::SelfDataHandler;
    SendHandler(const SendHandler &r) : SelfDataHandler(r) {}
    void operator()(const std::error_code &ec, std::size_t n) { self->on_send(data, ec, n); }
  };

  thread_local static Data::Producer s_dp;
};

} // namespace pipy

#endif // SOCKET_HPP
