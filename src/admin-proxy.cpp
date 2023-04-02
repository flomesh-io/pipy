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

#include "admin-proxy.hpp"
#include "filters/connect.hpp"
#include "filters/http.hpp"
#include "filters/tls.hpp"
#include "gui-tarball.hpp"
#include "listener.hpp"
#include "log.hpp"

namespace pipy {

thread_local static Data::Producer s_dp("Admin Proxy");

static std::string s_server_name("pipy-repo");

//
// AdminProxyHandler
//

class AdminProxyHandler : public Filter {
public:
  AdminProxyHandler(AdminProxy *proxy)
    : m_proxy(proxy) {}

  AdminProxyHandler(const AdminProxyHandler &r)
    : Filter(r)
    , m_proxy(r.m_proxy) {}

private:
  virtual auto clone() -> Filter* override {
    return new AdminProxyHandler(*this);
  }

  virtual void reset() override {
    Filter::reset();
    m_head = nullptr;
    m_pipeline = nullptr;
  }

  virtual void process(Event *evt) override {
    if (m_pipeline) {
      Filter::output(evt, m_pipeline->input());

    } else if (auto start = evt->as<MessageStart>()) {
      auto head = start->head()->as<http::RequestHead>();
      if (!m_pipeline) {
        auto &path = head->path->str();
        if (
          utils::starts_with(path, "/api/") ||
          utils::starts_with(path, "/repo/")
        ) {
          m_pipeline = sub_pipeline(0, false, Filter::output());
        }
      }

      if (m_pipeline) {
        Filter::output(evt, m_pipeline->input());
      } else {
        m_head = head;
      }

    } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
      if (m_head) {
        Filter::output(m_proxy->handle(m_head));
      }
    }
  }

  virtual void dump(Dump &d) override {
    Filter::dump(d);
    d.name = "AdminProxyHandler";
  }

  AdminProxy* m_proxy;
  pjs::Ref<http::RequestHead> m_head;
  pjs::Ref<Pipeline> m_pipeline;
};

//
// AdminProxy
//

AdminProxy::AdminProxy(const std::string &target)
  : m_module(new Module())
  , m_target(target)
  , m_www_files(GuiTarball::data(), GuiTarball::size())
{
  m_response_not_found = response(404);
  m_response_method_not_allowed = response(405);
}

void AdminProxy::open(const std::string &ip, int port, const Options &options) {
  Log::info("[admin] Starting admin proxy...");

  PipelineLayout *ppl = PipelineLayout::make(m_module);
  PipelineLayout *ppl_inbound = nullptr;
  PipelineLayout *ppl_request = PipelineLayout::make(m_module);
  PipelineLayout *ppl_forward = PipelineLayout::make(m_module);
  PipelineLayout *ppl_connect = PipelineLayout::make(m_module);

  if (!options.cert || !options.key) {
    ppl_inbound = ppl;

  } else {
    tls::Server::Options opts;
    auto certificate = pjs::Object::make();
    certificate->set("cert", options.cert.get());
    certificate->set("key", options.key.get());
    opts.certificate = certificate;
    opts.trusted = options.trusted;
    ppl_inbound = PipelineLayout::make(m_module);
    ppl->append(new tls::Server(opts))->add_sub_pipeline(ppl_inbound);
  }

  ppl_connect->append(new Connect(m_target, nullptr));

  if (options.fetch_options.tls) {
    tls::Client::Options opts;
    if (options.fetch_options.cert) {
      auto certificate = pjs::Object::make();
      certificate->set("cert", options.fetch_options.cert.get());
      certificate->set("key", options.fetch_options.key.get());
      opts.certificate = certificate;
    }
    opts.trusted = options.fetch_options.trusted;
    auto *ppl = PipelineLayout::make();
    ppl->append(new tls::Client(opts))->add_sub_pipeline(ppl_connect);
    ppl_connect = ppl;
  }

  ppl_inbound->append(new http::Demux(nullptr))->add_sub_pipeline(ppl_request);
  ppl_request->append(new AdminProxyHandler(this))->add_sub_pipeline(ppl_forward);
  ppl_forward->append(new http::Mux(nullptr, nullptr))->add_sub_pipeline(ppl_connect);

  Listener::Options opts;
  opts.reserved = true;
  auto listener = Listener::get(Listener::Protocol::TCP, ip, port);
  listener->set_options(opts);
  listener->pipeline_layout(ppl);
  m_ip = ip;
  m_port = port;
}

void AdminProxy::close() {
  if (auto listener = Listener::get(Listener::Protocol::TCP, m_ip, m_port)) {
    listener->pipeline_layout(nullptr);
  }
  m_module->shutdown();
}

auto AdminProxy::handle(http::RequestHead *head) -> Message* {
  static const std::string prefix_repo("/repo/");
  static const std::string prefix_api_v1_repo("/api/v1/repo/");
  static const std::string prefix_api_v1_files("/api/v1/files/");
  static const std::string header_accept("accept");
  static const std::string text_html("text/html");

  auto method = head->method->str();
  auto path = head->path->str();

  try {

    // Static GUI content
    if (method == "GET") {
      http::File *f = nullptr;
#ifdef PIPY_USE_GUI
      if (path == "/home" || path == "/home/") path = "/home/index.html";
      if (utils::starts_with(path, prefix_repo)) path = "/repo/[...]/index.html";
      auto i = m_www_file_cache.find(path);
      if (i != m_www_file_cache.end()) f = i->second;
      if (!f) {
        f = http::File::from(&m_www_files, path);
        if (f) m_www_file_cache[path] = f;
      }
#endif
      if (f) {
        auto headers = head->headers.get();
        pjs::Value v;
        if (headers) headers->ht_get("accept-encoding", v);
        return f->to_message(v.is_string() ? v.s() : pjs::Str::empty.get());
      } else {
        return m_response_not_found;
      }
    } else {
      return m_response_method_not_allowed;
    }

  } catch (std::runtime_error &err) {
    return response(500, err.what());
  }
}

Message* AdminProxy::response(int status_code) {
  return Message::make(
    response_head(status_code), nullptr
  );
}

Message* AdminProxy::response(int status_code, const std::string &message) {
  return Message::make(
    response_head(
      status_code,
      {
        { "server", s_server_name },
        { "content-type", "text/plain" },
      }
    ),
    s_dp.make(message)
  );
}

auto AdminProxy::response_head(int status) -> http::ResponseHead* {
  auto head = http::ResponseHead::make();
  auto headers_obj = pjs::Object::make();
  headers_obj->ht_set("server", s_server_name);
  head->headers = headers_obj;
  head->status = status;
  return head;
}

auto AdminProxy::response_head(
  int status,
  const std::map<std::string, std::string> &headers
) -> http::ResponseHead* {
  auto head = response_head(status);
  auto headers_obj = head->headers.get();
  for (const auto &i : headers) headers_obj->ht_set(i.first, i.second);
  return head;
}

} // namespace pipy
