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

#ifndef UPSTREAM_H
#define UPSTREAM_H

#include "object.hpp"
#include "pool.hpp"
#include "listener.hpp"
#include "net.hpp"

#include <functional>
#include <map>

NS_BEGIN

class Data;
class Context;
class Session;

//
// Upstream
//

class Upstream : public Pooled<Upstream> {
public:
  static void add_host(Listener *listener);
  static auto find_host(const std::string &host) -> Listener*;

  Upstream();
  Upstream(asio::ssl::context &&ssl_context);

  void connect(
    const std::string &host,
    std::shared_ptr<Context> context,
    Object::Receiver receiver
  );

  void ingress(std::unique_ptr<Object> obj);

private:
  static std::map<std::string, Listener*> s_host_map;

  std::string m_host;
  asio::ip::tcp::resolver m_resolver;
  asio::ip::tcp::socket m_socket;
  asio::ssl::context m_ssl_context;
  asio::ssl::stream<asio::ip::tcp::socket> m_ssl_socket;
  std::list<std::unique_ptr<Object>> m_blocked_objects;
  std::list<std::unique_ptr<Data>> m_buffer;
  Session* m_pipeline = nullptr;
  Object::Receiver m_receiver;

  bool m_ssl = false;
  bool m_connected = false;
  bool m_closed = false;
  bool m_async_waiting = false;

  void egress(std::unique_ptr<Object> obj);
  void send(std::unique_ptr<Data> data);
  void pump();
  void receive();
  void close();
  void free();
};

NS_END

#endif // UPSTREAM_H
