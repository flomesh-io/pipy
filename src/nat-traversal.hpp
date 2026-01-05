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
 *  SOFTWARE IS PROVIDED IN AN "AS IS" CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef NAT_TRAVERSAL_HPP
#define NAT_TRAVERSAL_HPP

#include "net.hpp"
#include "data.hpp"
#include "timer.hpp"

#include <memory>
#include <functional>

namespace pipy {
namespace nat {

//
// STUNClient - STUN protocol implementation for NAT discovery
//

class STUNClient {
public:
  struct PublicEndpoint {
    std::string ip;
    int port;
    bool is_v6;
  };

  using Callback = std::function<void(const std::error_code&, const PublicEndpoint&)>;

  STUNClient(asio::io_context &io_context);
  ~STUNClient();

  void discover(const std::string &stun_server, int stun_port, Callback callback);
  void cancel();

private:
  asio::io_context& m_io_context;
  std::unique_ptr<asio::ip::udp::socket> m_socket;
  asio::ip::udp::endpoint m_server_endpoint;
  asio::ip::udp::endpoint m_from_endpoint;
  Timer m_timeout_timer;
  Callback m_callback;
  uint8_t m_transaction_id[12];
  std::vector<uint8_t> m_recv_buffer;

  void send_binding_request();
  void handle_send(const std::error_code &ec, std::size_t bytes_sent);
  void start_receive();
  void handle_receive(const std::error_code &ec, std::size_t bytes_received);
  bool parse_stun_response(const uint8_t *data, size_t len, PublicEndpoint &endpoint);
  void complete(const std::error_code &ec, const PublicEndpoint &endpoint);
};

//
// HolePuncher - UDP hole punching coordinator
//

class HolePuncher {
public:
  struct PeerInfo {
    std::string public_ip;
    int public_port;
    std::string private_ip;
    int private_port;
  };

  struct ConnectionResult {
    bool success;
    std::string error_message;
    asio::ip::udp::endpoint endpoint;
  };

  using Callback = std::function<void(const ConnectionResult&)>;

  HolePuncher(asio::io_context &io_context);
  ~HolePuncher();

  void connect(const PeerInfo &peer, int local_port, Callback callback);
  void cancel();

private:
  asio::io_context& m_io_context;
  std::unique_ptr<asio::ip::udp::socket> m_socket;
  Timer m_timeout_timer;
  Timer m_retry_timer;
  Callback m_callback;
  PeerInfo m_peer;
  int m_retry_count;
  std::vector<uint8_t> m_recv_buffer;
  asio::ip::udp::endpoint m_from_endpoint;

  void try_connect();
  void send_punch_packet(const asio::ip::udp::endpoint &endpoint);
  void handle_send(const std::error_code &ec, std::size_t bytes_sent);
  void start_receive();
  void handle_receive(const std::error_code &ec, std::size_t bytes_received);
  void complete(const ConnectionResult &result);
};

//
// ConnectivityTester - Test P2P connection quality
//

class ConnectivityTester {
public:
  struct TestResult {
    bool reachable;
    double latency_ms;
    double packet_loss;
  };

  using Callback = std::function<void(const std::error_code&, const TestResult&)>;

  ConnectivityTester(asio::io_context &io_context);
  ~ConnectivityTester();

  void test(const std::string &peer_ip, int peer_port, Callback callback);
  void cancel();

private:
  asio::io_context& m_io_context;
  std::unique_ptr<asio::ip::udp::socket> m_socket;
  asio::ip::udp::endpoint m_peer_endpoint;
  asio::ip::udp::endpoint m_from_endpoint;
  Timer m_timeout_timer;
  Callback m_callback;
  std::vector<uint8_t> m_recv_buffer;
  double m_start_time;
  int m_packets_sent;
  int m_packets_received;

  void send_ping();
  void handle_send(const std::error_code &ec, std::size_t bytes_sent);
  void start_receive();
  void handle_receive(const std::error_code &ec, std::size_t bytes_received);
  void complete(const std::error_code &ec, const TestResult &result);
};

} // namespace nat
} // namespace pipy

#endif // NAT_TRAVERSAL_HPP
