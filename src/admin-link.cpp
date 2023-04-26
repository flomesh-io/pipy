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

#include "admin-link.hpp"
#include "context.hpp"
#include "pipeline.hpp"
#include "filters/connect.hpp"
#include "filters/http.hpp"
#include "filters/tls.hpp"
#include "filters/websocket.hpp"

#include <random>

namespace pipy {

//
// AdminLink
//

AdminLink::AdminLink(const std::string &url, const TLSSettings *tls_settings)
  : m_module(new Module())
  , m_url(URL::make(url))
{
  auto host = m_url->hostname()->str() + ':' + m_url->port()->str();

  uint8_t key[16];
  std::minstd_rand rand;
  for (size_t i = 0; i < sizeof(key); i++) key[i] = rand();

  char key_base64[32];
  key_base64[utils::encode_base64(key_base64, key, sizeof(key))] = 0;

  auto head = http::RequestHead::make();
  auto headers = pjs::Object::make();
  m_handshake = Message::make(head, nullptr);
  head->headers = headers;
  head->path = m_url->path();
  headers->set("upgrade", "websocket");
  headers->set("connection", "upgrade");
  headers->set("sec-websocket-key", key_base64);
  headers->set("sec-websocket-version", "13");

  auto *ppl_connect = PipelineLayout::make(m_module);
  ppl_connect->append(new Connect(pjs::Str::make(host), Connect::Options()));

  if (tls_settings) {
    tls::Client::Options opts;
    opts.trusted = tls_settings->trusted;
    if (tls_settings->cert) {
      opts.certificate = pjs::Object::make();
      opts.certificate->set("cert", tls_settings->cert.get());
      opts.certificate->set("key", tls_settings->key.get());
    }
    auto *ppl_tls = PipelineLayout::make(m_module);
    ppl_tls->append(new tls::Client(opts))->add_sub_pipeline(ppl_connect);
    ppl_connect = ppl_tls;
  }

  auto *ppl_tunnel = PipelineLayout::make(m_module);
  ppl_tunnel->append(new http::Mux(nullptr, nullptr))->add_sub_pipeline(ppl_connect);

  m_ppl = PipelineLayout::make(m_module);
  m_ppl->append(new websocket::Encoder());
  m_ppl->append(new http::TunnelClient(m_handshake))->add_sub_pipeline(ppl_tunnel);
  m_ppl->append(new websocket::Decoder());
  m_ppl->append(new Receiver(this));
}

auto AdminLink::connect() -> int {
  if (!m_pipeline) {
    auto ctx = Context::make();
    m_pipeline = Pipeline::make(m_ppl, ctx);
    m_connection_id++;
    if (m_connection_id <= 0) m_connection_id = 1;
  }
  return m_connection_id;
}

void AdminLink::add_handler(const Handler &handler) {
  m_handlers.push_back(handler);
}

void AdminLink::send(const Data &data) {
  if (m_pipeline) {
    auto head = websocket::MessageHead::make();
    head->opcode = 1;
    head->masked = true;
    auto inp = m_pipeline->input();
    inp->input(MessageStart::make(head));
    inp->input(Data::make(data));
    inp->input(MessageEnd::make());
  }
}

void AdminLink::close() {
  m_module->shutdown();
  m_pipeline = nullptr;
}

//
// AdminLink::Receiver
//

auto AdminLink::Receiver::clone() -> Filter* {
  return new Receiver(m_admin_link);
}

void AdminLink::Receiver::reset() {
  Filter::reset();
  m_payload.clear();
  m_started = false;
}

void AdminLink::Receiver::process(Event *evt) {
  if (evt->is<MessageStart>()) {
    m_payload.clear();
    m_started = true;

  } else if (auto *data = evt->as<Data>()) {
    if (m_started) {
      m_payload.push(*data);
    }

  } else if (evt->is<MessageEnd>()) {
    m_started = false;
    Data buf;
    m_payload.shift_to(
      [](int b) { return b == '\n'; },
      buf
    );
    auto command = buf.to_string();
    if (command.back() == '\n') command.pop_back();
    for (const auto &h : m_admin_link->m_handlers) {
      if (h(command, m_payload)) break;
    }

  } else if (evt->is<StreamEnd>()) {
    m_admin_link->m_pipeline = nullptr;
  }
}

void AdminLink::Receiver::dump(Dump &d) {
  Filter::dump(d);
  d.name = "AdminLink::Receiver";
}

} // namespace pipy
