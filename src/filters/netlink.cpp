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

#include "netlink.hpp"

namespace pipy {
namespace netlink {

// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                          Length                             |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |            Type              |           Flags              |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                      Sequence Number                        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                      Process ID (PID)                       |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

thread_local static Data::Producer s_dp("Netlink");

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
  d.name = "decodeNetlink";
}

auto Decoder::clone() -> Filter* {
  return new Decoder(*this);
}

void Decoder::reset() {
  Filter::reset();
  Deframer::reset(START);
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
      m_header[0] = c;
      Deframer::read(sizeof(m_header) - 1, m_header + 1);
      return HEADER;
    case HEADER: {
      const auto *hdr = reinterpret_cast<nlmsghdr*>(m_header);
      const auto size = hdr->nlmsg_len;
      auto *head = MessageHead::make();
      head->type = hdr->nlmsg_type;
      head->flags = hdr->nlmsg_flags;
      head->seq = hdr->nlmsg_seq;
      head->pid = hdr->nlmsg_pid;
      Filter::output(MessageStart::make(head));
      if (size > sizeof(m_header)) {
        auto n = size - sizeof(m_header);
        if (n > 0) {
          Deframer::pass(n);
          return PAYLOAD;
        } else {
          Filter::output(MessageEnd::make());
          return START;
        }
      } else {
        Filter::output(StreamEnd::make(StreamEnd::Error::PROTOCOL_ERROR));
        return -1;
      }
    }
    case PAYLOAD:
      Filter::output(MessageEnd::make());
      return START;
    default: return state;
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
  : Encoder()
{
}

Encoder::~Encoder()
{
}

void Encoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "encodeNetlink";
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
      m_buffer.clear();
      output(evt);
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_start) {
      m_buffer.push(*data);
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_start) {
      auto head = pjs::coerce<MessageHead>(m_start->head());
      nlmsghdr hdr;
      hdr.nlmsg_len = sizeof(hdr) + m_buffer.size();
      hdr.nlmsg_type = head->type;
      hdr.nlmsg_flags = head->flags;
      hdr.nlmsg_seq = head->seq;
      hdr.nlmsg_pid = head->pid;
      Data buf(&hdr, sizeof(hdr), &s_dp);
      buf.push(std::move(m_buffer));
      Filter::output(m_start);
      Filter::output(Data::make(std::move(buf)));
      Filter::output(evt);
    }

  } else if (evt->is<StreamEnd>()) {
    output(evt);
  }
}

} // namespace netlink
} // namespace pipy

namespace pjs {

using namespace pipy::netlink;

template<> void ClassDef<MessageHead>::init() {
  field<int>("type", [](MessageHead *obj) { return &obj->type; });
  field<int>("flags", [](MessageHead *obj) { return &obj->flags; });
  field<int>("seq", [](MessageHead *obj) { return &obj->seq; });
  field<int>("pid", [](MessageHead *obj) { return &obj->pid; });
}

} // namespace pjs
