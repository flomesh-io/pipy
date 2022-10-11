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

#ifndef ADMIN_PROXY_HPP
#define ADMIN_PROXY_HPP

#include "api/http.hpp"
#include "api/crypto.hpp"
#include "filter.hpp"
#include "module.hpp"
#include "message.hpp"
#include "fetch.hpp"
#include "tar.hpp"

#include <map>
#include <vector>

namespace pipy {

class AdminProxyHandler;

//
// AdminProxy
//

class AdminProxy {
public:
  struct Options {
    Fetch::Options fetch_options;
    pjs::Ref<crypto::Certificate> cert;
    pjs::Ref<crypto::PrivateKey> key;
    std::vector<pjs::Ref<crypto::Certificate>> trusted;
  };

  AdminProxy(const std::string &target);

  void open(int port, const Options &options);
  void close();

private:

  //
  // AdminProxy::Module
  //

  class Module : public ModuleBase {
  public:
    Module() : ModuleBase("AdminProxy") {}
    virtual auto new_context(pipy::Context *base) -> pipy::Context* override {
      return new Context(base);
    }
  };

  pjs::Ref<Module> m_module;

  std::string m_target;
  int m_port;

  Tarball m_www_files;
  std::map<std::string, pjs::Ref<http::File>> m_www_file_cache;

  pjs::Ref<Message> m_response_not_found;
  pjs::Ref<Message> m_response_method_not_allowed;

  auto handle(http::RequestHead *head) -> Message*;

  Message* response(int status_code);
  Message* response(int status_code, const std::string &message);

  static auto response_head(int status) -> http::ResponseHead*;

  static auto response_head(
    int status,
    const std::map<std::string, std::string> &headers
  ) -> http::ResponseHead*;

  friend class AdminProxyHandler;
};

} // namespace pipy

#endif // ADMIN_PROXY_HPP
