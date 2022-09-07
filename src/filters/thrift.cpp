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
  variable("type", Message::Field::type);
  variable("name", Message::Field::name);
  variable("value", Message::Field::value);
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
// Decoder::Options
//

Decoder::Options::Options(pjs::Object *options) {
  Value(options, "body")
    .get(body)
    .check_nullable();
}

//
// Decoder
//

Decoder::Decoder(const Options &options)
  : m_options(options)
{
}

Decoder::Decoder(const Decoder &r)
  : m_options(r.m_options)
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
  m_read_data = nullptr;
  m_msg = nullptr;
  while (auto *s = m_stack) {
    m_stack = s->back;
    delete s;
  }
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
          if (!set_message_type(m_read_buf[3] & 0x07)) return ERROR;
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
          if (!set_message_type(m_read_buf[1] >> 5)) return ERROR;
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
      if (!set_message_type(c)) return ERROR;
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
        auto obj = pjs::Object::make();
        set_value(obj);
        return push_struct(obj);
      }

    case STRUCT_FIELD_TYPE:
      if (m_format == COMPACT) {
        // TODO
        return ERROR;
      } else {
        if (0 == c) {
          return pop();
        } else {
          State state;
          int read_size;
          set_value_type(c, state, read_size);
          if (state == ERROR) return state;
          m_stack->element_types[0] = state;
          m_stack->element_types[1] = state;
          m_stack->element_sizes[0] = read_size;
          m_stack->element_sizes[1] = read_size;
          Deframer::read(2, m_read_buf);
          return STRUCT_FIELD_ID;
        }
      }

    case STRUCT_FIELD_ID:
      if (m_format == COMPACT) {
        // TODO
        return ERROR;
      } else {
        m_stack->index = (
          ((int16_t)m_read_buf[0] << 8) |
          ((int16_t)m_read_buf[1] << 0)
        );
        return set_value_start();
      }

    case VALUE_BOOL:
      set_value(bool(c));
      return set_value_end();

    case VALUE_I8:
      set_value(int(c));
      return set_value_end();

    case VALUE_I16:
      set_value(
        ((int16_t)m_read_buf[0] << 8) |
        ((int16_t)m_read_buf[1] << 0)
      );
      return set_value_end();

    case VALUE_I32:
      set_value(
        ((int32_t)m_read_buf[0] << 24) |
        ((int32_t)m_read_buf[1] << 16) |
        ((int32_t)m_read_buf[2] <<  8) |
        ((int32_t)m_read_buf[3] <<  0)
      );
      return set_value_end();

    case VALUE_I64:
      set_value(double(
        ((int64_t)m_read_buf[0] << 56) |
        ((int64_t)m_read_buf[1] << 48) |
        ((int64_t)m_read_buf[2] << 40) |
        ((int64_t)m_read_buf[3] << 32) |
        ((int64_t)m_read_buf[4] << 24) |
        ((int64_t)m_read_buf[5] << 16) |
        ((int64_t)m_read_buf[6] <<  8) |
        ((int64_t)m_read_buf[7] <<  0)
      ));
      return set_value_end();

    case VALUE_DOUBLE: {
      uint64_t n = (
        ((uint64_t)m_read_buf[0] << 56) |
        ((uint64_t)m_read_buf[1] << 48) |
        ((uint64_t)m_read_buf[2] << 40) |
        ((uint64_t)m_read_buf[3] << 32) |
        ((uint64_t)m_read_buf[4] << 24) |
        ((uint64_t)m_read_buf[5] << 16) |
        ((uint64_t)m_read_buf[6] <<  8) |
        ((uint64_t)m_read_buf[7] <<  0)
      );
      set_value(*reinterpret_cast<double*>(&n));
      return set_value_end();
    }

    case VALUE_UUID:
      // TODO
      set_value(pjs::Value::null);
      return set_value_end();

    case BINARY_SIZE: {
      int n = (
        ((int32_t)m_read_buf[0] << 24) |
        ((int32_t)m_read_buf[1] << 16) |
        ((int32_t)m_read_buf[2] <<  8) |
        ((int32_t)m_read_buf[3] <<  0)
      );
      m_read_data = Data::make();
      Deframer::read(n, m_read_data);
      return BINARY_DATA;
    }

    case BINARY_DATA:
      try {
        set_value(m_read_data->to_string(Data::Encoding::UTF8));
      } catch (std::runtime_error &err) {
        set_value(m_read_data.get());
      }
      return set_value_end();

    case LIST_HEAD: {
      auto n = (
        ((int32_t)m_read_buf[1] << 24) |
        ((int32_t)m_read_buf[2] << 16) |
        ((int32_t)m_read_buf[3] <<  8) |
        ((int32_t)m_read_buf[4] <<  0)
      );
      auto *obj = pjs::Array::make();
      set_value(obj);
      if (n < 0) return ERROR;
      if (n > 0) return push_list(m_read_buf[0], n, obj);
      return set_value_end();
    }

    case SET_HEAD: {
      auto n = (
        ((int32_t)m_read_buf[1] << 24) |
        ((int32_t)m_read_buf[2] << 16) |
        ((int32_t)m_read_buf[3] <<  8) |
        ((int32_t)m_read_buf[4] <<  0)
      );
      auto *obj = pjs::Array::make();
      set_value(obj);
      if (n < 0) return ERROR;
      if (n > 0) return push_set(m_read_buf[0], n, obj);
      return set_value_end();
    }

    case MAP_HEAD: {
      auto n = (
        ((int32_t)m_read_buf[2] << 24) |
        ((int32_t)m_read_buf[3] << 16) |
        ((int32_t)m_read_buf[4] <<  8) |
        ((int32_t)m_read_buf[5] <<  0)
      );
      auto *obj = pjs::Object::make();
      set_value(obj);
      if (n < 0) return ERROR;
      if (n > 0) return push_map(m_read_buf[0], m_read_buf[1], n, obj);
      return set_value_end();
    }

    default: return ERROR;
  }
}

bool Decoder::set_message_type(int type) {
  switch (type) {
    case 1: m_msg->type(s_call); return true;
    case 2: m_msg->type(s_reply); return true;
    case 3: m_msg->type(s_exception); return true;
    case 4: m_msg->type(s_oneway); return true;
    default: return false;
  }
}

void Decoder::set_value_type(int type, State &state, int &read_size) {
  switch (type) {
    case 2: state = VALUE_BOOL; read_size = 1; break;
    case 3: state = VALUE_I8; read_size = 1; break;
    case 4: state = VALUE_DOUBLE; read_size = 8; break;
    case 6: state = VALUE_I16; read_size = 2; break;
    case 8: state = VALUE_I32; read_size = 4; break;
    case 10: state = VALUE_I64; read_size = 8; break;
    case 11: state = BINARY_SIZE; read_size = 4; break;
    case 12: state = STRUCT_FIELD_TYPE; read_size = 1; break;
    case 13: state = MAP_HEAD; read_size = 6; break;
    case 14: state = SET_HEAD; read_size = 5; break;
    case 15: state = LIST_HEAD; read_size = 5; break;
    case 16: state = VALUE_UUID; read_size = 16; break;
    default: state = ERROR; read_size = 0; break;
  }
}

auto Decoder::set_value_start() -> State {
  auto i = m_stack->index & 1;
  auto t = m_stack->element_types[i];
  auto n = m_stack->element_sizes[i];
  if (t == STRUCT_FIELD_TYPE) {
    auto obj = pjs::Object::make();
    set_value(obj);
    return push_struct(obj);
  }
  if (n > 1) Deframer::read(n, m_read_buf);
  return t;
}

auto Decoder::set_value_end() -> State {
  if (auto *s = m_stack) {
    if (s->kind == Level::STRUCT) {
      return STRUCT_FIELD_TYPE;
    } else if (s->index >= s->size) {
      return pop();
    } else {
      return set_value_start();
    }
  } else {
    return START;
  }
}

void Decoder::set_value(const pjs::Value &v) {
  if (auto *s = m_stack) {
    auto &i = s->index;
    switch (s->kind) {
      case Level::STRUCT:
        s->obj->set(pjs::Str::make(i), v);
        break;
      case Level::LIST:
        s->obj->as<pjs::Array>()->set(i++, v);
        break;
      case Level::SET:
        s->obj->as<pjs::Array>()->set(i++, v);
        break;
      case Level::MAP:
        if (i++ & 1) {
          if (s->key.is_string()) { // TODO
            s->obj->set(m_stack->key.s(), v);
          }
        } else {
          s->key = v;
        }
        break;
      default: return;
    }
  } else {
    if (v.is_object()) {
      m_msg->value(v.o());
    }
  }
}

auto Decoder::push_struct(pjs::Object *obj) -> State {
  auto *l = new Level;
  l->back = m_stack;
  l->kind = Level::STRUCT;
  l->obj = obj;
  m_stack = l;
  return STRUCT_FIELD_TYPE;
}

auto Decoder::push_list(int type, int size, pjs::Object *obj) -> State {
  State state;
  int read_size;
  set_value_type(type, state, read_size);
  auto *l = new Level;
  l->back = m_stack;
  l->kind = Level::LIST;
  l->element_types[0] = state;
  l->element_types[1] = state;
  l->element_sizes[0] = read_size;
  l->element_sizes[1] = read_size;
  l->size = size;
  l->index = 0;
  l->obj = obj;
  m_stack = l;
  if (read_size > 1) Deframer::read(read_size, m_read_buf);
  return state;
}

auto Decoder::push_set(int type, int size, pjs::Object *obj) -> State {
  State state;
  int read_size;
  set_value_type(type, state, read_size);
  auto *l = new Level;
  l->back = m_stack;
  l->kind = Level::SET;
  l->element_types[0] = state;
  l->element_types[1] = state;
  l->element_sizes[0] = read_size;
  l->element_sizes[1] = read_size;
  l->size = size;
  l->index = 0;
  l->obj = obj;
  m_stack = l;
  if (read_size > 1) Deframer::read(read_size, m_read_buf);
  return state;
}

auto Decoder::push_map(int type_k, int type_v, int size, pjs::Object *obj) -> State {
  State state_k, state_v;
  int read_size_k, read_size_v;
  set_value_type(type_k, state_k, read_size_k);
  set_value_type(type_v, state_v, read_size_v);
  auto *l = new Level;
  l->back = m_stack;
  l->kind = Level::SET;
  l->element_types[0] = state_k;
  l->element_types[1] = state_v;
  l->element_sizes[0] = read_size_k;
  l->element_sizes[1] = read_size_v;
  l->size = size * 2;
  l->index = 0;
  l->obj = obj;
  m_stack = l;
  if (read_size_k > 1) Deframer::read(read_size_k, m_read_buf);
  return state_k;
}

auto Decoder::pop() -> State {
  if (!m_stack) return ERROR;
  do {
    auto *l = m_stack;
    m_stack = l->back;
    delete l;
    if (!m_stack) {
      message_start();
      message_end();
      return START;
    }
    if (m_stack->kind == Level::STRUCT) return STRUCT_FIELD_TYPE;
  } while (m_stack->index >= m_stack->size);
  return set_value_start();
}

void Decoder::message_start() {
  Filter::output(MessageStart::make(m_msg));
}

void Decoder::message_end() {
  Filter::output(MessageEnd::make());
}

} // namespace thrift
} // namespace pipy
