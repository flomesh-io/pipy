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

#include "ns.hpp"
#include "net.hpp"
#include "pipeline.hpp"

#include <functional>
#include <memory>
#include <string>

NS_BEGIN

//
// Listener
//

class Listener {
public:
  static void run();
  static void stop();
  static auto listen(const std::string &ip, int port, Pipeline *pipeline) -> Listener*;
  static void set_timeout(double duration, std::function<void()> handler);
  static void set_reuse_port(bool enabled);

  auto ip() const -> const std::string& { return m_ip; }
  auto port() const -> int { return m_port; }

  void close();

private:
  Listener(const std::string &ip, int port, Pipeline *pipeline);

  void accept();

  std::string m_ip;
  int m_port;
  asio::ip::tcp::acceptor m_acceptor;
  Pipeline* m_pipeline;
};

NS_END

#endif // LISTENER_HPP
