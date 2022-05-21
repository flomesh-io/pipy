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
    if (m_body) {
      m_fetch->on_response(m_head, m_body);
    }
    m_head = nullptr;
    m_body = nullptr;
  }
}

void Fetch::Receiver::dump(std::ostream &out) {
  out << "Fetch::Receiver";
}

Fetch::Fetch(pjs::Str *host, const Options &options)
  : m_host(host)
{
  m_pipeline_def_connect = PipelineDef::make(nullptr, PipelineDef::NAMED, "Fetch Connection");
  m_pipeline_def_connect->append(new Connect(m_host.get(), options));

  auto def_connect = m_pipeline_def_connect.get();

  if (options.tls) {
    tls::Client::Options opts;
    opts.trusted = options.trusted;
    if (options.cert) {
      auto certificate = pjs::Object::make();
      certificate->set("cert", options.cert.get());
      certificate->set("key", options.key.get());
      opts.certificate = certificate;
    }
    m_pipeline_def_tls = PipelineDef::make(nullptr, PipelineDef::NAMED, "Fetch TLS");
    m_pipeline_def_tls->append(new tls::Client(opts))->add_sub_pipeline(m_pipeline_def_connect);
    def_connect = m_pipeline_def_tls;
  }

  m_pipeline_def = PipelineDef::make(nullptr, PipelineDef::NAMED, "Fetch");
  m_pipeline_def->append(new http::Mux())->add_sub_pipeline(def_connect);
  m_pipeline_def->append(new Receiver(this));
}

Fetch::Fetch(const std::string &host, const Options &options)
  : Fetch(pjs::Str::make(host), options)
{
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
  if (!headers) headers = pjs::Object::make();
  headers->set(s_Host, m_host.get());

  auto head = http::RequestHead::make();
  head->path(path);
  head->headers(headers);

  switch (method) {
    case HEAD: head->method(s_HEAD); break;
    case GET: head->method(s_GET); break;
    case PUT: head->method(s_PUT); break;
    case POST: head->method(s_POST); break;
    case PATCH: head->method(s_PATCH); break;
    case DELETE: head->method(s_DELETE); break;
  }

  m_request_queue.emplace_back();
  auto &req = m_request_queue.back();
  req.method = method;
  req.message = Message::make(head, body);
  req.cb = cb;

  pump();
}

void Fetch::close() {
  m_pipeline = nullptr;
  m_current_request = nullptr;
  m_request_queue.clear();
}

void Fetch::pump() {
  if (!m_current_request && !m_request_queue.empty()) {
    auto ctx = new Context();
    m_pipeline = Pipeline::make(m_pipeline_def, ctx);

    m_current_request = &m_request_queue.front();
    auto msg = m_current_request->message;

    InputContext ic;
    auto inp = m_pipeline->input();
    inp->input(MessageStart::make(msg->head()));
    if (auto *body = msg->body()) inp->input(body);
    inp->input(MessageEnd::make(msg->tail()));
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
