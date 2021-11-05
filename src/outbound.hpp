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
#include "list.hpp"

#include <functional>

namespace pipy {

class Data;

//
// Outbound
//

class Outbound :
  public pjs::RefCount<Outbound>,
  public pjs::Pooled<Outbound>,
  public List<Outbound>::Item
{
public:
  Outbound(EventTarget::Input *output);
  ~Outbound();

  static void for_each(const std::function<void(Outbound*)> &cb) {
    for (auto p = s_all_outbounds.head(); p; p = p->next()) {
      cb(p);
    }
  }

  auto host() const -> const std::string& { return m_host; }
  auto address() const -> const std::string& { return m_address; }
  auto port() const -> int { return m_port; }
  bool connected() const { return m_connected; }
  bool overflowed() const { return m_overflowed; }
  auto retries() const -> int { return m_retries; }
  auto buffered() const -> int { return m_buffer.size(); }
  auto connection_time() const -> double { return m_connection_time; }

  void set_buffer_limit(size_t size) { m_buffer_limit = size; }
  void set_retry_count(int n) { m_retry_count = n; }
  void set_retry_delay(double t) { m_retry_delay = t; }

  void connect(const std::string &host, int port);

  void send(const pjs::Ref<Data> &data);
  void flush();
  void end();

private:
  std::string m_host;
  std::string m_address;
  int m_port;
  pjs::Ref<EventTarget::Input> m_output;
  asio::ip::tcp::resolver m_resolver;
  asio::ip::tcp::socket m_socket;
  Timer m_timer;
  int m_retry_count = 0;
  double m_retry_delay = 0;
  int m_retries = 0;
  double m_start_time = 0;
  double m_connection_time = 0;
  Data m_buffer;
  size_t m_buffer_limit = 0;
  size_t m_discarded_data_size = 0;
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
  void output(Event *evt);
  void output_end(StreamEnd *end);
  void pump();
  void close();
  void free();

  static List<Outbound> s_all_outbounds;
};

} // namespace pipy

#endif // OUTBOUND_HPP
