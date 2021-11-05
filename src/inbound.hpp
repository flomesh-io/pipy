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
#include "event.hpp"
#include "list.hpp"

namespace pipy {

class Data;
class Listener;
class PipelineDef;
class Pipeline;

//
// Inbound
//

class Inbound :
  public pjs::ObjectTemplate<Inbound>,
  public EventTarget,
  public List<Inbound>::Item
{
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
  auto buffered() const -> int { return m_buffer.size(); }

  auto pipeline() const -> Pipeline* { return m_pipeline; }
  void pause();
  void resume();
  void send(const pjs::Ref<Data> &data);
  void flush();
  void end();

private:
  Inbound(Listener *listener);
  ~Inbound();

  enum ReceivingState {
    RECEIVING,
    PAUSING,
    PAUSED,
  };

  Listener* m_listener;
  uint64_t m_id;
  pjs::Ref<Pipeline> m_pipeline;
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
  bool m_reading_ended = false;
  bool m_writing_ended = false;

  virtual void on_event(Event *evt) override;

  void start(PipelineDef *pipeline_def);
  void receive();
  void pump();
  void close();
  void free();

  static uint64_t s_inbound_id;

  friend class pjs::ObjectTemplate<Inbound>;
};

} // namespace pipy

#endif // INBOUND_HPP
