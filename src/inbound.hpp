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

#ifndef INBOUND_HPP
#define INBOUND_HPP

#include "net.hpp"
#include "event.hpp"
#include "input.hpp"
#include "output.hpp"
#include "timer.hpp"
#include "list.hpp"
#include "api/stats.hpp"

#include <atomic>

namespace pipy {

class Listener;
class PipelineLayout;
class Pipeline;

//
// Inbound
//

class Inbound :
  public pjs::ObjectTemplate<Inbound>,
  public EventTarget,
  public InputSource,
  public Ticker::Watcher,
  public Output::WeakPtr::Watcher
{
public:
  struct Options {
    double read_timeout = 0;
    double write_timeout = 0;
    double idle_timeout = 60;
    bool keep_alive = true;
    bool no_delay = true;
    bool transparent = false;
    bool masquerade = false;
    bool peer_stats = false;
  };

  auto id() const -> uint64_t { return m_id; }
  auto output() -> Output*;
  auto pipeline() const -> Pipeline* { return m_pipeline; }
  auto local_address() -> pjs::Str*;
  auto local_port() -> int { address(); return m_local_port; }
  auto remote_address() -> pjs::Str*;
  auto remote_port() -> int { address(); return m_remote_port; }
  auto ori_dst_address() -> pjs::Str*;
  auto ori_dst_port() -> int { address(); return m_ori_dst_port; }
  bool is_receiving() const { return m_receiving_state == RECEIVING; }

  virtual auto size_in_buffer() const -> size_t = 0;

protected:
  Inbound();
  ~Inbound();

  enum ReceivingState {
    RECEIVING,
    PAUSING,
    PAUSED,
  };

  std::string m_local_addr;
  std::string m_remote_addr;
  std::string m_ori_dst_addr;
  int m_local_port = 0;
  int m_remote_port = 0;
  int m_ori_dst_port = 0;
  ReceivingState m_receiving_state = RECEIVING;

  void start(PipelineLayout *layout);
  void stop();
  void address();

protected:
  thread_local static pjs::Ref<stats::Gauge> s_metric_concurrency;
  thread_local static pjs::Ref<stats::Counter> s_metric_traffic_in;
  thread_local static pjs::Ref<stats::Counter> s_metric_traffic_out;

private:
  virtual void on_get_address() = 0;
  virtual void on_inbound_resume() = 0;

  uint64_t m_id;
  pjs::WeakRef<Output> m_output;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<pjs::Str> m_str_local_addr;
  pjs::Ref<pjs::Str> m_str_remote_addr;
  pjs::Ref<pjs::Str> m_str_ori_dst_addr;
  bool m_addressed = false;

  virtual void on_tap_open() override;
  virtual void on_tap_close() override;
  virtual void on_weak_ptr_gone() override;

  static std::atomic<uint64_t> s_inbound_id;

  static void init_metrics();

  friend class pjs::ObjectTemplate<Inbound>;
};

//
// InboundTCP
//

class InboundTCP :
  public pjs::ObjectTemplate<InboundTCP, Inbound>,
  public List<InboundTCP>::Item,
  public FlushTarget
{
public:
  void accept(asio::ip::tcp::acceptor &acceptor);
  void dangle() { m_listener = nullptr; }

private:
  InboundTCP(Listener *listener, const Options &options);
  ~InboundTCP();

  Listener* m_listener;
  Options m_options;
  pjs::Ref<EventTarget::Input> m_input;
  asio::ip::tcp::endpoint m_peer;
  asio::ip::tcp::socket m_socket;
  pjs::Ref<stats::Counter> m_metric_traffic_in;
  pjs::Ref<stats::Counter> m_metric_traffic_out;
  Data m_buffer_receive;
  Data m_buffer_send;
  double m_tick_read;
  double m_tick_write;
  bool m_pumping = false;
  bool m_ended = false;

  virtual auto size_in_buffer() const -> size_t override { return m_buffer_send.size(); }
  virtual void on_get_address() override;
  virtual void on_inbound_resume() override { receive(); }
  virtual void on_event(Event *evt) override;
  virtual void on_flush() override;
  virtual void on_tick(double tick) override;

  void start();
  void receive();
  void linger();
  void pump();
  void output(Event *evt);
  void close(StreamEnd::Error err);
  void describe(char *desc);
  void get_original_dest(int sock);

  struct ReceiveHandler : public SelfHandler<InboundTCP> {
    using SelfHandler::SelfHandler;
    ReceiveHandler(const ReceiveHandler &r) : SelfHandler(r) {}
    void operator()(const std::error_code &ec, std::size_t n) { self->on_receive(ec, n); }
  };

  struct SendHandler : public SelfHandler<InboundTCP> {
    using SelfHandler::SelfHandler;
    SendHandler(const SendHandler &r) : SelfHandler(r) {}
    void operator()(const std::error_code &ec, std::size_t n) { self->on_send(ec, n); }
  };

  void on_receive(const std::error_code &ec, std::size_t n);
  void on_send(const std::error_code &ec, std::size_t n);

  friend class pjs::ObjectTemplate<InboundTCP, Inbound>;
};

//
// InboundUDP
//

class InboundUDP :
  public pjs::ObjectTemplate<InboundUDP, Inbound>,
  public List<InboundUDP>::Item
{
public:
  auto local() const -> const asio::ip::udp::endpoint& { return m_local; }
  auto peer() const -> const asio::ip::udp::endpoint& { return m_peer; }
  auto destination() const -> const asio::ip::udp::endpoint& { return m_destination; }

  void start();
  void receive(Data *data);
  void dangle() { m_listener = nullptr; }
  void stop();

private:
  InboundUDP(
    Listener* listener,
    const Options &options,
    asio::ip::udp::socket &socket,
    asio::generic::raw_protocol::socket &socket_raw,
    const asio::ip::udp::endpoint &local,
    const asio::ip::udp::endpoint &peer,
    const asio::ip::udp::endpoint &destination
  );

  ~InboundUDP();

  Listener* m_listener;
  Options m_options;
  Timer m_idle_timer;
  asio::generic::raw_protocol::socket& m_socket_raw;
  asio::ip::udp::socket& m_socket;
  asio::ip::udp::endpoint m_local;
  asio::ip::udp::endpoint m_peer;
  asio::ip::udp::endpoint m_destination;
  pjs::Ref<EventTarget::Input> m_input;
  Data m_buffer;
  bool m_message_started = false;
  uint8_t m_datagram_header[20+8];
  size_t m_sending_size = 0;

  virtual auto size_in_buffer() const -> size_t override;
  virtual void on_get_address() override;
  virtual void on_inbound_resume() override {}
  virtual void on_event(Event *evt) override;
  virtual void on_tick(double tick) override;

  void wait_idle();

  friend class pjs::ObjectTemplate<InboundUDP, Inbound>;
};

} // namespace pipy

#endif // INBOUND_HPP
