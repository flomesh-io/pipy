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

#include "thrift.hpp"
#include "log.hpp"

namespace pjs {

using namespace pipy::thrift;

template<> void ClassDef<Message>::init() {
  variable("seqID", Message::Field::seqID);
  variable("name", Message::Field::name);
}

} // namespace pjs

namespace pipy {
namespace thrift {

//
//
// Binary protocol Message, strict encoding, 12+ bytes:
// +--------+--------+--------+--------+--------+--------+--------+--------+--------+...+--------+--------+--------+--------+--------+
// |1vvvvvvv|vvvvvvvv|unused  |00000mmm| name length                       | name                | seq id                            |
// +--------+--------+--------+--------+--------+--------+--------+--------+--------+...+--------+--------+--------+--------+--------+
//
// Binary protocol Message, old encoding, 9+ bytes:
// +--------+--------+--------+--------+--------+...+--------+--------+--------+--------+--------+--------+
// | name length                       | name                |00000mmm| seq id                            |
// +--------+--------+--------+--------+--------+...+--------+--------+--------+--------+--------+--------+
//
// Compact protocol Message (4+ bytes):
// +--------+--------+--------+...+--------+--------+...+--------+--------+...+--------+
// |pppppppp|mmmvvvvv| seq id              | name length         | name                |
// +--------+--------+--------+...+--------+--------+...+--------+--------+...+--------+
//

static pjs::ConstStr s_call("call");
static pjs::ConstStr s_reply("reply");
static pjs::ConstStr s_exception("exception");
static pjs::ConstStr s_oneway("oneway");

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
  d.name = "decodeThrift";
}

auto Decoder::clone() -> Filter* {
  return new Decoder(*this);
}

void Decoder::reset() {
  Filter::reset();
  Deframer::reset();
  m_msg = nullptr;
  m_read_data = nullptr;
}

void Decoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    output(evt);
    Deframer::reset();
  } else if (auto *data = evt->as<Data>()) {
    Deframer::deframe(*data);
  }
}

auto Decoder::on_state(int state, int c) -> int {
  switch (state) {
    case START:
      m_read_buf[0] = c;
      if (c == 0x80) {
        m_format = BINARY;
        Deframer::read(7, m_read_buf + 1);
        return MESSAGE_HEAD;
      } else if (c == 0x82) {
        m_format = COMPACT;
        Deframer::read(1, m_read_buf + 1);
        return MESSAGE_HEAD;
      } else if (c & 0x80) {
        return ERROR;
      } else {
        m_format = BINARY_OLD;
        Deframer::read(3, m_read_buf + 1);
        return MESSAGE_HEAD;
      }

    case MESSAGE_HEAD:
      m_msg = Message::make();
      switch (m_format) {
        case BINARY: {
          if (m_read_buf[1] != 0x01) return ERROR;
          if (!set_type(m_read_buf[3] & 0x07)) return ERROR;
          int32_t len = (
            ((int32_t)m_read_buf[4] << 24) |
            ((int32_t)m_read_buf[5] << 16) |
            ((int32_t)m_read_buf[6] <<  8) |
            ((uint32_t)m_read_buf[7] <<  0)
          );
          if (len < 0) return ERROR;
          m_read_data = Data::make();
          Deframer::read(len, m_read_data);
          return MESSAGE_NAME;
        }
        case BINARY_OLD: {
          int32_t len = (
            ((int32_t)m_read_buf[4] << 24) |
            ((int32_t)m_read_buf[5] << 16) |
            ((int32_t)m_read_buf[6] <<  8) |
            ((int32_t)m_read_buf[7] <<  0)
          );
          if (len < 0) return ERROR;
          m_read_data = Data::make();
          Deframer::read(len, m_read_data);
          return MESSAGE_NAME;
        }
        case COMPACT: {
          if ((m_read_buf[1] & 0x1f) != 1) return ERROR;
          if (!set_type(m_read_buf[1] >> 5)) return ERROR;
          return SEQ_ID;
        }
      }
      return ERROR;

    case MESSAGE_NAME:
      m_msg->name(pjs::Str::make(m_read_data->to_string()));
      switch (m_format) {
        case BINARY: Deframer::read(4, m_read_buf); return SEQ_ID;
        case BINARY_OLD: return MESSAGE_TYPE;
        case COMPACT: return ERROR; // TODO
      }
      return ERROR;

    case MESSAGE_TYPE:
      if (!set_type(c)) return ERROR;
      Deframer::read(4, m_read_buf);
      return SEQ_ID;

    case SEQ_ID:
      if (m_format == COMPACT) {
        // TODO
        return ERROR;
      } else {
        m_msg->seqID(
          ((int32_t)m_read_buf[0] << 24) |
          ((int32_t)m_read_buf[1] << 16) |
          ((int32_t)m_read_buf[2] <<  8) |
          ((int32_t)m_read_buf[3] <<  0)
        );
        m_levels.push(new Level(Level::STRUCT));
        return STRUCT_FIELD;
      }

    case STRUCT_FIELD:
      return ERROR; // TODO

    default: return ERROR;
  }
}

bool Decoder::set_type(int type) {
  switch (type) {
    case 1: m_msg->type(s_call); return true;
    case 2: m_msg->type(s_reply); return true;
    case 3: m_msg->type(s_exception); return true;
    case 4: m_msg->type(s_oneway); return true;
    default: return false;
  }
}

} // namespace thrift
} // namespace pipy
