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
#include "timer.hpp"

namespace pipy {

class Data;

//
// Outbound
//

class Outbound : public pjs::Pooled<Outbound> {
public:
  Outbound(int buffer_limit = 0);
  Outbound(asio::ssl::context &ssl_context);
  ~Outbound();

  auto host() const -> const std::string& { return m_host; }
  auto address() const -> const std::string& { return m_address; }
  auto port() const -> int { return m_port; }
  bool connected() const { return m_connected; }
  bool overflowed() const { return m_overflowed; }
  auto retries() const -> int { return m_retries; }
  auto buffered() const -> int { return m_buffer.size(); }

  void set_retry_count(int n) { m_retry_count = n; }
  void set_retry_delay(double t) { m_retry_delay = t; }
  void set_buffer_limit(size_t size) { m_buffer_limit = size; }

  void on_receive(const Event::Receiver &receiver) { m_receiver = receiver; }
  void on_delete(const std::function<void()> &callback) { m_on_delete = callback; }
  void connect(const std::string &host, int port);

  void send(const pjs::Ref<Data> &data);
  void flush();
  void end();

private:
  std::string m_host;
  std::string m_address;
  int m_port;
  Event::Receiver m_receiver;
  std::function<void()> m_on_delete;
  asio::ip::tcp::resolver m_resolver;
  asio::ip::tcp::socket m_socket;
  asio::ssl::stream<asio::ip::tcp::socket> m_ssl_socket;
  Timer m_timer;
  int m_retry_count = 0;
  double m_retry_delay = 0;
  int m_retries = 0;
  Data m_buffer;
  size_t m_buffer_limit;
  size_t m_discarded_data_size = 0;
  bool m_ssl = false;
  bool m_connected = false;
  bool m_overflowed = false;
  bool m_pumping = false;
  bool m_reading_ended = false;
  bool m_writing_ended = false;

  void connect(double delay);
  bool should_reconnect();
  void cancel_connecting();
  void reconnect();
  void receive();
  void pump();
  void close();
  void free();
};

} // namespace pipy

#endif // OUTBOUND_HPP