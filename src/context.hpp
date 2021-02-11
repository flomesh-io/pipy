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

#ifndef CONTEXT_HPP
#define CONTEXT_HPP

#include "ns.hpp"
#include "pool.hpp"
#include "object.hpp"

#include <string>
#include <map>
#include <functional>

NS_BEGIN

//
// Context
//

class Context : public Pooled<Context> {
public:
  Context();
  Context(const Context &rhs);
  ~Context();

  uint64_t id;
  std::string remote_addr;
  std::string local_addr;
  int remote_port = 0;
  int local_port = 0;
  std::map<std::string, std::string> variables;

  bool find(const std::string &key, std::string &val) {
    const auto p = variables.find(key);
    if (p == variables.end()) return false;
    val = p->second;
    return true;
  }

  auto evaluate(const std::string &str, bool *solved = nullptr) const -> std::string;

  //
  // Context::Queue
  //

  class Queue : public Pool<Queue> {
  public:
    void clear();
    void send(std::unique_ptr<Object> obj);
    void receive(Object::Receiver receiver);

  private:
    std::list<Object::Receiver> m_receivers;
    std::list<std::unique_ptr<Object>> m_buffer;
  };

  auto get_queue(const std::string &name) -> Queue* {
    const auto p = m_queues.find(name);
    if (p != m_queues.end()) return p->second.get();
    auto ds = new Queue();
    m_queues.emplace(name, ds);
    return ds;
  }

private:
  std::map<std::string, std::unique_ptr<Queue>> m_queues;

  static uint64_t s_context_id;
};

NS_END

#endif // CONTEXT_HPP
