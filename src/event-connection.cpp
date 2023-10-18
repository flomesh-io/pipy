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

#include "event-connection.hpp"

namespace pipy {

std::map<std::string, EventConnection::Port> EventConnection::m_ports;
std::mutex EventConnection::m_ports_mutex;

void EventConnection::register_port(const std::string &port) {
  std::lock_guard<std::mutex> lock(m_ports_mutex);
  auto *l = new Listener;
  auto &p = m_ports[port];
  l->net = &Net::current();
  l->next = p.listeners;
  p.listeners = l;
}

void EventConnection::unregister_ports() {
  std::lock_guard<std::mutex> lock(m_ports_mutex);
  auto net = &Net::current();
  for (auto &p : m_ports) {
    Listener *b = nullptr;
    for (auto l = p.second.listeners; l; l = l->next) {
      if (l->net == net) {
        auto n = l->next;
        delete l;
        if (p.second.current == l) {
          p.second.current = n;
        }
        if (b) {
          l = b;
          b->next = n;
        } else {
          l = p.second.listeners;
          p.second.listeners = n;
        }
      }
    }
  }
}

EventConnection::EventConnection(const std::string &port)
  : m_output_net(&Net::current())
{
  std::lock_guard<std::mutex> lock(m_ports_mutex);
  auto i = m_ports.find(port);
  if (i != m_ports.end()) {
    auto l = i->second.current;
    if (!l) l = i->second.listeners;
    if (l) {
      m_input_net = l->net;
      i->second.current = l->next;
    }
  }
}

void EventConnection::input(Event *evt) {
  if (m_input_net) {
    m_input_queue.enqueue(evt);
    m_input_net->io_context().post(InputHandler(this));
  }
}

void EventConnection::output(Event *evt) {
  if (m_output_net) {
    m_output_queue.enqueue(evt);
    m_output_net->io_context().post(OutputHandler(this));
  }
}

void EventConnection::on_input() {
}

void EventConnection::on_output() {
}

} // namespace pipy
