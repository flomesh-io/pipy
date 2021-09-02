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
#include "pipeline.hpp"
#include "session.hpp"
#include "message.hpp"
#include "filters/connect.hpp"
#include "filters/demux.hpp"
#include "filters/http.hpp"

namespace pipy {

void Fetch::Receiver::process(Context *ctx, Event *inp) {
  if (auto e = inp->as<MessageStart>()) {
    m_head = e->head();
    m_body = Data::make();
  } else if (auto *data = inp->as<Data>()) {
    if (m_body && data->size() > 0) {
      m_body->push(*data);
    }
  } else if (inp->is<MessageEnd>()) {
    if (m_body) {
      m_fetch->on_response(m_head, m_body);
    }
    m_head = nullptr;
    m_body = nullptr;
  }
}

Fetch::Fetch(pjs::Str *host)
  : m_host(host)
{
  m_pipeline_connect = Pipeline::make(nullptr, Pipeline::NAMED, "Fetch Connection");
  m_pipeline_connect->append(new Connect(m_host.get(), nullptr));

  m_pipeline_request = Pipeline::make(nullptr, Pipeline::NAMED, "Fetch Request");
  m_pipeline_request->append(new http::Mux(m_pipeline_connect, pjs::Value::undefined));

  m_pipeline = Pipeline::make(nullptr, Pipeline::NAMED, "Fetch");
  m_pipeline->append(new Demux(m_pipeline_request));
  m_pipeline->append(new Receiver(this));
}

Fetch::Fetch(const std::string &host)
  : Fetch(pjs::Str::make(host))
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
  m_session = nullptr;
  m_current_request = nullptr;
}

void Fetch::pump() {
  if (!m_current_request && !m_request_queue.empty()) {
    if (!m_session) {
      auto ctx = new Context();
      m_session = Session::make(ctx, m_pipeline);
    }

    m_current_request = &m_request_queue.front();
    m_session->input(m_current_request->message);
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