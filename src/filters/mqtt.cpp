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

#include "mqtt.hpp"

namespace pipy {
namespace mqtt {

static Data::Producer s_dp("MQTT");

//
// PropertyMap
//

class PropertyMap {
public:
  struct Property {
    enum Type {
      INT,
      INT8,
      INT16,
      INT32,
      STR,
      BIN,
    };

    int id = 0;
    Type type;
    pjs::Ref<pjs::Str> name;
  };

  PropertyMap();

  auto by_id(int id) const -> const Property*;
  auto by_name(pjs::Str *name) const -> const Property*;

private:
  std::vector<Property> m_properties;
  std::map<pjs::Str*, int> m_name_map;
};

static const struct {
  int id;
  const char *name;
  PropertyMap::Property::Type type;
}
s_property_info[] = {
  { 1 , "payloadFormatIndicator",           PropertyMap::Property::INT8 },
  { 2 , "messageExpiryInterval",            PropertyMap::Property::INT32 },
  { 3 , "contentType",                      PropertyMap::Property::STR },
  { 8 , "responseTopic",                    PropertyMap::Property::STR },
  { 9 , "correlationData",                  PropertyMap::Property::BIN },
  { 11, "subscriptionIdentifier",           PropertyMap::Property::INT },
  { 17, "sessionExpiryInterval",            PropertyMap::Property::INT32 },
  { 18, "assignedClientIdentifier",         PropertyMap::Property::STR },
  { 19, "serverKeepAlive",                  PropertyMap::Property::INT16 },
  { 21, "authenticationMethod",             PropertyMap::Property::STR },
  { 22, "authenticationData",               PropertyMap::Property::BIN },
  { 23, "requestProblemInfo",               PropertyMap::Property::INT8 },
  { 24, "willDelayInterval",                PropertyMap::Property::INT32 },
  { 25, "requestResponseInfo",              PropertyMap::Property::INT8 },
  { 26, "responseInfo",                     PropertyMap::Property::STR },
  { 28, "serverReference",                  PropertyMap::Property::STR },
  { 31, "reasonString",                     PropertyMap::Property::STR },
  { 33, "receiveMaximum",                   PropertyMap::Property::INT16 },
  { 34, "topicAliasMaximum",                PropertyMap::Property::INT16 },
  { 35, "topicAlias",                       PropertyMap::Property::INT16 },
  { 36, "maximumQoS",                       PropertyMap::Property::INT8 },
  { 37, "retainAvailable",                  PropertyMap::Property::INT8 },
  //38, "User Property",
  { 39, "maximumPacketSize",                PropertyMap::Property::INT32 },
  { 40, "wildcardSubscriptionAvailable",    PropertyMap::Property::INT8 },
  { 41, "subscriptionIdentifierAvailable",  PropertyMap::Property::INT8 },
  { 42, "sharedSubscriptionAvailable",      PropertyMap::Property::INT8 },
};

PropertyMap::PropertyMap() {
  for (int i = 0, n = sizeof(s_property_info) / sizeof(s_property_info[0]); i < n; i++) {
    const auto &p = s_property_info[i];
    if (p.id >= m_properties.size()) m_properties.resize(p.id + 1);
    auto &prop = m_properties[p.id];
    prop.id = p.id;
    prop.type = p.type;
    prop.name = pjs::Str::make(p.name);
    m_name_map[prop.name] = p.id;
  }
}

auto PropertyMap::by_id(int id) const -> const Property* {
  if (id < 1 || id >= m_properties.size()) return nullptr;
  const auto &p = m_properties[id];
  if (p.id) return &p;
  return nullptr;
}

auto PropertyMap::by_name(pjs::Str *name) const -> const Property* {
  auto i = m_name_map.find(name);
  if (i == m_name_map.end()) return nullptr;
  return &m_properties[i->second];
}

thread_local static const PropertyMap s_property_map;

//
// PacketParser
//

class PacketParser {
public:
  PacketParser(MessageHead *head, const Data &data)
    : m_protocol_level(head->protocolLevel)
    , m_head(head)
    , m_payload_data(Data::make(data))
    , m_reader(data) {}

  int protocol_level() const {
    return m_protocol_level;
  }

  auto payload() const -> pjs::Object* {
    return m_payload;
  }

  auto payload_data() const -> Data* {
    return m_payload_data;
  }

  bool decode() {
    switch (m_head->type.get()) {
      case PacketType::CONNECT: {
        int flags = 0;
        if (!read_protocol_name()) return false;
        if (!read_protocol_level()) return false;
        if (!read_connect_flags(flags)) return false;
        if (!read_keep_alive()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        payload_start();
        if (!read_connect_payload(flags)) return false;
        break;
      }
      case PacketType::CONNACK: {
        if (!read_connect_ack_flags()) return false;
        if (!read_reason_code()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        payload_start();
        break;
      }
      case PacketType::PUBLISH: {
        if (!read_topic_name()) return false;
        if (m_head->qos > 0 && !read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        payload_start();
        break;
      }
      case PacketType::PUBACK:
      case PacketType::PUBREC:
      case PacketType::PUBREL:
      case PacketType::PUBCOMP: {
        if (!read_packet_identifier()) return false;
        if (!read_reason_code()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        payload_start();
        break;
      }
      case PacketType::SUBSCRIBE: {
        if (!read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        payload_start();
        if (!read_subscribe_payload()) return false;
        break;
      }
      case PacketType::SUBACK: {
        if (!read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        payload_start();
        if (!read_suback_payload()) return false;
        break;
      }
      case PacketType::UNSUBSCRIBE: {
        if (!read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        payload_start();
        if (!read_unsubscribe_payload()) return false;
        break;
      }
      case PacketType::UNSUBACK: {
        if (!read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        payload_start();
        if (!read_suback_payload()) return false;
        break;
      }
      case PacketType::PINGREQ:
      case PacketType::PINGRESP:
        payload_start();
        break;
      case PacketType::DISCONNECT:
      case PacketType::AUTH: {
        if (!read_reason_code()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        payload_start();
        break;
      }
      default: return false;
    }

    m_payload_data->shift(m_position_payload);
    return true;
  }

private:
  int m_protocol_level;
  pjs::Ref<MessageHead> m_head;
  pjs::Ref<pjs::Object> m_payload;
  pjs::Ref<Data> m_payload_data;
  Data::Reader m_reader;
  int m_position = 0;
  int m_position_payload = 0;

  void payload_start() {
    m_position_payload = m_position;
  }

  auto read() -> int {
    int c = m_reader.get();
    if (c >= 0) m_position++;
    return c;
  }

  bool read(uint8_t &n) {
    int c = read();
    if (c < 0) return false;
    n = c;
    return true;
  }

  bool read(uint16_t &n) {
    auto msb = read(); if (msb < 0) return false;
    auto lsb = read(); if (lsb < 0) return false;
    n = ((msb & 0xff) << 8) | (lsb & 0xff);
    return true;
  }

  bool read(uint32_t &n) {
    uint8_t b[4];
    for (int i = 0; i < 4; i++) {
      int c = read();
      if (c < 0) return false;
      b[i] = c;
    }
    n = (
      ((uint32_t)b[0] << 24) |
      ((uint32_t)b[1] << 16) |
      ((uint32_t)b[2] <<  8) |
      ((uint32_t)b[3] <<  0)
    );
    return true;
  }

  bool read(int &n) {
    n = 0;
    int c, s = 0;
    do {
      c = read();
      if (c < 0) return false;
      n |= (c & 0x7f) << s;
      s += 7;
    } while (c & 0x80);
    return true;
  }

  bool read(pjs::Str* &str) {
    uint16_t len;
    if (!read(len)) return false;
    pjs::vl_array<char, 1000> buf(len);
    for (auto i = 0; i < len; i++) {
      int c = read();
      if (c < 0) return false;
      buf[i] = c;
    }
    str = pjs::Str::make(buf, len);
    return true;
  }

  bool read(Data &data) {
    uint16_t len;
    if (!read(len)) return false;
    auto n = m_reader.read(len, data);
    m_position += n;
    return n == len;
  }

  bool read_protocol_name() {
    if (read() !=  0 ) return false;
    if (read() !=  4 ) return false;
    if (read() != 'M') return false;
    if (read() != 'Q') return false;
    if (read() != 'T') return false;
    if (read() != 'T') return false;
    return true;
  }

  bool read_protocol_level() {
    auto c = read();
    if (c < 0) return false;
    m_protocol_level = c;
    m_head->protocolLevel = c;
    return true;
  }

  bool read_connect_flags(int &flags) {
    auto c = read();
    if (c < 0) return false;
    flags = c;
    return true;
  }

  bool read_keep_alive() {
    uint16_t n;
    if (!read(n)) return false;
    m_head->keepAlive = n;
    return true;
  }

  bool read_connect_ack_flags() {
    auto c = read();
    if (c < 0) return false;
    m_head->sessionPresent = (c & 0x01 ? true : false);
    return true;
  }

  bool read_reason_code() {
    auto c = read();
    m_head->reasonCode = (c < 0 ? 0 : c);
    return true;
  }

  bool read_topic_name() {
    pjs::Str *s;
    if (!read(s)) return false;
    m_head->topicName = s;
    return true;
  }

  bool read_packet_identifier() {
    uint16_t n;
    if (!read(n)) return false;
    m_head->packetIdentifier = n;
    return true;
  }

  bool read_properties(pjs::Object *props = nullptr) {
    int size; if (!read(size)) return false;
    if (!size) return true;
    if (!props) {
      props = pjs::Object::make();
      m_head->properties = props;
    }
    int start = m_position;
    while (m_position - start < size) {
      uint8_t id; if (!read(id)) return false;
      if (id == 38) { // user property
        pjs::Str *k, *v;
        if (!read(k)) return false;
        if (!read(v)) return false;
        props->set(k, v);
      } else {
        auto p = s_property_map.by_id(id);
        if (!p) return false;
        switch (p->type) {
          case PropertyMap::Property::INT: {
            int n;
            if (!read(n)) return false;
            props->set(p->name, n);
            break;
          }
          case PropertyMap::Property::INT8: {
            uint8_t n;
            if (!read(n)) return false;
            props->set(p->name, int(n));
            break;
          }
          case PropertyMap::Property::INT16: {
            uint16_t n;
            if (!read(n)) return false;
            props->set(p->name, int(n));
            break;
          }
          case PropertyMap::Property::INT32: {
            uint32_t n;
            if (!read(n)) return false;
            props->set(p->name, int(n));
            break;
          }
          case PropertyMap::Property::STR: {
            pjs::Str *s;
            if (!read(s)) return false;
            props->set(p->name, s);
            break;
          }
          case PropertyMap::Property::BIN: {
            Data buf;
            if (!read(buf)) return false;
            props->set(p->name, Data::make(buf));
            break;
          }
        }
      }
    }
    return true;
  }

  bool read_connect_payload(int flags) {
    auto payload = ConnectPayload::make();
    m_payload = payload;

    pjs::Str *client_id;
    if (!read(client_id)) return false;
    payload->clientID = client_id;
    payload->cleanStart = bool(flags & 0x02);

    if (flags & 0x04) {
      auto will = Will::make();
      auto props = pjs::Object::make();
      payload->will = will;
      will->qos = int((flags >> 3) & 0x03);
      will->retained = bool(flags & 0x20);
      will->properties = props;
      if (!read_properties(props)) return false;

      pjs::Str *topic;
      if (!read(topic)) return false;
      will->topic = topic;

      Data buf;
      if (!read(buf)) return false;
      will->payload = Data::make(std::move(buf));
    }

    if (flags & 0x40) {
      pjs::Str *username;
      if (!read(username)) return false;
      payload->username = username;
    }

    if (flags & 0x80) {
      Data password;
      if (!read(password)) return false;
      payload->password = Data::make(password);
    }

    return true;
  }

  bool read_subscribe_payload() {
    auto payload = SubscribePayload::make();
    m_payload = payload;

    auto filters = pjs::Array::make();
    payload->topicFilters = filters;

    for (;;) {
      pjs::Str *filter;
      if (!read(filter)) break;

      auto f = TopicFilter::make();
      filters->push(f);
      f->filter = filter;

      uint8_t options;
      if (!read(options)) return false;
      f->qos = int(options & 0x03);
    }

    return true;
  }

  bool read_suback_payload() {
    auto payload = pjs::Array::make();
    m_payload = payload;
    for (;;) {
      uint8_t code;
      if (!read(code)) break;
      payload->push(code);
    }
    return true;
  }

  bool read_unsubscribe_payload() {
    auto payload = pjs::Array::make();
    m_payload = payload;
    for (;;) {
      pjs::Str *filter;
      if (!read(filter)) break;
      payload->push(filter);
    }
    return true;
  }
};

//
// DataBuilder
//

class DataBuilder {
public:
  DataBuilder(Data &buffer)
    : m_db(buffer, &s_dp) {}

  ~DataBuilder() {
    m_db.flush();
  }

  void push(int i) {
    uint8_t buf[4];
    m_db.push(buf, make_var_int(i, buf));
  }

  void push(uint8_t i) {
    m_db.push(i);
  }

  void push(uint16_t i) {
    m_db.push(uint8_t(i >> 8));
    m_db.push(uint8_t(i >> 0));
  }

  void push(uint32_t i) {
    m_db.push(uint8_t(i >> 24));
    m_db.push(uint8_t(i >> 16));
    m_db.push(uint8_t(i >>  8));
    m_db.push(uint8_t(i >>  0));
  }

  void push(const void *p, size_t n) {
    m_db.push(p, n);
  }

  void push(const std::string &s) {
    push(uint16_t(s.length()));
    push(s.c_str(), s.length());
  }

  void push(pjs::Str *s) {
    if (s) {
      push(s->str());
    } else {
      push(uint16_t(0));
    }
  }

  void push(const Data *d) {
    if (d) {
      Data buf(*d);
      if (buf.size() > 0xffff) buf.pop(buf.size() - 0xffff);
      push(uint16_t(d->size()));
      m_db.push(std::move(buf));
    } else {
      push(uint16_t(0));
    }
  }

  void append(const Data &data) {
    m_db.push(data);
  }

  void append(Data &&data) {
    m_db.push(std::move(data));
  }

private:
  Data::Builder m_db;

  static int make_var_int(int n, uint8_t buf[4]) {
    int i = 0;
    do {
      uint8_t b = n & 0x7f;
      n >>= 7;
      if (n && i < 3) b |= 0x80;
      buf[i++] = b;
    } while (n && i < 4);
    return i;
  }
};

//
// PacketBuilder
//

class PacketBuilder {
public:
  void build(Data &out, MessageHead *head, pjs::Object *payload) {
    Data buf;
    DataBuilder db(out);
    auto t = head->type.get();
    switch (t) {
      case PacketType::CONNECT:
        CONNECT(buf, head, pjs::Ref<ConnectPayload>(pjs::coerce<ConnectPayload>(payload)));
        push_fixed_header(db, t, 0, buf.size());
        db.append(buf);
        break;
      case PacketType::CONNACK:
        CONNACK(buf, head);
        push_fixed_header(db, t, 0, buf.size());
        db.append(buf);
        break;
      case PacketType::PUBLISH:
        PUBLISH(buf, head);
        if (payload && payload->is<Data>()) {
          Data data(*payload->as<Data>());
          push_fixed_header(db, t, make_flags(head), buf.size() + data.size());
          db.append(buf);
          db.append(std::move(data));
        } else {
          push_fixed_header(db, t, make_flags(head), buf.size());
          db.append(buf);
        }
        break;
      case PacketType::PUBACK:
        PUBACK(buf, head);
        push_fixed_header(db, t, 0, buf.size());
        db.append(buf);
        break;
      case PacketType::PUBREC:
        PUBACK(buf, head);
        push_fixed_header(db, t, 0, buf.size());
        db.append(buf);
        break;
      case PacketType::PUBREL:
        PUBACK(buf, head);
        push_fixed_header(db, t, 0x02, buf.size());
        db.append(buf);
        break;
      case PacketType::PUBCOMP:
        PUBACK(buf, head);
        push_fixed_header(db, t, 0, buf.size());
        db.append(buf);
        break;
      case PacketType::SUBSCRIBE:
        SUBSCRIBE(buf, head, pjs::Ref<SubscribePayload>(pjs::coerce<SubscribePayload>(payload)));
        push_fixed_header(db, t, 0x02, buf.size());
        db.append(buf);
        break;
      case PacketType::SUBACK:
        SUBACK(buf, head, payload);
        push_fixed_header(db, t, 0, buf.size());
        db.append(buf);
        break;
      case PacketType::UNSUBSCRIBE:
        UNSUBSCRIBE(buf, head, payload);
        push_fixed_header(db, t, 0x02, buf.size());
        db.append(buf);
        break;
      case PacketType::UNSUBACK:
        SUBACK(buf, head, payload);
        push_fixed_header(db, t, 0, buf.size());
        db.append(buf);
        break;
      case PacketType::PINGREQ:
        push_fixed_header(db, t, 0, 0);
        break;
      case PacketType::PINGRESP:
        push_fixed_header(db, t, 0, 0);
        db.append(buf);
        break;
      case PacketType::DISCONNECT:
        DISCONNECT(buf, head);
        push_fixed_header(db, t, 0, buf.size());
        db.append(buf);
        break;
      case PacketType::AUTH:
        AUTH(buf, head);
        push_fixed_header(db, t, 0, buf.size());
        db.append(buf);
        break;
    }
  }

private:
  void CONNECT(Data &out, MessageHead *head, ConnectPayload *payload) {
    DataBuilder db(out);
    int flags = 0;
    pjs::Ref<Will> will = payload->will ? pjs::coerce<Will>(payload->will) : nullptr;
    if (payload->cleanStart) flags |= 0x02;
    if (will) {
      flags |= 0x04;
      flags |= (will->qos & 3) << 3;
      if (will->retained) flags |= 0x20;
    }
    if (payload->username) flags |= 0x80;
    if (payload->password) flags |= 0x40;
    db.push(uint16_t(4));
    db.push("MQTT", 4);
    db.push(uint8_t(head->protocolLevel));
    db.push(uint8_t(flags));
    db.push(uint16_t(head->keepAlive));
    push_properties(db, head);
    db.push(payload->clientID);
    if (will) {
      db.push(will->topic);
      db.push(will->payload);
    }
    if (payload->username) db.push(payload->username);
    if (payload->password) db.push(payload->password);
  }

  void CONNACK(Data &out, MessageHead *head) {
    DataBuilder db(out);
    db.push(uint8_t(head->sessionPresent));
    db.push(uint8_t(head->reasonCode));
    push_properties(db, head);
  }

  void PUBLISH(Data &out, MessageHead *head) {
    DataBuilder db(out);
    db.push(head->topicName);
    if (head->qos > 0) db.push(uint16_t(head->packetIdentifier));
    push_properties(db, head);
  }

  void PUBACK(Data &out, MessageHead *head) {
    DataBuilder db(out);
    db.push(uint16_t(head->packetIdentifier));
    if (head->protocolLevel >= 5) {
      db.push(uint8_t(head->reasonCode));
      push_properties(db, head);
    }
  }

  void SUBSCRIBE(Data &out, MessageHead *head, SubscribePayload *payload) {
    DataBuilder db(out);
    db.push(uint16_t(head->packetIdentifier));
    push_properties(db, head);
    if (auto filters = payload->topicFilters.get()) {
      filters->iterate_all(
        [&](pjs::Value &v, int) {
          pjs::Ref<TopicFilter> f = pjs::coerce<TopicFilter>(v.is_object() ? v.o() : nullptr);
          db.push(f->filter);
          db.push(uint8_t(f->qos & 3));
        }
      );
    }
  }

  void SUBACK(Data &out, MessageHead *head, pjs::Object *payload) {
    DataBuilder db(out);
    db.push(uint16_t(head->packetIdentifier));
    push_properties(db, head);
    push_reason_codes(db, payload);
  }

  void UNSUBSCRIBE(Data &out, MessageHead *head, pjs::Object *payload) {
    DataBuilder db(out);
    db.push(uint16_t(head->packetIdentifier));
    push_properties(db, head);
    if (payload && payload->is_array()) {
      payload->as<pjs::Array>()->iterate_all(
        [&](pjs::Value &v, int) {
          auto *s = v.to_string();
          db.push(s);
          s->release();
        }
      );
    }
  }

  void DISCONNECT(Data &out, MessageHead *head) {
    DataBuilder db(out);
    if (head->protocolLevel >= 5) {
      db.push(uint8_t(head->reasonCode));
      push_properties(db, head);
    }
  }

  void AUTH(Data &out, MessageHead *head) {
    DataBuilder db(out);
    if (head->protocolLevel >= 5) {
      db.push(uint8_t(head->reasonCode));
      push_properties(db, head);
    }
  }

  auto make_flags(MessageHead *head) -> uint8_t {
    uint8_t flags = (head->qos & 3) << 1;
    if (head->dup) flags |= 0x04;
    if (head->retained) flags |= 0x01;
    return flags;
  }

  void push_fixed_header(DataBuilder &db, PacketType type, uint8_t flags, size_t size) {
    db.push(uint8_t((int(type) << 4) | (flags & 0x0f)));
    db.push(int(size));
  }

  void push_properties(DataBuilder &db, MessageHead *head) {
    if (head->protocolLevel >= 5) {
      Data buffer;
      if (auto props = head->properties.get()) {
        DataBuilder db(buffer);
        props->iterate_all(
          [&](pjs::Str *k, pjs::Value &v) {
            if (auto *p = s_property_map.by_name(k)) {
              db.push(p->id);
              switch (p->type) {
                case PropertyMap::Property::INT:
                  db.push(v.to_int32());
                  break;
                case PropertyMap::Property::INT8:
                  db.push(uint8_t(v.to_int32()));
                  break;
                case PropertyMap::Property::INT16:
                  db.push(uint16_t(v.to_int32()));
                  break;
                case PropertyMap::Property::INT32:
                  db.push(uint32_t(v.to_int32()));
                  break;
                case PropertyMap::Property::STR: {
                  auto s = v.to_string();
                  db.push(s);
                  s->release();
                  break;
                }
                case PropertyMap::Property::BIN:
                  db.push(v.is_instance_of<Data>() ? v.as<Data>() : nullptr);
                  break;
                default: break;
              }
            } else {
              auto s = v.to_string();
              db.push(int(38)); // user property
              db.push(k);
              db.push(s);
              s->release();
            }
          }
        );
      }
      db.push(int(buffer.size()));
      db.append(buffer);
    }
  }

  void push_reason_codes(DataBuilder &db, pjs::Object *payload) {
    if (payload && payload->is_array()) {
      payload->as<pjs::Array>()->iterate_all(
        [&](pjs::Value &v, int) {
          db.push(uint8_t(v.to_int32()));
        }
      );
    }
  }
};

//
// Decoder
//

Decoder::Decoder()
{
}

Decoder::Decoder(const Decoder &r)
  : Decoder()
{
}

Decoder::~Decoder()
{
}

void Decoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "decodeMQTT";
}

auto Decoder::clone() -> Filter* {
  return new Decoder(*this);
}

void Decoder::reset() {
  Filter::reset();
  Deframer::reset();
  m_buffer = nullptr;
}

void Decoder::process(Event *evt) {
  if (auto *data = evt->as<Data>()) {
    Deframer::deframe(*data);
  } else if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  }
}

auto Decoder::on_state(int state, int c) -> int {
  switch (state) {
    case FIXED_HEADER: {
      auto type = (c >> 4);
      if (type < 1 || type > 15) return ERROR;
      m_fixed_header = c;
      m_remaining_length = 0;
      m_remaining_length_shift = 0;
      return REMAINING_LENGTH;
    }
    case REMAINING_LENGTH:
      m_remaining_length |= (c & 0x7f) << m_remaining_length_shift;
      m_remaining_length_shift += 7;
      if (c & 0x80) return REMAINING_LENGTH;
      if (!m_remaining_length) {
        auto type = PacketType(m_fixed_header >> 4);
        if (type != PacketType::PINGREQ && type != PacketType::PINGRESP) return ERROR;
        m_buffer = Data::make();
        message();
        return FIXED_HEADER;
      } else {
        m_buffer = Data::make();
        Deframer::read(m_remaining_length, m_buffer);
        return REMAINING_DATA;
      }
    case REMAINING_DATA:
      message();
      return FIXED_HEADER;
    default: return ERROR;
  }
}

void Decoder::on_pass(Data &data) {
  Filter::output(Data::make(std::move(data)));
}

void Decoder::message() {
  auto type = PacketType(m_fixed_header >> 4);
  auto head = MessageHead::make();
  head->type = type;
  head->qos = (m_fixed_header >> 1) & 3;
  head->dup = bool(m_fixed_header & 0x08);
  head->retained = bool(m_fixed_header & 1);

  PacketParser parser(head, *m_buffer);
  if (parser.decode()) {
    output(MessageStart::make(head));
    if (!parser.payload_data()->empty()) {
      output(parser.payload_data());
    }
    if (auto payload = parser.payload()) {
      output(MessageEnd::make(nullptr, payload));
    } else {
      output(MessageEnd::make());
    }
  }

  m_buffer = nullptr;
}

//
// Encoder
//

Encoder::Encoder()
{
}

Encoder::Encoder(const Encoder &r)
  : Encoder()
{
}

Encoder::~Encoder()
{
}

void Encoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "encodeMQTT";
}

auto Encoder::clone() -> Filter* {
  return new Encoder(*this);
}

void Encoder::reset() {
  Filter::reset();
  m_head = nullptr;
  m_buffer = nullptr;
}

void Encoder::process(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_head) {
      m_head = pjs::coerce<MessageHead>(start->head());
      if (m_head->type.get() == PacketType::PUBLISH) {
        m_buffer = Data::make();
      }
      Filter::output(evt);
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_buffer) {
      m_buffer->push(*data);
    }

  } else if (auto end = evt->as<MessageEnd>()) {
    if (m_head) {
      pjs::Object *payload = nullptr;
      if (m_head->type == PacketType::PUBLISH) {
        payload = m_buffer.get();
      } else if (end->payload().is_object()) {
        payload = end->payload().o();
      }
      Data buf;
      PacketBuilder pb;
      pb.build(buf, m_head, payload);
      Filter::output(Data::make(std::move(buf)));
      Filter::output(evt);
      m_head = nullptr;
    }

  } else if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  }
}

} // namespace mqtt
} // namespace pipy

namespace pjs {

using namespace pipy;
using namespace pipy::mqtt;

//
// PacketType
//

template<> void EnumDef<PacketType>::init() {
  define(PacketType::CONNECT    , "CONNECT");
  define(PacketType::CONNACK    , "CONNACK");
  define(PacketType::PUBLISH    , "PUBLISH");
  define(PacketType::PUBACK     , "PUBACK");
  define(PacketType::PUBREC     , "PUBREC");
  define(PacketType::PUBREL     , "PUBREL");
  define(PacketType::PUBCOMP    , "PUBCOMP");
  define(PacketType::SUBSCRIBE  , "SUBSCRIBE");
  define(PacketType::SUBACK     , "SUBACK");
  define(PacketType::UNSUBSCRIBE, "UNSUBSCRIBE");
  define(PacketType::UNSUBACK   , "UNSUBACK");
  define(PacketType::PINGREQ    , "PINGREQ");
  define(PacketType::PINGRESP   , "PINGRESP");
  define(PacketType::DISCONNECT , "DISCONNECT");
  define(PacketType::AUTH       , "AUTH");
}

template<> void ClassDef<MessageHead>::init() {
  field<EnumValue<PacketType>>("type", [](MessageHead *obj) { return &obj->type; });
  field<bool>("dup", [](MessageHead *obj) { return &obj->dup; });
  field<bool>("retain", [](MessageHead *obj) { return &obj->retained; });
  field<bool>("sessionPresent", [](MessageHead *obj) { return &obj->sessionPresent; });
  field<int>("qos", [](MessageHead *obj) { return &obj->qos; });
  field<int>("packetIdentifier", [](MessageHead *obj) { return &obj->packetIdentifier; });
  field<int>("protocolLevel", [](MessageHead *obj) { return &obj->protocolLevel; });
  field<int>("keepAlive", [](MessageHead *obj) { return &obj->keepAlive; });
  field<int>("reasonCode", [](MessageHead *obj) { return &obj->reasonCode; });
  field<Ref<Str>>("topicName", [](MessageHead *obj) { return &obj->topicName; });
  field<Ref<Object>>("properties", [](MessageHead *obj) { return &obj->properties; });
}

template<> void ClassDef<Will>::init() {
  field<int>("qos", [](Will *obj) { return &obj->qos; });
  field<bool>("retain", [](Will *obj) { return &obj->retained; });
  field<Ref<Object>>("properties", [](Will *obj) { return &obj->properties; });
  field<Ref<Str>>("topic", [](Will *obj) { return &obj->topic; });
  field<Ref<pipy::Data>>("payload", [](Will *obj) { return &obj->payload; });
}

template<> void ClassDef<ConnectPayload>::init() {
  field<Ref<Str>>("clientID", [](ConnectPayload *obj) { return &obj->clientID; });
  field<Ref<Str>>("username", [](ConnectPayload *obj) { return &obj->username; });
  field<Ref<pipy::Data>>("password", [](ConnectPayload *obj) { return &obj->password; });
  field<Ref<Will>>("will", [](ConnectPayload *obj) { return &obj->will; });
  field<bool>("cleanStart", [](ConnectPayload *obj) { return &obj->cleanStart; });
}

template<> void ClassDef<TopicFilter>::init() {
  field<Ref<Str>>("filter", [](TopicFilter *obj) { return &obj->filter; });
  field<int>("qos", [](TopicFilter *obj) { return &obj->qos; });
}

template<> void ClassDef<SubscribePayload>::init() {
  field<Ref<Array>>("topicFilters", [](SubscribePayload *obj) { return &obj->topicFilters; });
}

} // namespace pjs
