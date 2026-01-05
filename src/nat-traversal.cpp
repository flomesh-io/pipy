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

#include "nat-traversal.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <random>
#include <cstring>

namespace pipy {
namespace nat {

static const int STUN_TIMEOUT_MS = 5000;
static const int HOLE_PUNCH_TIMEOUT_MS = 10000;
static const int HOLE_PUNCH_RETRY_MS = 500;
static const int HOLE_PUNCH_MAX_RETRIES = 20;
static const int CONNECTIVITY_TEST_TIMEOUT_MS = 3000;

// STUN message types
static const uint16_t STUN_BINDING_REQUEST = 0x0001;
static const uint16_t STUN_BINDING_RESPONSE = 0x0101;

// STUN attribute types
static const uint16_t STUN_ATTR_MAPPED_ADDRESS = 0x0001;
static const uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;

// STUN magic cookie
static const uint32_t STUN_MAGIC_COOKIE = 0x2112A442;

//
// STUNClient
//

STUNClient::STUNClient(asio::io_context &io_context)
  : m_io_context(io_context)
  , m_recv_buffer(1500)
{
}

STUNClient::~STUNClient() {
  cancel();
}

void STUNClient::discover(const std::string &stun_server, int stun_port, Callback callback) {
  m_callback = callback;

  try {
    // Resolve STUN server
    asio::ip::udp::resolver resolver(m_io_context);
    auto endpoints = resolver.resolve(
      asio::ip::udp::v4(),
      stun_server,
      std::to_string(stun_port)
    );

    if (endpoints.empty()) {
      complete(asio::error::host_not_found, PublicEndpoint());
      return;
    }

    m_server_endpoint = *endpoints.begin();

    // Create UDP socket
    m_socket.reset(new asio::ip::udp::socket(m_io_context));
    m_socket->open(asio::ip::udp::v4());

    // Generate random transaction ID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 12; i++) {
      m_transaction_id[i] = dis(gen);
    }

    // Set timeout
    m_timeout_timer.schedule(
      STUN_TIMEOUT_MS / 1000.0,
      [this]() {
        complete(asio::error::timed_out, PublicEndpoint());
      }
    );

    // Send STUN binding request
    send_binding_request();
    start_receive();

  } catch (const std::exception &e) {
    Log::error("STUN discovery error: %s", e.what());
    complete(asio::error::fault, PublicEndpoint());
  }
}

void STUNClient::cancel() {
  m_timeout_timer.cancel();
  if (m_socket) {
    std::error_code ec;
    m_socket->close(ec);
    m_socket.reset();
  }
}

void STUNClient::send_binding_request() {
  // Build STUN Binding Request
  std::vector<uint8_t> request(20);

  // Message Type: Binding Request
  request[0] = (STUN_BINDING_REQUEST >> 8) & 0xFF;
  request[1] = STUN_BINDING_REQUEST & 0xFF;

  // Message Length: 0 (no attributes)
  request[2] = 0;
  request[3] = 0;

  // Magic Cookie
  request[4] = (STUN_MAGIC_COOKIE >> 24) & 0xFF;
  request[5] = (STUN_MAGIC_COOKIE >> 16) & 0xFF;
  request[6] = (STUN_MAGIC_COOKIE >> 8) & 0xFF;
  request[7] = STUN_MAGIC_COOKIE & 0xFF;

  // Transaction ID
  std::memcpy(&request[8], m_transaction_id, 12);

  m_socket->async_send_to(
    asio::buffer(request),
    m_server_endpoint,
    [this](const std::error_code &ec, std::size_t bytes_sent) {
      handle_send(ec, bytes_sent);
    }
  );
}

void STUNClient::handle_send(const std::error_code &ec, std::size_t bytes_sent) {
  if (ec) {
    Log::error("STUN send error: %s", ec.message().c_str());
    complete(ec, PublicEndpoint());
  }
}

void STUNClient::start_receive() {
  m_socket->async_receive_from(
    asio::buffer(m_recv_buffer),
    m_from_endpoint,
    [this](const std::error_code &ec, std::size_t bytes_received) {
      handle_receive(ec, bytes_received);
    }
  );
}

void STUNClient::handle_receive(const std::error_code &ec, std::size_t bytes_received) {
  if (ec) {
    if (ec != asio::error::operation_aborted) {
      Log::error("STUN receive error: %s", ec.message().c_str());
      complete(ec, PublicEndpoint());
    }
    return;
  }

  PublicEndpoint endpoint;
  if (parse_stun_response(m_recv_buffer.data(), bytes_received, endpoint)) {
    complete(std::error_code(), endpoint);
  } else {
    // Continue receiving
    start_receive();
  }
}

bool STUNClient::parse_stun_response(const uint8_t *data, size_t len, PublicEndpoint &endpoint) {
  if (len < 20) return false;

  // Check message type
  uint16_t msg_type = (data[0] << 8) | data[1];
  if (msg_type != STUN_BINDING_RESPONSE) return false;

  // Check magic cookie
  uint32_t magic = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
  if (magic != STUN_MAGIC_COOKIE) return false;

  // Check transaction ID
  if (std::memcmp(&data[8], m_transaction_id, 12) != 0) return false;

  // Parse message length
  uint16_t msg_len = (data[2] << 8) | data[3];
  if (len < 20 + msg_len) return false;

  // Parse attributes
  size_t pos = 20;
  while (pos + 4 <= len) {
    uint16_t attr_type = (data[pos] << 8) | data[pos + 1];
    uint16_t attr_len = (data[pos + 2] << 8) | data[pos + 3];
    pos += 4;

    if (pos + attr_len > len) break;

    if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS || attr_type == STUN_ATTR_MAPPED_ADDRESS) {
      if (attr_len < 8) {
        pos += attr_len;
        continue;
      }

      uint8_t family = data[pos + 1];
      uint16_t port = (data[pos + 2] << 8) | data[pos + 3];

      if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
        port ^= (STUN_MAGIC_COOKIE >> 16);
      }

      if (family == 0x01) { // IPv4
        uint32_t ip = (data[pos + 4] << 24) | (data[pos + 5] << 16) |
                      (data[pos + 6] << 8) | data[pos + 7];

        if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
          ip ^= STUN_MAGIC_COOKIE;
        }

        char ip_str[16];
        std::snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
          (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
          (ip >> 8) & 0xFF, ip & 0xFF);

        endpoint.ip = ip_str;
        endpoint.port = port;
        endpoint.is_v6 = false;
        return true;
      }
    }

    pos += attr_len;
    // Attributes are padded to 4-byte boundary
    pos = (pos + 3) & ~3;
  }

  return false;
}

void STUNClient::complete(const std::error_code &ec, const PublicEndpoint &endpoint) {
  m_timeout_timer.cancel();
  if (m_socket) {
    std::error_code close_ec;
    m_socket->close(close_ec);
    m_socket.reset();
  }

  if (m_callback) {
    auto cb = std::move(m_callback);
    cb(ec, endpoint);
  }
}

//
// HolePuncher
//

HolePuncher::HolePuncher(asio::io_context &io_context)
  : m_io_context(io_context)
  , m_retry_count(0)
  , m_recv_buffer(1500)
{
}

HolePuncher::~HolePuncher() {
  cancel();
}

void HolePuncher::connect(const PeerInfo &peer, int local_port, Callback callback) {
  m_peer = peer;
  m_callback = callback;
  m_retry_count = 0;

  try {
    m_socket.reset(new asio::ip::udp::socket(m_io_context));
    m_socket->open(asio::ip::udp::v4());

    if (local_port > 0) {
      m_socket->bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), local_port));
    }

    m_timeout_timer.schedule(
      HOLE_PUNCH_TIMEOUT_MS / 1000.0,
      [this]() {
        ConnectionResult result;
        result.success = false;
        result.error_message = "Connection timeout";
        complete(result);
      }
    );

    start_receive();
    try_connect();

  } catch (const std::exception &e) {
    Log::error("Hole punching error: %s", e.what());
    ConnectionResult result;
    result.success = false;
    result.error_message = e.what();
    complete(result);
  }
}

void HolePuncher::cancel() {
  m_timeout_timer.cancel();
  m_retry_timer.cancel();
  if (m_socket) {
    std::error_code ec;
    m_socket->close(ec);
    m_socket.reset();
  }
}

void HolePuncher::try_connect() {
  if (m_retry_count >= HOLE_PUNCH_MAX_RETRIES) {
    ConnectionResult result;
    result.success = false;
    result.error_message = "Max retries exceeded";
    complete(result);
    return;
  }

  m_retry_count++;

  // Try public endpoint
  try {
    asio::ip::address addr = asio::ip::make_address(m_peer.public_ip);
    asio::ip::udp::endpoint public_ep(addr, m_peer.public_port);
    send_punch_packet(public_ep);
  } catch (...) {}

  // Try private endpoint if different
  if (!m_peer.private_ip.empty() && m_peer.private_ip != m_peer.public_ip) {
    try {
      asio::ip::address addr = asio::ip::make_address(m_peer.private_ip);
      asio::ip::udp::endpoint private_ep(addr, m_peer.private_port);
      send_punch_packet(private_ep);
    } catch (...) {}
  }

  // Schedule retry
  m_retry_timer.schedule(
    HOLE_PUNCH_RETRY_MS / 1000.0,
    [this]() {
      try_connect();
    }
  );
}

void HolePuncher::send_punch_packet(const asio::ip::udp::endpoint &endpoint) {
  // Send a simple punch packet
  static const char punch_msg[] = "PUNCH";
  m_socket->async_send_to(
    asio::buffer(punch_msg, sizeof(punch_msg)),
    endpoint,
    [this](const std::error_code &ec, std::size_t bytes_sent) {
      handle_send(ec, bytes_sent);
    }
  );
}

void HolePuncher::handle_send(const std::error_code &ec, std::size_t bytes_sent) {
  // Silently ignore send errors during hole punching
  (void)ec;
  (void)bytes_sent;
}

void HolePuncher::start_receive() {
  m_socket->async_receive_from(
    asio::buffer(m_recv_buffer),
    m_from_endpoint,
    [this](const std::error_code &ec, std::size_t bytes_received) {
      handle_receive(ec, bytes_received);
    }
  );
}

void HolePuncher::handle_receive(const std::error_code &ec, std::size_t bytes_received) {
  if (ec) {
    if (ec != asio::error::operation_aborted) {
      // Silently ignore receive errors during hole punching
    }
    return;
  }

  // Check if this is a response from peer
  if (bytes_received >= 5 && std::memcmp(m_recv_buffer.data(), "PUNCH", 5) == 0) {
    ConnectionResult result;
    result.success = true;
    result.endpoint = m_from_endpoint;
    complete(result);
    return;
  }

  // Continue receiving
  start_receive();
}

void HolePuncher::complete(const ConnectionResult &result) {
  m_timeout_timer.cancel();
  m_retry_timer.cancel();

  if (m_callback) {
    auto cb = std::move(m_callback);
    cb(result);
  }
}

//
// ConnectivityTester
//

ConnectivityTester::ConnectivityTester(asio::io_context &io_context)
  : m_io_context(io_context)
  , m_recv_buffer(1500)
  , m_start_time(0)
  , m_packets_sent(0)
  , m_packets_received(0)
{
}

ConnectivityTester::~ConnectivityTester() {
  cancel();
}

void ConnectivityTester::test(const std::string &peer_ip, int peer_port, Callback callback) {
  m_callback = callback;
  m_packets_sent = 0;
  m_packets_received = 0;

  try {
    asio::ip::address addr = asio::ip::make_address(peer_ip);
    m_peer_endpoint = asio::ip::udp::endpoint(addr, peer_port);

    m_socket.reset(new asio::ip::udp::socket(m_io_context));
    m_socket->open(asio::ip::udp::v4());

    m_timeout_timer.schedule(
      CONNECTIVITY_TEST_TIMEOUT_MS / 1000.0,
      [this]() {
        TestResult result;
        result.reachable = m_packets_received > 0;
        result.latency_ms = 0;
        result.packet_loss = m_packets_sent > 0 ?
          1.0 - (double)m_packets_received / m_packets_sent : 1.0;
        complete(std::error_code(), result);
      }
    );

    start_receive();
    send_ping();

  } catch (const std::exception &e) {
    Log::error("Connectivity test error: %s", e.what());
    complete(asio::error::fault, TestResult());
  }
}

void ConnectivityTester::cancel() {
  m_timeout_timer.cancel();
  if (m_socket) {
    std::error_code ec;
    m_socket->close(ec);
    m_socket.reset();
  }
}

void ConnectivityTester::send_ping() {
  static const char ping_msg[] = "PING";
  m_start_time = utils::now();
  m_packets_sent++;

  m_socket->async_send_to(
    asio::buffer(ping_msg, sizeof(ping_msg)),
    m_peer_endpoint,
    [this](const std::error_code &ec, std::size_t bytes_sent) {
      handle_send(ec, bytes_sent);
    }
  );
}

void ConnectivityTester::handle_send(const std::error_code &ec, std::size_t bytes_sent) {
  // Silently ignore send errors during connectivity test
  (void)ec;
  (void)bytes_sent;
}

void ConnectivityTester::start_receive() {
  m_socket->async_receive_from(
    asio::buffer(m_recv_buffer),
    m_from_endpoint,
    [this](const std::error_code &ec, std::size_t bytes_received) {
      handle_receive(ec, bytes_received);
    }
  );
}

void ConnectivityTester::handle_receive(const std::error_code &ec, std::size_t bytes_received) {
  if (ec) {
    if (ec != asio::error::operation_aborted) {
      // Silently ignore receive errors during connectivity test
    }
    return;
  }

  if (bytes_received >= 4 && std::memcmp(m_recv_buffer.data(), "PONG", 4) == 0) {
    m_packets_received++;
    double latency = (utils::now() - m_start_time) * 1000.0;

    TestResult result;
    result.reachable = true;
    result.latency_ms = latency;
    result.packet_loss = 0;
    complete(std::error_code(), result);
    return;
  }

  start_receive();
}

void ConnectivityTester::complete(const std::error_code &ec, const TestResult &result) {
  m_timeout_timer.cancel();
  if (m_socket) {
    std::error_code close_ec;
    m_socket->close(close_ec);
    m_socket.reset();
  }

  if (m_callback) {
    auto cb = std::move(m_callback);
    cb(ec, result);
  }
}

} // namespace nat
} // namespace pipy
