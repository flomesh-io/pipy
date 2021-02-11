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

#ifndef PROXY_HPP
#define PROXY_HPP

#include "module.hpp"
#include "net.hpp"

#include <list>

NS_BEGIN

class Session;
class Outbound;

//
// Proxy
//

class Proxy : public Module {
  virtual auto help() -> std::list<std::string> override;
  virtual void config(const std::map<std::string, std::string> &params) override;
  virtual auto clone() -> Module* override;
  virtual void pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) override;

private:
  std::string m_to;
  bool m_address_known = false;
  uint64_t m_context_id = 0;
  Session* m_target = nullptr;
  std::list<std::unique_ptr<Object>> m_buffer;
};

//
// ProxyTCP
//

class ProxyTCP : public Module {
  virtual auto help() -> std::list<std::string> override;
  virtual void config(const std::map<std::string, std::string> &params) override;
  virtual auto clone() -> Module* override;
  virtual void pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) override;

private:
  std::string m_to;
  int m_retry_count = 0;
  double m_retry_delay = 0;
  size_t m_buffer_limit = 0;
  asio::ssl::context::method m_ssl_method;
  bool m_ssl = false;
  bool m_open = false;
  Outbound* m_target = nullptr;

  void try_connect(
    std::shared_ptr<Context> ctx,
    Object::Receiver out
  );
};

NS_END

#endif // PROXY_HPP
