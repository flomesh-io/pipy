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
#include "data.hpp"

#include <cstdio>
#include <stack>

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
    try {
      Hessian::decode(*data, ret);
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    Value val;
    if (!ctx.arguments(1, &val)) return;
    auto *data = pipy::Data::make();
    Hessian::encode(val, *data);
    ret.set(data);
  });
}

} // namespace pjs

namespace pipy {

thread_local static Data::Producer s_dp("Hessian");

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

//
// Hessian
//

void Hessian::decode(const Data &data, pjs::Value &val) {
  HessianParser parser(val);
  for (const auto c : data.chunks()) {
    if (!parser.parse(std::get<0>(c), std::get<1>(c))) {
      throw std::runtime_error(parser.error());
    }
  }
}

bool Hessian::encode(const pjs::Value &val, Data &data) {
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
      s_dp.push(&data, (char)n);
      s_dp.push(&data, str.c_str());
    } else if (n < 1024) {
      s_dp.push(&data, (char)(n >> 8) | 0x30);
      s_dp.push(&data, (char)(n >> 0));
      s_dp.push(&data, str.c_str());
    } else if (n < 65536) {
      s_dp.push(&data, 'S');
      s_dp.push(&data, (char)(n >> 8));
      s_dp.push(&data, (char)(n >> 0));
      s_dp.push(&data, str.c_str());
    } else {
      s_dp.push(&data, 'S');
      s_dp.push(&data, 0xff);
      s_dp.push(&data, 0xff);
      s_dp.push(&data, str.c_str(), 65536);
    }
  };

  std::function<bool(const pjs::Value&)> write;
  write = [&](const pjs::Value &v) -> bool {
    if (v.is_undefined()) {
      s_dp.push(&data, 'N');

    } else if (v.is_boolean()) {
      s_dp.push(&data, v.b() ? 'T' : 'F');

    } else if (v.is_number()) {
      auto n = v.n();
      double i;
      if (std::isnan(n) || std::isinf(n) || std::modf(n, &i)) {
        int64_t tmp; *(double*)&tmp = n;
        s_dp.push(&data, 'D');
        s_dp.push(&data, (char)(tmp >> 56));
        s_dp.push(&data, (char)(tmp >> 48));
        s_dp.push(&data, (char)(tmp >> 40));
        s_dp.push(&data, (char)(tmp >> 32));
        s_dp.push(&data, (char)(tmp >> 24));
        s_dp.push(&data, (char)(tmp >> 16));
        s_dp.push(&data, (char)(tmp >> 8 ));
        s_dp.push(&data, (char)(tmp >> 0 ));

      } else {
        auto n = int64_t(v.n());
        if (std::numeric_limits<int>::min() <= n && n <= std::numeric_limits<int>::max()) {
          s_dp.push(&data, 'I');
          s_dp.push(&data, (char)(n >> 24));
          s_dp.push(&data, (char)(n >> 16));
          s_dp.push(&data, (char)(n >> 8 ));
          s_dp.push(&data, (char)(n >> 0 ));
        } else {
          s_dp.push(&data, 'L');
          s_dp.push(&data, (char)(n >> 56));
          s_dp.push(&data, (char)(n >> 48));
          s_dp.push(&data, (char)(n >> 40));
          s_dp.push(&data, (char)(n >> 32));
          s_dp.push(&data, (char)(n >> 24));
          s_dp.push(&data, (char)(n >> 16));
          s_dp.push(&data, (char)(n >> 8 ));
          s_dp.push(&data, (char)(n >> 0 ));
        }
      }

    } else if (v.is_string()) {
      write_str(v.s()->str());

    } else if (v.is_object()) {

      if (v.is_array()) {
        if (level++ > 0) s_dp.push(&data, 0x57);
        auto a = v.as<pjs::Array>();
        auto n = a->iterate_while([&](pjs::Value &v, int i) -> bool {
          return write(v);
        });
        if (n < a->length()) return false;
        if (--level > 0) s_dp.push(&data, 'Z');

      } else if (!v.o()) {
        s_dp.push(&data, 'N');

      } else {
        s_dp.push(&data, 'H');
        level++;
        auto done = v.o()->iterate_while([&](pjs::Str *k, pjs::Value &v) -> bool {
          write_str(k->str());
          return write(v);
        });
        if (!done) return false;
        s_dp.push(&data, 'Z');
        level--;
      }
    }

    return true;
  };

  return write(val);
}

} // namespace pipy
