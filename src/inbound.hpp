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
#include "pjs/pjs.hpp"

namespace pipy {

class Data;
class Listener;
class Pipeline;
class Session;

//
// Inbound
//

class Inbound : public pjs::ObjectTemplate<Inbound> {
public:
  void accept(
    Listener* listener,
    asio::ip::tcp::acceptor &acceptor,
    std::function<void(const std::error_code&)> on_result
  );

  auto id() const -> uint64_t { return m_id; }

  auto remote_address() -> pjs::Str* {
    if (!m_str_remote_addr) {
      m_str_remote_addr = pjs::Str::make(m_remote_addr);
    }
    return m_str_remote_addr;
  }

  auto local_address() -> pjs::Str* {
    if (!m_str_local_addr) {
      m_str_local_addr = pjs::Str::make(m_local_addr);
    }
    return m_str_local_addr;
  }

  auto remote_port() const -> int { return m_remote_port; }
  auto local_port() const -> int { return m_local_port; }

  void set_keep_alive_request(bool b) { m_keep_alive = b; }
  void increase_request_count() { m_request_count++; }
  bool increase_response_count();

  auto session() const -> Session* { return m_session; }
  void pause();
  void resume();
  void send(const pjs::Ref<Data> &data);
  void flush();
  void end();

private:
  Inbound();
  Inbound(asio::ssl::context &ssl_context);
  ~Inbound();

  enum ReceivingState {
    RECEIVING,
    PAUSING,
    PAUSED,
  };

  uint64_t m_id;
  pjs::Ref<Session> m_session;
  asio::ip::tcp::endpoint m_peer;
  asio::ip::tcp::socket m_socket;
  asio::ssl::stream<asio::ip::tcp::socket> m_ssl_socket;
  pjs::Ref<pjs::Str> m_str_remote_addr;
  pjs::Ref<pjs::Str> m_str_local_addr;
  std::string m_remote_addr;
  std::string m_local_addr;
  int m_remote_port = 0;
  int m_local_port = 0;
  Data m_buffer;
  ReceivingState m_receiving_state = RECEIVING;
  bool m_ssl = false;
  bool m_pumping = false;
  bool m_reading_ended = false;
  bool m_writing_ended = false;
  bool m_keep_alive = true;
  int m_request_count = 0;
  int m_response_count = 0;

  auto socket() -> asio::basic_socket<asio::ip::tcp>& {
    return m_ssl ? m_ssl_socket.lowest_layer() : m_socket;
  }

  void start(Pipeline *pipeline);
  void receive();
  void pump();
  void close();
  void free();

  static uint64_t s_inbound_id;

  friend class pjs::ObjectTemplate<Inbound>;
};

} // namespace pipy

#endif // INBOUND_HPP