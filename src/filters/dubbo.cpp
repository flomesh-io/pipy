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

namespace pipy {
namespace dubbo {

thread_local static Data::Producer s_dp("Dubbo");

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
  d.name = "decodeDubbo";
}

auto Decoder::clone() -> Filter* {
  return new Decoder(*this);
}

void Decoder::reset() {
  Filter::reset();
  Deframer::reset();
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
    case START: {
      m_head[0] = c;
      Deframer::read(sizeof(m_head) - 1, m_head + 1);
      return HEAD;
    }
    case HEAD: {
      const auto &head = m_head;
      if (head[0] != 0xda ||
          head[1] != 0xbb
      ) {
        Filter::error(StreamEnd::PROTOCOL_ERROR);
        return -1;
      }
      auto flags = head[2];
      auto *mh = MessageHead::make();
      mh->isRequest = (flags & 0x80);
      mh->isTwoWay = (flags & 0x40);
      mh->isEvent = (flags & 0x20);
      mh->serializationType = (flags & 0x1f);
      mh->status = head[3];
      mh->requestID = (
        ((uint64_t)m_head[4] << 56)|
        ((uint64_t)m_head[5] << 48)|
        ((uint64_t)m_head[6] << 40)|
        ((uint64_t)m_head[7] << 32)|
        ((uint64_t)m_head[8] << 24)|
        ((uint64_t)m_head[9] << 16)|
        ((uint64_t)m_head[10] << 8)|
        ((uint64_t)m_head[11] << 0)
      );
      Filter::output(MessageStart::make(mh));
      Deframer::pass(
        ((uint32_t)m_head[12] << 24)|
        ((uint32_t)m_head[13] << 16)|
        ((uint32_t)m_head[14] <<  8)|
        ((uint32_t)m_head[15] <<  0)
      );
      return BODY;
    }
    case BODY: {
      Filter::output(MessageEnd::make());
      return START;
    }
    default: return -1;
  }
}

void Decoder::on_pass(Data &data) {
  Filter::output(Data::make(std::move(data)));
}

//
// Encoder
//

Encoder::Encoder()
{
}

Encoder::Encoder(const Encoder &r)
{
}

Encoder::~Encoder()
{
}

void Encoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "encodeDubbo";
}

auto Encoder::clone() -> Filter* {
  return new Encoder(*this);
}

void Encoder::reset() {
  Filter::reset();
  m_head = nullptr;
  m_buffer.clear();
}

void Encoder::process(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_head) {
      m_head = start->head();
      m_buffer.clear();
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_head) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_head) {
      MessageHead *mh = pjs::coerce<MessageHead>(m_head);

      uint8_t flags = mh->serializationType & 0x1f;
      if (mh->isRequest) flags |= 0x80;
      if (mh->isTwoWay) flags |= 0x40;
      if (mh->isEvent) flags |= 0x20;

      auto rid = mh->requestID;
      auto len = m_buffer.size();

      uint8_t hdr[16];
      hdr[0] = 0xda;
      hdr[1] = 0xbb;
      hdr[2] = flags;
      hdr[3] = mh->status;
      hdr[4] = rid >> 56;
      hdr[5] = rid >> 48;
      hdr[6] = rid >> 40;
      hdr[7] = rid >> 32;
      hdr[8] = rid >> 24;
      hdr[9] = rid >> 16;
      hdr[10] = rid >> 8;
      hdr[11] = rid >> 0;
      hdr[12] = len >> 24;
      hdr[13] = len >> 16;
      hdr[14] = len >> 8;
      hdr[15] = len >> 0;

      auto *body = Data::make(hdr, sizeof(hdr), &s_dp);
      body->push(std::move(m_buffer));

      Filter::output(MessageStart::make(mh));
      Filter::output(body);
      Filter::output(evt);

      m_head = nullptr;

    } else if (evt->is<StreamEnd>()) {
      Filter::output(evt);
    }
  }
}

} // namespace dubbo
} // namespace pipy

namespace pjs {

using namespace pipy::dubbo;

//
// MessageHead
//

template<> void ClassDef<MessageHead>::init() {
  field<uint64_t>("requestID", [](MessageHead *obj) { return &obj->requestID; });
  field<bool>("isRequest", [](MessageHead *obj) { return &obj->isRequest; });
  field<bool>("isTwoWay", [](MessageHead *obj) { return &obj->isTwoWay; });
  field<bool>("isEvent", [](MessageHead *obj) { return &obj->isEvent; });
  field<int>("serializationType", [](MessageHead *obj) { return &obj->serializationType; });
  field<int>("status", [](MessageHead *obj) { return &obj->status; });
}

} // namespace pjs
