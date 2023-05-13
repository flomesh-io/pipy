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

#ifndef OUTBOUND_HPP
#define OUTBOUND_HPP

#include "net.hpp"
#include "socket.hpp"
#include "event.hpp"
#include "input.hpp"
#include "timer.hpp"
#include "list.hpp"
#include "api/stats.hpp"

#include <functional>

namespace pipy {

class Data;

//
// Outbound
//

class Outbound :
  public pjs::ObjectTemplate<Outbound>,
  public List<Outbound>::Item
{
public:
  enum class Protocol {
    TCP,
    UDP,
  };

  enum class State {
    idle,
    resolving,
    connecting,
    connected,
    closed,
  };

  struct Options : public SocketTCP::Options {
    Protocol  protocol = Protocol::TCP;
    size_t    max_packet_size = 16 * 1024;
    int       retry_count = 0;
    double    retry_delay = 0;
    double    connect_timeout = 0;

    std::function<void(Outbound*)> on_state_changed;
  };

  static void for_each(const std::function<void(Outbound*)> &cb) {
    for (auto p = s_all_outbounds.head(); p; p = p->List<Outbound>::Item::next()) {
      cb(p);
    }
  }

  auto protocol() const -> Protocol { return m_options.protocol; }
  auto protocol_name() const -> pjs::Str*;
  auto address() -> pjs::Str*;
  auto host() const -> const std::string& { return m_host; }
  auto port() const -> int { return m_port; }
  auto state() const -> State { return m_state; }
  auto local_address() -> pjs::Str*;
  auto local_port() -> int { address(); return m_local_port; }
  auto remote_address() -> pjs::Str*;
  auto remote_port() -> int { address(); return m_port; }
  auto retries() const -> int { return m_retries; }
  auto connection_time() const -> double { return m_connection_time; }

  virtual void bind(const std::string &ip, int port) = 0;
  virtual void connect(const std::string &host, int port) = 0;
  virtual void send(Event *evt) = 0;
  virtual void close() = 0;

  virtual auto get_buffered() const -> size_t = 0;
  virtual auto get_traffic_in() ->size_t = 0;
  virtual auto get_traffic_out() ->size_t = 0;

protected:
  Outbound(EventTarget::Input *input, const Options &options);
  ~Outbound();

  Options m_options;
  std::string m_host;
  std::string m_remote_addr;
  std::string m_local_addr;
  pjs::Ref<pjs::Str> m_address;
  pjs::Ref<pjs::Str> m_local_addr_str;
  pjs::Ref<pjs::Str> m_remote_addr_str;
  pjs::Ref<EventTarget::Input> m_input;
  State m_state = State::idle;
  StreamEnd::Error m_error = StreamEnd::Error::NO_ERROR;
  int m_port = 0;
  int m_local_port = 0;
  int m_retries = 0;
  double m_start_time = 0;
  double m_connection_time = 0;
  bool m_connecting = false;

  auto options() const -> const Options& { return m_options; }

  void state(State state);
  void input(Event *evt);
  void error(StreamEnd::Error err);
  void describe(char *buf, size_t len);

  thread_local static pjs::Ref<stats::Gauge> s_metric_concurrency;
  thread_local static pjs::Ref<stats::Counter> s_metric_traffic_in;
  thread_local static pjs::Ref<stats::Counter> s_metric_traffic_out;
  thread_local static pjs::Ref<stats::Histogram> s_metric_conn_time;

  pjs::Ref<stats::Counter> m_metric_traffic_out;
  pjs::Ref<stats::Counter> m_metric_traffic_in;

private:
  thread_local static List<Outbound> s_all_outbounds;

  static void init_metrics();

  friend class pjs::ObjectTemplate<Outbound>;
};

//
// OutboundTCP
//

class OutboundTCP :
  public pjs::ObjectTemplate<OutboundTCP, Outbound>,
  public SocketTCP
{
public:
  auto buffered() const -> size_t { return SocketTCP::buffered(); }

  virtual void bind(const std::string &ip, int port) override;
  virtual void connect(const std::string &host, int port) override;
  virtual void send(Event *evt) override;
  virtual void close() override;

private:
  OutboundTCP(EventTarget::Input *output, const Outbound::Options &options);

  pjs::Ref<stats::Histogram> m_metric_conn_time;
  asio::ip::tcp::resolver m_resolver;
  Timer m_connect_timer;
  Timer m_retry_timer;
  Data m_buffer_receive;
  Data m_buffer_send;

  void start(double delay);
  void resolve();
  void connect(const asio::ip::tcp::endpoint &target);
  void connect_error(StreamEnd::Error err);

  virtual auto get_buffered() const -> size_t override { return SocketTCP::buffered(); }
  virtual auto get_traffic_in() ->size_t override;
  virtual auto get_traffic_out() ->size_t override;

  virtual void on_socket_start() override { retain(); }
  virtual void on_socket_input(Event *evt) override { Outbound::input(evt); }
  virtual void on_socket_overflow(size_t size) override {}
  virtual void on_socket_describe(char *buf, size_t len) override { describe(buf, len); }
  virtual void on_socket_stop() override { release(); }

  friend class pjs::ObjectTemplate<OutboundTCP, Outbound>;
};

//
// OutboundUDP
//

class OutboundUDP : public pjs::ObjectTemplate<OutboundUDP, Outbound> {
public:
  virtual void bind(const std::string &ip, int port) override;
  virtual void connect(const std::string &host, int port) override;
  virtual void send(Event *evt) override;
  virtual void close() override;

private:
  OutboundUDP(EventTarget::Input *output, const Options &options);

  pjs::Ref<stats::Histogram> m_metric_conn_time;
  asio::ip::udp::resolver m_resolver;
  asio::ip::udp::socket m_socket;
  Timer m_connect_timer;
  Timer m_retry_timer;
  Timer m_idle_timer;
  Data m_buffer;
  EventBuffer m_pending_buffer;
  bool m_message_started = false;
  bool m_connecting = false;
  bool m_connected = false;
  bool m_ended = false;

  void start(double delay);
  void resolve();
  void connect(const asio::ip::udp::endpoint &target);
  void restart(StreamEnd::Error err);
  void receive();
  void pump();
  void wait();
  void close(StreamEnd::Error err);

  virtual auto get_buffered() const -> size_t override;
  virtual auto get_traffic_in() -> size_t override;
  virtual auto get_traffic_out() -> size_t override;

  friend class pjs::ObjectTemplate<OutboundUDP, Outbound>;
};

} // namespace pipy

#endif // OUTBOUND_HPP
