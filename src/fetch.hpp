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

#ifndef FETCH_HPP
#define FETCH_HPP

#include "pjs/pjs.hpp"
#include "api/http.hpp"
#include "api/crypto.hpp"
#include "filters/connect.hpp"
#include "filter.hpp"
#include "module.hpp"
#include "outbound.hpp"

#include <functional>
#include <string>
#include <list>
#include <vector>

namespace pipy {

class Data;
class Message;
class PipelineLayout;
class Pipeilne;

//
// Fetch
//

class Fetch {
public:
  enum Method {
    HEAD,
    GET,
    PUT,
    POST,
    PATCH,
    DELETE,
  };

  struct Options : public Connect::Options {
    bool tls = false;
    pjs::Ref<crypto::Certificate> cert;
    pjs::Ref<crypto::PrivateKey> key;
    std::vector<pjs::Ref<crypto::Certificate>> trusted;
  };

  Fetch(pjs::Str *host, const Options &options);
  Fetch(const std::string &host, const Options &options);
  ~Fetch();

  bool busy() const {
    return !m_request_queue.empty();
  }

  auto outbound() const -> Outbound* {
    return m_outbound;
  }

  void operator()(
    Method method,
    pjs::Str *path,
    pjs::Object *headers,
    pipy::Data *body,
    const std::function<void(http::ResponseHead*, pipy::Data*)> &cb
  ) {
    fetch(method, path, headers, body, cb);
  }

  void close();

private:

  //
  // Fetch::Module
  //

  class Module : public ModuleBase {
  public:
    Module() : ModuleBase("Fetch") {}
    virtual auto new_context(pipy::Context *base) -> pipy::Context* override {
      return Context::make();
    }
  };

  //
  // Fetch::Request
  //

  struct Request {
    Method method;
    pjs::Ref<Message> message;
    std::function<void(http::ResponseHead*, pipy::Data*)> cb;
  };

  //
  // Fetch::Receiver
  //

  class Receiver : public Filter {
  public:
    Receiver(Fetch *fetch) : m_fetch(fetch) {}

  private:
    virtual auto clone() -> Filter* override;
    virtual void reset() override;
    virtual void process(Event *evt) override;
    virtual void dump(Dump &d) override;

    Fetch* m_fetch;
    pjs::Ref<http::ResponseHead> m_head;
    pjs::Ref<pipy::Data> m_body;
  };

  pjs::Ref<Module> m_module;
  pjs::Ref<pjs::Method> m_mux_grouper;
  pjs::Ref<pjs::Object> m_mux_group;
  pjs::Ref<pjs::Str> m_host;
  std::list<Request> m_request_queue;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<PipelineLayout> m_ppl;
  pjs::Ref<Outbound> m_outbound;
  Request* m_current_request = nullptr;

  void fetch(
    Method method,
    pjs::Str *path,
    pjs::Object *headers,
    pipy::Data *body,
    const std::function<void(http::ResponseHead*, pipy::Data*)> &cb
  );

  void pump();
  bool is_bodiless_response();
  void on_response(http::ResponseHead *head, pipy::Data *body);

  friend class Receiver;
};

} // namespace pipy

#endif // FETCH_HPP
