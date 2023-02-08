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

template<> void ClassDef<MessageHead>::init() {
  variable("seqID", MessageHead::Field::seqID);
  variable("type", MessageHead::Field::type);
  variable("name", MessageHead::Field::name);
  variable("protocol", MessageHead::Field::protocol);
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

thread_local static Data::Producer s_dp("Thrift");
thread_local static pjs::ConstStr s_binary("binary");
thread_local static pjs::ConstStr s_compact("compact");
thread_local static pjs::ConstStr s_call("call");
thread_local static pjs::ConstStr s_reply("reply");
thread_local static pjs::ConstStr s_exception("exception");
thread_local static pjs::ConstStr s_oneway("oneway");

//
// Decoder::Options
//

Decoder::Options::Options(pjs::Object *options) {
  Value(options, "payload")
    .get(payload)
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
  : Filter(r)
  , m_options(r.m_options)
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
  m_head = nullptr;
  m_payload = nullptr;
  while (auto *s = m_stack) {
    m_stack = s->back;
    delete s;
  }
  m_started = false;
}

void Decoder::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
    Deframer::reset();
  } else if (auto *data = evt->as<Data>()) {
    Deframer::deframe(*data);
    if (Deframer::state() == START) message_end();
  }
}

auto Decoder::on_state(int state, int c) -> int {
  switch (state) {
    case START:
      message_end();
      m_read_buf[0] = c;
      if (c == 0x80) {
        m_format = BINARY;
        Deframer::read(7, m_read_buf + 1);
        return MESSAGE_HEAD;
      } else if (c == 0x82) {
        m_format = COMPACT;
        return MESSAGE_HEAD;
      } else if (c & 0x80) {
        return ERROR;
      } else {
        m_format = BINARY_OLD;
        Deframer::read(3, m_read_buf + 1);
        return MESSAGE_HEAD;
      }

    case MESSAGE_HEAD:
      m_head = MessageHead::make();
      switch (m_format) {
        case BINARY: {
          m_head->protocol(s_binary);
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
          m_head->protocol(s_binary);
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
          m_head->protocol(s_compact);
          if ((c & 0x1f) != 1) return ERROR;
          if (!set_message_type(c >> 5)) return ERROR;
          m_var_int = 0;
          return SEQ_ID;
        }
      }
      return ERROR;

    case MESSAGE_NAME_LEN: // must be compact protocol
      if (var_int(c)) return MESSAGE_NAME_LEN;
      m_read_data = Data::make();
      Deframer::read(m_var_int, m_read_data);
      return MESSAGE_NAME;

    case MESSAGE_NAME:
      m_head->name(pjs::Str::make(m_read_data->to_string()));
      switch (m_format) {
        case BINARY: Deframer::read(4, m_read_buf); return SEQ_ID;
        case BINARY_OLD: return MESSAGE_TYPE;
        case COMPACT: return message_start();
      }
      return ERROR;

    case MESSAGE_TYPE: // must be binary protocol old encoding
      if (!set_message_type(c)) return ERROR;
      Deframer::read(4, m_read_buf);
      return SEQ_ID;

    case SEQ_ID:
      if (m_format == COMPACT) {
        if (var_int(c)) return SEQ_ID;
        m_head->seqID(m_var_int);
        m_var_int = 0;
        return MESSAGE_NAME_LEN;
      } else {
        m_head->seqID(
          ((int32_t)m_read_buf[0] << 24) |
          ((int32_t)m_read_buf[1] << 16) |
          ((int32_t)m_read_buf[2] <<  8) |
          ((int32_t)m_read_buf[3] <<  0)
        );
        return message_start();
      }

    case STRUCT_FIELD_TYPE:
      if (0 == c) {
        return pop();
      } else if (m_format == COMPACT) {
        auto state = set_field_type(c & 0xf);
        if (state == ERROR) return state;
        if (c & 0xf0) {
          m_stack->index += (c >> 4) & 0x0f;
          if (m_format == COMPACT && state == VALUE_BOOL) {
            set_value(m_bool_field);
            return set_value_end();
          }
          return state;
        }
        m_var_int = 0;
      } else {
        auto state = set_field_type(c);
        if (state == ERROR) return state;
        Deframer::read(2, m_read_buf);
      }
      return STRUCT_FIELD_ID;

    case STRUCT_FIELD_ID:
      if (m_format == COMPACT) {
        if (var_int(c)) return STRUCT_FIELD_ID;
        m_stack->index = zigzag_to_int((uint32_t)m_var_int);
        auto state = m_stack->element_types[0];
        if (state == VALUE_BOOL) {
          set_value(m_bool_field);
          return set_value_end();
        }
        return state;
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
      if (m_format == COMPACT) {
        if (var_int(c)) return VALUE_I16;
        set_value(zigzag_to_int((uint32_t)m_var_int));
      } else {
        set_value(
          ((int16_t)m_read_buf[0] << 8) |
          ((int16_t)m_read_buf[1] << 0)
        );
      }
      return set_value_end();

    case VALUE_I32:
      if (m_format == COMPACT) {
        if (var_int(c)) return VALUE_I32;
        set_value(zigzag_to_int((uint32_t)m_var_int));
      } else {
        set_value(
          ((int32_t)m_read_buf[0] << 24) |
          ((int32_t)m_read_buf[1] << 16) |
          ((int32_t)m_read_buf[2] <<  8) |
          ((int32_t)m_read_buf[3] <<  0)
        );
      }
      return set_value_end();

    case VALUE_I64:
      if (m_format == COMPACT) {
        if (var_int(c)) return VALUE_I64;
        set_value((double)zigzag_to_int(m_var_int));
      } else {
        set_value((double)(
          ((int64_t)m_read_buf[0] << 56) |
          ((int64_t)m_read_buf[1] << 48) |
          ((int64_t)m_read_buf[2] << 40) |
          ((int64_t)m_read_buf[3] << 32) |
          ((int64_t)m_read_buf[4] << 24) |
          ((int64_t)m_read_buf[5] << 16) |
          ((int64_t)m_read_buf[6] <<  8) |
          ((int64_t)m_read_buf[7] <<  0)
        ));
      }
      return set_value_end();

    case VALUE_DOUBLE:
      if (m_format == COMPACT) {
        uint64_t n = (
          ((uint64_t)m_read_buf[0] <<  0) |
          ((uint64_t)m_read_buf[1] <<  8) |
          ((uint64_t)m_read_buf[2] << 16) |
          ((uint64_t)m_read_buf[3] << 24) |
          ((uint64_t)m_read_buf[4] << 32) |
          ((uint64_t)m_read_buf[5] << 40) |
          ((uint64_t)m_read_buf[6] << 48) |
          ((uint64_t)m_read_buf[7] << 56)
        );
        set_value(*reinterpret_cast<double*>(&n));
      } else {
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
      }
      return set_value_end();

    case VALUE_UUID:
      // TODO
      set_value(pjs::Value::null);
      return set_value_end();

    case BINARY_SIZE:
      if (m_format == COMPACT) {
        if (var_int(c)) return BINARY_SIZE;
        m_read_data = Data::make();
        Deframer::read(m_var_int, m_read_data);
      } else {
        int n = (
          ((int32_t)m_read_buf[0] << 24) |
          ((int32_t)m_read_buf[1] << 16) |
          ((int32_t)m_read_buf[2] <<  8) |
          ((int32_t)m_read_buf[3] <<  0)
        );
        m_read_data = Data::make();
        Deframer::read(n, m_read_data);
      }
      return BINARY_DATA;

    case BINARY_DATA:
      try {
        set_value(m_read_data->to_string(Data::Encoding::UTF8));
      } catch (std::runtime_error &err) {
        set_value(m_read_data.get());
      }
      return set_value_end();

    case LIST_HEAD:
      if (m_format == COMPACT) {
        m_element_type = c & 0x0f;
        if ((c & 0xf0) == 0xf0) return LIST_SIZE;
        return push_list(m_element_type, (c & 0xf0) >> 4);
      } else {
        auto n = (
          ((int32_t)m_read_buf[1] << 24) |
          ((int32_t)m_read_buf[2] << 16) |
          ((int32_t)m_read_buf[3] <<  8) |
          ((int32_t)m_read_buf[4] <<  0)
        );
        if (n < 0) return ERROR;
        return push_list(m_read_buf[0], n);
      }

    case LIST_SIZE: // must be compact protocol
      if (var_int(c)) return LIST_SIZE;
      return push_list(m_element_type, m_var_int);

    case SET_HEAD:
      if (m_format == COMPACT) {
        m_element_type = c & 0x0f;
        if ((c & 0xf0) == 0xf0) return SET_SIZE;
        return push_set(m_element_type, (c & 0xf0) >> 4);
      } else {
        auto n = (
          ((int32_t)m_read_buf[1] << 24) |
          ((int32_t)m_read_buf[2] << 16) |
          ((int32_t)m_read_buf[3] <<  8) |
          ((int32_t)m_read_buf[4] <<  0)
        );
        if (n < 0) return ERROR;
        return push_set(m_read_buf[0], n);
      }

    case SET_SIZE: // must be compact protocol
      if (var_int(c)) return SET_SIZE;
      return push_set(m_element_type, m_var_int);

    case MAP_HEAD:
      if (m_format == COMPACT) {
        if (var_int(c)) return MAP_HEAD;
        return MAP_TYPE;
      } else {
        auto n = (
          ((int32_t)m_read_buf[2] << 24) |
          ((int32_t)m_read_buf[3] << 16) |
          ((int32_t)m_read_buf[4] <<  8) |
          ((int32_t)m_read_buf[5] <<  0)
        );
        if (n < 0) return ERROR;
        return push_map(m_read_buf[0], m_read_buf[1], n);
      }

    case MAP_TYPE: // must be compact protocol
      return push_map(
        (c & 0xf0) >> 4,
        (c & 0x0f) >> 0,
        m_var_int
      );

    default: return ERROR;
  }
}

void Decoder::on_pass(const Data &data) {
  Filter::output(Data::make(data));
}

bool Decoder::set_message_type(int type) {
  switch (type) {
    case 1: m_head->type(s_call); return true;
    case 2: m_head->type(s_reply); return true;
    case 3: m_head->type(s_exception); return true;
    case 4: m_head->type(s_oneway); return true;
    default: return false;
  }
}

auto Decoder::set_field_type(int type) -> State {
  State state;
  int read_size;
  if (m_format == COMPACT) {
    if (type == 1) {
      state = VALUE_BOOL;
      read_size = 0;
      m_bool_field = true;
    } else if (type == 2) {
      state = VALUE_BOOL;
      read_size = 0;
      m_bool_field = false;
    } else {
      set_value_type(type, state, read_size);
    }
  } else {
    set_value_type(type, state, read_size);
  }
  if (state == ERROR) return state;
  m_stack->element_types[0] = state;
  m_stack->element_types[1] = state;
  m_stack->element_sizes[0] = read_size;
  m_stack->element_sizes[1] = read_size;
  return state;
}

void Decoder::set_value_type(int type, State &state, int &read_size) {
  if (m_format == COMPACT) {
    switch (type) {
      case 2: state = VALUE_BOOL; read_size = 1; break;
      case 3: state = VALUE_I8; read_size = 1; break;
      case 4: state = VALUE_I16; read_size = 1; m_var_int = 0; break;
      case 5: state = VALUE_I32; read_size = 1; m_var_int = 0; break;
      case 6: state = VALUE_I64; read_size = 1; m_var_int = 0; break;
      case 7: state = VALUE_DOUBLE; read_size = 8; break;
      case 8: state = BINARY_SIZE; read_size = 1; m_var_int = 0; break;
      case 9: state = LIST_HEAD; read_size = 1; m_var_int = 0; break;
      case 10: state = SET_HEAD; read_size = 1; m_var_int = 0; break;
      case 11: state = MAP_HEAD; read_size = 1; m_var_int = 0; break;
      case 12: state = STRUCT_FIELD_TYPE; read_size = 1; break;
      case 13: state = VALUE_UUID; read_size = 16; break;
      default: state = ERROR; read_size = 0; break;
    }
  } else {
    switch (type) {
      case 2: state = VALUE_BOOL; read_size = 1; break;
      case 3: state = VALUE_I8; read_size = 1; break;
      case 4: state = VALUE_DOUBLE; read_size = 8; break;
      case 6: state = VALUE_I16; read_size = 2; m_var_int = 0; break;
      case 8: state = VALUE_I32; read_size = 4; m_var_int = 0; break;
      case 10: state = VALUE_I64; read_size = 8; m_var_int = 0; break;
      case 11: state = BINARY_SIZE; read_size = 4; m_var_int = 0; break;
      case 12: state = STRUCT_FIELD_TYPE; read_size = 1; break;
      case 13: state = MAP_HEAD; read_size = 6; m_var_int = 0; break;
      case 14: state = SET_HEAD; read_size = 5; m_var_int = 0; break;
      case 15: state = LIST_HEAD; read_size = 5; m_var_int = 0; break;
      case 16: state = VALUE_UUID; read_size = 16; break;
      default: state = ERROR; read_size = 0; break;
    }
  }
}

auto Decoder::set_value_start() -> State {
  auto i = m_stack->index & 1;
  auto t = m_stack->element_types[i];
  auto n = m_stack->element_sizes[i];
  if (t == STRUCT_FIELD_TYPE) return push_struct();
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
    Deframer::pass_all(false);
    Deframer::need_flush();
    return START;
  }
}

void Decoder::set_value(const pjs::Value &v) {
  if (m_options.payload) {
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
          if (i & 1) {
            auto *ent = pjs::Array::make(2);
            ent->set(0, s->key);
            ent->set(1, v);
            s->obj->as<pjs::Array>()->set(i/2, ent);
          } else {
            s->key = v;
          }
          i++;
          break;
        default: return;
      }
    } else {
      if (v.is_object()) {
        m_payload = v.o();
      }
    }
  }
}

auto Decoder::push_struct() -> State {
  auto *l = new Level;
  l->back = m_stack;
  l->kind = Level::STRUCT;
  l->index = 0;
  if (m_options.payload) {
    auto obj = pjs::Object::make();
    set_value(obj);
    l->obj = obj;
  }
  m_stack = l;
  return STRUCT_FIELD_TYPE;
}

auto Decoder::push_list(int type, int size) -> State {
  if (size <= 0) return set_value_end();
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
  if (m_options.payload) {
    auto *obj = pjs::Array::make();
    set_value(obj);
    l->obj = obj;
  }
  m_stack = l;
  if (read_size > 1) Deframer::read(read_size, m_read_buf);
  return state;
}

auto Decoder::push_set(int type, int size) -> State {
  if (size <= 0) return set_value_end();
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
  if (m_options.payload) {
    auto *obj = pjs::Array::make();
    set_value(obj);
    l->obj = obj;
  }
  m_stack = l;
  if (read_size > 1) Deframer::read(read_size, m_read_buf);
  return state;
}

auto Decoder::push_map(int type_k, int type_v, int size) -> State {
  if (size <= 0) return set_value_end();
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
  if (m_options.payload) {
    auto *obj = pjs::Array::make();
    set_value(obj);
    l->obj = obj;
  }
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
      Deframer::pass_all(false);
      Deframer::need_flush();
      return START;
    }
    if (m_stack->kind == Level::STRUCT) return STRUCT_FIELD_TYPE;
  } while (m_stack->index >= m_stack->size);
  return set_value_start();
}

bool Decoder::var_int(int c) {
  m_var_int = (m_var_int << 7) | (c & 0x7f);
  return (c & 0x80);
}

auto Decoder::message_start() -> State {
  if (!m_started) {
    Filter::output(MessageStart::make(m_head));
    m_started = true;
  }
  Deframer::pass_all(true);
  return push_struct();
}

void Decoder::message_end() {
  if (m_started) {
    Filter::output(MessageEnd::make(nullptr, m_payload.get()));
    m_started = false;
  }
}

auto Decoder::zigzag_to_int(uint32_t i) -> int32_t {
  return (i >> 1) ^ - (i & 1);
}

auto Decoder::zigzag_to_int(uint64_t i) -> int64_t {
  return (i >> 1) ^ - (i & 1);
}

//
// Encoder
//

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
