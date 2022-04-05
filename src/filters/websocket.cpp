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

#include "websocket.hpp"
#include "logging.hpp"

namespace pipy {
namespace websocket {

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-------+-+-------------+-------------------------------+
// |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
// |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
// |N|V|V|V|       |S|             |   (if payload len==126/127)   |
// | |1|2|3|       |K|             |                               |
// +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
// |     Extended payload length continued, if payload len == 127  |
// + - - - - - - - - - - - - - - - +-------------------------------+
// |                               |Masking-key, if MASK set to 1  |
// +-------------------------------+-------------------------------+
// | Masking-key (continued)       |          Payload Data         |
// +-------------------------------- - - - - - - - - - - - - - - - +
// :                     Payload Data continued ...                :
// + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
// |                     Payload Data continued ...                |
// +---------------------------------------------------------------+

class MessageHead : public pjs::ObjectTemplate<MessageHead> {
public:
  enum class Field {
    opcode,
    mask,
  };
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

void Decoder::dump(std::ostream &out) {
  out << "decodeWebSocket";
}

auto Decoder::clone() -> Filter* {
  return new Decoder(*this);
}

void Decoder::chain() {
  Filter::chain();
  Deframer::chain(Filter::output());
}

void Decoder::reset() {
  Filter::reset();
  Deframer::reset();
}

void Decoder::process(Event *evt) {
  Deframer::input()->input(evt);
}

auto Decoder::on_state(int state, int c) -> int {
  switch (state) {
  case OPCODE:
    m_opcode = c;
    return LENGTH;
  case LENGTH:
    m_has_mask = (c & 0x80);
    m_payload_size = (c &= 0x7f);
    if (c == 127) {
      Deframer::read(8, m_buffer);
      return LENGTH_64;
    }
    if (c == 126) {
      Deframer::read(2, m_buffer);
      return LENGTH_16;
    }
    if (m_has_mask) {
      Deframer::read(4, m_buffer);
      return MASK;
    }
    message_start();
    Deframer::pass(c);
    return PAYLOAD;
  case LENGTH_16:
    m_payload_size = (
      ((uint64_t)m_buffer[0] << 8)|
      ((uint64_t)m_buffer[1] << 0)
    );
    if (m_has_mask) {
      Deframer::read(4, m_buffer);
      return MASK;
    }
    message_start();
    Deframer::pass(m_payload_size);
    return OPCODE;
  case LENGTH_64:
    m_payload_size = (
      ((uint64_t)m_buffer[0] << 56)|
      ((uint64_t)m_buffer[1] << 48)|
      ((uint64_t)m_buffer[2] << 40)|
      ((uint64_t)m_buffer[3] << 32)|
      ((uint64_t)m_buffer[4] << 24)|
      ((uint64_t)m_buffer[5] << 16)|
      ((uint64_t)m_buffer[6] << 8 )|
      ((uint64_t)m_buffer[7] << 0 )
    );
    if (m_has_mask) {
      Deframer::read(4, m_buffer);
      return MASK;
    }
    message_start();
    Deframer::pass(m_payload_size);
    return OPCODE;
  case MASK:
    m_mask = (
      ((uint32_t)m_buffer[0] << 8)|
      ((uint32_t)m_buffer[1] << 0)
    );
    message_start();
    Deframer::pass(m_payload_size);
    return PAYLOAD;
  case PAYLOAD:
    Filter::output(MessageEnd::make());
    return OPCODE;
  }
  return state;
}

void Decoder::message_start() {
  auto head = MessageHead::make();
  pjs::set<MessageHead>(head, MessageHead::Field::opcode, int(m_opcode & 0x0f));
  if (m_has_mask) pjs::set<MessageHead>(head, MessageHead::Field::mask, m_mask);
  Filter::output(MessageStart::make(head));
}

//
// Encoder
//

Encoder::Encoder()
  : m_prop_opcode("opcode")
  , m_prop_mask("mask")
{
}

Encoder::Encoder(const Encoder &r)
  : Encoder()
{
}

Encoder::~Encoder()
{
}

void Encoder::dump(std::ostream &out) {
  out << "encodeWebSocket";
}

auto Encoder::clone() -> Filter* {
  return new Encoder(*this);
}

void Encoder::reset() {
  Filter::reset();
}

void Encoder::process(Event *evt) {
  static Data::Producer s_dp("encodeWebSocket");
}

} // namespace websocket
} // namespace pipy

namespace pjs {

using namespace pipy::websocket;

template<> void ClassDef<MessageHead>::init() {
  ctor();
  variable("opcode", MessageHead::Field::opcode);
  variable("mask", MessageHead::Field::mask);
}

} // namespace pjs
