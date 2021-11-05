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

#include "dubbo.hpp"
#include "logging.hpp"

namespace pipy {
namespace dubbo {

class DubboHead : public pjs::ObjectTemplate<DubboHead> {
public:
  enum class Field {
    id,
    status,
    isRequest,
    isTwoWay,
    isEvent,
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
  out << "decodeDubbo";
}

auto Decoder::clone() -> Filter* {
  return new Decoder(*this);
}

void Decoder::reset() {
  Filter::reset();
  m_state = FRAME_HEAD;
  m_size = 0;
  m_head_size = 0;
  m_stream_end = false;
}

void Decoder::process(Event *evt) {
  if (m_stream_end) return;

  // Data
  if (auto data = evt->as<Data>()) {
    while (!data->empty()) {
      pjs::Ref<Data> read(Data::make());
      auto old_state = m_state;
      data->shift_while([&](int c) {
        if (m_state != old_state)
          return false;

        // Parse one character.
        switch (m_state) {

        // Read frame header.
        case FRAME_HEAD:
          m_head[m_head_size++] = c;
          if (m_head_size == 16) {
            if ((unsigned char)m_head[0] != 0xda ||
                (unsigned char)m_head[1] != 0xbb
            ) {
              Log::error("[dubbo] magic number not found");
            }

            auto F = m_head[2];
            auto S = m_head[3];
            auto R = ((long long)(unsigned char)m_head[4] << 56)
                  | ((long long)(unsigned char)m_head[5] << 48)
                  | ((long long)(unsigned char)m_head[6] << 40)
                  | ((long long)(unsigned char)m_head[7] << 32)
                  | ((long long)(unsigned char)m_head[8] << 24)
                  | ((long long)(unsigned char)m_head[9] << 16)
                  | ((long long)(unsigned char)m_head[10] << 8)
                  | ((long long)(unsigned char)m_head[11] << 0);
            auto L = ((int)(unsigned char)m_head[12] << 24)
                  | ((int)(unsigned char)m_head[13] << 16)
                  | ((int)(unsigned char)m_head[14] << 8)
                  | ((int)(unsigned char)m_head[15] << 0);

            auto obj = DubboHead::make();
            pjs::set<DubboHead>(obj, DubboHead::Field::id, std::to_string(R));
            pjs::set<DubboHead>(obj, DubboHead::Field::status, int(S));
            pjs::set<DubboHead>(obj, DubboHead::Field::isRequest, bool(F & 0x80));
            pjs::set<DubboHead>(obj, DubboHead::Field::isTwoWay, bool(F & 0x40));
            pjs::set<DubboHead>(obj, DubboHead::Field::isEvent, bool(F & 0x20));

            m_size = L;
            m_head_object = obj;
            m_state = FRAME_DATA;
          }
          break;

        // Read data.
        case FRAME_DATA:
          if (!--m_size) {
            m_state = FRAME_HEAD;
            m_head_size = 0;
          }
          break;
        }
        return true;

      }, *read);

      // Pass the body data.
      if (old_state == FRAME_DATA) {
        if (!read->empty()) output(read);
        if (m_state != FRAME_DATA) output(MessageEnd::make());

      // Start of the body.
      } else if (m_state == FRAME_DATA) {
        output(MessageStart::make(m_head_object));
        if (m_size == 0) {
          output(MessageEnd::make());
          m_state = FRAME_HEAD;
          m_head_size = 0;
        }
      }
    }

  // End of stream
  } else if (evt->is<StreamEnd>()) {
    output(evt);
  }
}

//
// Encoder
//

Encoder::Encoder()
{
}

Encoder::Encoder(pjs::Object *head)
  : m_head(head)
  , m_prop_id("id")
  , m_prop_status("status")
  , m_prop_is_request("isRequest")
  , m_prop_is_two_way("isTwoWay")
  , m_prop_is_event("isEvent")
{
}

Encoder::Encoder(const Encoder &r)
  : Encoder(r.m_head)
{
}

Encoder::~Encoder()
{
}

void Encoder::dump(std::ostream &out) {
  out << "encodeDubbo";
}

auto Encoder::clone() -> Filter* {
  return new Encoder(*this);
}

void Encoder::reset() {
  Filter::reset();
  m_buffer = nullptr;
  m_auto_id = 0;
}

void Encoder::process(Event *evt) {
  static Data::Producer s_dp("encodeDubbo");

  if (auto start = evt->as<MessageStart>()) {
    m_message_start = start;
    m_buffer = Data::make();

  } else if (evt->is<MessageEnd>()) {
    if (!m_message_start) return;

    pjs::Value head_obj(m_head), head;
    if (!eval(head_obj, head)) return;
    if (!head.is_object() || head.is_null()) head.set(m_message_start->head());

    pjs::Object *obj = head.is_object() ? head.o() : nullptr;
    auto ctx = context();
    auto R = get_header(*ctx, obj, m_prop_id, m_auto_id++);
    auto S = get_header(*ctx, obj, m_prop_status, 0);
    char F = get_header(*ctx, obj, m_prop_is_request, 1) ? 0x82 : 0x02;
    auto D = get_header(*ctx, obj, m_prop_is_two_way, 1);
    auto E = get_header(*ctx, obj, m_prop_is_event, 0);
    auto L = m_buffer->size();

    if (D) F |= 0x40;
    if (E) F |= 0x20;

    char header[16];
    header[0] = 0xda;
    header[1] = 0xbb;
    header[2] = F;
    header[3] = S;
    header[4] = R >> 56;
    header[5] = R >> 48;
    header[6] = R >> 40;
    header[7] = R >> 32;
    header[8] = R >> 24;
    header[9] = R >> 16;
    header[10] = R >> 8;
    header[11] = R >> 0;
    header[12] = L >> 24;
    header[13] = L >> 16;
    header[14] = L >> 8;
    header[15] = L >> 0;

    output(m_message_start);
    output(s_dp.make(header, sizeof(header)));
    output(m_buffer);
    output(evt);

    m_buffer = nullptr;

  } else if (auto data = evt->as<Data>()) {
    if (m_buffer) m_buffer->push(*data);

  } else if (evt->is<StreamEnd>()) {
    output(evt);
  }
}

long long Encoder::get_header(
  const Context &ctx,
  pjs::Object *obj,
  pjs::PropertyCache &prop,
  long long value
) {
  if (!obj) return value;
  pjs::Value v;
  prop.get(obj, v);
  if (v.is_undefined()) return value;
  if (v.is_string()) return std::atoll(v.s()->c_str());
  return (long long)v.to_number();
}

} // namespace dubbo
} // namespace pipy

namespace pjs {

using namespace pipy::dubbo;

template<> void ClassDef<DubboHead>::init() {
  ctor();
  variable("id", DubboHead::Field::id);
  variable("status", DubboHead::Field::status);
  variable("isRequest", DubboHead::Field::isRequest);
  variable("isTwoWay", DubboHead::Field::isTwoWay);
  variable("isEvent", DubboHead::Field::isEvent);
}

} // namespace pjs
