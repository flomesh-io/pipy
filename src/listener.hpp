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

#ifndef LISTENER_HPP
#define LISTENER_HPP

#include "net.hpp"

#include <functional>
#include <string>

namespace pipy {

class Pipeline;

//
// Listener
//

class Listener {
public:
  static auto make(const std::string &ip, int port, bool reuse) -> Listener* {
    return new Listener(ip, port, reuse);
  }

  static auto make(const std::string &ip, int port, bool reuse, asio::ssl::context &&ssl_context) -> Listener* {
    return new Listener(ip, port, reuse, std::move(ssl_context));
  }

  static auto get(int port) -> Listener* {
    auto i = s_all_listeners.find(port);
    if (i == s_all_listeners.end()) return nullptr;
    return i->second;
  }

  static void close_all() {
    for (auto &i : s_all_listeners) {
      i.second->close();
    }
  }

  auto ip() const -> const std::string& { return m_ip; }
  auto port() const -> int { return m_port; }
  auto pipeline() const -> Pipeline* { return m_pipeline; }
  void open(Pipeline *pipeline);
  void close();

private:
  Listener(const std::string &ip, int port, bool reuse);
  Listener(const std::string &ip, int port, bool reuse, asio::ssl::context &&ssl_context);
  ~Listener();

  void start();
  void accept();

  std::string m_ip;
  int m_port;
  bool m_reuse;
  bool m_ssl;
  asio::ip::tcp::acceptor m_acceptor;
  asio::ssl::context m_ssl_context;
  pjs::Ref<Pipeline> m_pipeline;

  static std::list<asio::steady_timer*> s_timer_pool;
  static std::map<int, Listener*> s_all_listeners;
};

} // namespace pipy

#endif // LISTENER_HPP