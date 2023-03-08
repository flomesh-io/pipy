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

#include "hessian.hpp"

//
// Bytecode Map
// ============
//
// x00 - x1f    # utf-8 string length 0-31
// x20 - x2f    # binary data length 0-15
// x30 - x33    # utf-8 string length 0-1023
// x34 - x37    # binary data length 0-1023
// x38 - x3f    # three-octet compact long (-x40000 to x3ffff)
// x40          # reserved (expansion/escape)
// x41          # 8-bit binary data non-final chunk ('A')
// x42          # 8-bit binary data final chunk ('B')
// x43          # object type definition ('C')
// x44          # 64-bit IEEE encoded double ('D')
// x45          # reserved
// x46          # boolean false ('F')
// x47          # reserved
// x48          # untyped map ('H')
// x49          # 32-bit signed integer ('I')
// x4a          # 64-bit UTC millisecond date
// x4b          # 32-bit UTC minute date
// x4c          # 64-bit signed long integer ('L')
// x4d          # map with type ('M')
// x4e          # null ('N')
// x4f          # object instance ('O')
// x50          # reserved
// x51          # reference to map/list/object - integer ('Q')
// x52          # utf-8 string non-final chunk ('R')
// x53          # utf-8 string final chunk ('S')
// x54          # boolean true ('T')
// x55          # variable-length list/vector ('U')
// x56          # fixed-length list/vector ('V')
// x57          # variable-length untyped list/vector ('W')
// x58          # fixed-length untyped list/vector ('X')
// x59          # long encoded as 32-bit int ('Y')
// x5a          # list/map terminator ('Z')
// x5b          # double 0.0
// x5c          # double 1.0
// x5d          # double represented as byte (-128.0 to 127.0)
// x5e          # double represented as short (-32768.0 to 327676.0)
// x5f          # double represented as float
// x60 - x6f    # object with direct type
// x70 - x77    # fixed list with direct length
// x78 - x7f    # fixed untyped list with direct length
// x80 - xbf    # one-octet compact int (-x10 to x3f, x90 is 0)
// xc0 - xcf    # two-octet compact int (-x800 to x7ff)
// xd0 - xd7    # three-octet compact int (-x40000 to x3ffff)
// xd8 - xef    # one-octet compact long (-x8 to xf, xe0 is 0)
// xf0 - xff    # two-octet compact long (-x800 to x7ff, xf8 is 0)
//
// Serialization Grammar
// =====================
//
//            # starting production
// top        ::= value
//
//            # 8-bit binary data split into 64k chunks
// binary     ::= x41 b1 b0 <binary-data> binary # non-final chunk
//            ::= 'B' b1 b0 <binary-data>        # final chunk
//            ::= [x20-x2f] <binary-data>        # binary data of length 0-15
//            ::= [x34-x37] <binary-data>        # binary data of length 0-1023
//
//            # boolean true/false
// boolean    ::= 'T'
//            ::= 'F'
//
//            # definition for an object (compact map)
// class-def  ::= 'C' string int string*
//
//            # time in UTC encoded as 64-bit long milliseconds since
//            #  epoch
// date       ::= x4a b7 b6 b5 b4 b3 b2 b1 b0
//            ::= x4b b3 b2 b1 b0       # minutes since epoch
//
//            # 64-bit IEEE double
// double     ::= 'D' b7 b6 b5 b4 b3 b2 b1 b0
//            ::= x5b                   # 0.0
//            ::= x5c                   # 1.0
//            ::= x5d b0                # byte cast to double (-128.0 to 127.0)
//            ::= x5e b1 b0             # short cast to double
//            ::= x5f b3 b2 b1 b0       # 32-bit float cast to double
//
//            # 32-bit signed integer
// int        ::= 'I' b3 b2 b1 b0
//            ::= [x80-xbf]             # -x10 to x3f
//            ::= [xc0-xcf] b0          # -x800 to x7ff
//            ::= [xd0-xd7] b1 b0       # -x40000 to x3ffff
//
//            # list/vector
// list       ::= x55 type value* 'Z'   # variable-length list
// 	          ::= 'V' type int value*   # fixed-length list
//            ::= x57 value* 'Z'        # variable-length untyped list
//            ::= x58 int value*        # fixed-length untyped list
// 	          ::= [x70-77] type value*  # fixed-length typed list
// 	          ::= [x78-7f] value*       # fixed-length untyped list
//
//            # 64-bit signed long integer
// long       ::= 'L' b7 b6 b5 b4 b3 b2 b1 b0
//            ::= [xd8-xef]             # -x08 to x0f
//            ::= [xf0-xff] b0          # -x800 to x7ff
//            ::= [x38-x3f] b1 b0       # -x40000 to x3ffff
//            ::= x59 b3 b2 b1 b0       # 32-bit integer cast to long
//
//            # map/object
// map        ::= 'M' type (value value)* 'Z'  # key, value map pairs
// 	          ::= 'H' (value value)* 'Z'       # untyped key, value
//
//            # null value
// null       ::= 'N'
//
//            # Object instance
// object     ::= 'O' int value*
// 	          ::= [x60-x6f] value*
//
//            # value reference (e.g. circular trees and graphs)
// ref        ::= x51 int            # reference to nth map/list/object
//
//            # UTF-8 encoded character string split into 64k chunks
// string     ::= x52 b1 b0 <utf8-data> string  # non-final chunk
//            ::= 'S' b1 b0 <utf8-data>         # string of length 0-65535
//            ::= [x00-x1f] <utf8-data>         # string of length 0-31
//            ::= [x30-x34] <utf8-data>         # string of length 0-1023
//
//            # map/list types for OO languages
// type       ::= string                        # type name
//            ::= int                           # type reference
//
//            # main production
// value      ::= null
//            ::= binary
//            ::= boolean
//            ::= class-def value
//            ::= date
//            ::= double
//            ::= int
//            ::= list
//            ::= long
//            ::= map
//            ::= object
//            ::= ref
//            ::= string
//

namespace pipy {

//
// Hessian
//

thread_local static Data::Producer s_dp("Hessian");

auto Hessian::decode(const Data &data) -> pjs::Array* {
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

void Hessian::encode(const pjs::Value &value, Data &data) {
  Data::Builder db(data, &s_dp);
  encode(value, db);
  db.flush();
}

void Hessian::encode(const pjs::Value &value, Data::Builder &db) {
  int level = 0;

  auto write_str = [&](const std::string &str) {
    size_t n = 0;
    for (size_t i = 0; i < str.length(); n++) {
      auto b = str[i];
      if (b & 0x80) {
        if ((b & 0xe0) == 0xc0) {
          i += 2;
        } else if ((b & 0xf0) == 0xe0) {
          i += 3;
        } else if ((b & 0xf8) == 0xf0) {
          i += 4;
        } else {
          i += 1;
        }
      } else {
        i += 1;
      }
    }

    if (n < 32) {
      db.push((char)n);
      db.push(str.c_str());
    } else if (n < 1024) {
      db.push((char)(n >> 8) | 0x30);
      db.push((char)(n >> 0));
      db.push(str.c_str());
    } else if (n < 65536) {
      db.push('S');
      db.push((char)(n >> 8));
      db.push((char)(n >> 0));
      db.push(str.c_str());
    } else {
      db.push('S');
      db.push(0xff);
      db.push(0xff);
      db.push(str.c_str(), 65536);
    }
  };

  std::function<bool(const pjs::Value&)> write;
  write = [&](const pjs::Value &v) -> bool {
    if (v.is_undefined()) {
      db.push('N');

    } else if (v.is_boolean()) {
      db.push(v.b() ? 'T' : 'F');

    } else if (v.is_number()) {
      auto n = v.n();
      double i;
      if (std::isnan(n) || std::isinf(n) || std::modf(n, &i)) {
        int64_t tmp; *(double*)&tmp = n;
        db.push('D');
        db.push((char)(tmp >> 56));
        db.push((char)(tmp >> 48));
        db.push((char)(tmp >> 40));
        db.push((char)(tmp >> 32));
        db.push((char)(tmp >> 24));
        db.push((char)(tmp >> 16));
        db.push((char)(tmp >> 8 ));
        db.push((char)(tmp >> 0 ));

      } else {
        auto n = int64_t(v.n());
        if (std::numeric_limits<int>::min() <= n && n <= std::numeric_limits<int>::max()) {
          db.push('I');
          db.push((char)(n >> 24));
          db.push((char)(n >> 16));
          db.push((char)(n >> 8 ));
          db.push((char)(n >> 0 ));
        } else {
          db.push('L');
          db.push((char)(n >> 56));
          db.push((char)(n >> 48));
          db.push((char)(n >> 40));
          db.push((char)(n >> 32));
          db.push((char)(n >> 24));
          db.push((char)(n >> 16));
          db.push((char)(n >> 8 ));
          db.push((char)(n >> 0 ));
        }
      }

    } else if (v.is_string()) {
      write_str(v.s()->str());

    } else if (v.is_object()) {

      if (v.is_array()) {
        if (level++ > 0) db.push(0x57);
        auto a = v.as<pjs::Array>();
        auto n = a->iterate_while([&](pjs::Value &v, int i) -> bool {
          return write(v);
        });
        if (n < a->length()) return false;
        if (--level > 0) db.push('Z');

      } else if (!v.o()) {
        db.push('N');

      } else {
        db.push('H');
        level++;
        auto done = v.o()->iterate_while([&](pjs::Str *k, pjs::Value &v) -> bool {
          write_str(k->str());
          return write(v);
        });
        if (!done) return false;
        db.push('Z');
        level--;
      }
    }

    return true;
  };

  write(value);
  db.flush();
}

//
// Hessian::Parser
//

Hessian::Parser::Parser()
  : m_read_data(Data::make())
  , m_utf8_decoder([this](int c) { push_utf8_char(c); })
{
}

void Hessian::Parser::reset() {
  Deframer::reset();
  Deframer::pass_all(true);
  while (auto *s = m_stack) {
    m_stack = s->back;
    delete s;
  }
  m_stack = nullptr;
  m_root = pjs::Value::undefined;
  m_read_data->clear();
  m_obj_refs.clear();
  m_def_refs.clear();
  m_type_refs.clear();
  m_is_ref = false;
}

auto Hessian::Parser::on_state(int state, int c) -> int {
  switch (state) {
    case START:
      end();
      start();
      if (c < 0x20) {
        // x00 - x1f : utf-8 string length 0-31
        if (c == 0) {
          push(pjs::Str::empty.get());
          return START;
        } else {
          m_read_data->clear();
          m_utf8_decoder.reset();
          m_utf8_length = c;
          return STRING_DATA_FINAL;
        }

      } else if (c < 0x30) {
        // x20 - x2f : binary data length 0-15
        auto len = c - 0x20;
        if (!len) {
          push(Data::make());
          return START;
        } else {
          m_read_data->clear();
          Deframer::read(len, m_read_data);
          return BINARY_DATA_FINAL;
        }

      } else if (c < 0x34) {
        // x30 - x33 : utf-8 string length 0-1023
        m_read_number[0] = c - 0x30;
        Deframer::read(1, m_read_number + 1);
        return STRING_SIZE_FINAL;

      } else if (c < 0x38) {
        // x34 - x37 : binary data length 0-1023
        m_read_number[0] = c - 0x34;
        Deframer::read(1, m_read_number + 1);
        return BINARY_SIZE_FINAL;

      } else if (c < 0x40) {
        // x38 - x3f : three-octet compact long (-x40000 to x3ffff)
        auto n = int64_t(c - 0x3c) << 16;
        m_read_number[0] = (n >> 56);
        m_read_number[1] = (n >> 48);
        m_read_number[2] = (n >> 40);
        m_read_number[3] = (n >> 32);
        m_read_number[4] = (n >> 24);
        m_read_number[5] = (n >> 16);
        Deframer::read(2, m_read_number + 6);
        return LONG;

      } else if (c < 0x60) {
        // x40 - x5f
        switch (c) {
          case 'A': // x41 : 8-bit binary data non-final chunk
            Deframer::read(2, m_read_number);
            return BINARY_SIZE;
          case 'B': // x42 : 8-bit binary data final chunk
            Deframer::read(2, m_read_number);
            return BINARY_SIZE_FINAL;
          case 'C': // x43 : object type definition
            return push(Collection::make(Collection::Kind::class_def), CollectionState::TYPE_LENGTH);
          case 'D': // x44 : 64-bit IEEE encoded double
            Deframer::read(8, m_read_number);
            return DOUBLE;
          case 'F': // x46 : boolean false
            return push(false);
          case 'H': // x48 : untyped map
            return push(Collection::make(Collection::Kind::map), CollectionState::VALUE);
          case 'I': // x49 : int
            Deframer::read(4, m_read_number);
            return INT;
          case 'J': // x4a : 64-bit UTC millisecond date
            Deframer::read(8, m_read_number);
            return DATE_64;
          case 'K': // x4b : 32-bit UTC minute date
            Deframer::read(4, m_read_number);
            return DATE_32;
          case 'L': // x4c : 64-bit signed long integer
            Deframer::read(8, m_read_number);
            return LONG;
          case 'M': // x4d : map with type
            return push(Collection::make(Collection::Kind::map), CollectionState::TYPE);
          case 'N': // x4e : null
            return push(pjs::Value::null);
          case 'O': // x4f : object instance
            return push(Collection::make(Collection::Kind::object), CollectionState::CLASS_DEF);
          case 'Q': // x51 : reference to map/list/object - integer
            if (m_is_ref) return ERROR;
            m_is_ref = true;
            return START;
          case 'R': // x52 : utf-8 string non-final chunk
            Deframer::read(2, m_read_number);
            return STRING_SIZE;
          case 'S': // x53 : utf-8 string final chunk
            Deframer::read(2, m_read_number);
            return STRING_SIZE_FINAL;
          case 'T': // x54 : boolean true
            return push(true);
          case 'U': // x55 : variable-length list/vector
            return push(Collection::make(Collection::Kind::list), CollectionState::TYPE);
          case 'V': // x56 : fixed-length list/vector
            return push(Collection::make(Collection::Kind::list), CollectionState::TYPE_LENGTH);
          case 'W': // x57 : variable-length untyped list/vector
            return push(Collection::make(Collection::Kind::list), CollectionState::VALUE);
          case 'X': // x58 : fixed-length untyped list/vector
            push(Collection::make(Collection::Kind::list), CollectionState::LENGTH);
          case 'Y': // x59 : long encoded as 32-bit int
            Deframer::read(4, m_read_number);
            return INT;
          case 'Z': // x5a : end of list or map
            if (auto l = m_stack) l->length = l->count; else return ERROR;
            pop();
            return START;
          case 0x5b: // double 0.0
            return push(0.0);
          case 0x5c: // double 1.0
            return push(1.0);
          case 0x5d: // double represented as byte (-128.0 to 127.0)
            return DOUBLE_8;
          case 0x5e: // double represented as short (-32768.0 to 327676.0)
            Deframer::read(2, m_read_number);
            return DOUBLE_16;
          case 0x5f: // double represented as float
            Deframer::read(4, m_read_number);
            return DOUBLE_32;
          default: return ERROR;
        }
      } else if (c < 0x70) {
        // x60 - x6f : object with direct type
        auto d = m_def_refs.get(c - 0x60);
        if (!d) return ERROR;
        auto n = d->elements->as<pjs::Array>()->length();
        auto o = Collection::make(Collection::Kind::object);
        o->definition = d;
        return push(o, CollectionState::VALUE, n);

      } else if (c < 0x78) {
        // x70 - x77 : fixed list with direct length
        return push(Collection::make(Collection::Kind::list), CollectionState::TYPE, c - 0x70);

      } else if (c < 0x80) {
        // x78 - x7f : fixed untyped list with direct length
        return push(Collection::make(Collection::Kind::list), CollectionState::VALUE, c - 0x78);

      } else if (c < 0xc0) {
        // x80 - xbf : one-octet compact int (-x10 to x3f, x90 is 0)
        return push(c - 0x90);

      } else if (c < 0xd0) {
        // xc0 - xcf : two-octet compact int (-x800 to x7ff)
        auto n = int32_t(c - 0xc8) << 8;
        m_read_number[0] = (n >> 24);
        m_read_number[1] = (n >> 16);
        m_read_number[2] = (n >>  8);
        Deframer::read(1, m_read_number + 3);
        return INT;

      } else if (c < 0xd8) {
        // xd0 - xd7 : three-octet compact int (-x40000 to x3ffff)
        auto n = int32_t(c - 0xd4) << 16;
        m_read_number[0] = (n >> 24);
        m_read_number[1] = (n >> 16);
        Deframer::read(2, m_read_number + 2);
        return INT;

      } else if (c < 0xf0) {
        // xd8 - xef : one-octet compact long (-x8 to xf, xe0 is 0)
        return push(c - 0xe0);

      } else {
        // xf0 - xff : two-octet compact long (-x800 to x7ff, xf8 is 0)
        auto n = int64_t(c - 0xf8) << 8;
        m_read_number[0] = (n >> 56);
        m_read_number[1] = (n >> 48);
        m_read_number[2] = (n >> 40);
        m_read_number[3] = (n >> 32);
        m_read_number[4] = (n >> 24);
        m_read_number[5] = (n >> 16);
        m_read_number[6] = (n >>  8);
        Deframer::read(1, m_read_number + 7);
        return LONG;
      }

      return ERROR;

    case INT:
      return push(
        ((int32_t)m_read_number[0] << 24)|
        ((int32_t)m_read_number[1] << 16)|
        ((int32_t)m_read_number[2] <<  8)|
        ((int32_t)m_read_number[3] <<  0)
      );
    case LONG:
      return push(
        ((int64_t)m_read_number[0] << 56)|
        ((int64_t)m_read_number[1] << 48)|
        ((int64_t)m_read_number[2] << 40)|
        ((int64_t)m_read_number[3] << 32)|
        ((int64_t)m_read_number[4] << 24)|
        ((int64_t)m_read_number[5] << 16)|
        ((int64_t)m_read_number[6] <<  8)|
        ((int64_t)m_read_number[7] <<  0)
      );
    case DOUBLE: {
      auto n = (
        ((uint64_t)m_read_number[0] << 56)|
        ((uint64_t)m_read_number[1] << 48)|
        ((uint64_t)m_read_number[2] << 40)|
        ((uint64_t)m_read_number[3] << 32)|
        ((uint64_t)m_read_number[4] << 24)|
        ((uint64_t)m_read_number[5] << 16)|
        ((uint64_t)m_read_number[6] <<  8)|
        ((uint64_t)m_read_number[7] <<  0)
      );
      return push(*reinterpret_cast<double*>(&n));
    }
    case DOUBLE_8:
      return push(double((int8_t)c));
    case DOUBLE_16:
      return push(double(
        ((int16_t)m_read_number[0] << 8)|
        ((int16_t)m_read_number[1] << 0)
      ));
    case DOUBLE_32: {
      auto n = (
        ((uint32_t)m_read_number[0] << 24)|
        ((uint32_t)m_read_number[1] << 16)|
        ((uint32_t)m_read_number[2] <<  8)|
        ((uint32_t)m_read_number[3] <<  0)
      );
      return push(*reinterpret_cast<float*>(&n));
    }
    case DATE_32:
      return push(pjs::Date::make(
        double(
          ((uint64_t)m_read_number[0] << 24)|
          ((uint64_t)m_read_number[1] << 16)|
          ((uint64_t)m_read_number[2] <<  8)|
          ((uint64_t)m_read_number[3] <<  0)
        ) * (60 * 1000)
      ));
    case DATE_64:
      return push(pjs::Date::make(
        double(
          ((uint64_t)m_read_number[0] << 56)|
          ((uint64_t)m_read_number[1] << 48)|
          ((uint64_t)m_read_number[2] << 40)|
          ((uint64_t)m_read_number[3] << 32)|
          ((uint64_t)m_read_number[4] << 24)|
          ((uint64_t)m_read_number[5] << 16)|
          ((uint64_t)m_read_number[6] <<  8)|
          ((uint64_t)m_read_number[7] <<  0)
        )
      ));
    case STRING_SIZE:
    case STRING_SIZE_FINAL:
      m_utf8_decoder.reset();
      m_utf8_length = (
        ((uint16_t)m_read_number[0] << 8)|
        ((uint16_t)m_read_number[1] << 0)
      );
      if (m_utf8_length == 0) return state == STRING_SIZE_FINAL ? push_string() : START;
      return state == STRING_SIZE ? STRING_DATA : STRING_DATA_FINAL;
    case STRING_DATA:
    case STRING_DATA_FINAL:
      m_read_data->push(uint8_t(c), &s_dp);
      m_utf8_decoder.input(c);
      if (m_utf8_length > 0) return state;
      return state == STRING_DATA_FINAL ? push_string() : START;
    case BINARY_SIZE:
    case BINARY_SIZE_FINAL: {
      auto n = (
        ((uint16_t)m_read_number[0] << 8)|
        ((uint16_t)m_read_number[1] << 0)
      );
      if (n == 0) return state == BINARY_SIZE_FINAL ? push(Data::make(std::move(*m_read_data))) : START;
      Deframer::read(n, m_read_data);
      return state == BINARY_SIZE ? BINARY_DATA : BINARY_DATA_FINAL;
    }
    case BINARY_DATA: return START;
    case BINARY_DATA_FINAL: return push(Data::make(std::move(*m_read_data)));
    default: return ERROR;
  }
}

void Hessian::Parser::parse(Data &data) {
  Deframer::deframe(data);
  if (Deframer::state() == START) end();
}

void Hessian::Parser::push_utf8_char(int c) {
  m_utf8_length--;
}

auto Hessian::Parser::push_string() -> State {
  std::string s = m_read_data->to_string();
  m_read_data->clear();
  return push(pjs::Str::make(std::move(s)));
}

auto Hessian::Parser::push(const pjs::Value &value, CollectionState state, int length) -> State {
  pjs::Value v(value);

  if (m_is_ref) {
    if (!v.is_number()) return ERROR;
    auto obj = m_obj_refs.get(v.n());
    if (!obj) return ERROR;
    v.set(obj);
  }

  if (auto *l = m_stack) {
    auto &i = l->count;
    auto *c = l->collection.get();
    switch (l->state) {
      case CollectionState::VALUE: {
        if (!c->elements) {
          c->elements = (c->kind == Collection::Kind::object
            ? pjs::Object::make()
            : pjs::Array::make(std::max(0, l->length))
          );
        }
        switch (c->kind.get()) {
          case Collection::Kind::list:
            c->elements->as<pjs::Array>()->set(i, v);
            break;
          case Collection::Kind::map:
            if (i & 1) {
              c->elements->as<pjs::Array>()->elements()->at(i>>1).as<pjs::Array>()->set(1, v);
            } else {
              auto *a = pjs::Array::make(2);
              a->set(0, v);
              c->elements->as<pjs::Array>()->set(i>>1, a);
            }
            break;
          case Collection::Kind::class_def:
            if (m_is_ref) return ERROR;
            if (!v.is_string()) return ERROR;
            c->elements->as<pjs::Array>()->set(i, v.s());
            break;
          case Collection::Kind::object:
            if (auto *d = c->definition.get()) {
              pjs::Value k;
              d->elements->as<pjs::Array>()->get(i, k);
              if (!k.is_string()) return ERROR;
              c->elements->set(k.s(), v);
            } else {
              return ERROR;
            }
            break;
          default: return ERROR;
        }
        i++;
        break;
      }
      case CollectionState::LENGTH:
        if (m_is_ref) return ERROR;
        if (v.is_number()) {
          int n = v.n();
          if (n < 0) return ERROR;
          l->length = n;
          l->state = CollectionState::VALUE;
          if (!n) pop();
          return START;
        }
        return ERROR;
      case CollectionState::TYPE:
      case CollectionState::TYPE_LENGTH:
        if (m_is_ref) return ERROR;
        if (v.is_string()) {
          m_type_refs.add(v.s());
          c->type = v.s();
          if (
            c->kind == Collection::Kind::list ||
            c->kind == Collection::Kind::map
          ) m_type_refs.add(v.s());
        } else if (value.is_number()) {
          auto s = m_type_refs.get(v.n());
          if (!s) return ERROR;
          c->type = s;
        } else {
          return ERROR;
        }
        l->state = (l->state == CollectionState::TYPE_LENGTH ? CollectionState::LENGTH : CollectionState::VALUE);
        return START;
      case CollectionState::CLASS_DEF:
        if (!m_is_ref && v.is_number()) {
          if (auto d = m_def_refs.get(v.n())) {
            c->definition = d;
            l->state = CollectionState::LENGTH;
            return START;
          }
        }
        return ERROR;
      default: return ERROR;
    }
  } else {
    m_root = v;
  }

  if (m_is_ref) {
    m_is_ref = false;

  } else if (v.is<Collection>()) {
    auto *c = v.as<Collection>();
    auto *l = new Level;
    l->back = m_stack;
    l->collection = c;
    l->state = state;
    l->length = length;
    m_stack = l;
    if (c->kind == Collection::Kind::class_def) {
      m_def_refs.add(c);
    } else {
      m_obj_refs.add(c);
    }
  }

  pop();
  return START;
}

void Hessian::Parser::pop() {
  auto *l = m_stack;
  while (l && l->length == l->count) {
    auto *level = l; l = l->back;
    delete level;
  }
  m_stack = l;
  if (!l) Deframer::need_flush();
}

void Hessian::Parser::start() {
  if (!m_stack && m_root.is_undefined()) {
    on_message_start();
  }
}

void Hessian::Parser::end() {
  if (!m_stack && !m_root.is_undefined()) {
    on_message_end(m_root);
    m_root = pjs::Value::undefined;
  }
}

//
// ByteBuf
//

template<int N>
class ByteBuf {
  char m_buf[N+1];
  int  m_len = 0;

public:
  void clear() {
    m_len = 0;
  }

  bool empty() const {
    return m_len == 0;
  }

  int length() const {
    return m_len;
  }

  char operator[](int i) const {
    return m_buf[i];
  }

  auto str() const -> std::string {
    return std::string(m_buf, m_len);
  }

  auto c_str() -> const char* {
    m_buf[m_len] = 0;
    return m_buf;
  }

  void push(char c) {
    if (m_len < N) m_buf[m_len++] = c;
  }
};

//
// HessianParser
//

class HessianParser {
public:
  HessianParser(pjs::Value &out) : m_output(out) {}

  auto error() const -> const std::string& { return m_error; }

  bool parse(const char *buffer, int size) {
    for (int i = 0; i < size; ++i) {
      auto ch = (unsigned char)buffer[i];

      // code
      if (m_state == CODE) {
        code(ch);

      // head
      } else if (m_state == HEAD) {
        head(ch);

      // data
      } else if (m_state == DATA) {
        data(ch);

      // utf8
      } else if (m_state == UTF8) {
        utf8(ch);

      // error
      } else {
        break;
      }
    }
    return m_state != ERROR;
  }

private:
  enum State {
    ERROR,
    CODE,
    HEAD,
    DATA,
    UTF8,
  };

  enum Collection {
    LIST,
    MAP,
    CLASS,
    OBJECT,
  };

  enum Semantic {
    VALUE,
    KEY,
    NAME,
    COUNT,
    DEFINITION,
  };

  struct Level {
    Collection collection;
    Semantic   semantic;
    int        definition;
    int        count;
  };

  State m_state = CODE;
  int m_head_size;
  int m_data_size;
  int m_char_size;
  ByteBuf<0x100> m_head;
  ByteBuf<0x10000> m_data;
  std::stack<Level> m_stack;
  std::stack<pjs::Object*> m_object_stack;
  std::string m_current_map_key;
  std::string m_error;
  pjs::Value& m_output;
  int m_count = 0;

  std::vector<std::vector<std::string>> m_class_map;

  void code(unsigned char ch) {
    m_head.clear();
    m_head.push(ch);

    // null
    if (ch == 'N') {
      data_null();

    // true
    } else if (ch == 'T') {
      data_bool(true);

    // false
    } else if (ch == 'F') {
      data_bool(false);

    // int
    } else if (ch == 'I') {
      m_data_size = 4;
      m_data.clear();
      m_state = DATA;
    } else if (0x80 <= ch && ch <= 0xd7) {
      if (ch < 0xc0) {
        data_int((int)ch - 0x90);
      } else {
        m_data_size = ch < 0xd0 ? 1 : 2;
        m_data.clear();
        m_state = DATA;
      }

    // long
    } else if (ch == 'L') {
      m_data_size = 8;
      m_data.clear();
      m_state = DATA;
    } else if (ch == 0x59) {
      m_data_size = 4;
      m_data.clear();
      m_state = DATA;
    } else if (ch >= 0xd8) {
      if (ch < 0xf0) {
        data_long((int)ch - 0xe0);
      } else {
        m_data_size = 1;
        m_data.clear();
        m_state = DATA;
      }
    } else if (0x38 <= ch && ch <= 0x3f) {
      m_data_size = 2;
      m_data.clear();
      m_state = DATA;

    // double
    } else if (ch == 'D') {
      m_data_size = 8;
      m_data.clear();
      m_state = DATA;
    } else if (ch == 0x5b) {
      data_double(0);
    } else if (ch == 0x5c) {
      data_double(1);
    } else if (ch == 0x5d) {
      m_data_size = 1;
      m_data.clear();
      m_state = DATA;
    } else if (ch == 0x5e) {
      m_data_size = 2;
      m_data.clear();
      m_state = DATA;
    } else if (ch == 0x5f) {
      m_data_size = 4;
      m_data.clear();
      m_state = DATA;

    // string
    } else if (ch == 'S') {
      m_head_size = 3;
      m_state = HEAD;
    } else if (ch < 0x20) {
      if (ch == 0) {
        data_string(std::string());
        m_state = CODE;
      } else {
        m_data_size = ch;
        m_char_size = 0;
        m_data.clear();
        m_state = UTF8;
      }
    } else if (0x30 <= ch && ch <= 0x33) {
      m_head_size = 2;
      m_state = HEAD;

    // list
    } else if (ch == 0x57) {
      data_list();

    // map
    } else if (ch == 'H') {
      data_map();

    // class
    } else if (ch == 'C') {
      data_class();

    // object
    } else if (ch == 'O') {
      data_object(-1);

    // object (short form)
    } else if (0x60 <= ch && ch <= 0x6f) {
      data_object(ch - 0x60);

    // list/map end
    } else if (ch == 'Z') {
      if (m_stack.size() > 0) {
        switch (m_stack.top().collection) {
          case MAP:
            map_end();
            data_end();
            break;
          case LIST:
            list_end();
            data_end();
            break;
          default:
            error("unexpected bytecode 'Z'");
            break;
        }
        m_stack.pop();
      } else {
        error("unexpected bytecode 'Z'");
      }

    // unrecognized stuff
    } else {
      char msg[100];
      std::sprintf(msg, "unrecognized bytecode 0x%02x", ch);
      error(msg);
    }
  }

  void head(unsigned char ch) {
    m_head.push(ch);
    if (m_head.length() < m_head_size)
      return;

    auto c = (unsigned char)m_head[0];
    auto n = 0;
    if (c == 'S') {
      n = ((int)(unsigned char)m_head[1] << 8)
        | ((int)(unsigned char)m_head[2] << 0);
    } else {
      n = ((int)(unsigned char)(m_head[0] & 0x03) << 8)
        | ((int)(unsigned char)(m_head[1] & 0xff) << 0);
    }

    if (n == 0) {
      data_string(std::string());
      m_state = CODE;
    } else {
      m_data_size = n;
      m_char_size = 0;
      m_data.clear();
      m_state = UTF8;
    }
  }

  void utf8(unsigned char ch) {
    m_data.push(ch);
    if (m_char_size > 0) {
      if (--m_char_size) return;
    } else if (ch & 0x80) {
      if ((ch & 0xe0) == 0xc0) {
        m_char_size = 1;
        return;
      } else if ((ch & 0xf0) == 0xe0) {
        m_char_size = 2;
        return;
      } else if ((ch & 0xf8) == 0xf0) {
        m_char_size = 3;
        return;
      }
    }
    if (!--m_data_size) {
      data_string(m_data.str());
      m_state = CODE;
    }
  }

  void data(unsigned char ch) {
    m_data.push(ch);
    if (m_data.length() < m_data_size)
      return;

    // int
    ch = (unsigned char)m_head[0];
    if (ch == 'I') {
      uint32_t n;
      n = ((uint32_t)(unsigned char)m_data[0] << 24)
        | ((uint32_t)(unsigned char)m_data[1] << 16)
        | ((uint32_t)(unsigned char)m_data[2] << 8)
        | ((uint32_t)(unsigned char)m_data[3] << 0);
      data_int(n);
    } else if (0xc0 <= ch && ch <= 0xcf) {
      data_int((((int)ch - 0xc8) << 8) | m_data[0]);
    } else if (0xd0 <= ch && ch <= 0xd7) {
      data_int((((int)ch - 0xd4) << 16)
        | ((int)(unsigned char)m_data[0] << 8)
        | ((int)(unsigned char)m_data[1] << 0));

    // long
    } else if (ch == 'L') {
      int64_t n;
      n = ((int64_t)(unsigned char)m_data[0] << 56)
        | ((int64_t)(unsigned char)m_data[1] << 48)
        | ((int64_t)(unsigned char)m_data[2] << 40)
        | ((int64_t)(unsigned char)m_data[3] << 32)
        | ((int64_t)(unsigned char)m_data[4] << 24)
        | ((int64_t)(unsigned char)m_data[5] << 16)
        | ((int64_t)(unsigned char)m_data[6] << 8)
        | ((int64_t)(unsigned char)m_data[7] << 0);
      data_long(n);
    } else if (ch == 0x59) {
      int32_t n;
      n = ((int32_t)(unsigned char)m_data[0] << 24)
        | ((int32_t)(unsigned char)m_data[1] << 16)
        | ((int32_t)(unsigned char)m_data[2] << 8)
        | ((int32_t)(unsigned char)m_data[3] << 0);
      data_long(n);
    } else if (0xf0 <= ch && ch <= 0xff) {
      data_long((((int)ch - 0xf8) << 8) | m_data[0]);
    } else if (0x38 <= ch && ch <= 0x3f) {
      data_long((((int)ch - 0x3c) << 16)
        | ((int)(unsigned char)m_data[0] << 8)
        | ((int)(unsigned char)m_data[1] << 0));

    // double
    } else if (ch == 'D') {
      uint64_t n;
      n = ((uint64_t)(unsigned char)m_data[0] << 56)
        | ((uint64_t)(unsigned char)m_data[1] << 48)
        | ((uint64_t)(unsigned char)m_data[2] << 40)
        | ((uint64_t)(unsigned char)m_data[3] << 32)
        | ((uint64_t)(unsigned char)m_data[4] << 24)
        | ((uint64_t)(unsigned char)m_data[5] << 16)
        | ((uint64_t)(unsigned char)m_data[6] << 8)
        | ((uint64_t)(unsigned char)m_data[7] << 0);
      data_double(*(double*)&n);
    } else if (ch == 0x5d) {
      data_double((signed char)m_data[0]);
    } else if (ch == 0x5e) {
      int16_t n;
      n = ((int16_t)(unsigned char)m_data[0] << 8)
        | ((int16_t)(unsigned char)m_data[1] << 0);
      data_double(n);
    } else if (ch == 0x5f) {
      uint32_t n;
      n = ((uint32_t)(unsigned char)m_data[0] << 24)
        | ((uint32_t)(unsigned char)m_data[1] << 16)
        | ((uint32_t)(unsigned char)m_data[2] << 8)
        | ((uint32_t)(unsigned char)m_data[3] << 0);
      data_double(*(float*)&n);

    // string
    } else if (
      ch == 'S' ||
      ch < 0x20 ||
      (0x30 <= ch && ch <= 0x33)
    ) {
      data_string(m_data.str());
    }

    m_state = CODE;
  }

  void data_null() {
    if (m_stack.size() > 0 && m_stack.top().semantic != VALUE) {
      error("unexpected null value");
    } else {
      data_begin();
      value(pjs::Value::null);
      data_end();
    }
  }

  void data_bool(bool val) {
    if (m_stack.size() > 0 && m_stack.top().semantic != VALUE) {
      error("unexpected boolean value");
    } else {
      data_begin();
      value(val);
      data_end();
    }
  }

  void data_int(int val) {
    if (m_stack.size() > 0) {
      switch (m_stack.top().semantic) {
        case VALUE:
          data_begin();
          value(val);
          data_end();
          break;
        case COUNT:
          m_stack.top().count = val;
          data_end();
          break;
        case DEFINITION:
          if (0 <= val && val < m_class_map.size()) {
            m_stack.top().definition = val;
            data_end();
          } else {
            error("class def out of range");
          }
          break;
        default:
          error("unexpected int value");
          break;
      }
    } else {
      value(val);
      data_end();
    }
  }

  void data_long(long long val) {
    if (m_stack.size() > 0) {
      switch (m_stack.top().semantic) {
        case VALUE:
          data_begin();
          value(double(val));
          data_end();
          break;
        case COUNT:
          m_stack.top().count = val;
          data_end();
          break;
        case DEFINITION:
          if (0 <= val && val < m_class_map.size()) {
            m_stack.top().definition = val;
            data_end();
          } else {
            error("class def out of range");
          }
          break;
        default:
          error("unexpected long value");
          break;
      }
    } else {
      value(double(val));
      data_end();
    }
  }

  void data_double(double val) {
    if (m_stack.size() > 0 && m_stack.top().semantic != VALUE) {
      error("unexpected double value");
    } else {
      data_begin();
      value(val);
      data_end();
    }
  }

  void data_string(const std::string &val) {
    if (m_stack.size() > 0) {
      switch (m_stack.top().semantic) {
        case VALUE:
          data_begin();
          value(val);
          data_end();
          break;
        case KEY:
          if (m_stack.top().collection == CLASS) {
            m_class_map.back().push_back(val);
            data_end();
          } else {
            map_key(val);
            data_end();
          }
          break;
        case NAME:
          data_end();
          break;
        default:
          error("unexpected string value");
          break;
      }
    } else {
      value(val);
      data_end();
    }
  }

  void data_list() {
    if (m_stack.size() > 0 && m_stack.top().semantic != VALUE) {
      error("unexpected list value");
    } else {
      data_begin();
      m_stack.push({ LIST, VALUE, 0, 0 });
      list_start();
    }
  }

  void data_map() {
    if (m_stack.size() > 0 && m_stack.top().semantic != VALUE) {
      error("unexpected map value");
    } else {
      data_begin();
      m_stack.push({ MAP, KEY, 0, 0 });
      map_start();
    }
  }

  void data_class() {
    if (m_stack.size() > 0 && m_stack.top().semantic != VALUE) {
      error("unexpected class def");
    } else {
      m_class_map.push_back(std::vector<std::string>());
      m_stack.push({ CLASS, NAME, 0, 0 });
    }
  }

  void data_object(int definition) {
    if (m_stack.size() > 0 && m_stack.top().semantic != VALUE) {
      error("unexpected object value");
    } else if (definition >= 0) {
      data_begin();
      m_stack.push({ OBJECT, VALUE, definition, 0 });
      map_start();
    } else {
      data_begin();
      m_stack.push({ OBJECT, DEFINITION, 0, 0 });
      map_start();
    }
  }

  void data_begin() {
    if (m_stack.size() > 0) {
      const auto &top = m_stack.top();
      if (top.collection == OBJECT) {
        const auto &key = m_class_map[top.definition][top.count];
        map_key(key);
      }
    }
  }

  void data_end() {
    if (m_stack.size() > 0) {
      auto &top = m_stack.top();
      switch (top.collection) {
        case LIST:
          break;
        case MAP:
          switch (top.semantic) {
            case VALUE:
              top.semantic = KEY;
              break;
            default:
              top.semantic = VALUE;
              break;
          }
          break;
        case CLASS:
          switch (top.semantic) {
            case NAME:
              top.semantic = COUNT;
              break;
            case COUNT:
              if (top.semantic > 0) {
                top.semantic = KEY;
              } else {
                m_stack.pop();
              }
              break;
            case KEY:
              if (--top.count == 0) m_stack.pop();
              break;
            default:
              break;
          }
          break;
        case OBJECT:
          switch (top.semantic) {
            case DEFINITION:
              if (m_class_map[top.definition].size() > 0) {
                top.semantic = VALUE;
              } else {
                map_end();
                m_stack.pop();
                data_end();
              }
              break;
            case VALUE:
              if (++top.count == m_class_map[top.definition].size()) {
                map_end();
                m_stack.pop();
                data_end();
              }
              break;
            default:
              break;
          }
          break;
      }
    }
  }

  void value(const pjs::Value &v) {
    if (m_object_stack.size() > 0) {
      auto parent = m_object_stack.top();
      if (parent->type() == pjs::class_of<pjs::Array>()) {
        parent->as<pjs::Array>()->push(v);
      } else {
        parent->ht_set(m_current_map_key, v);
      }
    } else {
      if (m_count == 0) {
        m_output = v;
      } else if (m_count == 1) {
        auto *a = pjs::Array::make();
        a->push(m_output);
        a->push(v);
        m_output.set(a);
      } else {
        m_output.as<pjs::Array>()->push(v);
      }
      m_count++;
    }
  }

  void list_start() {
    auto *a = pjs::Array::make();
    value(a);
    m_object_stack.push(a);
  }

  void list_end() {
    if (m_object_stack.size() > 0) {
      m_object_stack.pop();
    }
  }

  void map_start() {
    auto *o = pjs::Object::make();
    value(o);
    m_object_stack.push(o);
  }

  void map_key(const std::string &k) {
    m_current_map_key = k;
  }

  void map_end() {
    if (m_object_stack.size() > 0) {
      m_object_stack.pop();
    }
  }

  void error(const char *msg) {
    m_error = msg;
    m_state = ERROR;
  }
};

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// Hessian
//

template<> void ClassDef<Hessian>::init() {
  ctor();

  method("decode", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    if (!data) { ret = Value::null; return; }
    ret.set(Hessian::decode(*data));
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Value val;
    if (!ctx.arguments(1, &val)) return;
    auto *data = pipy::Data::make();
    Hessian::encode(val, *data);
    ret.set(data);
  });
}

//
// Hessian::Collection::Kind
//

template<> void EnumDef<Hessian::Collection::Kind>::init() {
  define(Hessian::Collection::Kind::list, "list");
  define(Hessian::Collection::Kind::map, "map");
  define(Hessian::Collection::Kind::class_def, "class_def");
  define(Hessian::Collection::Kind::object, "object");
}

//
// Hessian::Collection
//

template<> void ClassDef<Hessian::Collection>::init() {
  field<EnumValue<Hessian::Collection::Kind>>("kind", [](Hessian::Collection *obj) { return &obj->kind; });
  field<Ref<Str>>("type", [](Hessian::Collection *obj) { return &obj->type; });
  field<Ref<Object>>("elements", [](Hessian::Collection *obj) { return &obj->elements; });
  field<Ref<Hessian::Collection>>("definition", [](Hessian::Collection *obj) { return &obj->definition; });
}

} // namespace pjs
