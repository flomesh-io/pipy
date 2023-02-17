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

#include "bgp.hpp"

#include <cstring>
#include <functional>

namespace pipy {

//
// BGP
//

thread_local static Data::Producer s_dp("BGP");

inline static void clamp_data_size(Data &data, size_t limit) {
  if (data.size() > limit) {
    data.pop(data.size() - limit);
  }
}

auto BGP::decode(const Data &data) -> pjs::Array* {
  pjs::Array *a = pjs::Array::make();
  StreamParser sp(
    [=](const pjs::Value &value) {
      a->push(value);
    }
  );
  Data buf(data);
  sp.parse(buf);
  return a;
}

void BGP::encode(pjs::Object *payload, Data &data) {
  Data payload_buffer;

  pjs::Ref<Message> msg;
  if (payload && payload->is<Message>()) {
    msg = payload->as<Message>();
  } else {
    msg = Message::make();
    if (payload) pjs::class_of<Message>()->assign(msg, payload);
  }

  if (auto *body = msg->body.get()) {
    Data::Builder db(payload_buffer, &s_dp);
    switch (msg->type) {
      case MessageType::OPEN: {
        pjs::Ref<MessageOpen> m;
        if (body->is<MessageOpen>()) {
          m = body->as<MessageOpen>();
        } else {
          m = MessageOpen::make();
          pjs::class_of<MessageOpen>()->assign(m, body);
        }
        uint8_t ip[4];
        if (!m->identifier || !utils::get_ip_v4(m->identifier->str(), ip)) {
          std::memset(ip, 0, sizeof(ip));
        }
        db.push((char)m->version);
        db.push((char)m->myAS >> 8);
        db.push((char)m->myAS >> 0);
        db.push((char)m->holdTime >> 8);
        db.push((char)m->holdTime >> 0);
        db.push((char *)ip, sizeof(ip));
        Data param_buffer;
        Data::Builder db2(param_buffer, &s_dp);
        if (auto *caps = m->capabilities.get()) {
          Data caps_buffer;
          Data::Builder db3(caps_buffer, &s_dp);
          caps->iterate_all(
            [&](pjs::Str *k, pjs::Value &v) {
              auto n = k->parse_int();
              if (!std::isnan(n)) {
                int id(n);
                if (v.is<Data>()) {
                  Data data(*v.as<Data>());
                  clamp_data_size(data, 255);
                  db3.push((char)id);
                  db3.push((char)data.size());
                  db3.push(data, 0);
                } else {
                  db3.push((char)id);
                  switch (id) {
                    default: {
                      db3.push('\0');
                      break;
                    }
                  }
                }
              }
            }
          );
          db3.flush();
          if (caps_buffer.size() > 0) {
            clamp_data_size(caps_buffer, 255);
            db2.push('\02');
            db2.push((char)caps_buffer.size());
            db2.push(caps_buffer, 0);
          }
        }
        if (auto *params = m->parameters.get()) {
          params->iterate_all(
            [&](pjs::Str *k, pjs::Value &v) {
              if (v.is<Data>()) {
                auto n = k->parse_int();
                if (!std::isnan(n)) {
                  int id(n);
                  Data data(*v.as<Data>());
                  clamp_data_size(data, 255);
                  db2.push((char)id);
                  db2.push((char)data.size());
                  db2.push(data, 0);
                }
              }
            }
          );
        }
        db2.flush();
        clamp_data_size(param_buffer, 255);
        db.push((char)param_buffer.size());
        db.push(std::move(param_buffer));
        break;
      }

      case MessageType::UPDATE: {
        pjs::Ref<MessageUpdate> m;
        if (body->is<MessageUpdate>()) {
          m = body->as<MessageUpdate>();
        } else {
          m = MessageUpdate::make();
          pjs::class_of<MessageUpdate>()->assign(m, body);
        }
        break;
      }

      case MessageType::NOTIFICATION: {
        pjs::Ref<MessageNotification> m;
        if (body->is<MessageNotification>()) {
          m = body->as<MessageNotification>();
        } else {
          m = MessageNotification::make();
          pjs::class_of<MessageNotification>()->assign(m, body);
        }
        db.push((char)m->errorCode);
        db.push((char)m->errorSubcode);
        if (auto *data = m->data.get()) db.push(*data, 0);
        break;
      }
      default: break;
    }
    db.flush();
  }

  uint8_t header[19];
  std::memset(header, 0xff, 16);
  auto length = payload_buffer.size() + sizeof(header);
  header[16] = length >> 8;
  header[17] = length >> 0;
  header[18] = int(msg->type);
  data.push(header, sizeof(header), &s_dp);
  data.push(std::move(payload_buffer));
}

//
// BGP::Parser
//

BGP::Parser::Parser()
  : m_payload(Data::make())
{
}

void BGP::Parser::reset() {
  Deframer::reset();
  Deframer::pass_all(true);
  m_payload->clear();
}

void BGP::Parser::parse(Data &data) {
  Deframer::deframe(data);
}

auto BGP::Parser::on_state(int state, int c) -> int {
  return 0;
}

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// BGP
//

template<> void ClassDef<BGP>::init() {
  ctor();

  method("decode", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    ret.set(BGP::decode(*data));
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    pjs::Object *payload;
    if (!ctx.arguments(1, &payload)) return;
    auto *data = pipy::Data::make();
    BGP::encode(payload, *data);
    ret.set(data);
  });
}

//
// BGP::MessageType
//

template<> void EnumDef<BGP::MessageType>::init() {
  define(BGP::MessageType::OPEN, "OPEN");
  define(BGP::MessageType::UPDATE, "UPDATE");
  define(BGP::MessageType::NOTIFICATION, "NOTIFICATION");
  define(BGP::MessageType::KEEPALIVE, "KEEPALIVE");
}

//
// BGP::Message
//

template<> void ClassDef<BGP::Message>::init() {
  accessor(
    "type",
    [](Object *obj, Value &val) { val.set(EnumDef<BGP::MessageType>::name(obj->as<BGP::Message>()->type)); },
    [](Object *obj, const Value &val) {
      auto s = val.to_string();
      auto i = EnumDef<BGP::MessageType>::value(s);
      s->release();
      obj->as<BGP::Message>()->type = int(i) > 0 ? i : BGP::MessageType::KEEPALIVE;
    }
  );

  accessor(
    "body",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::Message>()->body.get()); },
    [](Object *obj, const Value &val) { obj->as<BGP::Message>()->body = val.is_object() ? val.o() : nullptr; }
  );
}

//
// BGP::MessageOpen
//

template<> void ClassDef<BGP::MessageOpen>::init() {
  accessor(
    "version",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageOpen>()->version); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageOpen>()->version = val.to_number(); }
  );

  accessor(
    "myAS",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageOpen>()->myAS); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageOpen>()->myAS = val.to_number(); }
  );

  accessor(
    "holdTime",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageOpen>()->holdTime); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageOpen>()->holdTime = val.to_number(); }
  );

  accessor(
    "identifier",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageOpen>()->identifier); },
    [](Object *obj, const Value &val) { (obj->as<BGP::MessageOpen>()->identifier = val.to_string())->release(); }
  );

  accessor(
    "capabilities",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageOpen>()->capabilities); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageOpen>()->capabilities = val.is_object() ? val.o() : nullptr; }
  );

  accessor(
    "parameters",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageOpen>()->parameters); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageOpen>()->parameters = val.is_object() ? val.o() : nullptr; }
  );
}

//
// BGP::MessageUpdate
//

template<> void ClassDef<BGP::MessageUpdate>::init() {
}

//
// BGP::MessageNotification
//

template<> void ClassDef<BGP::MessageNotification>::init() {
  accessor(
    "errorCode",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageNotification>()->errorCode); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageNotification>()->errorCode = val.to_number(); }
  );

  accessor(
    "errorSubcode",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageNotification>()->errorSubcode); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageNotification>()->errorSubcode = val.to_number(); }
  );

  accessor(
    "data",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageNotification>()->data.get()); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageNotification>()->data = val.is<pipy::Data>() ? val.as<pipy::Data>() : nullptr; }
  );
}

} // namespace pjs
