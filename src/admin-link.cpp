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
#include "filters/dump.hpp"
#include "filters/http.hpp"
#include "filters/websocket.hpp"

#include <random>

namespace pipy {

//
// AdminLink
//

AdminLink::AdminLink(
  const std::string &url,
  const std::function<void(const Data&)> &on_receive
) : m_url(URL::make(url))
  , m_on_receive(on_receive)
{
  Outbound::Options options;

  auto host = m_url->hostname()->str() + ':' + m_url->port()->str();

  uint8_t key[16];
  std::minstd_rand rand;
  for (size_t i = 0; i < sizeof(key); i++) key[i] = rand();

  char key_base64[32];
  key_base64[utils::encode_base64(key_base64, key, sizeof(key))] = 0;

  auto head = http::RequestHead::make();
  auto headers = pjs::Object::make();
  m_handshake = Message::make(head, nullptr);
  head->headers(headers);
  head->path(m_url->path());
  headers->set("upgrade", "websocket");
  headers->set("connection", "upgrade");
  headers->set("sec-websocket-key", key_base64);
  headers->set("sec-websocket-version", "13");

  m_pipeline_def_connect = PipelineDef::make(nullptr, PipelineDef::NAMED, "AdminLink Connection");
  m_pipeline_def_connect->append(new Connect(pjs::Str::make(host), options));
  m_pipeline_def_connect->append(new Dump());

  m_pipeline_def_tunnel = PipelineDef::make(nullptr, PipelineDef::NAMED, "AdminLink Tunnel");
  m_pipeline_def_tunnel->append(new http::Mux(pjs::Str::empty.get(), nullptr))->add_sub_pipeline(m_pipeline_def_connect);

  m_pipeline_def = PipelineDef::make(nullptr, PipelineDef::NAMED, "AdminLink");
  m_pipeline_def->append(new websocket::Encoder());
  m_pipeline_def->append(new http::TunnelClient(m_handshake.get()))->add_sub_pipeline(m_pipeline_def_tunnel);
  m_pipeline_def->append(new websocket::Decoder());
  m_pipeline_def->append(new Receiver(this));
}

auto AdminLink::connect() -> int {
  if (!m_pipeline) {
    auto ctx = new Context();
    m_pipeline = Pipeline::make(m_pipeline_def, ctx);
    m_connection_id++;
    if (m_connection_id <= 0) m_connection_id = 1;
  }
  return m_connection_id;
}

void AdminLink::send(const Data &data) {
  if (m_pipeline) {
    auto head = websocket::MessageHead::make();
    pjs::set<websocket::MessageHead>(head, websocket::MessageHead::Field::opcode, 1);
    pjs::set<websocket::MessageHead>(head, websocket::MessageHead::Field::masked, true);
    auto inp = m_pipeline->input();
    inp->input(MessageStart::make(head));
    inp->input(Data::make(data));
    inp->input(MessageEnd::make());
  }
}

void AdminLink::close() {
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
    m_admin_link->m_on_receive(m_payload);

  } else if (evt->is<StreamEnd>()) {
    m_admin_link->m_pipeline = nullptr;
  }
}

void AdminLink::Receiver::dump(std::ostream &out) {
  out << "AdminLink::Receiver";
}

} // namespace pipy
