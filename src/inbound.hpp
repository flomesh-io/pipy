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
#include "socket.hpp"
#include "event.hpp"
#include "input.hpp"
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
  public List<Inbound>::Item,
  public EventTarget
{
public:
  struct Options : public SocketTCP::Options {
    bool transparent = false;
    bool masquerade = false;
    bool peer_stats = false;
  };

  static auto count() -> int;
  static void for_each(const std::function<bool(Inbound*)> &cb);

  auto id() const -> uint64_t { return m_id; }
  auto pipeline() const -> Pipeline* { return m_pipeline; }
  auto local_address() -> pjs::Str*;
  auto local_port() -> int { address(); return m_local_port; }
  auto remote_address() -> pjs::Str*;
  auto remote_port() -> int { address(); return m_remote_port; }
  auto ori_dst_address() -> pjs::Str*;
  auto ori_dst_port() -> int { address(); return m_ori_dst_port; }
  bool is_receiving() const { return m_receiving_state == RECEIVING; }

  virtual auto get_buffered() const -> size_t = 0;
  virtual auto get_traffic_in() ->size_t = 0;
  virtual auto get_traffic_out() ->size_t = 0;

  void dangle() { m_listener = nullptr; }

protected:
  Inbound(Listener *listener, const Options &options);
  ~Inbound();

  enum ReceivingState {
    RECEIVING,
    PAUSING,
    PAUSED,
  };

  Listener* m_listener;
  Options m_options;
  std::string m_local_addr;
  std::string m_remote_addr;
  std::string m_ori_dst_addr;
  int m_local_port = 0;
  int m_remote_port = 0;
  int m_ori_dst_port = 0;
  ReceivingState m_receiving_state = RECEIVING;
  pjs::Ref<EventTarget::Input> m_input;

  void start();
  void collect();
  void address();

protected:
  thread_local static pjs::Ref<stats::Gauge> s_metric_concurrency;
  thread_local static pjs::Ref<stats::Counter> s_metric_traffic_in;
  thread_local static pjs::Ref<stats::Counter> s_metric_traffic_out;

  pjs::Ref<stats::Counter> m_metric_traffic_in;
  pjs::Ref<stats::Counter> m_metric_traffic_out;

private:
  virtual void on_get_address() = 0;

  uint64_t m_id;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<pjs::Str> m_str_local_addr;
  pjs::Ref<pjs::Str> m_str_remote_addr;
  pjs::Ref<pjs::Str> m_str_ori_dst_addr;
  bool m_addressed = false;

  static std::atomic<uint64_t> s_inbound_id;

  static void init_metrics();

  friend class pjs::ObjectTemplate<Inbound>;
};

//
// InboundTCP
//

class InboundTCP :
  public pjs::ObjectTemplate<InboundTCP, Inbound>,
  public SocketTCP
{
public:
  void accept(asio::ip::tcp::acceptor &acceptor);
  void cancel() { m_canceled = true; }

private:
  InboundTCP(Listener *listener, const Inbound::Options &options);
  ~InboundTCP();

  asio::ip::tcp::endpoint m_peer;
  bool m_canceled = false;

  virtual auto get_buffered() const -> size_t override { return SocketTCP::buffered(); }
  virtual auto get_traffic_in() -> size_t override;
  virtual auto get_traffic_out() -> size_t override;
  virtual void on_get_address() override;
  virtual void on_event(Event *evt) override { SocketTCP::output(evt); }
  virtual void on_socket_input(Event *evt) override { m_input->input(evt); }
  virtual void on_socket_close() override { release(); }
  virtual void on_socket_describe(char *buf, size_t len) override { describe(buf, len); }

  void start();
  void receive();
  void linger();
  void pump();
  void output(Event *evt);
  void close(StreamEnd::Error err);
  void describe(char *desc, size_t len);
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
  public SocketUDP::Peer
{
  InboundUDP(Listener* listener, const Options &options);
  ~InboundUDP();

  virtual auto get_buffered() const -> size_t override;
  virtual auto get_traffic_in() -> size_t override;
  virtual auto get_traffic_out() -> size_t override;
  virtual void on_get_address() override;
  virtual void on_event(Event *evt) override;
  virtual void on_peer_open() override;
  virtual void on_peer_input(Event *evt) override;
  virtual void on_peer_close() override;

  friend class pjs::ObjectTemplate<InboundUDP, Inbound>;
};

//
// InboundWrapper
//

class InboundWrapper : public pjs::ObjectTemplate<InboundWrapper> {
public:
  auto get() const -> Inbound* { return m_weak_ref; }

private:
  InboundWrapper(Inbound *inbound)
    : m_weak_ref(inbound) {}

  pjs::WeakRef<Inbound> m_weak_ref;

  friend class pjs::ObjectTemplate<InboundWrapper>;
};

} // namespace pipy

#endif // INBOUND_HPP
