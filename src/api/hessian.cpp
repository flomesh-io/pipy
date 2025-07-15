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
#include "api/c-string.hpp"

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
// x80 - xbf    # one-octet compact int (-x10 to x2f, x90 is 0)
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
//            ::= [x80-xbf]             # -x10 to x2f
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
// JSON::DecodeOptions
//

Hessian::DecodeOptions::DecodeOptions(pjs::Object *options) {
  Value(options, "maxStringSize")
    .get(max_string_size)
    .check_nullable();
}

//
// Hessian
//

static Data::Producer s_dp("Hessian");

auto Hessian::decode(const Data &data, const DecodeOptions &options) -> pjs::Array* {
  pjs::Array *a = pjs::Array::make();
  StreamParser sp(
    [=](const pjs::Value &value) {
      a->push(value);
    }
  );
  sp.set_max_string_size(options.max_string_size);
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
  ReferenceMap<pjs::Object> map_objs;
  ReferenceMap<pjs::Str> map_defs;
  ReferenceMap<pjs::Str> map_types;

  auto write_int = [&](int value) {
    auto i = int32_t(value);
    if (-0x10 <= i && i <= 0x2f) {
      db.push(int(i) + 0x90);
    } else if (-0x800 <= i && i <= 0x7ff) {
      db.push(int(i >> 8) + 0xc8);
      db.push(int(i >> 0) & 0xff);
    } else if (-0x40000 <= i && i <= 0x3ffff) {
      db.push(int(i >> 16) + 0xd4);
      db.push(int(i >>  8) & 0xff);
      db.push(int(i >>  0) & 0xff);
    } else {
      db.push('I');
      db.push(int(i >> 24) & 0xff);
      db.push(int(i >> 16) & 0xff);
      db.push(int(i >>  8) & 0xff);
      db.push(int(i >>  0) & 0xff);
    }
  };

  auto write_string = [&](pjs::Str *value) {
    auto n = value->length();
    if (n < 32) {
      db.push(n);
      db.push(value->str());
    } else if (n < 1024) {
      db.push(0x30 + (n >> 8));
      db.push(n);
      db.push(value->str());
    } else {
      int i = 0;
      while (i < n) {
        auto l = std::min(n - i, 0xffff);
        auto a = value->chr_to_pos(i);
        auto b = value->chr_to_pos(i + l);
        // if (b == value->size()) db.push('S'); else db.push('R');
        db.push(b == value->size() ? 'S' : 'R');
        db.push(l >> 8);
        db.push(l >> 0);
        db.push(value->c_str() + a, b - a);
        i += l;
      }
    }
  };

  auto write_c_string = [&](CString *value) {
    char buf[1024 * 4];
    int n = 0, p = 0, end = value->data()->size();
    Data chunk;
    Data::Builder chunk_db(chunk, &s_dp);
    auto flush_chunk = [&](int n) {
      chunk_db.flush();
      chunk_db.reset();
      db.push(p == end ? 'S' : 'R');
      db.push(n >> 8);
      db.push(n >> 0);
      db.push(std::move(chunk));
    };
    pjs::Utf8Decoder decoder(
      [&](int) {
        if (++n >= 1024) {
          if (n == 1024) chunk_db.push(buf, p);
          if (n % 0xffff == 0) flush_chunk(0xffff);
        }
      }
    );
    for (const auto chk : value->data()->chunks()) {
      auto str = std::get<0>(chk);
      auto len = std::get<1>(chk);
      for (int i = 0; i < len; i++, p++) {
        auto c = str[i];
        if (n < 1023 && p < sizeof(buf)) buf[p] = c;
        decoder.input(c);
      }
    }
    if (n < 32) {
      db.push(n);
      db.push(buf, p);
    } else if (n < 1024) {
      db.push(0x30 + (n >> 8));
      db.push(n);
      db.push(buf, p);
    } else if (chunk_db.size() > 0) {
      flush_chunk(n % 0xffff);
    }
  };

  auto write_type = [&](pjs::Str *value) {
    int i = map_types.find(value);
    if (i >= 0) {
      write_int(i);
    } else {
      write_string(value);
      map_types.add(value);
    }
  };

  std::function<bool(const pjs::Value &)> write_value;
  write_value = [&](const pjs::Value &value) -> bool {
    if (value.is_null()) {
      db.push('N');

    } else if (value.is_boolean()) {
      db.push(value.b() ? 'T' : 'F');

    } else if (value.is<pjs::Int>()) {
      auto *i = value.as<pjs::Int>();
      if (i->width() == 64) {
        auto n = i->value();
        if (-0x8 <= n && n <= 0xf) {
          db.push(int(n) + 0xe0);
        } else if (-0x800 <= n && n <= 0x7ff) {
          db.push(int(n >> 8) + 0xf8);
          db.push(int(n >> 0) & 0xff);
        } else if (-0x40000 <= n && n <= 0x3ffff) {
          db.push(int(n >> 16) + 0x3c);
          db.push(int(n >>  8) & 0xff);
          db.push(int(n >>  0) & 0xff);
        } else if (-0x80000000ll <= n && n <= 0x7fffffffll) {
          db.push(0x59);
          db.push(int(n >> 24) & 0xff);
          db.push(int(n >> 16) & 0xff);
          db.push(int(n >>  8) & 0xff);
          db.push(int(n >>  0) & 0xff);
        } else {
          db.push('L');
          db.push(int(n >> 56) & 0xff);
          db.push(int(n >> 48) & 0xff);
          db.push(int(n >> 40) & 0xff);
          db.push(int(n >> 32) & 0xff);
          db.push(int(n >> 24) & 0xff);
          db.push(int(n >> 16) & 0xff);
          db.push(int(n >>  8) & 0xff);
          db.push(int(n >>  0) & 0xff);
        }
      } else {
        write_int(i->value());
      }
    } else if (value.is_number()) {
      auto n = value.n();
      auto done = false;
      if (pjs::Number::is_integer(n)) {
        auto i = int32_t(n);
        if (i == 0) {
          db.push(0x5b);
          done = true;
        } else if (i == 1) {
          db.push(0x5c);
          done = true;
        } else if (-128 <= i && i <= 127) {
          db.push(0x5d);
          db.push(i);
          done = true;
        } else if (-32768 <= i && i <= 32767) {
          db.push(0x5e);
          db.push(int(i >> 8) & 0xff);
          db.push(int(i >> 0) & 0xff);
          done = true;
        }
      }
      if (!done) {
        int64_t i; *(double*)&i = n;
        db.push('D');
        db.push(int(i >> 56) & 0xff);
        db.push(int(i >> 48) & 0xff);
        db.push(int(i >> 40) & 0xff);
        db.push(int(i >> 32) & 0xff);
        db.push(int(i >> 24) & 0xff);
        db.push(int(i >> 16) & 0xff);
        db.push(int(i >>  8) & 0xff);
        db.push(int(i >>  0) & 0xff);
      }

    } else if (value.is_string()) {
      write_string(value.s());

    } else if (value.is<CString>()) {
      write_c_string(value.as<CString>());

    } else if (value.is<pjs::Date>()) {
      uint64_t t = value.as<pjs::Date>()->getTime();
      db.push(0x4a);
      db.push((char)(t >> 56));
      db.push((char)(t >> 48));
      db.push((char)(t >> 40));
      db.push((char)(t >> 32));
      db.push((char)(t >> 24));
      db.push((char)(t >> 16));
      db.push((char)(t >>  8));
      db.push((char)(t >>  0));

    } else if (value.is<Data>()) {
      Data &d(*value.as<Data>());
      if (d.size() < 16) {
        db.push(0x20 + d.size());
        db.push(d);
      } else if (d.size() < 1024) {
        db.push(0x34 + (d.size() >> 8));
        db.push(d.size() & 0xff);
        db.push(d);
      } else {
        Data buf(d);
        do {
          Data chk;
          buf.shift(std::min(buf.size(), 0xffff), chk);
          db.push(buf.empty() ? 'B' : 0x41);
          db.push(chk.size() >> 8);
          db.push(chk.size() >> 0);
          db.push(chk);
        } while (!buf.empty());
      }

    } else if (value.is_object()) {
      int i = map_objs.find(value.o());
      if (i >= 0) {
        db.push(0x51);
        write_int(i);

      } else {
        pjs::Ref<Collection> c = pjs::coerce<Collection>(value.o());
        auto *t = c->type.get();
        auto *e = c->elements.get();
        auto *a = e && e->is_array() ? e->as<pjs::Array>() : nullptr;

        switch (c->kind.get()) {
          case Collection::Kind::list: {
            int n = a ? a->length() : 0;
            if (t && t != pjs::Str::empty) {
              // Small fixed-length list optimization is currently disabled
#if 0
              if (n < 8) {
                db.push(0x70 + n);
                write_type(t);
              } else
#endif
              {
                db.push('V');
                write_type(t);
                write_int(n);
              }
            } else {
              // Small fixed-length list optimization is currently disabled
#if 0
              if (n < 8) {
                db.push(0x78 + n);
              } else
#endif
              {
                db.push(0x58);
                write_int(n);
              }
            }
            for (int i = 0; i < n; i++) {
              pjs::Value v;
              a->get(i, v);
              if (!write_value(v)) return false;
            }
            break;
          }

          case Collection::Kind::map: {
            if (t && t != pjs::Str::empty) {
              db.push('M');
              write_type(t);
            } else {
              db.push('H');
            }
            if (a) {
              for (int i = 0, n = a->length(); i < n; i++) {
                pjs::Value e, k, v;
                a->get(i, e); if (!e.is_array()) return false;
                e.as<pjs::Array>()->get(0, k); if (!write_value(k)) return false;
                e.as<pjs::Array>()->get(1, v); if (!write_value(v)) return false;
              }
            }
            db.push('Z');
            break;
          }

          case Collection::Kind::object: {
            if (!t || t == pjs::Str::empty) return false;
            int n = e ? e->ht_size() : 0;
            int i = map_defs.find(t);
            if (i < 0) {
              i = map_defs.add(t);
              db.push('C');
              write_string(t);
              write_int(n);
              if (e) {
                e->iterate_hash(
                  [&](pjs::Str *k, pjs::Value &) {
                    write_string(k);
                    return true;
                  }
                );
              }
            }
            if (i < 16) {
              db.push(0x60 + i);
            } else {
              db.push('O');
              write_int(i);
            }
            if (e) {
              auto done = e->iterate_hash(
                [&](pjs::Str *, pjs::Value &v) {
                  return write_value(v);
                }
              );
              if (!done) return false;
            }
            break;
          }

          default: return false;
        }

        map_objs.add(value.o());
      }
    }

    return true;
  };

  if (value.is_array()) {
    value.as<pjs::Array>()->iterate_while(
      [&](pjs::Value &v, int) {
        return write_value(v);
      }
    );
  } else {
    write_value(value);
  }
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

void Hessian::Parser::parse(Data &data) {
  Deframer::deframe(data);
  if (Deframer::state() == START) end();
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
        o->type = d->type;
        return push(o, CollectionState::VALUE, n, d);

      } else if (c < 0x78) {
        // x70 - x77 : fixed list with direct length
        return push(Collection::make(Collection::Kind::list), CollectionState::TYPE, c - 0x70);

      } else if (c < 0x80) {
        // x78 - x7f : fixed untyped list with direct length
        return push(Collection::make(Collection::Kind::list), CollectionState::VALUE, c - 0x78);

      } else if (c < 0xc0) {
        // x80 - xbf : one-octet compact int (-x10 to x2f, x90 is 0)
        return push(pjs::Int::make(pjs::Int::Type::i32, int64_t(c - 0x90)));

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
        return push(pjs::Int::make(pjs::Int::Type::i64, int64_t(c - 0xe0)));

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
      return push(pjs::Int::make(pjs::Int::Type::i32, int64_t(
        ((int32_t)m_read_number[0] << 24)|
        ((int32_t)m_read_number[1] << 16)|
        ((int32_t)m_read_number[2] <<  8)|
        ((int32_t)m_read_number[3] <<  0)
      )));
    case LONG:
      return push(pjs::Int::make(pjs::Int::Type::i64,
        ((int64_t)m_read_number[0] << 56)|
        ((int64_t)m_read_number[1] << 48)|
        ((int64_t)m_read_number[2] << 40)|
        ((int64_t)m_read_number[3] << 32)|
        ((int64_t)m_read_number[4] << 24)|
        ((int64_t)m_read_number[5] << 16)|
        ((int64_t)m_read_number[6] <<  8)|
        ((int64_t)m_read_number[7] <<  0)
      ));
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

void Hessian::Parser::push_utf8_char(int c) {
  m_utf8_length--;
}

auto Hessian::Parser::push_string() -> State {
  if (m_max_string_size >= 0 && m_read_data->size() > m_max_string_size) {
    auto s = CString::make(*m_read_data);
    m_read_data->clear();
    return push(s);
  } else {
    std::string s = m_read_data->to_string();
    m_read_data->clear();
    return push(pjs::Str::make(std::move(s)));
  }
}

auto Hessian::Parser::push(const pjs::Value &value, CollectionState state, int length, Collection *class_def) -> State {
  pjs::Value v(value);

  if (m_is_ref) {
    if (!v.is_number()) return ERROR;
    auto obj = m_obj_refs.get(v.n());
    if (!obj) return ERROR;
    v.set(obj);
  }

  if (!v.is<Collection>() || v.as<Collection>()->kind != Collection::Kind::class_def) {
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
              if (v.is_string()) {
                c->elements->as<pjs::Array>()->set(i, v.s());
              } else if (v.is<CString>()) {
                c->elements->as<pjs::Array>()->set(i, v.o());
              } else {
                return ERROR;
              }
              break;
            case Collection::Kind::object:
              if (auto *d = l->class_def.get()) {
                pjs::Value k;
                d->elements->as<pjs::Array>()->get(i, k);
                if (k.is_string()) {
                  c->elements->set(k.s(), v);
                } else if (k.is<CString>()) {
                  c->elements->set(k.as<CString>()->to_str(), v);
                } else {
                  return ERROR;
                }
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
          if (v.is_number_like()) {
            int n = v.to_int32();
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
          if (v.is<CString>()) v.set(v.as<CString>()->to_str());
          if (v.is_string()) {
            c->type = v.s();
            if (
              c->kind == Collection::Kind::list ||
              c->kind == Collection::Kind::map
            ) m_type_refs.add(v.s());
          } else if (value.is_number_like()) {
            auto s = m_type_refs.get(v.to_int32());
            if (!s) return ERROR;
            c->type = s;
          } else {
            return ERROR;
          }
          l->state = (l->state == CollectionState::TYPE_LENGTH ? CollectionState::LENGTH : CollectionState::VALUE);
          return START;
        case CollectionState::CLASS_DEF:
          if (!m_is_ref && v.is_number_like()) {
            if (auto d = m_def_refs.get(v.to_int32())) {
              c->type = d->type;
              l->class_def = d;
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
    l->class_def = class_def;
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
    Object *options = nullptr;
    if (!ctx.arguments(1, &data, &options)) return;
    if (!data) { ret = Value::null; return; }
    ret.set(Hessian::decode(*data, options));
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
}

} // namespace pjs
