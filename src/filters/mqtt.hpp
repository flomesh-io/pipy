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

#ifndef MQTT_HPP
#define MQTT_HPP

#include "filter.hpp"
#include "deframer.hpp"

namespace pipy {
namespace mqtt {

//
// PacketType
//

enum class PacketType {
  CONNECT     = 1,
  CONNACK     = 2,
  PUBLISH     = 3,
  PUBACK      = 4,
  PUBREC      = 5,
  PUBREL      = 6,
  PUBCOMP     = 7,
  SUBSCRIBE   = 8,
  SUBACK      = 9,
  UNSUBSCRIBE = 10,
  UNSUBACK    = 11,
  PINGREQ     = 12,
  PINGRESP    = 13,
  DISCONNECT  = 14,
  AUTH        = 15,
};

//
// MessageHead
//

class MessageHead : public pjs::ObjectTemplate<MessageHead> {
public:
  pjs::EnumValue<PacketType> type = PacketType::CONNECT;
  bool dup = false;
  bool retained = false;
  bool sessionPresent = false;
  int qos = 0;
  int packetIdentifier = 0;
  int protocolLevel = 5;
  int keepAlive = 0;
  int reasonCode = 0;
  pjs::Ref<pjs::Str> topicName;
  pjs::Ref<pjs::Object> properties;
};

//
// Will
//

class Will : public pjs::ObjectTemplate<Will> {
public:
  int qos;
  bool retained;
  pjs::Ref<pjs::Object> properties;
  pjs::Ref<pjs::Str> topic;
  pjs::Ref<Data> payload;
};

//
// ConnectPayload
//

class ConnectPayload : public pjs::ObjectTemplate<ConnectPayload> {
public:
  pjs::Ref<pjs::Str> clientID;
  pjs::Ref<pjs::Str> username;
  pjs::Ref<Data> password;
  pjs::Ref<Will> will;
  bool cleanStart;
};

//
// TopicFilter
//

class TopicFilter : public pjs::ObjectTemplate<TopicFilter> {
public:
  pjs::Ref<pjs::Str> filter;
  int qos;
};

//
// SubscribePayload
//

class SubscribePayload : public pjs::ObjectTemplate<SubscribePayload> {
public:
  pjs::Ref<pjs::Array> topicFilters;
};

//
// Decoder
//

class Decoder : public Filter, public Deframer {
public:
  Decoder();

private:
  Decoder(const Decoder &r);
  ~Decoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  enum State {
    ERROR = -1,
    FIXED_HEADER = 0,
    REMAINING_LENGTH,
    REMAINING_DATA,
  };

  int m_fixed_header;
  int m_remaining_length;
  int m_remaining_length_shift;
  pjs::Ref<Data> m_buffer;

  virtual auto on_state(int state, int c) -> int override;
  virtual void on_pass(Data &data) override;

  void message();
};

//
// Encoder
//

class Encoder : public Filter {
public:
  Encoder();

private:
  Encoder(const Encoder &r);
  ~Encoder();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Ref<MessageHead> m_head;
  pjs::Ref<Data> m_buffer;
};

} // namespace mqtt
} // namespace pipy

#endif // MQTT_HPP
