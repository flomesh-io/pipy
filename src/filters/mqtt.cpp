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
#include "logging.hpp"

namespace pipy {
namespace mqtt {

static pjs::ConstStr STR_MQTT("MQTT");
static pjs::ConstStr STR_cleanStart("cleanStart");
static pjs::ConstStr STR_clientID("clientID");
static pjs::ConstStr STR_filter("filter");
static pjs::ConstStr STR_keepAlive("keepAlive");
static pjs::ConstStr STR_packetIdentifier("packetIdentifier");
static pjs::ConstStr STR_password("password");
static pjs::ConstStr STR_payload("payload");
static pjs::ConstStr STR_properties("properties");
static pjs::ConstStr STR_protocolLevel("protocolLevel");
static pjs::ConstStr STR_qos("qos");
static pjs::ConstStr STR_reasonCode("reasonCode");
static pjs::ConstStr STR_reasonCodes("reasonCodes");
static pjs::ConstStr STR_retain("retain");
static pjs::ConstStr STR_sessionPresent("sessionPresent");
static pjs::ConstStr STR_topic("topic");
static pjs::ConstStr STR_topicFilters("topicFilters");
static pjs::ConstStr STR_topicName("topicName");
static pjs::ConstStr STR_username("username");
static pjs::ConstStr STR_will("will");

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

  auto by_id(int id) -> const Property*;
  auto by_name(pjs::Str *name) -> const Property*;

private:
  std::vector<Property> m_properties;
  std::map<pjs::Str*, int> m_name_map;
};

static struct {
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

auto PropertyMap::by_id(int id) -> const Property* {
  if (id < 1 || id >= m_properties.size()) return nullptr;
  const auto &p = m_properties[id];
  if (p.id) return &p;
  return nullptr;
}

auto PropertyMap::by_name(pjs::Str *name) -> const Property* {
  auto i = m_name_map.find(name);
  if (i == m_name_map.end()) return nullptr;
  return &m_properties[i->second];
}

static PropertyMap s_property_map;

//
// PacketParser
//

class PacketParser {
public:
  PacketParser(PacketType type, int protocol_level, MessageHead *packet, const Data &data)
    : m_type(type)
    , m_protocol_level(protocol_level)
    , m_packet(packet)
    , m_reader(data) {}

  int protocol_level() const {
    return m_protocol_level;
  }

  int position() const {
    return m_position;
  }

  bool decode() {
    switch (m_type) {
      case PacketType::CONNECT: {
        int flags = 0;
        if (!read_protocol_name()) return false;
        if (!read_protocol_level()) return false;
        if (!read_connect_flags(flags)) return false;
        if (!read_keep_alive()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        if (!read_connect_payload(flags)) return false;
        break;
      }
      case PacketType::CONNACK: {
        if (!read_connect_ack_flags()) return false;
        if (!read_reason_code()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        break;
      }
      case PacketType::PUBLISH: {
        if (!read_topic_name()) return false;
        if (m_packet->qos() > 0 && !read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        break;
      }
      case PacketType::PUBACK:
      case PacketType::PUBREC:
      case PacketType::PUBREL:
      case PacketType::PUBCOMP: {
        if (!read_packet_identifier()) return false;
        if (!read_reason_code()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        break;
      }
      case PacketType::SUBSCRIBE: {
        if (!read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        if (!read_subscribe_payload()) return false;
        break;
      }
      case PacketType::SUBACK: {
        if (!read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        if (!read_reason_codes()) return false;
        break;
      }
      case PacketType::UNSUBSCRIBE: {
        if (!read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        if (!read_unsubscribe_payload()) return false;
        break;
      }
      case PacketType::UNSUBACK: {
        if (!read_packet_identifier()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        if (!read_reason_codes()) return false;
        break;
      }
      case PacketType::PINGREQ:
      case PacketType::PINGRESP:
        break;
      case PacketType::DISCONNECT:
      case PacketType::AUTH: {
        if (!read_reason_code()) return false;
        if (m_protocol_level >= 5 && !read_properties()) return false;
        break;
      }
      default: return false;
    }
    return true;
  }

private:
  PacketType m_type;
  int m_protocol_level;
  MessageHead* m_packet;
  Data::Reader m_reader;
  int m_position = 0;

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
      ((uint32_t)b[3] << 24) |
      ((uint32_t)b[2] << 16) |
      ((uint32_t)b[1] <<  8) |
      ((uint32_t)b[0] <<  0)
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
    char buf[len];
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
    m_packet->set(STR_protocolLevel, int(c));
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
    m_packet->set(STR_keepAlive, int(n));
    return true;
  }

  bool read_connect_ack_flags() {
    auto c = read();
    if (c < 0) return false;
    m_packet->set(STR_sessionPresent, (c & 0x01 ? true : false));
    return true;
  }

  bool read_reason_code() {
    auto c = read();
    if (c < 0) {
      m_packet->set(STR_reasonCode, 0);
    } else {
      m_packet->set(STR_reasonCode, c);
    }
    return true;
  }

  bool read_topic_name() {
    pjs::Str *s;
    if (!read(s)) return false;
    m_packet->set(STR_topicName, s);
    return true;
  }

  bool read_packet_identifier() {
    uint16_t n;
    if (!read(n)) return false;
    m_packet->set(STR_packetIdentifier, n);
    return true;
  }

  bool read_properties(pjs::Object *props = nullptr) {
    int size; if (!read(size)) return false;
    if (!size) return true;
    if (!props) {
      props = pjs::Object::make();
      m_packet->set(STR_properties, props);
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
    pjs::Str *client_id;
    if (!read(client_id)) return false;
    m_packet->set(STR_clientID, client_id);
    m_packet->set(STR_cleanStart, bool(flags & 0x02));

    if (flags & 0x04) {
      pjs::Object *will = pjs::Object::make();
      pjs::Object *props = pjs::Object::make();
      m_packet->set(STR_will, will);
      will->set(STR_qos, int((flags >> 3) & 0x03));
      will->set(STR_retain, bool(flags & 0x20));
      will->set(STR_properties, will);
      if (!read_properties(props)) return false;

      pjs::Str *topic;
      if (!read(topic)) return false;
      will->set(STR_topic, topic);

      Data payload;
      if (!read(payload)) return false;
      will->set(STR_payload, Data::make(payload));
    }

    if (flags & 0x40) {
      pjs::Str *username;
      if (!read(username)) return false;
      m_packet->set(STR_username, username);
    }

    if (flags & 0x80) {
      Data password;
      if (!read(password)) return false;
      m_packet->set(STR_password, Data::make(password));
    }

    return true;
  }

  bool read_subscribe_payload() {
    auto filters = pjs::Array::make();
    m_packet->set(STR_topicFilters, filters);
    for (;;) {
      pjs::Str *filter;
      if (!read(filter)) break;
      uint8_t options;
      if (!read(options)) return false;
      auto *f = pjs::Object::make();
      filters->push(f);
      f->set(STR_filter, filter);
      f->set(STR_qos, int(options & 0x03));
    }
    return true;
  }

  bool read_reason_codes() {
    auto codes = pjs::Array::make();
    m_packet->set(STR_reasonCodes, codes);
    for (;;) {
      uint16_t code;
      if (!read(code)) break;
      codes->push(code);
    }
    return true;
  }

  bool read_unsubscribe_payload() {
    auto filters = pjs::Array::make();
    m_packet->set(STR_topicFilters, filters);
    for (;;) {
      pjs::Str *filter;
      if (!read(filter)) break;
      filters->push(filter);
    }
    return true;
  }
};

//
// DataBuilder
//

class DataBuilder {
public:
  auto buffer() const -> const Data& {
    return m_buffer;
  }

  void push(const Data &data) {
    m_buffer.push(data);
  }

  void push(uint8_t c) {
    s_dp.push(&m_buffer, c);
  }

  void push(uint16_t n) {
    push(uint8_t((n >> 8) & 0xff));
    push(uint8_t((n >> 0) & 0xff));
  }

  void push(uint32_t n) {
    push(uint8_t((n >> 24) & 0xff));
    push(uint8_t((n >> 16) & 0xff));
    push(uint8_t((n >>  8) & 0xff));
    push(uint8_t((n >>  0) & 0xff));
  }

  void push(int n) {
    uint8_t buf[4];
    int len = make_var_int(n, buf);
    s_dp.push(&m_buffer, buf, len);
  }

  void push(pjs::Str *s) {
    push(uint16_t(s->size()));
    s_dp.push(&m_buffer, s->str());
  }

  void push(Data *data) {
    if (data) {
      push(uint16_t(data->size()));
      push(*data);
    } else {
      push(uint16_t(0));
    }
  }

protected:
  Data m_buffer;

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

  static Data::Producer s_dp;
};

Data::Producer DataBuilder::s_dp("MQTT Encoder");

//
// PacketBuilder
//

class PacketBuilder : private DataBuilder {
public:
  PacketBuilder(int protocol_level, pjs::Object *packet, Data &out)
    : m_protocol_level(protocol_level)
    , m_packet(packet)
    , m_out(out) {}

  int protocol_level() const {
    return m_protocol_level;
  }

  void CONNECT() {
    m_protocol_level = get(m_packet, STR_protocolLevel, 4);
    int flags = 0;
    if (get(m_packet, STR_cleanStart, false)) flags |= 0x02;
    auto *will = get(m_packet, STR_will);
    if (will) {
      flags |= 0x04;
      flags |= (get(will, STR_qos, 0) & 3) << 3;
      if (get(will, STR_retain, false)) flags |= 0x20;
    }
    auto username = get(m_packet, STR_username, (pjs::Str*)nullptr);
    auto password = get(m_packet, STR_password, (Data*)nullptr);
    if (username) flags |= 0x80;
    if (password) flags |= 0x40;
    push(STR_MQTT);
    push(uint8_t(m_protocol_level));
    push(uint8_t(flags));
    push(get(m_packet, STR_keepAlive, uint16_t(0)));
    push_properties();
    push(get(m_packet, STR_clientID, pjs::Str::empty));
    if (will) {
      push(get(will, STR_topic, pjs::Str::empty));
      push(pjs::Ref<Data>(get(will, STR_payload, Data::make())).get());
    }
    if (username) push(username);
    if (password) push(password);
    frame(PacketType::CONNECT, 0);
  }

  void CONNACK() {
    m_protocol_level = get(m_packet, STR_protocolLevel, 4);
    push(get(m_packet, STR_sessionPresent, false) ? '\x01' : '\x00');
    push(get(m_packet, STR_reasonCode, '\x00'));
    push_properties();
    frame(PacketType::CONNACK, 0);
  }

  void PUBLISH(int qos, bool dup, bool retain, const Data &payload) {
    int flags = (qos & 3) << 1;
    if (dup) flags |= 0x04;
    if (retain) flags |= 0x01;
    push(get(m_packet, STR_topicName, pjs::Str::empty));
    if (qos > 0) push(get(m_packet, STR_packetIdentifier, uint16_t(0)));
    push_properties();
    push(payload);
    frame(PacketType::PUBLISH, flags);
  }

  void PUBACK() {
    push(get(m_packet, STR_packetIdentifier, uint16_t(0)));
    if (m_protocol_level >= 5) {
      push(get(m_packet, STR_reasonCode, '\x00'));
      push_properties();
    }
    frame(PacketType::PUBACK, 0);
  }

  void PUBREC() {
    push(get(m_packet, STR_packetIdentifier, uint16_t(0)));
    if (m_protocol_level >= 5) {
      push(get(m_packet, STR_reasonCode, '\x00'));
      push_properties();
    }
    frame(PacketType::PUBREC, 0);
  }

  void PUBREL() {
    push(get(m_packet, STR_packetIdentifier, uint16_t(0)));
    if (m_protocol_level >= 5) {
      push(get(m_packet, STR_reasonCode, '\x00'));
      push_properties();
    }
    frame(PacketType::PUBREL, 0);
  }

  void PUBCOMP() {
    push(get(m_packet, STR_packetIdentifier, uint16_t(0)));
    if (m_protocol_level >= 5) {
      push(get(m_packet, STR_reasonCode, '\x00'));
      push_properties();
    }
    frame(PacketType::PUBCOMP, 0);
  }

  void SUBSCRIBE() {
    push(get(m_packet, STR_packetIdentifier, uint16_t(0)));
    push_properties();
    if (auto filters = get(m_packet, STR_topicFilters)) {
      if (filters->is_array()) {
        filters->as<pjs::Array>()->iterate_all(
          [this](pjs::Value &v, int i) {
            if (v.is_object() && v.o()) {
              auto filter = get(v.o(), STR_filter, pjs::Str::empty);
              auto qos = get(v.o(), STR_qos, 0);
              push(filter);
              push(uint8_t(qos & 3));
            }
          }
        );
      }
    }
    frame(PacketType::SUBSCRIBE, 0x02);
  }

  void SUBACK() {
    push(get(m_packet, STR_packetIdentifier, uint16_t(0)));
    push_properties();
    push_reason_codes();
    frame(PacketType::SUBACK, 0);
  }

  void UNSUBSCRIBE() {
    push(get(m_packet, STR_packetIdentifier, uint16_t(0)));
    push_properties();
    if (auto filters = get(m_packet, STR_topicFilters)) {
      if (filters->is_array()) {
        filters->as<pjs::Array>()->iterate_all(
          [this](pjs::Value &v, int i) {
            auto *s = v.to_string();
            push(s);
            s->release();
          }
        );
      }
    }
    frame(PacketType::UNSUBSCRIBE, 0x02);
  }

  void UNSUBACK() {
    push(get(m_packet, STR_packetIdentifier, uint16_t(0)));
    push_properties();
    push_reason_codes();
    frame(PacketType::UNSUBACK, 0);
  }

  void PINGREQ() {
    frame(PacketType::PINGREQ, 0);
  }

  void PINGRESP() {
    frame(PacketType::PINGRESP, 0);
  }

  void DISCONNECT() {
    if (m_protocol_level >= 5) {
      push(get(m_packet, STR_reasonCode, '\x00'));
      push_properties();
    }
    frame(PacketType::DISCONNECT, 0);
  }

  void AUTH() {
    if (m_protocol_level >= 5) {
      push(get(m_packet, STR_reasonCode, '\x00'));
      push_properties();
    }
    frame(PacketType::AUTH, 0);
  }

private:
  int m_protocol_level;
  pjs::Object* m_packet;
  Data& m_out;

  bool get(pjs::Object *obj, pjs::Str *k, bool def) {
    pjs::Value v; obj->get(k, v);
    return v.is_undefined() ? def : v.to_boolean();
  }

  uint8_t get(pjs::Object *obj, pjs::Str *k, uint8_t def) {
    pjs::Value v; obj->get(k, v);
    return v.is_number() ? v.n() : def;
  }

  uint16_t get(pjs::Object *obj, pjs::Str *k, uint16_t def) {
    pjs::Value v; obj->get(k, v);
    return v.is_number() ? v.n() : def;
  }

  int get(pjs::Object *obj, pjs::Str *k, int def) {
    pjs::Value v; obj->get(k, v);
    return v.is_number() ? v.n() : def;
  }

  pjs::Str* get(pjs::Object *obj, pjs::Str *k, pjs::Str* def) {
    pjs::Value v; obj->get(k, v);
    return v.is_string() ? v.s() : def;
  }

  Data* get(pjs::Object *obj, pjs::Str *k, Data* def) {
    pjs::Value v; obj->get(k, v);
    if (v.is_instance_of<Data>()) {
      return v.as<Data>();
    } else if (!v.is_nullish()) {
      auto *s = v.to_string();
      auto *d = s_dp.make(s->str());
      s->release();
      return d;
    } else {
      return def;
    }
  }

  pjs::Object* get(pjs::Object *obj, pjs::Str *k) {
    pjs::Value v; obj->get(k, v);
    return v.is_object() ? v.o() : nullptr;
  }

  void push_properties() {
    if (m_protocol_level >= 5) {
      DataBuilder db;
      pjs::Value v; m_packet->get(STR_properties, v);
      if (v.is_object() && v.o()) {
        v.o()->iterate_all(
          [&](pjs::Str *k, pjs::Value &v) {
            if (auto *p = s_property_map.by_name(k)) {
              db.push(p->id);
              switch (p->type) {
                case PropertyMap::Property::INT: {
                  int n = v.to_number();
                  db.push(n);
                  break;
                }
                case PropertyMap::Property::INT8: {
                  uint8_t n = v.to_number();
                  db.push(n);
                  break;
                }
                case PropertyMap::Property::INT16: {
                  uint16_t n = v.to_number();
                  db.push(n);
                  break;
                }
                case PropertyMap::Property::INT32: {
                  uint32_t n = v.to_number();
                  db.push(n);
                  break;
                }
                case PropertyMap::Property::STR: {
                  auto s = v.to_string();
                  db.push(s);
                  s->release();
                  break;
                }
                case PropertyMap::Property::BIN: {
                  if (v.is_instance_of<Data>()) {
                    auto data = v.as<Data>();
                    db.push(uint16_t(data->size()));
                    db.push(*data);
                  } else {
                    db.push(uint16_t(0));
                  }
                  break;
                }
                default: break;
              }
            } else {
              int id = 38; // user property
              auto s = v.to_string();
              db.push(id);
              db.push(k);
              db.push(s);
              s->release();
            }
          }
        );
      }
      push(int(db.buffer().size()));
      push(db.buffer());
    }
  }

  void push_reason_codes() {
    pjs::Value v;
    m_packet->get(STR_reasonCodes, v);
    if (v.is_array()) {
      auto *a = v.as<pjs::Array>();
      a->iterate_all(
        [this](pjs::Value &v, int i) {
          if (v.is_number()) {
            push(uint8_t(v.n()));
          } else {
            push(uint8_t(0));
          }
        }
      );
    }
  }

  void frame(PacketType type, uint8_t flags) {
    uint8_t buf[5];
    buf[0] = ((uint8_t)type << 4) | (flags & 0x0f);
    int len = 1 + make_var_int(m_buffer.size(), &buf[1]);
    s_dp.push(&m_out, buf, len);
    m_out.push(m_buffer);
  }
};

//
// DecoderFunction
//

void DecoderFunction::reset() {
  m_protocol_level = 0;
  m_state = FIXED_HEADER;
  m_buffer.clear();
}

void DecoderFunction::on_event(Event *evt) {

  auto data = evt->as<Data>();
  if (!data) return;

  while (!data->empty() && m_state != ERROR) {
    auto state = m_state;
    pjs::Ref<Data> output(Data::make());

    // byte scan
    data->shift_to(
      [&](int c) -> bool {
        switch (state) {
          case FIXED_HEADER:
            m_fixed_header = c;
            m_remaining_length = 0;
            m_remaining_length_shift = 0;
            state = REMAINING_LENGTH;
            return false;
          case REMAINING_LENGTH:
            m_remaining_length |= (c & 0x7f) << m_remaining_length_shift;
            m_remaining_length_shift += 7;
            if (c & 0x80) return false;
            if (!m_remaining_length) {
              auto type = PacketType(m_fixed_header >> 4);
              if (type != PacketType::PINGREQ && type != PacketType::PINGRESP) {
                state = ERROR;
                return true;
              }
              message();
              state = FIXED_HEADER;
              return false;
            } else {
              m_buffer.clear();
              state = REMAINING_DATA;
              return true;
            }
          case REMAINING_DATA:
            if (--m_remaining_length) return false;
            state = FIXED_HEADER;
            return true;
          default: return false;
        }
      },
      *output
    );

    // old state
    if (m_state == REMAINING_DATA) {
      m_buffer.push(*output);
    }

    // new state
    if (state == FIXED_HEADER) {
      message();
    }

    m_state = state;
  }
}

void DecoderFunction::message() {
  auto type = PacketType(m_fixed_header >> 4);
  auto head = MessageHead::make();
  head->type(pjs::EnumDef<PacketType>::name(type));
  head->qos((m_fixed_header >> 1) & 3);
  head->dup(m_fixed_header & 0x08);
  head->retained(m_fixed_header & 1);

  if (!m_protocol_level) m_protocol_level = on_get_protocol_level();
  if (!m_protocol_level) return;

  PacketParser parser(type, m_protocol_level, head, m_buffer);
  parser.decode();
  output(MessageStart::make(head));

  if (type == PacketType::PUBLISH) {
    m_buffer.shift(parser.position());
    if (!m_buffer.empty()) {
      output(Data::make(m_buffer));
    }
  }

  m_protocol_level = parser.protocol_level();
  m_buffer.clear();

  output(MessageEnd::make());
}

//
// EncoderFunction
//

EncoderFunction::EncoderFunction()
  : m_prop_type("type")
  , m_prop_qos("qos")
  , m_prop_dup("dup")
  , m_prop_retain("retain")
{
}

void EncoderFunction::reset() {
  m_protocol_level = 0;
  m_start = nullptr;
  m_buffer.clear();
}

void EncoderFunction::on_event(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    m_start = start;
    m_buffer.clear();

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_start) {
      auto head = m_start->head();
      if (!head) {
        on_encode_error("trying to encode a packet without a head");
        return;
      }
      pjs::Str *type_str;
      if (!m_prop_type.get(head, type_str)) {
        m_start = nullptr;
        m_buffer.clear();
        on_encode_error("invalid packet type");
        return;
      }
      auto type = pjs::EnumDef<PacketType>::value(type_str);
      if (int(type) < 0) {
        m_start = nullptr;
        m_buffer.clear();
        on_encode_error("invalid packet type");
        return;
      }
      Data buf;
      PacketBuilder builder(m_protocol_level, head, buf);
      switch (type) {
        case PacketType::CONNECT: {
          builder.CONNECT();
          m_protocol_level = builder.protocol_level();
          break;
        }
        case PacketType::CONNACK: {
          builder.CONNACK();
          m_protocol_level = builder.protocol_level();
          break;
        }
        case PacketType::PUBLISH: {
          int qos = 0;
          bool dup = false, retain = false;
          m_prop_qos.get(head, qos);
          m_prop_dup.get(head, dup);
          m_prop_retain.get(head, retain);
          builder.PUBLISH(qos, dup, retain, m_buffer);
          break;
        }
        case PacketType::PUBACK: builder.PUBACK(); break;
        case PacketType::PUBREC: builder.PUBREC(); break;
        case PacketType::PUBREL: builder.PUBREL(); break;
        case PacketType::PUBCOMP: builder.PUBCOMP(); break;
        case PacketType::SUBSCRIBE: builder.SUBSCRIBE(); break;
        case PacketType::SUBACK: builder.SUBACK(); break;
        case PacketType::UNSUBSCRIBE: builder.UNSUBSCRIBE(); break;
        case PacketType::UNSUBACK: builder.UNSUBACK(); break;
        case PacketType::PINGREQ: builder.PINGREQ(); break;
        case PacketType::PINGRESP: builder.PINGRESP(); break;
        case PacketType::DISCONNECT: builder.DISCONNECT(); break;
        case PacketType::AUTH: builder.AUTH(); break;
      }
      output(m_start);
      output(Data::make(buf));
      output(evt);
      m_start = nullptr;
      m_buffer.clear();
    }
  }
}

//
// Decoder::Options
//

Decoder::Options::Options(pjs::Object *options) {
  Value(options, "protocolLevel")
    .get(protocol_level)
    .get(protocol_level_f)
    .check_nullable();
}

//
// Decoder
//

Decoder::Decoder(const Options &options)
  : m_options(options)
{
}

Decoder::Decoder(const Decoder &r)
  : Filter(r)
  , m_options(r.m_options)
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

void Decoder::chain() {
  Filter::chain();
  DecoderFunction::chain(Filter::output());
}

void Decoder::reset() {
  Filter::reset();
  DecoderFunction::reset();
}

void Decoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  } else {
    Filter::output(evt, DecoderFunction::input());
  }
}

auto Decoder::on_get_protocol_level() -> int {
  pjs::Value protocol_level(m_options.protocol_level);
  if (!eval(m_options.protocol_level_f, protocol_level)) return 0;
  if (!protocol_level.is_undefined()) {
    if (protocol_level.is_number()) {
      int n = protocol_level.n();
      if (n == 4 || n == 5) {
        return protocol_level.n();
      } else {
        Log::error("[decodeMQTT] options.protocolLevel expects 4 or 5");
      }
    } else {
      Log::error("[decodeMQTT] options.protocolLevel expects a number or a function returning a number");
    }
  }
  return 0;
}

//
// Encoder
//

Encoder::Encoder()
{
}

Encoder::Encoder(const Encoder &r)
  : Filter(r)
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

void Encoder::chain() {
  Filter::chain();
  EncoderFunction::chain(Filter::output());
}

void Encoder::reset() {
  Filter::reset();
  EncoderFunction::reset();
}

void Encoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  } else {
    Filter::output(evt, EncoderFunction::input());
  }
}

void Encoder::on_encode_error(const char *msg) {
  Log::error("[encodeMQTT] %s", msg);
}

} // namespace mqtt
} // namespace pipy
