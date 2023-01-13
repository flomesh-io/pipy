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
  public pjs::RefCount<Outbound>,
  public List<Outbound>::Item
{
public:
  enum class Protocol {
    TCP,
    UDP,
  };

  struct Options {
    Protocol  protocol = Protocol::TCP;
    size_t    max_packet_size = 16 * 1024;
    size_t    buffer_limit = 0;
    int       retry_count = 0;
    double    retry_delay = 0;
    double    connect_timeout = 0;
    double    read_timeout = 0;
    double    write_timeout = 0;
    double    idle_timeout = 60;
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
  auto retries() const -> int { return m_retries; }
  auto connection_time() const -> double { return m_connection_time; }

  virtual void bind(const std::string &ip, int port) = 0;
  virtual void connect(const std::string &host, int port) = 0;
  virtual void send(Event *evt) = 0;
  virtual void reset() = 0;

protected:
  Outbound(EventTarget::Input *output, const Options &options);
  ~Outbound();

  Options m_options;
  std::string m_host;
  std::string m_remote_addr;
  std::string m_local_addr;
  pjs::Ref<pjs::Str> m_address;
  pjs::Ref<EventTarget::Input> m_output;
  int m_port;
  int m_local_port;
  int m_retries = 0;
  double m_start_time = 0;
  double m_connection_time = 0;

  void output(Event *evt);
  void describe(char *desc);

  thread_local static pjs::Ref<stats::Gauge> s_metric_concurrency;
  thread_local static pjs::Ref<stats::Counter> s_metric_traffic_in;
  thread_local static pjs::Ref<stats::Counter> s_metric_traffic_out;
  thread_local static pjs::Ref<stats::Histogram> s_metric_conn_time;

private:
  virtual void finalize() = 0;

  thread_local static List<Outbound> s_all_outbounds;

  static void init_metrics();

  friend class pjs::RefCount<Outbound>;
};

//
// OutboundTCP
//

class OutboundTCP :
  public pjs::Pooled<OutboundTCP>,
  public Outbound,
  public InputSource,
  public FlushTarget
{
public:
  OutboundTCP(EventTarget::Input *output, const Options &options);

  bool overflowed() const { return m_overflowed; }
  auto buffered() const -> int { return m_buffer.size(); }

  virtual void bind(const std::string &ip, int port) override;
  virtual void connect(const std::string &host, int port) override;
  virtual void send(Event *evt) override;
  virtual void reset() override;

private:
  pjs::Ref<stats::Counter> m_metric_traffic_out;
  pjs::Ref<stats::Counter> m_metric_traffic_in;
  pjs::Ref<stats::Histogram> m_metric_conn_time;
  asio::ip::tcp::resolver m_resolver;
  asio::ip::tcp::socket m_socket;
  Timer m_connect_timer;
  Timer m_retry_timer;
  Timer m_read_timer;
  Timer m_write_timer;
  Timer m_idle_timer;
  Data m_buffer;
  size_t m_discarded_data_size = 0;
  bool m_connecting = false;
  bool m_connected = false;
  bool m_overflowed = false;
  bool m_pumping = false;
  bool m_ended = false;

  virtual void on_flush() override;
  virtual void on_tap_open() override;
  virtual void on_tap_close() override;

  void start(double delay);
  void resolve();
  void connect(const asio::ip::tcp::endpoint &target);
  void restart(StreamEnd::Error err);
  void receive();
  void pump();
  void wait();
  void close(StreamEnd::Error err);

  virtual void finalize() override { delete this; }
};

//
// OutboundUDP
//

class OutboundUDP :
  public pjs::Pooled<OutboundUDP>,
  public Outbound,
  public InputSource
{
public:
  OutboundUDP(EventTarget::Input *output, const Options &options);

  virtual void bind(const std::string &ip, int port) override;
  virtual void connect(const std::string &host, int port) override;
  virtual void send(Event *evt) override;
  virtual void reset() override;

private:
  pjs::Ref<stats::Counter> m_metric_traffic_out;
  pjs::Ref<stats::Counter> m_metric_traffic_in;
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

  virtual void on_tap_open() override;
  virtual void on_tap_close() override;

  void start(double delay);
  void resolve();
  void connect(const asio::ip::udp::endpoint &target);
  void restart(StreamEnd::Error err);
  void receive();
  void pump();
  void wait();
  void close(StreamEnd::Error err);

  virtual void finalize() override { delete this; }
};

} // namespace pipy

#endif // OUTBOUND_HPP
