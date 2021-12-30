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
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACT// ION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "mqtt.hpp"

namespace pipy {
namespace mqtt {

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

//
// MessageHead
//

template<> void ClassDef<MessageHead>::init() {
  variable("type", MessageHead::Field::type);
  variable("qos", MessageHead::Field::qos);
  variable("dup", MessageHead::Field::dup);
  variable("retain", MessageHead::Field::retained);
}

} // namespace pjs
