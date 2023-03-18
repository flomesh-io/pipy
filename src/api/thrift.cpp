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

//
// # Binary protocol
//
// ## Message
//
// Strict encoding, 12+ bytes:
// +--------+--------+--------+--------+--------+--------+--------+--------+--------+...+--------+--------+--------+--------+--------+
// |1vvvvvvv|vvvvvvvv|unused  |00000mmm| name length                       | name                | seq id                            |
// +--------+--------+--------+--------+--------+--------+--------+--------+--------+...+--------+--------+--------+--------+--------+
//
// Old encoding, 9+ bytes:
// +--------+--------+--------+--------+--------+...+--------+--------+--------+--------+--------+--------+
// | name length                       | name                |00000mmm| seq id                            |
// +--------+--------+--------+--------+--------+...+--------+--------+--------+--------+--------+--------+
//
// ## Struct
//
// Field header and field value:
// +--------+--------+--------+--------+...+--------+
// |tttttttt| field id        | field value         |
// +--------+--------+--------+--------+...+--------+
//
// Stop field
// +--------+
// |00000000|
// +--------+
//
// ## List and set
//
// List header and elements:
// +--------+--------+--------+--------+--------+--------+...+--------+
// |tttttttt| size                              | elements            |
// +--------+--------+--------+--------+--------+--------+...+--------+
//
// ## Map
//
// Map header and key value pairs:
// +--------+--------+--------+--------+--------+--------+--------+...+--------+
// |kkkkkkkk|vvvvvvvv| size                              | key value pairs     |
// +--------+--------+--------+--------+--------+--------+--------+...+--------+
//
//
//
// # Compact protocol
//
// ## Message (4+ bytes)
// +--------+--------+--------+...+--------+--------+...+--------+--------+...+--------+
// |pppppppp|mmmvvvvv| seq id              | name length         | name                |
// +--------+--------+--------+...+--------+--------+...+--------+--------+...+--------+
//
// ## Struct
//
// Field header (short form) and field value:
// +--------+--------+...+--------+
// |ddddtttt| field value         |
// +--------+--------+...+--------+
//
// Field header (1 to 3 bytes, long form) and field value:
// +--------+--------+...+--------+--------+...+--------+
// |0000tttt| field id            | field value         |
// +--------+--------+...+--------+--------+...+--------+
//
// Stop field:
// +--------+
// |00000000|
// +--------+
//
// ## List and set
//
// List header (1 byte, short form) and elements:
// +--------+--------+...+--------+
// |sssstttt| elements            |
// +--------+--------+...+--------+
//
// List header (2+ bytes, long form) and elements:
// +--------+--------+...+--------+--------+...+--------+
// |1111tttt| size                | elements            |
// +--------+--------+...+--------+--------+...+--------+
//
// ## Map
//
// Map header (1 byte, empty map):
// +--------+
// |00000000|
// +--------+
//
// Map header (2+ bytes, non empty map) and key value pairs:
// +--------+...+--------+--------+--------+...+--------+
// | size                |kkkkvvvv| key value pairs     |
// +--------+...+--------+--------+--------+...+--------+
//

namespace pipy {

//
// Thrift
//

thread_local static Data::Producer s_dp("Thrift");

auto Thrift::decode(const Data &data) -> pjs::Array* {
  pjs::Array *a = pjs::Array::make();
  StreamParser sp(
    [=](const pjs::Value &value) {
      a->push(value);
    }
  );
  Data buf(data);
  sp.parse(buf);
  return a;
}

void Thrift::encode(pjs::Object *msg, Data &data) {
  Data::Builder db(data, &s_dp);
  encode(msg, db);
  db.flush();
}

void Thrift::encode(pjs::Object *msg, Data::Builder &db) {
  static struct { int binary_code, compact_code; }
  s_type_codes[] = {
    { 2 , 2  }, // BOOL
    { 3 , 3  }, // I8
    { 6 , 4  }, // I16
    { 8 , 5  }, // I32
    { 10, 6  }, // I64
    { 4 , 7  }, // DOUBLE
    { 11, 8  }, // BINARY
    { 12, 12 }, // STRUCT
    { 13, 11 }, // MAP
    { 14, 10 }, // SET
    { 15, 9  }, // LIST
    { 16, 13 }, // UUID
  };

  if (!msg) return;

  auto get_type_code = [](Protocol protocol, Type type) -> int {
    if (Type::BOOL <= type && type <= Type::UUID) {
      int i = int(type) - int(Type::BOOL);
      if (protocol == Protocol::compact) {
        return s_type_codes[i].compact_code;
      } else {
        return s_type_codes[i].binary_code;
      }
    }
    return 0;
  };

  auto get_zigzag_int32 = [](int32_t i) -> uint32_t {
    return (i << 1) ^ (i >> 31);
  };

  auto get_zigzag_int64 = [](int64_t i) -> uint64_t {
    return (i << 1) ^ (i >> 63);
  };

  auto write_varint = [&](uint64_t i) {
    do {
      char c = i & 0x7f;
      i >>= 7;
      if (!i) db.push(c); else db.push(c | 0x80);
    } while (i);
  };

  std::function<void(Protocol, Type, const pjs::Value &)> write_value;
  write_value = [&](Protocol protocol, Type type, const pjs::Value &value) {
    switch (type) {
      case Type::BOOL:
        db.push(value.to_boolean() ? 1 : 0);
        break;
      case Type::I8:
        db.push(int(value.to_int32()));
        break;
      case Type::I16:
        if (protocol == Protocol::compact) {
          write_varint(
            get_zigzag_int32(
              int16_t(value.to_int32())
            )
          );
        } else {
          auto i = int16_t(value.to_int32());
          db.push(i >> 8);
          db.push(i >> 0);
        }
        break;
      case Type::I32:
        if (protocol == Protocol::compact) {
          write_varint(
            get_zigzag_int32(
              int32_t(value.to_int32())
            )
          );
        } else {
          auto i = int32_t(value.to_int32());
          db.push(i >> 24);
          db.push(i >> 16);
          db.push(i >>  8);
          db.push(i >>  0);
        }
        break;
      case Type::I64:
        if (protocol == Protocol::compact) {
          write_varint(
            get_zigzag_int64(
              value.to_int64()
            )
          );
        } else {
          auto i = int64_t(value.to_int64());
          db.push(char(i >> 56));
          db.push(char(i >> 48));
          db.push(char(i >> 40));
          db.push(char(i >> 32));
          db.push(char(i >> 24));
          db.push(char(i >> 16));
          db.push(char(i >>  8));
          db.push(char(i >>  0));
        }
        break;
      case Type::DOUBLE: {
        auto n = value.to_int64();
        auto i = *reinterpret_cast<uint64_t*>(&n);
        db.push(char(i >> 56));
        db.push(char(i >> 48));
        db.push(char(i >> 40));
        db.push(char(i >> 32));
        db.push(char(i >> 24));
        db.push(char(i >> 16));
        db.push(char(i >>  8));
        db.push(char(i >>  0));
        break;
      }
      case Type::BINARY: {
        uint32_t size = 0;
        if (value.is_string()) size = value.s()->size();
        else if (value.is<Data>()) size = value.as<Data>()->size();
        if (protocol == Protocol::compact) {
          write_varint(size);
        } else {
          int i = size;
          db.push(i >> 24);
          db.push(i >> 16);
          db.push(i >>  8);
          db.push(i >>  0);
        }
        if (value.is_string()) db.push(value.s()->str());
        else if (value.is<Data>()) db.push(*value.as<Data>());
        break;
      }
      case Type::STRUCT:
        if (value.is_array()) {
          auto a = value.as<pjs::Array>();
          auto i = 0;
          a->iterate_all(
            [&](pjs::Value &v, int) {
              if (v.is_object() && v.o()) {
                pjs::Ref<Field> f = pjs::coerce<Field>(v.o());
                int t = get_type_code(protocol, f->type);
                if (protocol == Protocol::compact) {
                  if (f->type == Type::BOOL) t = f->value.to_boolean() ? 1 : 2;
                  int d = f->id - i;
                  if (1 <= d && d <= 15) {
                    db.push((d << 4) | t);
                  } else {
                    db.push(t);
                    write_varint(get_zigzag_int32(f->id));
                  }
                  if (f->type != Type::BOOL) write_value(protocol, f->type, f->value);
                  i = f->id;
                } else {
                  db.push(t);
                  db.push(f->id >> 8);
                  db.push(f->id >> 0);
                  write_value(protocol, f->type, f->value);
                }
              }
            }
          );
        }
        db.push(0);
        break;
      case Type::MAP: {
        pjs::Ref<Map> m = pjs::coerce<Map>(value.is_object() ? value.o() : nullptr);
        auto size = m->pairs ? m->pairs->length() : 0;
        auto kt = get_type_code(protocol, m->keyType);
        auto vt = get_type_code(protocol, m->valueType);
        if (protocol == Protocol::compact) {
          if (size == 0) {
            db.push(0); // empty map
          } else {
            write_varint(size);
            db.push((kt << 4) | (vt & 0xf));
          }
        } else {
          int i = size;
          db.push(kt);
          db.push(vt);
          db.push(i >> 24);
          db.push(i >> 16);
          db.push(i >>  8);
          db.push(i >>  0);
        }
        for (int i = 0; i < size; i++) {
          pjs::Value p, k, v;
          m->pairs->get(i, p);
          if (p.is_array()) {
            p.as<pjs::Array>()->get(0, k);
            p.as<pjs::Array>()->get(1, v);
          }
          write_value(protocol, m->keyType, k);
          write_value(protocol, m->valueType, v);
        }
        break;
      }
      case Type::SET:
      case Type::LIST: {
        pjs::Ref<List> l = pjs::coerce<List>(value.is_object() ? value.o() : nullptr);
        auto size = l->elements ? l->elements->length() : 0;
        auto type = get_type_code(protocol, l->elementType);
        if (protocol == Protocol::compact) {
          if (0 <= size && size <= 14) {
            db.push((size << 4) | (type & 0xf));
          } else {
            db.push(0xf0 | (type & 0xf));
            write_varint(size);
          }
        } else {
          int i = size;
          db.push(type);
          db.push(i >> 24);
          db.push(i >> 16);
          db.push(i >>  8);
          db.push(i >>  0);
        }
        for (int i = 0; i < size; i++) {
          pjs::Value e;
          l->elements->get(i, e);
          write_value(protocol, l->elementType, e);
        }
        break;
      }
      case Type::UUID: {
        uint8_t data[16] = { 0 };
        if (value.is_string()) {
          utils::get_uuid(value.s()->str(), data);
        }
        db.push(data, sizeof(data));
        break;
      }
    }
  };

  auto write_message = [&](pjs::Object *obj) {
    pjs::Ref<Message> msg = pjs::coerce<Message>(obj);
    auto protocol = msg->protocol.get();

    switch (protocol) {
      case Protocol::binary:
        db.push(0x80);
        db.push(0x01);
        db.push(0x00);
        db.push(int(msg->type));
        if (auto s = msg->name.get()) {
          int len = s->size();
          db.push(0xff & (len >> 24));
          db.push(0xff & (len >> 16));
          db.push(0xff & (len >>  8));
          db.push(0xff & (len >>  0));
          db.push(s->str());
        } else {
          db.push(0);
          db.push(0);
          db.push(0);
          db.push(0);
        }
        db.push(0xff & (msg->seqID >> 24));
        db.push(0xff & (msg->seqID >> 16));
        db.push(0xff & (msg->seqID >>  8));
        db.push(0xff & (msg->seqID >>  0));
        break;

      case Protocol::compact:
        db.push(0x82);
        db.push(0x01 | (int(msg->type) << 5));
        write_varint(uint32_t(msg->seqID));
        write_varint(uint32_t(msg->name ? msg->name->size() : 0));
        if (auto s = msg->name.get()) db.push(s->str());
        break;

      case Protocol::old:
        if (auto s = msg->name.get()) {
          int len = s->size();
          db.push(0xff & (len >> 24));
          db.push(0xff & (len >> 16));
          db.push(0xff & (len >>  8));
          db.push(0xff & (len >>  0));
          db.push(s->str());
        } else {
          db.push(0);
          db.push(0);
          db.push(0);
          db.push(0);
        }
        db.push(int(msg->type));
        db.push(0xff & (msg->seqID >> 24));
        db.push(0xff & (msg->seqID >> 16));
        db.push(0xff & (msg->seqID >>  8));
        db.push(0xff & (msg->seqID >>  0));
        break;

      default: return;
    }

    write_value(protocol, Type::STRUCT, msg->fields.get());
  };

  if (msg->is_array()) {
    msg->as<pjs::Array>()->iterate_while(
      [&](pjs::Value v, int) {
        if (!v.is_object()) return false;
        if (!v.is_null()) write_message(v.o());
        return true;
      }
    );
  } else {
    write_message(msg);
  }
}

//
// Thrift::Parser
//

Thrift::Parser::Parser()
  : m_read_data(Data::make())
{
}

void Thrift::Parser::reset() {
  Deframer::reset();
  Deframer::pass_all(true);
  while (auto *s = m_stack) {
    m_stack = s->back;
    delete s;
  }
  m_stack = nullptr;
  m_read_data->clear();
}

void Thrift::Parser::parse(Data &data) {
  Deframer::deframe(data);
  if (Deframer::state() == START) end();
}

auto Thrift::Parser::on_state(int state, int c) -> int {
  switch (state) {
    case START:
      end();
      start();
      m_read_buf[0] = c;
      if (c == 0x80) {
        m_message = Message::make(m_protocol = Protocol::binary);
        Deframer::read(7, m_read_buf + 1);
        return MESSAGE_HEAD;
      } else if (c == 0x82) {
        m_message = Message::make(m_protocol = Protocol::compact);
        return MESSAGE_HEAD;
      } else if (c & 0x80) {
        return ERROR;
      } else {
        m_message = Message::make(m_protocol = Protocol::old);
        Deframer::read(3, m_read_buf + 1);
        return MESSAGE_HEAD;
      }

    case MESSAGE_HEAD:
      switch (m_protocol) {
        case Protocol::binary: {
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
        case Protocol::old: {
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
        case Protocol::compact: {
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
      m_message->name = pjs::Str::make(m_read_data->to_string());
      switch (m_protocol) {
        case Protocol::binary: Deframer::read(4, m_read_buf); return SEQ_ID;
        case Protocol::old: return MESSAGE_TYPE;
        case Protocol::compact: return push_struct();
      }
      return ERROR;

    case MESSAGE_TYPE: // must be binary protocol old encoding
      if (!set_message_type(c)) return ERROR;
      Deframer::read(4, m_read_buf);
      return SEQ_ID;

    case SEQ_ID:
      if (m_protocol == Protocol::compact) {
        if (var_int(c)) return SEQ_ID;
        m_message->seqID = m_var_int;
        m_var_int = 0;
        return MESSAGE_NAME_LEN;
      } else {
        m_message->seqID = (
          ((int32_t)m_read_buf[0] << 24) |
          ((int32_t)m_read_buf[1] << 16) |
          ((int32_t)m_read_buf[2] <<  8) |
          ((int32_t)m_read_buf[3] <<  0)
        );
        return push_struct();
      }

    case STRUCT_FIELD_TYPE:
      if (0 == c) {
        return pop();
      } else if (m_protocol == Protocol::compact) {
        auto state = set_field_type(c & 0xf);
        if (state == ERROR) return state;
        if (c & 0xf0) {
          m_stack->index += (c >> 4) & 0x0f;
          if (m_field_type == Type::BOOL) {
            set_value(m_field_bool);
            return set_value_end();
          }
          return set_value_start();
        }
        m_var_int = 0;
      } else {
        auto state = set_field_type(c);
        if (state == ERROR) return state;
        Deframer::read(2, m_read_buf);
      }
      return STRUCT_FIELD_ID;

    case STRUCT_FIELD_ID:
      if (m_protocol == Protocol::compact) {
        if (var_int(c)) return STRUCT_FIELD_ID;
        m_stack->index = zigzag_to_int((uint32_t)m_var_int);
        if (m_field_type == Type::BOOL) {
          set_value(m_field_bool);
          return set_value_end();
        }
        return set_value_start();
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
      if (m_protocol == Protocol::compact) {
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
      if (m_protocol == Protocol::compact) {
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
      if (m_protocol == Protocol::compact) {
        if (var_int(c)) return VALUE_I64;
        set_value(pjs::Int::make(pjs::Int::Type::i64, zigzag_to_int(m_var_int)));
      } else {
        set_value(pjs::Int::make(pjs::Int::Type::i64,
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
      if (m_protocol == Protocol::compact) {
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
      set_value(pjs::Str::make(utils::make_uuid(m_read_buf)));
      return set_value_end();

    case BINARY_SIZE:
      if (m_protocol == Protocol::compact) {
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

    case LIST_HEAD:
      if (m_protocol == Protocol::compact) {
        m_element_type_code = c & 0x0f;
        if ((c & 0xf0) == 0xf0) return LIST_SIZE;
        return push_list(m_element_type_code, false, (c & 0xf0) >> 4);
      } else {
        auto n = (
          ((int32_t)m_read_buf[1] << 24) |
          ((int32_t)m_read_buf[2] << 16) |
          ((int32_t)m_read_buf[3] <<  8) |
          ((int32_t)m_read_buf[4] <<  0)
        );
        if (n < 0) return ERROR;
        return push_list(m_read_buf[0], false, n);
      }

    case LIST_SIZE: // must be compact protocol
      if (var_int(c)) return LIST_SIZE;
      return push_list(m_element_type_code, false, m_var_int);

    case SET_HEAD:
      if (m_protocol == Protocol::compact) {
        m_element_type_code = c & 0x0f;
        if ((c & 0xf0) == 0xf0) return SET_SIZE;
        return push_list(m_element_type_code, true, (c & 0xf0) >> 4);
      } else {
        auto n = (
          ((int32_t)m_read_buf[1] << 24) |
          ((int32_t)m_read_buf[2] << 16) |
          ((int32_t)m_read_buf[3] <<  8) |
          ((int32_t)m_read_buf[4] <<  0)
        );
        if (n < 0) return ERROR;
        return push_list(m_read_buf[0], true, n);
      }

    case SET_SIZE: // must be compact protocol
      if (var_int(c)) return SET_SIZE;
      return push_list(m_element_type_code, true, m_var_int);

    case MAP_HEAD:
      if (m_protocol == Protocol::compact) {
        if (var_int(c)) return MAP_HEAD;
        if (m_var_int == 0) {
          set_value(Map::make());
          return set_value_end();
        }
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

    case BINARY_DATA:
      try {
        set_value(m_read_data->to_string(Data::Encoding::UTF8));
      } catch (std::runtime_error &err) {
        set_value(m_read_data.get());
      }
      return set_value_end();

    default: return ERROR;
  }
}

bool Thrift::Parser::var_int(int c) {
  m_var_int = (m_var_int << 7) | (c & 0x7f);
  return (c & 0x80);
}

auto Thrift::Parser::zigzag_to_int(uint32_t i) -> int32_t {
  return (i >> 1) ^ - (i & 1);
}

auto Thrift::Parser::zigzag_to_int(uint64_t i) -> int64_t {
  return (i >> 1) ^ - (i & 1);
}

bool Thrift::Parser::set_message_type(int type) {
  if (1 <= type && type <= 4) {
    m_message->type = Message::Type(type);
    return true;
  }
  return false;
}

auto Thrift::Parser::set_field_type(int code) -> State {
  State state;
  int read_size;
  if (m_protocol == Protocol::compact) {
    if (code == 1) {
      state = VALUE_BOOL;
      read_size = 0;
      m_field_type = Type::BOOL;
      m_field_bool = true;
    } else if (code == 2) {
      state = VALUE_BOOL;
      read_size = 0;
      m_field_type = Type::BOOL;
      m_field_bool = false;
    } else {
      set_value_type(code, m_field_type, state, read_size);
    }
  } else {
    set_value_type(code, m_field_type, state, read_size);
  }
  if (state == ERROR) return state;
  auto l = m_stack;
  l->element_types[0] = state;
  l->element_types[1] = state;
  l->element_sizes[0] = read_size;
  l->element_sizes[1] = read_size;
  return state;
}

void Thrift::Parser::set_value_type(int code, Type &type, State &state, int &read_size) {
  static const struct { Type t; State s; int w; }
  s_compact_types[] = {
    { Type::BOOL, VALUE_BOOL, 1 }, // 2
    { Type::I8, VALUE_I8, 1 }, // 3
    { Type::I16, VALUE_I16, 1 }, // 4
    { Type::I32, VALUE_I32, 1 }, // 5
    { Type::I64, VALUE_I64, 1 }, // 6
    { Type::DOUBLE, VALUE_DOUBLE, 8 }, // 7
    { Type::BINARY, BINARY_SIZE, 1 }, // 8
    { Type::LIST, LIST_HEAD, 1 }, // 9
    { Type::SET, SET_HEAD, 1 }, // 10
    { Type::MAP, MAP_HEAD, 1 }, // 11
    { Type::STRUCT, STRUCT_FIELD_TYPE, 1 }, // 12
    { Type::UUID, VALUE_UUID, 16 }, // 13
  },
  s_binary_types[] = {
    { Type::BOOL, VALUE_BOOL, 1 }, // 2
    { Type::I8, VALUE_I8, 1 }, // 3
    { Type::DOUBLE, VALUE_DOUBLE, 8 }, // 4
    { Type::DOUBLE, VALUE_DOUBLE, 0 }, // 5 (invalid)
    { Type::I16, VALUE_I16, 2 }, // 6
    { Type::I16, VALUE_I16, 0 }, // 7 (invalid)
    { Type::I32, VALUE_I32, 4 }, // 8
    { Type::I32, VALUE_I32, 0 }, // 9 (invalid)
    { Type::I64, VALUE_I64, 8 }, // 10
    { Type::BINARY, BINARY_SIZE, 4 }, // 11
    { Type::STRUCT, STRUCT_FIELD_TYPE, 1 }, // 12
    { Type::MAP, MAP_HEAD, 6 }, // 13
    { Type::SET, SET_HEAD, 5 }, // 14
    { Type::LIST, LIST_HEAD, 5 }, // 15
    { Type::UUID, VALUE_UUID, 16 }, // 16
  };

  if (m_protocol == Protocol::compact) {
    if (2 <= code && code <= 13) {
      const auto &r = s_compact_types[code - 2];
      type = r.t;
      state = r.s;
      read_size = r.w;
      m_var_int = 0;
      return;
    }
  } else {
    if (2 <= code && code <= 16) {
      const auto &r = s_binary_types[code - 2];
      if (r.w > 0) {
        type = r.t;
        state = r.s;
        read_size = r.w;
        return;
      }
    }
  }

  state = ERROR;
  read_size = 0;
}

auto Thrift::Parser::set_value_start() -> State {
  auto l = m_stack;
  auto i = l->index & 1;
  auto t = l->element_types[i];
  auto n = l->element_sizes[i];
  if (t == STRUCT_FIELD_TYPE) return push_struct();
  if (n > 1) Deframer::read(n, m_read_buf);
  return t;
}

auto Thrift::Parser::set_value_end() -> State {
  if (auto l = m_stack) {
    if (l->kind == Level::STRUCT) {
      return STRUCT_FIELD_TYPE;
    } else if (l->index >= l->size) {
      return pop();
    } else {
      return set_value_start();
    }
  } else {
    Deframer::need_flush();
    return START;
  }
}

void Thrift::Parser::set_value(const pjs::Value &v) {
  if (auto l = m_stack) {
    auto &i = l->index;
    switch (l->kind) {
      case Level::STRUCT: {
        auto f = Field::make();
        f->id = i;
        f->type = m_field_type;
        f->value = v;
        l->obj->as<pjs::Array>()->push(f);
        break;
      }
      case Level::LIST:
      case Level::SET:
        l->obj->as<List>()->elements->set(i++, v);
        break;
      case Level::MAP:
        if (i & 1) {
          auto *ent = pjs::Array::make(2);
          ent->set(0, l->key);
          ent->set(1, v);
          l->obj->as<Map>()->pairs->set(i>>1, ent);
        } else {
          l->key = v;
        }
        i++;
        break;
      default: return;
    }

  } else if (v.is_array()) {
    m_message->fields = v.as<pjs::Array>();
  }
}

auto Thrift::Parser::push_struct() -> State {
  auto obj = pjs::Array::make();
  set_value(obj);
  auto l = new Level;
  l->back = m_stack;
  l->kind = Level::STRUCT;
  l->index = 0;
  l->obj = obj;
  m_stack = l;
  return STRUCT_FIELD_TYPE;
}

auto Thrift::Parser::push_list(int code, bool is_set, int size) -> State {
  Type type;
  State state;
  int read_size;
  set_value_type(code, type, state, read_size);
  if (state == ERROR) return state;
  auto *obj = List::make();
  obj->elementType = type;
  obj->elements = pjs::Array::make();
  set_value(obj);
  if (size <= 0) return set_value_end();
  auto l = new Level;
  l->back = m_stack;
  l->kind = is_set ? Level::SET : Level::LIST;
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

auto Thrift::Parser::push_map(int code_k, int code_v, int size) -> State {
  Type type_k, type_v;
  State state_k, state_v;
  int read_size_k, read_size_v;
  set_value_type(code_k, type_k, state_k, read_size_k);
  set_value_type(code_v, type_v, state_v, read_size_v);
  auto obj = Map::make();
  obj->keyType = type_k;
  obj->valueType = type_v;
  obj->pairs = pjs::Array::make();
  set_value(obj);
  if (size <= 0) return set_value_end();
  auto l = new Level;
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

auto Thrift::Parser::pop() -> State {
  if (!m_stack) return ERROR;
  do {
    auto *l = m_stack;
    m_stack = l->back;
    delete l;
    if (!m_stack) {
      Deframer::need_flush();
      return START;
    }
    if (m_stack->kind == Level::STRUCT) return STRUCT_FIELD_TYPE;
  } while (m_stack->index >= m_stack->size);
  return set_value_start();
}

void Thrift::Parser::start() {
  if (!m_stack && !m_message) {
    on_message_start();
  }
}

void Thrift::Parser::end() {
  if (!m_stack && m_message) {
    on_message_end(m_message);
    m_message = nullptr;
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// Thrift::Protocol
//

template<> void EnumDef<Thrift::Protocol>::init() {
  define(Thrift::Protocol::binary, "binary");
  define(Thrift::Protocol::compact, "compact");
  define(Thrift::Protocol::old, "old");
}

//
// Thrift::Type
//

template<> void EnumDef<Thrift::Type>::init() {
  define(Thrift::Type::BOOL, "BOOL");
  define(Thrift::Type::I8, "I8");
  define(Thrift::Type::I16, "I16");
  define(Thrift::Type::I32, "I32");
  define(Thrift::Type::I64, "I64");
  define(Thrift::Type::DOUBLE, "DOUBLE");
  define(Thrift::Type::BINARY, "BINARY");
  define(Thrift::Type::STRUCT, "STRUCT");
  define(Thrift::Type::MAP, "MAP");
  define(Thrift::Type::SET, "SET");
  define(Thrift::Type::LIST, "LIST");
  define(Thrift::Type::UUID, "UUID");
}

//
// Thrift::Message::Type
//

template<> void EnumDef<Thrift::Message::Type>::init() {
  define(Thrift::Message::Type::call, "call");
  define(Thrift::Message::Type::reply, "reply");
  define(Thrift::Message::Type::exception, "exception");
  define(Thrift::Message::Type::oneway, "oneway");
}

//
// Thrift::Field
//

template<> void ClassDef<Thrift::Field>::init() {
  field<int>("id", [](Thrift::Field *obj) { return &obj->id; });
  field<EnumValue<Thrift::Type>>("type", [](Thrift::Field *obj) { return &obj->type; });
  field<Value>("value", [](Thrift::Field *obj) { return &obj->value; });
}

//
// Thrift::List
//

template<> void ClassDef<Thrift::List>::init() {
  field<EnumValue<Thrift::Type>>("elementType", [](Thrift::List *obj) { return &obj->elementType; });
  field<Ref<Array>>("elements", [](Thrift::List *obj) { return &obj->elements; });
}

//
// Thrift::Map
//

template<> void ClassDef<Thrift::Map>::init() {
  field<EnumValue<Thrift::Type>>("keyType", [](Thrift::Map *obj) { return &obj->keyType; });
  field<EnumValue<Thrift::Type>>("valueType", [](Thrift::Map *obj) { return &obj->valueType; });
  field<Ref<Array>>("pairs", [](Thrift::Map *obj) { return &obj->pairs; });
}

//
// Thrift::Message
//

template<> void ClassDef<Thrift::Message>::init() {
  field<EnumValue<Thrift::Protocol>>("protocol", [](Thrift::Message *obj) { return &obj->protocol; });
  field<EnumValue<Thrift::Message::Type>>("type", [](Thrift::Message *obj) { return &obj->type; });
  field<int>("seqID", [](Thrift::Message *obj) { return &obj->seqID; });
  field<Ref<Str>>("name", [](Thrift::Message *obj) { return &obj->name; });
  field<Ref<Array>>("fields", [](Thrift::Message *obj) { return &obj->fields; });
}

//
// Thrift
//

template<> void ClassDef<Thrift>::init() {
  ctor();

  method("decode", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    if (!data) { ret = Value::null; return; }
    ret.set(Thrift::decode(*data));
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Object *msg;
    if (!ctx.arguments(1, &msg)) return;
    auto *data = pipy::Data::make();
    Thrift::encode(msg, *data);
    ret.set(data);
  });
}

} // namespace pjs
