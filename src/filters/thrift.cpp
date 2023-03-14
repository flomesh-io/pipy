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

namespace pipy {
namespace thrift {

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
  d.name = "decodeThrift";
}

auto Decoder::clone() -> Filter* {
  return new Decoder(*this);
}

void Decoder::reset() {
  Filter::reset();
  Thrift::Parser::reset();
}

void Decoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
    Thrift::Parser::reset();
  } else if (auto *data = evt->as<Data>()) {
    Thrift::Parser::parse(*data);
  }
}

void Decoder::on_pass(Data &data) {
  Filter::output(Data::make(std::move(data)));
}

void Decoder::on_message_start() {
  Filter::output(MessageStart::make());
}

void Decoder::on_message_end(Thrift::Message *msg) {
  Filter::output(MessageEnd::make(nullptr, msg));
}

//
// Encoder
//

thread_local static Data::Producer s_dp("Thrift");
thread_local static pjs::ConstStr s_binary("binary");
thread_local static pjs::ConstStr s_compact("compact");
thread_local static pjs::ConstStr s_call("call");
thread_local static pjs::ConstStr s_reply("reply");
thread_local static pjs::ConstStr s_exception("exception");
thread_local static pjs::ConstStr s_oneway("oneway");

Encoder::Encoder()
  : m_prop_seqID("seqID")
  , m_prop_type("type")
  , m_prop_name("name")
  , m_prop_protocol("protocol")
{
}

Encoder::Encoder(const Encoder &r)
  : Filter(r)
  , m_prop_seqID("seqID")
  , m_prop_type("type")
  , m_prop_name("name")
  , m_prop_protocol("protocol")
{
}

Encoder::~Encoder()
{
}

void Encoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "encodeThrift";
}

auto Encoder::clone() -> Filter* {
  return new Encoder(*this);
}

void Encoder::reset() {
  Filter::reset();
  m_started = false;
}

void Encoder::process(Event *evt) {
  if (auto *start = evt->as<MessageStart>()) {
    if (!m_started) {
      int seq_id = 0;
      pjs::Str *type = nullptr;
      pjs::Str *name = nullptr;
      pjs::Str *protocol = nullptr;
      if (auto *head = start->head()) {
        m_prop_seqID.get(head, seq_id);
        m_prop_type.get(head, type);
        m_prop_name.get(head, name);
        m_prop_protocol.get(head, protocol);
      }

      Data data;
      Data::Builder db(data, &s_dp);

      if (protocol == s_compact) {
        char t = 1;
        if (type == s_reply) t = 2;
        else if (type == s_exception) t = 3;
        else if (type == s_oneway) t = 4;
        db.push(0x82);
        db.push(0x01 | (t << 5));
        var_int(db, (uint32_t)seq_id);
        var_int(db, (uint32_t)(name ? name->size() : 0));
        if (name) db.push(name->c_str(), name->size());

      } else {
        db.push(0x80);
        db.push(0x01);
        db.push('\0');
        if (type == s_reply) db.push(0x02);
        else if (type == s_exception) db.push(0x03);
        else if (type == s_oneway) db.push(0x04);
        else db.push(0x01);
        if (name) {
          int len = name->size();
          db.push(0xff & (len >> 24));
          db.push(0xff & (len >> 16));
          db.push(0xff & (len >>  8));
          db.push(0xff & (len >>  0));
          db.push(name->c_str(), len);
        } else {
          db.push('\0');
          db.push('\0');
          db.push('\0');
          db.push('\0');
        }
        db.push(0xff & (seq_id >> 24));
        db.push(0xff & (seq_id >> 16));
        db.push(0xff & (seq_id >>  8));
        db.push(0xff & (seq_id >>  0));
      }

      db.flush();
      Filter::output(evt);
      Filter::output(Data::make(std::move(data)));
      m_started = true;
    }

  } else if (evt->is<Data>()) {
    if (m_started) {
      Filter::output(evt);
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_started) {
      m_started = false;
      Filter::output(evt);
    }

  } else if (evt->is<StreamEnd>()) {
    m_started = false;
    Filter::output(evt);
  }
}

void Encoder::var_int(Data::Builder &db, uint64_t i) {
  do {
    char c = i & 0x7f;
    i >>= 7;
    if (!i) db.push(c); else db.push(c | 0x80);
  } while (i);
}

} // namespace thrift
} // namespace pipy
