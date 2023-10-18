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

#ifndef EVENT_CONNECTION_HPP
#define EVENT_CONNECTION_HPP

#include "event.hpp"
#include "event-queue.hpp"
#include "net.hpp"

#include <atomic>
#include <mutex>

namespace pipy {

//
// EventConnection
//

class EventConnection : public pjs::Pooled<EventConnection> {
public:
  static void register_port(const std::string &port);
  static void unregister_ports();

  static auto make(const std::string &port) -> EventConnection* {
    return new EventConnection(port);
  }

  auto retain() -> EventConnection* { m_refs.fetch_add(1, std::memory_order_relaxed); return this; }
  void release() { if (m_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this; }

  void input(Event *evt);
  void output(Event *evt);

private:

  //
  // EventConnection::Listener
  //

  struct Listener {
    Net* net = nullptr;
    Listener* next = nullptr;
  };

  //
  // EventConnection::Port
  //

  struct Port {
    Listener* listeners = nullptr;
    Listener* current = nullptr;
    void add(Net *net);
    void remove(Net *net);
    auto next() -> Net*;
  };

  //
  // Handlers
  //

  struct OpenHandler : SelfHandler<EventConnection> {
    using SelfHandler::SelfHandler;
    OpenHandler(const OpenHandler &r) : SelfHandler(r) {}
    void operator()() { self->on_open(); }
  };

  struct CloseHandler : SelfHandler<EventConnection> {
    using SelfHandler::SelfHandler;
    CloseHandler(const CloseHandler &r) : SelfHandler(r) {}
    void operator()() { self->on_close(); }
  };

  struct InputHandler : SelfHandler<EventConnection> {
    using SelfHandler::SelfHandler;
    InputHandler(const InputHandler &r) : SelfHandler(r) {}
    void operator()() { self->on_input(); }
  };

  struct OutputHandler : SelfHandler<EventConnection> {
    using SelfHandler::SelfHandler;
    OutputHandler(const OutputHandler &r) : SelfHandler(r) {}
    void operator()() { self->on_input(); }
  };

  EventConnection(const std::string &port);
  ~EventConnection() {}

  void on_open();
  void on_close();
  void on_input();
  void on_output();

  std::atomic<int> m_refs;
  EventQueue m_input_queue;
  EventQueue m_output_queue;
  Net* m_input_net = nullptr;
  Net* m_output_net;

  static std::map<std::string, Port> m_ports;
  static std::mutex m_ports_mutex;
};

} // namespace pipy

#endif // EVENT_CONNECTION_HPP
