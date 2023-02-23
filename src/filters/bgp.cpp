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

#include "bgp.hpp"

namespace pipy {
namespace bgp {

//
// Decoder
//

Decoder::Decoder()
{
}

Decoder::Decoder(const Decoder &r)
  : Filter(r)
{
}

Decoder::~Decoder()
{
}

void Decoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "decodeBGP";
}

auto Decoder::clone() -> Filter* {
  return new Decoder(*this);
}

void Decoder::reset() {
  Filter::reset();
  BGP::Parser::reset();
}

void Decoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
    BGP::Parser::reset();
  } else if (auto *data = evt->as<Data>()) {
    BGP::Parser::parse(*data);
  }
}

void Decoder::on_pass(const Data &data) {
  Filter::output(Data::make(data));
}

void Decoder::on_message_start() {
  Filter::output(MessageStart::make());
}

void Decoder::on_message_end(pjs::Object *payload) {
  Filter::output(MessageEnd::make(nullptr, payload));
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
  d.name = "encodeBGP";
}

auto Encoder::clone() -> Filter* {
  return new Encoder(*this);
}

void Encoder::reset() {
  Filter::reset();
  m_message_started = false;
}

void Encoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    m_message_started = false;
    Filter::output(evt);
  } else if (evt->is<MessageStart>()) {
    if (!m_message_started) {
      m_message_started = true;
      Filter::output(evt);
    }
  } else if (evt->is<MessageEnd>()) {
    if (m_message_started) {
      m_message_started = false;
      const auto &payload = evt->as<MessageEnd>()->payload();
      if (payload.is_object()) {
        if (auto *obj = payload.o()) {
          Data buf;
          BGP::encode(obj, buf);
          Filter::output(Data::make(std::move(buf)));
        }
      }
      Filter::output(evt);
    }
  }
}

} // namespace bgp
} // namespace pipy
