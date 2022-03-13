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
#include "timer.hpp"
#include "list.hpp"

namespace pipy {

class Listener;
class PipelineDef;
class Pipeline;

//
// Inbound
//

class Inbound :
  public pjs::ObjectTemplate<Inbound>,
  public EventTarget,
  public InputSource,
  public List<Inbound>::Item
{
public:
  struct Options {
    double read_timeout = 0;
    double write_timeout = 0;
  };

  auto id() const -> uint64_t { return m_id; }
  auto pipeline() const -> Pipeline* { return m_pipeline; }
  auto remote_address() -> pjs::Str*;
  auto local_address() -> pjs::Str*;
  auto remote_port() const -> int { return m_remote_port; }
  auto local_port() const -> int { return m_local_port; }
  auto buffered() const -> int { return m_buffer.size(); }

  void accept(asio::ip::tcp::acceptor &acceptor);

private:
  Inbound(Listener *listener, const Options &options);
  ~Inbound();

  enum ReceivingState {
    RECEIVING,
    PAUSING,
    PAUSED,
  };

  uint64_t m_id;
  Listener* m_listener;
  Options m_options;
  Timer m_read_timer;
  Timer m_write_timer;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<EventTarget::Input> m_output;
  asio::ip::tcp::endpoint m_peer;
  asio::ip::tcp::socket m_socket;
  pjs::Ref<pjs::Str> m_str_remote_addr;
  pjs::Ref<pjs::Str> m_str_local_addr;
  std::string m_remote_addr;
  std::string m_local_addr;
  int m_remote_port = 0;
  int m_local_port = 0;
  Data m_buffer;
  ReceivingState m_receiving_state = RECEIVING;
  bool m_pumping = false;
  bool m_ended = false;

  virtual void on_event(Event *evt) override;
  virtual void on_tap_open() override;
  virtual void on_tap_close() override;

  void start();
  void receive();
  void pump();
  void output(Event *evt);
  void close(StreamEnd::Error err);
  void describe(char *desc);
  void free();

  static uint64_t s_inbound_id;

  friend class pjs::ObjectTemplate<Inbound>;
};

} // namespace pipy

#endif // INBOUND_HPP
