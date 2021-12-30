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

#ifndef API_MQTT_HPP
#define API_MQTT_HPP

#include "pjs/pjs.hpp"
#include "data.hpp"

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
  enum class Field {
    type,
    dup,
    retained,
    qos,
  };

  auto type() -> pjs::Str* {
    pjs::Value ret;
    pjs::get<MessageHead>(this, MessageHead::Field::type, ret);
    return ret.is_string() ? ret.s() : pjs::Str::empty.get();
  }

  auto qos() -> int {
    pjs::Value ret;
    pjs::get<MessageHead>(this, MessageHead::Field::qos, ret);
    return ret.is_number() ? ret.n() : 0;
  }

  bool dup() {
    pjs::Value ret;
    pjs::get<MessageHead>(this, MessageHead::Field::dup, ret);
    return ret.to_boolean();
  }

  bool retained() {
    pjs::Value ret;
    pjs::get<MessageHead>(this, MessageHead::Field::retained, ret);
    return ret.to_boolean();
  }

  void type(pjs::Str *s) { pjs::set<MessageHead>(this, MessageHead::Field::type, s); }
  void qos(int n) { pjs::set<MessageHead>(this, MessageHead::Field::qos, n); }
  void dup(bool b) { pjs::set<MessageHead>(this, MessageHead::Field::dup, b); }
  void retained(bool b) { pjs::set<MessageHead>(this, MessageHead::Field::retained, b); }
};

} // namespace mqtt
} // namespace pipy

#endif // API_MQTT_HPP
