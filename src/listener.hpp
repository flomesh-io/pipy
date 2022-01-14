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
#include "inbound.hpp"

#include <functional>
#include <string>
#include <list>

namespace pipy {

class PipelineDef;

//
// Listener
//

class Listener {
public:
  struct Options : public Inbound::Options {
    int max_connections = -1;
    bool reserved = false;
  };

  static void set_reuse_port(bool reuse);

  static auto all() -> const std::list<Listener*>& {
    return s_all_listeners;
  }

  static auto get(const std::string &ip, int port) -> Listener* {
    if (auto *l = find(ip, port)) return l;
    return new Listener(ip, port);
  }

  static bool is_open(const std::string &ip, int port) {
    if (auto *l = find(ip, port)) return l->m_pipeline_def;
    return false;
  }

  static void for_each(const std::function<void(Listener*)> &cb) {
    for (const auto &p : s_all_listeners) {
      cb(p);
    }
  }

  auto ip() const -> const std::string& { return m_ip; }
  auto port() const -> int { return m_port; }
  bool open() const { return m_pipeline_def; }
  bool reserved() const { return m_options.reserved; }
  auto pipeline_def() const -> PipelineDef* { return m_pipeline_def; }
  void pipeline_def(PipelineDef *def);
  auto peak_connections() const -> int { return m_peak_connections; }

  void set_options(const Options &options);

  void for_each_inbound(const std::function<void(Inbound*)> &cb) {
    for (auto p = m_inbounds.head(); p; p = p->next()) {
      cb(p);
    }
  }

private:
  Listener(const std::string &ip, int port);
  ~Listener();

  void start();
  void accept();
  void pause();
  void resume();
  void open(Inbound *inbound);
  void close(Inbound *inbound);
  void close();

  std::string m_ip;
  int m_port;
  int m_peak_connections = 0;
  Options m_options;
  bool m_paused = false;
  asio::ip::address m_address;
  asio::ip::tcp::acceptor m_acceptor;
  pjs::Ref<PipelineDef> m_pipeline_def;
  List<Inbound> m_inbounds;

  static bool s_reuse_port;
  static std::list<asio::steady_timer*> s_timer_pool;
  static std::list<Listener*> s_all_listeners;

  static auto find(const std::string &ip, int port) -> Listener*;

  friend class Inbound;
  friend class pjs::RefCount<Listener>;
};

} // namespace pipy

#endif // LISTENER_HPP