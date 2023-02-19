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
#include "api/netmask.hpp"
#include "utils.hpp"

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

inline static void write_address_prefix(Data::Builder &db, const pjs::Value &addr) {
  uint8_t ip[4] = { 0 };
  int mask = 0;
  if (addr.is_string()) {
    utils::get_cidr(addr.s()->str(), ip, mask);
  } else if (addr.is<Netmask>()) {
    auto nm = addr.as<Netmask>();
    if (nm->decompose_v4(ip)) {
      mask = nm->bitmask();
    }
  }
  int n = (mask + 7) / 8;
  int t = (n * 8 - mask);
  if (t > 0) ip[n-1] &= uint8_t(0xff << t);
  db.push(uint8_t(mask));
  db.push(ip, n);
}

inline static void write_path_attribute_value(
  Data::Builder &db,
  BGP::PathAttribute::TypeCode type_code,
  const pjs::Value &value
) {
  switch (type_code) {
    case BGP::PathAttribute::TypeCode::ORIGIN:
      db.push(int(value.to_number()));
      break;
    case BGP::PathAttribute::TypeCode::AS_PATH:
      if (value.is_array()) {
        auto *a = value.as<pjs::Array>();
        a->iterate_all(
          [&](pjs::Value &v, int) {
            db.push(v.is_array() ? 2 : 1);
            pjs::Ref<pjs::Array> a;
            if (v.is_array()) {
              a = v.as<pjs::Array>();
            } else if (v.is_object() && v.o()) {
              a = pjs::Object::keys(v.o());
            }
            auto n = std::min((a ? a->length() : 0), 0xff);
            db.push(n);
            for (int i = 0; i < n; i++) {
              int as = a->data()->at(i).to_number();
              db.push(uint8_t(as >> 8));
              db.push(uint8_t(as >> 0));
            }
          }
        );
      }
      break;
    case BGP::PathAttribute::TypeCode::NEXT_HOP: {
      uint8_t ip[4] = { 0 };
      auto *s = value.to_string();
      utils::get_ip_v4(s->str(), ip);
      s->release();
      db.push(ip, sizeof(ip));
      break;
    }
    case BGP::PathAttribute::TypeCode::MULTI_EXIT_DISC:
    case BGP::PathAttribute::TypeCode::LOCAL_PREF: {
      unsigned int n = value.to_number();
      db.push(uint8_t(n >> 24));
      db.push(uint8_t(n >> 16));
      db.push(uint8_t(n >>  8));
      db.push(uint8_t(n >>  0));
      break;
    }
    case BGP::PathAttribute::TypeCode::ATOMIC_AGGREGATE: {
      // zero-length attribute
      break;
    }
    case BGP::PathAttribute::TypeCode::AGGREGATOR: {
      uint16_t as = 0;
      uint8_t ip[4] = { 0 };
      if (value.is_array()) {
        auto *a = value.as<pjs::Array>();
        if (a->length() > 0) as = a->data()->at(0).to_number();
        if (a->length() > 1) {
          auto *s = a->data()->at(1).to_string();
          utils::get_ip_v4(s->str(), ip);
          s->release();
        }
      }
      db.push(uint8_t(as >> 8));
      db.push(uint8_t(as >> 0));
      db.push(ip, sizeof(ip));
      break;
    }
    default: break;
  }
}

inline static auto ipv4_to_str(const uint8_t ip[]) -> pjs::Str* {
  char str[100];
  auto len = std::snprintf(str, sizeof(str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return pjs::Str::make(str, len);
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
        db.push(uint8_t(m->version));
        db.push(uint8_t(m->myAS >> 8));
        db.push(uint8_t(m->myAS >> 0));
        db.push(uint8_t(m->holdTime >> 8));
        db.push(uint8_t(m->holdTime >> 0));
        db.push(ip, sizeof(ip));
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
                  clamp_data_size(data, 0xff);
                  db3.push(uint8_t(id));
                  db3.push(uint8_t(data.size()));
                  db3.push(data, 0);
                } else {
                  db3.push(uint8_t(id));
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
            clamp_data_size(caps_buffer, 0xff);
            db2.push(0x02);
            db2.push(uint8_t(caps_buffer.size()));
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
                  clamp_data_size(data, 0xff);
                  db2.push(uint8_t(id));
                  db2.push(uint8_t(data.size()));
                  db2.push(data, 0);
                }
              }
            }
          );
        }
        db2.flush();
        clamp_data_size(param_buffer, 0xff);
        db.push(uint8_t(param_buffer.size()));
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
        Data withdrawn, path_addr;
        if (auto *a = m->withdrawnRoutes.get()) {
          Data::Builder db(withdrawn, &s_dp);
          a->iterate_all(
            [&](pjs::Value &v, int) {
              write_address_prefix(db, v);
            }
          );
          db.flush();
        }
        if (auto *a = m->pathAttributes.get()) {
          Data::Builder db(path_addr, &s_dp);
          a->iterate_all(
            [&](pjs::Value &v, int) {
              pjs::Ref<PathAttribute> pa;
              if (v.is<PathAttribute>()) {
                pa = v.as<PathAttribute>();
              } else {
                pa = PathAttribute::make();
                if (v.is_object()) pjs::class_of<PathAttribute>()->assign(pa, v.o());
              }
              int type_code = pa->code;
              if (auto *s = pa->name.get()) {
                int i = int(pjs::EnumDef<PathAttribute::TypeCode>::value(s));
                if (i >= 0) type_code = i;
              }
              Data buf;
              if (pa->value.is<Data>()) {
                buf.push(*pa->value.as<Data>());
              } else {
                Data::Builder db(buf, &s_dp);
                write_path_attribute_value(db, PathAttribute::TypeCode(type_code), pa->value);
                db.flush();
              }
              clamp_data_size(buf, 0xffff);
              uint8_t flags = 0;
              if (pa->optional) flags |= 0x80;
              if (pa->transitive) flags |= 0x40;
              if (pa->partial) flags |= 0x20;
              if (buf.size() > 0xff) {
                db.push(uint8_t(flags | 0x10));
                db.push(uint8_t(type_code));
                db.push(uint8_t(buf.size() >> 8));
                db.push(uint8_t(buf.size() >> 0));
              } else {
                db.push(flags);
                db.push(uint8_t(type_code));
                db.push(uint8_t(buf.size()));
              }
              db.push(buf, 0);
            }
          );
          db.flush();
        }
        clamp_data_size(withdrawn, 0xffff);
        clamp_data_size(path_addr, 0xffff);
        db.push(uint8_t(withdrawn.size() >> 8));
        db.push(uint8_t(withdrawn.size() >> 0));
        db.push(withdrawn, 0);
        db.push(uint8_t(path_addr.size() >> 8));
        db.push(uint8_t(path_addr.size() >> 0));
        db.push(path_addr, 0);
        if (auto *a = m->pathAttributes.get()) {
          a->iterate_all(
            [&](pjs::Value &v, int) {
              write_address_prefix(db, v);
            }
          );
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
        db.push(uint8_t(m->errorCode));
        db.push(uint8_t(m->errorSubcode));
        if (auto *data = m->data.get()) db.push(*data, 0);
        break;
      }
      default: break;
    }
    db.flush();
  }

  uint8_t header[19];
  std::memset(header, 0xff, 16);
  clamp_data_size(payload_buffer, 4096 - sizeof(header));
  auto length = payload_buffer.size() + sizeof(header);
  header[16] = length >> 8;
  header[17] = length >> 0;
  header[18] = uint8_t(msg->type);
  data.push(header, sizeof(header), &s_dp);
  data.push(std::move(payload_buffer));
}

//
// BGP::Parser
//

BGP::Parser::Parser()
  : m_body(Data::make())
{
}

void BGP::Parser::reset() {
  Deframer::reset();
  Deframer::pass_all(true);
  m_body->clear();
  m_message = nullptr;
}

void BGP::Parser::parse(Data &data) {
  Deframer::deframe(data);
}

auto BGP::Parser::on_state(int state, int c) -> int {
  switch (state) {
    case START:
      on_message_start();
      Deframer::read(sizeof(m_header) - 1, m_header + 1);
      return HEADER;
    case HEADER: {
      uint16_t size =
        (uint16_t(m_header[16]) << 8)|
        (uint16_t(m_header[17]) << 0);
      m_message = Message::make();
      m_message->type = MessageType(m_header[18]);
      switch (m_message->type) {
        case MessageType::OPEN:
          m_message->body = MessageOpen::make();
          break;
        case MessageType::UPDATE:
          m_message->body = MessageUpdate::make();
          break;
        case MessageType::NOTIFICATION:
          m_message->body = MessageNotification::make();
          break;
        case MessageType::KEEPALIVE:
          break;
        default: error(0, 0); return ERROR;
      }
      if (size > 0) {
        m_body->clear();
        Deframer::read(size, m_body);
        return BODY;
      } else {
        on_message_end(m_message);
      }
    }
    case BODY: {
      Data::Reader r(*m_body);
      bool parse_ok = false;
      switch (m_message->type) {
        case MessageType::OPEN:
          parse_ok = parse_open(r);
          break;
        case MessageType::UPDATE:
          parse_ok = parse_update(r);
          break;
        case MessageType::NOTIFICATION:
          parse_ok = parse_notification(r);
          break;
        default: break;
      }
      if (parse_ok) {
        on_message_end(m_message);
        m_message = nullptr;
        return START;
      } else {
        return ERROR;
      }
    }
    case ERROR: break;
  }
  return ERROR;
}

bool BGP::Parser::parse_open(Data::Reader &r) {
  auto *body = m_message->body->as<MessageOpen>();

  uint8_t version;
  uint16_t my_as;
  uint16_t hold_time;
  uint8_t identifier[4];
  uint8_t param_size;

  if (!read(r, version)) return false;
  if (!read(r, my_as)) return false;
  if (!read(r, hold_time)) return false;
  if (!read(r, identifier, sizeof(identifier))) return false;
  if (!read(r, param_size)) return false;

  Data params;
  if (!read(r, params, param_size)) return false;

  Data::Reader r2(params);
  while (!r2.eof()) {
    pjs::Value value;
    uint8_t type;
    uint8_t size;
    if (!read(r2, type)) return false;
    if (!read(r2, size)) return false;
    switch (type) {
      case 2: { // capabilities
        auto *caps = pjs::Object::make();
        value.set(caps);
        while (!r2.eof()) {
          uint8_t code;
          uint8_t size;
          Data data;
          if (!read(r2, code)) return false;
          if (!read(r2, size)) return false;
          if (!read(r2, data, size)) return false;
          pjs::Ref<pjs::Str> k(pjs::Str::make(int(code)));
          caps->set(k, data.empty() ? nullptr : Data::make(std::move(data)));
        }
        break;
      }
      default: {
        Data data;
        if (!read(r2, data, size)) return false;
        value.set(data.empty() ? nullptr : Data::make(std::move(data)));
        break;
      }
    }
    auto params = body->parameters.get();
    if (!params) body->parameters = params = pjs::Object::make();
    pjs::Ref<pjs::Str> k(pjs::Str::make(int(type)));
    pjs::Value old;
    if (params->get(k, old)) {
      if (old.is_array()) {
        old.as<pjs::Array>()->push(value);
      } else {
        auto a = pjs::Array::make(2);
        a->set(0, old);
        a->set(1, value);
        params->set(k, a);
      }
    } else {
      params->set(k, value);
    }
  }

  body->version = version;
  body->myAS = my_as;
  body->holdTime = hold_time;
  body->identifier = ipv4_to_str(identifier);

  return true;
}

bool BGP::Parser::parse_update(Data::Reader &r) {
  auto *body = m_message->body->as<MessageUpdate>();

  uint16_t withdrawn_size; Data withdrawn_data;
  uint16_t path_addr_size; Data path_addr_data;

  if (!read(r, withdrawn_size)) return false;
  if (!read(r, withdrawn_data, withdrawn_size)) return false;
  if (!read(r, path_addr_size)) return false;
  if (!read(r, path_addr_data, path_addr_size)) return false;

  if (!withdrawn_data.empty()) {
    Data::Reader r(withdrawn_data);
    while (!r.eof()) {
      auto nm = read_address_prefix(r);
      if (!nm) return false;
      if (!body->withdrawnRoutes) body->withdrawnRoutes = pjs::Array::make();
      body->withdrawnRoutes->push(nm);
    }
  }

  if (!path_addr_data.empty()) {
    Data::Reader r(path_addr_data);
    while (!r.eof()) {
      auto pa = read_path_attribute(r);
      if (!pa) return false;
      if (!body->pathAttributes) body->pathAttributes = pjs::Array::make();
      body->pathAttributes->push(pa);
    }
  }

  while (!r.eof()) {
    auto nm = read_address_prefix(r);
    if (!nm) return false;
    if (!body->destinations) body->destinations = pjs::Array::make();
    body->destinations->push(nm);
  }

  return true;
}

bool BGP::Parser::parse_notification(Data::Reader &r) {
  auto *body = m_message->body->as<MessageNotification>();

  uint8_t code;
  uint8_t subcode;
  Data data;

  if (!read(r, code)) return false;
  if (!read(r, subcode)) return false;
  r.read(data);

  body->errorCode = code;
  body->errorSubcode = subcode;
  if (!data.empty()) body->data = Data::make(std::move(data));

  return true;
}

bool BGP::Parser::error(int code, int subcode) {
  pjs::Ref<MessageNotification> msg(MessageNotification::make(code, subcode));
  on_message_error(msg);
  return false;
}

bool BGP::Parser::read(Data::Reader &r, Data &data, size_t size) {
  return r.read(size, data) == size;
}

bool BGP::Parser::read(Data::Reader &r, uint8_t *data, size_t size) {
  return r.read(size, data) == size;
}

bool BGP::Parser::read(Data::Reader &r, uint8_t &data) {
  auto b = r.get();
  if (b < 0) return false;
  data = uint8_t(b);
  return true;
}

bool BGP::Parser::read(Data::Reader &r, uint16_t &data) {
  auto b0 = r.get(); if (b0 < 0) return false;
  auto b1 = r.get(); if (b1 < 0) return false;
  data = (uint16_t(b0) << 8)|
         (uint16_t(b1) << 0);
  return true;
}

bool BGP::Parser::read(Data::Reader &r, uint32_t &data) {
  auto b0 = r.get(); if (b0 < 0) return false;
  auto b1 = r.get(); if (b1 < 0) return false;
  auto b2 = r.get(); if (b2 < 0) return false;
  auto b3 = r.get(); if (b3 < 0) return false;
  data = (uint32_t(b0) << 24)|
         (uint32_t(b1) << 16)|
         (uint32_t(b2) <<  8)|
         (uint32_t(b3) <<  0);
  return true;
}

auto BGP::Parser::read_address_prefix(Data::Reader &r) -> Netmask* {
  uint8_t mask, ip[4];
  if (!read(r, mask)) return nullptr;
  if (mask > 32) return nullptr;
  int n = (mask + 7) / 8;
  for (int i = 0; i < 4; i++) {
    if (i < n) {
      if (!read(r, ip[i])) return nullptr;
    } else {
      ip[i] = 0;
    }
  }
  return Netmask::make(mask, ip);
}

auto BGP::Parser::read_path_attribute(Data::Reader &r) -> PathAttribute* {
  uint8_t flags, type_code;
  if (!read(r, flags)) return nullptr;
  if (!read(r, type_code)) return nullptr;

  Data data;
  if (flags & 0x10) {
    uint16_t size;
    if (!read(r, size)) return nullptr;
    if (!read(r, data, size)) return nullptr;
  } else {
    uint8_t size;
    if (!read(r, size)) return nullptr;
    if (!read(r, data, size)) return nullptr;
  }

  pjs::Value value;
  auto type = PathAttribute::TypeCode(type_code);
  if (!data.empty()) {
    Data::Reader r(data);
    switch (type) {
      case PathAttribute::TypeCode::ORIGIN: {
        uint8_t origin;
        if (!read(r, origin)) return nullptr;
        value.set(origin);
        break;
      }
      case PathAttribute::TypeCode::AS_PATH: {
        auto *segs = pjs::Array::make();
        value.set(segs);
        while (!r.eof()) {
          uint8_t type, size;
          if (!read(r, type)) return nullptr;
          if (!read(r, size)) return nullptr;
          if (type == 1) {
            auto *o = pjs::Object::make();
            segs->push(o);
            for (int i = 0; i < size; i++) {
              uint16_t as;
              if (!read(r, as)) return nullptr;
              o->set(pjs::Str::make(int(as)), as);
            }
          } else if (type == 2) {
            auto *a = pjs::Array::make(size);
            segs->push(a);
            for (int i = 0; i < size; i++) {
              uint16_t as;
              if (!read(r, as)) return nullptr;
              a->set(i, as);
            }
          } else {
            return nullptr;
          }
        }
        break;
      }
      case PathAttribute::TypeCode::NEXT_HOP: {
        uint8_t ip[4];
        if (!read(r, ip, sizeof(ip))) return nullptr;
        value.set(ipv4_to_str(ip));
        break;
      }
      case PathAttribute::TypeCode::MULTI_EXIT_DISC:
      case PathAttribute::TypeCode::LOCAL_PREF: {
        uint32_t num;
        if (!read(r, num)) return nullptr;
        value.set(num);
        break;
      }
      case PathAttribute::TypeCode::ATOMIC_AGGREGATE:
        break;
      case PathAttribute::TypeCode::AGGREGATOR: {
        uint16_t as;
        uint8_t ip[4];
        if (!read(r, as)) return nullptr;
        if (!read(r, ip, 4)) return nullptr;
        auto *a = pjs::Array::make(2);
        a->set(0, as);
        a->set(1, ipv4_to_str(ip));
        value.set(a);
        break;
      }
      default: value.set(Data::make(std::move(data))); break;
    }
  }

  auto *pa = PathAttribute::make();
  if (auto *k = pjs::EnumDef<PathAttribute::TypeCode>::name(type)) {
    pa->name = k;
  }

  pa->value = value;
  pa->code = type_code;
  pa->optional = flags & 0x80;
  pa->transitive = flags & 0x40;
  pa->partial = flags & 0x20;

  return pa;
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
// BGP::PathAttribute::TypeCode
//

template<> void EnumDef<BGP::PathAttribute::TypeCode>::init() {
  define(BGP::PathAttribute::TypeCode::ORIGIN, "ORIGIN");
  define(BGP::PathAttribute::TypeCode::AS_PATH, "AS_PATH");
  define(BGP::PathAttribute::TypeCode::NEXT_HOP, "NEXT_HOP");
  define(BGP::PathAttribute::TypeCode::MULTI_EXIT_DISC, "MULTI_EXIT_DISC");
  define(BGP::PathAttribute::TypeCode::LOCAL_PREF, "LOCAL_PREF");
  define(BGP::PathAttribute::TypeCode::ATOMIC_AGGREGATE, "ATOMIC_AGGREGATE");
  define(BGP::PathAttribute::TypeCode::AGGREGATOR, "AGGREGATOR");
}

//
// BGP::PathAttribute
//

template<> void ClassDef<BGP::PathAttribute>::init() {
  accessor(
    "name",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::PathAttribute>()->name); },
    [](Object *obj, const Value &val) { (obj->as<BGP::PathAttribute>()->name = val.to_string())->release(); }
  );

  accessor(
    "value",
    [](Object *obj, Value &val) { val = obj->as<BGP::PathAttribute>()->value; },
    [](Object *obj, const Value &val) { obj->as<BGP::PathAttribute>()->value = val; }
  );

  accessor(
    "code",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::PathAttribute>()->code); },
    [](Object *obj, const Value &val) { obj->as<BGP::PathAttribute>()->code = val.to_number(); }
  );

  accessor(
    "optional",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::PathAttribute>()->optional); },
    [](Object *obj, const Value &val) { obj->as<BGP::PathAttribute>()->optional = val.to_boolean(); }
  );

  accessor(
    "transitive",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::PathAttribute>()->transitive); },
    [](Object *obj, const Value &val) { obj->as<BGP::PathAttribute>()->transitive = val.to_boolean(); }
  );

  accessor(
    "partial",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::PathAttribute>()->partial); },
    [](Object *obj, const Value &val) { obj->as<BGP::PathAttribute>()->partial = val.to_boolean(); }
  );
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
  accessor(
    "withdrawnRoutes",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageUpdate>()->withdrawnRoutes); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageUpdate>()->withdrawnRoutes = val.is_array() ? val.as<Array>() : nullptr; }
  );

  accessor(
    "pathAttributes",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageUpdate>()->withdrawnRoutes); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageUpdate>()->withdrawnRoutes = val.is_array() ? val.as<Array>() : nullptr; }
  );

  accessor(
    "destinations",
    [](Object *obj, Value &val) { val.set(obj->as<BGP::MessageUpdate>()->withdrawnRoutes); },
    [](Object *obj, const Value &val) { obj->as<BGP::MessageUpdate>()->withdrawnRoutes = val.is_array() ? val.as<Array>() : nullptr; }
  );
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
