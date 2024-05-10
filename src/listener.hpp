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

#ifndef LISTENER_HPP
#define LISTENER_HPP

#include "net.hpp"
#include "socket.hpp"
#include "inbound.hpp"
#include "signal.h"
#include "options.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <set>
#include <map>

namespace pipy {

class Worker;
class PipelineLayout;
class Listener;

//
// Port
//

class Port : public pjs::RefCountMT<Port> {
public:
  enum class Protocol : int {
    TCP,
    UDP,
    MAX,
  };

  static auto get(Protocol protocol, int port_num, const std::string &ip) -> Port*;

  auto protocol() const -> Protocol { return m_protocol; }
  auto num() const -> int { return m_port_num; }
  auto ip() const -> const std::string& { return m_ip; }
  auto num_connections() const -> int { return m_num_connections.load(); }
  auto max_connections() const -> int { return m_max_connections.load(); }
  bool set_max_connections(int n);

private:
  Port(Protocol protocol, int port_num, const std::string &ip)
    : m_port_num(port_num)
    , m_protocol(protocol)
    , m_ip(ip)
    , m_max_connections(-1)
    , m_num_connections(0) {}

  bool increase_num_connections();
  bool decrease_num_connections();
  void append_listener(Listener *l);
  void remove_listener(Listener *l);
  void wake_up_listeners();

  int m_port_num;
  Protocol m_protocol;
  std::string m_ip;
  std::atomic<int> m_max_connections;
  std::atomic<int> m_num_connections;
  std::set<Listener*> m_listeners;
  std::mutex m_listeners_mutex;

  static std::list<pjs::Ref<Port>> s_port_list;
  static std::mutex s_port_list_mutex;

  friend class Listener;
};

//
// Listener
//

class Listener {
public:
  struct Options : public Inbound::Options, public pipy::Options {
    Port::Protocol protocol = Port::Protocol::TCP;
    size_t max_packet_size = 16 * 1024;
    int max_connections = -1;
    int max_port_connections = -1;
    Options() {}
    Options(pjs::Object *options);
  };

  static void set_reuse_port(bool reuse);

  static auto get(Port::Protocol protocol, const std::string &ip, int port) -> Listener* {
    if (auto *l = find(protocol, ip, port)) return l;
    return new Listener(protocol, ip, port);
  }

  static bool is_open(Port::Protocol protocol, const std::string &ip, int port) {
    if (auto *l = find(protocol, ip, port)) return l->is_open();
    return false;
  }

  static bool for_each(const std::function<bool(Listener*)> &cb) {
    for (auto *l : s_listeners) {
      if (!cb(l)) return false;
    }
    return true;
  }

  static void commit_all();
  static void rollback_all();
  static void delete_all();

  auto options() const -> const Options& { return m_options; }
  auto protocol() const -> Port::Protocol { return m_port->protocol(); }
  auto ip() const -> const std::string& { return m_port->ip(); }
  auto port() const -> int { return m_port->num(); }
  auto label() const -> pjs::Str* { return m_label; }
  bool is_open() const { return m_pipeline_layout; }
  bool is_new_listen() const { return m_new_listen; }
  bool reserved() const { return m_reserved; }
  auto pipeline_layout() const -> PipelineLayout* { return m_pipeline_layout; }
  bool pipeline_layout(PipelineLayout *layout);
  auto current_connections() const -> int { return m_inbounds.size(); }
  auto peak_connections() const -> int { return m_peak_connections; }

  void set_reserved(bool b) { m_reserved = b; }
  void set_options(const Options &options);
  bool set_next_state(PipelineLayout *pipeline_layout, const Options &options);
  void commit();
  void rollback();
  bool for_each_inbound(const std::function<bool(Inbound*)> &cb);

private:
  Listener(Port::Protocol protocol, const std::string &ip, int port);
  ~Listener();

  //
  // Listener::Acceptor
  //

  class Acceptor : public pjs::RefCount<Acceptor> {
  public:
    virtual ~Acceptor() {}
    virtual void accept() = 0;
    virtual void cancel() = 0;
    virtual void stop() = 0;
  };

  //
  // Listener::AcceptorTCP
  //

  class AcceptorTCP : public Acceptor {
  public:
    AcceptorTCP(Listener *listener);
    virtual ~AcceptorTCP();

    void start(const asio::ip::tcp::endpoint &endpoint);

    virtual void accept() override;
    virtual void cancel() override;
    virtual void stop() override;

  private:
    Listener* m_listener;
    asio::ip::tcp::acceptor m_acceptor;
    pjs::Ref<InboundTCP> m_accepting;
  };

  //
  // Listener::AcceptorUDP
  //

  class AcceptorUDP : public Acceptor, public SocketUDP {
  public:
    AcceptorUDP(Listener *listener);
    virtual ~AcceptorUDP();

    void start(const asio::ip::udp::endpoint &endpoint);

    virtual void accept() override;
    virtual void cancel() override;
    virtual void stop() override;

  private:
    Listener* m_listener;
    pjs::Ref<Socket> m_socket;
    std::string m_local_addr;
    int m_local_port = 0;
    bool m_accepting = false;

    virtual void on_socket_input(Event *evt) override {}
    virtual auto on_socket_new_peer() -> Peer* override;
    virtual void on_socket_describe(char *buf, size_t len) override;
    virtual void on_socket_close() override { release(); }
  };

  bool start();
  bool start_listening();
  bool start_accepting();
  void pause();
  void resume();
  void stop();
  void open(Inbound *inbound);
  void close(Inbound *inbound);
  void wake_up();
  void print_state(const char *msg);
  void describe(char *buf, size_t len);
  void set_sock_opts(int sock);

  Net& m_net;
  Options m_options;
  Options m_options_next;
  pjs::Ref<Port> m_port;
  int m_peak_connections = 0;
  bool m_reserved = false;
  bool m_paused = false;
  bool m_new_listen = false; // TODO: Remove this
  asio::ip::address m_address;
  std::unique_ptr<Signal> m_keep_alive;
  pjs::Ref<Acceptor> m_acceptor;
  pjs::Ref<PipelineLayout> m_pipeline_layout;
  pjs::Ref<PipelineLayout> m_pipeline_layout_next;
  pjs::Ref<pjs::Str> m_label;
  List<Inbound> m_inbounds;

  thread_local static std::set<Listener*> s_listeners;
  static bool s_reuse_port;

  static auto find(Port::Protocol protocol, const std::string &ip, int port) -> Listener*;

  friend class Port;
  friend class Inbound;
  friend class pjs::RefCount<Listener>;
};

//
// ListenerArray
//

class ListenerArray : public pjs::ObjectTemplate<ListenerArray> {
public:
  auto add_listener(int port, pjs::Object *options = nullptr) -> Listener*;
  auto add_listener(pjs::Str *port, pjs::Object *options = nullptr) -> Listener*;
  auto remove_listener(int port, pjs::Object *options = nullptr) -> Listener*;
  auto remove_listener(pjs::Str *port, pjs::Object *options = nullptr) -> Listener*;
  void set_listeners(pjs::Array *array);
  void apply(Worker *worker, PipelineLayout *layout);
  void close();

private:
  ListenerArray(pjs::Object *options = nullptr)
    : m_default_options(options) {}

  void get_ip_port(const std::string &ip_port, std::string &ip, int &port);

  Worker* m_worker = nullptr;
  pjs::Ref<PipelineLayout> m_pipeline_layout;
  pjs::Ref<pjs::Object> m_default_options;
  std::map<Listener*, Listener::Options> m_listeners;

  friend class pjs::ObjectTemplate<ListenerArray>;
};

} // namespace pipy

#endif // LISTENER_HPP
