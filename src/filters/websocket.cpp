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
    masked,
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
  m_started = false;
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
    return message_start();
  case LENGTH_16:
    m_payload_size = (
      ((uint64_t)m_buffer[0] << 8)|
      ((uint64_t)m_buffer[1] << 0)
    );
    if (m_has_mask) {
      Deframer::read(4, m_buffer);
      return MASK;
    }
    return message_start();
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
    return message_start();
  case MASK:
    std::memcpy(m_mask, m_buffer, 4);
    m_mask_pointer = 0;
    return message_start();
  case PAYLOAD:
    message_end();
    return OPCODE;
  }
  return state;
}

auto Decoder::on_pass(const Data &data) -> Data* {
  static Data::Producer s_dp("decodeWebSocket");

  if (m_has_mask) {
    uint8_t buf[DATA_CHUNK_SIZE];
    auto output = Data::make();
    auto &p = m_mask_pointer;
    for (const auto c : data.chunks()) {
      const auto ptr = std::get<0>(c);
      const auto len = std::get<1>(c);
      for (auto i = 0; i < len; i++) buf[i] = ptr[i] ^ m_mask[p++ & 3];
      s_dp.push(output, buf, len);
    }
    return output;
  } else {
    return Data::make(data);
  }
}

auto Decoder::message_start() -> State {
  if (!m_started) {
    auto head = MessageHead::make();
    pjs::set<MessageHead>(head, MessageHead::Field::opcode, int(m_opcode & 0x0f));
    pjs::set<MessageHead>(head, MessageHead::Field::masked, m_has_mask);
    Filter::output(MessageStart::make(head));
    m_started = true;
  }

  if (m_payload_size > 0) {
    Deframer::pass(m_payload_size);
    return PAYLOAD;
  } else {
    message_end();
    return OPCODE;
  }
}

void Decoder::message_end() {
  if (m_opcode & 0x80) {
    Filter::output(MessageEnd::make());
    m_started = false;
  }
}

//
// Encoder
//

Encoder::Encoder()
  : m_prop_opcode("opcode")
  , m_prop_masked("masked")
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
  m_buffer.clear();
  m_start = nullptr;
}

void Encoder::process(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_start) {
      m_start = start;
      auto *head = start->head();
      int opcode;
      bool masked;
      if (!m_prop_opcode.get(head, opcode)) opcode = 1;
      if (!m_prop_masked.get(head, masked)) masked = false;
      m_opcode = opcode;
      m_masked = masked;
      m_continuation = false;
      m_buffer.clear();
      output(evt);
    }

  } else if (auto data = evt->as<Data>()) {
    m_buffer.push(*data);
    while (m_buffer.size() >= DATA_CHUNK_SIZE) {
      Data buf;
      m_buffer.shift(DATA_CHUNK_SIZE, buf);
      frame(buf, false);
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      frame(m_buffer, true);
      m_buffer.clear();
      m_continuation = false;
      output(evt);
    }

  } else if (evt->is<StreamEnd>()) {
    output(evt);
  }
}

void Encoder::frame(const Data &data, bool final) {
  static Data::Producer s_dp("encodeWebSocket");

  int p = 0;
  uint8_t head[12];
  if (m_continuation) {
    head[p++] = (final ? 0x80 : 0);
  } else {
    head[p++] = (m_opcode & 0x0f) | (final ? 0x80 : 0);
    m_continuation = true;
  }

  auto size = data.size();
  if (size < 126) {
    head[p++] = size | (m_masked ? 0x80 : 0);
  } else if (size <= 0xffff) {
    head[p+0] = 126 | (m_masked ? 0x80 : 0);
    head[p+1] = size >> 8;
    head[p+2] = size >> 0;
    p += 3;
  } else {
    head[p+0] = 127 | (m_masked ? 0x80 : 0);
    head[p+1] = 0;
    head[p+2] = 0;
    head[p+3] = 0;
    head[p+4] = 0;
    head[p+5] = size >> 24;
    head[p+6] = size >> 16;
    head[p+7] = size >> 8;
    head[p+8] = size >> 0;
    p += 9;
  }

  uint8_t mask[4];

  if (m_masked) {
    *(uint32_t*)mask = m_rand();
    std::memcpy(head + p, mask, 4);
    p += 4;
  }

  Data *out = Data::make();
  s_dp.push(out, head, p);

  if (m_masked) {
    uint8_t buf[DATA_CHUNK_SIZE], p = 0;
    for (const auto c : data.chunks()) {
      auto ptr = std::get<0>(c);
      auto len = std::get<1>(c);
      for (int i = 0; i < len; i++) buf[i] = ptr[i] ^ mask[p++ & 3];
      s_dp.push(out, buf, len);
    }

  } else {
    out->push(data);
  }

  output(out);
}

} // namespace websocket
} // namespace pipy

namespace pjs {

using namespace pipy::websocket;

template<> void ClassDef<MessageHead>::init() {
  ctor();
  variable("opcode", MessageHead::Field::opcode);
  variable("masked", MessageHead::Field::masked);
}

} // namespace pjs
