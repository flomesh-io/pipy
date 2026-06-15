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

#include "fetch.hpp"
#include "context.hpp"
#include "input.hpp"
#include "pipeline.hpp"
#include "message.hpp"
#include "filters/connect.hpp"
#include "filters/demux.hpp"
#include "filters/http.hpp"
#include "filters/tls.hpp"
#include "utils.hpp"

namespace pipy {

auto Fetch::Receiver::clone() -> Filter* {
  return new Receiver(m_fetch);
}

void Fetch::Receiver::reset() {
  Filter::reset();
  m_head = nullptr;
  m_body = nullptr;
}

void Fetch::Receiver::process(Event *evt) {
  if (auto e = evt->as<MessageStart>()) {
    m_head = e->head();
    m_body = Data::make();
  } else if (auto *data = evt->as<Data>()) {
    if (m_body && data->size() > 0) {
      m_body->push(*data);
    }
  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    m_fetch->on_response(m_head, m_body);
    m_head = nullptr;
    m_body = nullptr;
  }
}

void Fetch::Receiver::dump(Dump &d) {
  Filter::dump(d);
  d.name = "Fetch::Receiver";
}

Fetch::Fetch(pjs::Str *host, const Options &options)
  : m_module(new Module())
  , m_host(host)
{
  m_mux_grouper = pjs::Method::make(
    "", [this](pjs::Context &, pjs::Object *, pjs::Value &ret) {
      ret.set(m_mux_group);
    }
  );

  Connect::Options connect_options(options);
  connect_options.on_state_changed = [this](Outbound *ob) {
    m_outbound = ob;
  };

  auto *ppl_connect = PipelineLayout::make(m_module);
  ppl_connect->append(new Connect(m_host.get(), connect_options));

  if (options.tls) {
    tls::Client::Options opts;
    opts.trusted = options.trusted;
    if (options.cert) {
      opts.certificate = pjs::Object::make();
      opts.certificate->set("cert", options.cert.get());
      opts.certificate->set("key", options.key.get());
    }
    std::string sni;
    int port;
    utils::get_host_port(host->str(), sni, port);
    opts.sni = pjs::Str::make(sni);
    auto *ppl_tls = PipelineLayout::make(m_module);
    ppl_tls->append(new tls::Client(opts))->add_sub_pipeline(ppl_connect);
    ppl_connect = ppl_tls;
  }

  m_ppl = PipelineLayout::make(m_module);
  m_ppl->append(new http::Mux(pjs::Function::make(m_mux_grouper)))->add_sub_pipeline(ppl_connect);
  m_ppl->append(new Receiver(this));
}

Fetch::Fetch(const std::string &host, const Options &options)
  : Fetch(pjs::Str::make(host), options)
{
}

Fetch::~Fetch() {
  m_module->shutdown();
}

static const pjs::Ref<pjs::Str> s_HEAD(pjs::Str::make("HEAD"));
static const pjs::Ref<pjs::Str> s_GET(pjs::Str::make("GET"));
static const pjs::Ref<pjs::Str> s_PUT(pjs::Str::make("PUT"));
static const pjs::Ref<pjs::Str> s_POST(pjs::Str::make("POST"));
static const pjs::Ref<pjs::Str> s_PATCH(pjs::Str::make("PATCH"));
static const pjs::Ref<pjs::Str> s_DELETE(pjs::Str::make("DELETE"));
static const pjs::Ref<pjs::Str> s_Host(pjs::Str::make("Host"));

void Fetch::fetch(
  Method method,
  pjs::Str *path,
  pjs::Object *headers,
  pipy::Data *body,
  const std::function<void(http::ResponseHead*, pipy::Data*)> &cb
) {
  if (!m_mux_group) m_mux_group = pjs::Object::make();

  if (!headers) headers = pjs::Object::make();
  headers->set(s_Host, m_host.get());

  auto head = http::RequestHead::make();
  head->path = path;
  head->headers = headers;

  switch (method) {
    case HEAD: head->method = s_HEAD; break;
    case GET: head->method = s_GET; break;
    case PUT: head->method = s_PUT; break;
    case POST: head->method = s_POST; break;
    case PATCH: head->method = s_PATCH; break;
    case DELETE: head->method = s_DELETE; break;
  }

  m_request_queue.emplace_back();
  auto &req = m_request_queue.back();
  req.method = method;
  req.message = Message::make(head, body);
  req.cb = cb;

  pump();
}

void Fetch::close() {
  m_mux_group = nullptr;
  m_pipeline = nullptr;
  m_current_request = nullptr;
  m_request_queue.clear();
}

void Fetch::pump() {
  if (!m_current_request && !m_request_queue.empty()) {
    m_current_request = &m_request_queue.front();
    auto msg = m_current_request->message;
    Net::current().post(
      [=]() {
        InputContext ic;
        m_pipeline = Pipeline::make(m_ppl, m_ppl->new_context());
        msg->write(m_pipeline->input());
      }
    );
  }
}

bool Fetch::is_bodiless_response() {
  if (m_current_request) {
    return m_current_request->method == HEAD;
  } else {
    return false;
  }
}

void Fetch::on_response(http::ResponseHead *head, pipy::Data *body) {
  if (m_current_request) {
    auto cb = m_current_request->cb;
    m_current_request = nullptr;
    m_request_queue.pop_front();
    cb(head, body);
    pump();
  }
}

} // namespace pipy
