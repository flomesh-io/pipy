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

#include "hessian2.hpp"
#include "buffer.hpp"
#include "logging.hpp"

NS_BEGIN

namespace hessian2 {

  //
  // Parser
  //

  class Parser {
  public:
    Parser() {
    }

    ~Parser() {
    }

    void reset() {
      m_state = CODE;
      m_stack.clear();
      m_class_map.clear();
    }

    void code(unsigned char ch, Object::Receiver out) {
      m_head.clear();
      m_head.push(ch);

      // null
      if (ch == 'N') {
        data_null(out);

      // true
      } else if (ch == 'T') {
        data_bool(true, out);

      // false
      } else if (ch == 'F') {
        data_bool(false, out);

      // int
      } else if (ch == 'I') {
        m_data_size = 4;
        m_data.clear();
        m_state = DATA;
      } else if (0x80 <= ch && ch <= 0xd7) {
        if (ch < 0xc0) {
          data_int((int)ch - 0x90, out);
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
          data_long((int)ch - 0xe0, out);
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
        data_double(0, out);
      } else if (ch == 0x5c) {
        data_double(1, out);
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
          data_string(std::string(), out);
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
        data_list(out);

      // map
      } else if (ch == 'H') {
        data_map(out);

      // class
      } else if (ch == 'C') {
        data_class();

      // object
      } else if (ch == 'O') {
        data_object(-1, out);

      // object (short form)
      } else if (0x60 <= ch && ch <= 0x6f) {
        data_object(ch - 0x60, out);

      // list/map end
      } else if (ch == 'Z') {
        if (m_stack.size() > 0) {
          switch (m_stack.back().collection) {
            case MAP:
              out(make_object<MapEnd>());
              data_end(out);
              break;
            case LIST:
              out(make_object<ListEnd>());
              data_end(out);
              break;
            default:
              error("unexpected bytecode 'Z'");
              break;
          }
          m_stack.pop_back();
        } else {
          error("unexpected bytecode 'Z'");
        }

      // unrecognized stuff
      } else {
        Log::error("[hessian2] unrecognized bytecode 0x%02x", ch);
        m_state = ERROR;
      }
    }

    void head(unsigned char ch, Object::Receiver out) {
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
        data_string(std::string(), out);
        m_state = CODE;
      } else {
        m_data_size = n;
        m_char_size = 0;
        m_data.clear();
        m_state = UTF8;
      }
    }

    void utf8(unsigned char ch, Object::Receiver out) {
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
        data_string(m_data.str(), out);
        m_state = CODE;
      }
    }

    void data(unsigned char ch, Object::Receiver out) {
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
        data_int(n, out);
      } else if (0xc0 <= ch && ch <= 0xcf) {
        data_int((((int)ch - 0xc8) << 8) | m_data[0], out);
      } else if (0xd0 <= ch && ch <= 0xd7) {
        data_int((((int)ch - 0xd4) << 16)
          | ((int)(unsigned char)m_data[0] << 8)
          | ((int)(unsigned char)m_data[1] << 0),
          out);

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
        data_long(n, out);
      } else if (ch == 0x59) {
        int32_t n;
        n = ((int32_t)(unsigned char)m_data[0] << 24)
          | ((int32_t)(unsigned char)m_data[1] << 16)
          | ((int32_t)(unsigned char)m_data[2] << 8)
          | ((int32_t)(unsigned char)m_data[3] << 0);
        data_long(n, out);
      } else if (0xf0 <= ch && ch <= 0xff) {
        data_long((((int)ch - 0xf8) << 8) | m_data[0], out);
      } else if (0x38 <= ch && ch <= 0x3f) {
        data_long((((int)ch - 0x3c) << 16)
          | ((int)(unsigned char)m_data[0] << 8)
          | ((int)(unsigned char)m_data[1] << 0),
          out);

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
        data_double(*(double*)&n, out);
      } else if (ch == 0x5d) {
        data_double((signed char)m_data[0], out);
      } else if (ch == 0x5e) {
        int16_t n;
        n = ((int16_t)(unsigned char)m_data[0] << 8)
          | ((int16_t)(unsigned char)m_data[1] << 0);
        data_double(n, out);
      } else if (ch == 0x5f) {
        uint32_t n;
        n = ((uint32_t)(unsigned char)m_data[0] << 24)
          | ((uint32_t)(unsigned char)m_data[1] << 16)
          | ((uint32_t)(unsigned char)m_data[2] << 8)
          | ((uint32_t)(unsigned char)m_data[3] << 0);
        data_double(*(float*)&n, out);

      // string
      } else if (
        ch == 'S' ||
        ch < 0x20 ||
        (0x30 <= ch && ch <= 0x33)
      ) {
        data_string(m_data.str(), out);
      }

      m_state = CODE;
    }

    void data_null(Object::Receiver out) {
      if (m_stack.size() > 0 && m_stack.back().semantic != VALUE) {
        error("unexpected null value");
      } else {
        data_begin(out);
        out(make_object<NullValue>());
        data_end(out);
      }
    }

    void data_bool(bool val, Object::Receiver out) {
      if (m_stack.size() > 0 && m_stack.back().semantic != VALUE) {
        error("unexpected boolean value");
      } else {
        data_begin(out);
        out(make_object<BoolValue>(val));
        data_end(out);
      }
    }

    void data_int(int val, Object::Receiver out) {
      if (m_stack.size() > 0) {
        switch (m_stack.back().semantic) {
          case VALUE:
            data_begin(out);
            out(make_object<IntValue>(val));
            data_end(out);
            break;
          case COUNT:
            m_stack.back().count = val;
            data_end(out);
            break;
          case DEFINITION:
            if (0 <= val && val < m_class_map.size()) {
              m_stack.back().definition = val;
              data_end(out);
            } else {
              error("class def out of range");
            }
            break;
          default:
            error("unexpected int value");
            break;
        }
      } else {
        out(make_object<IntValue>(val));
        data_end(out);
      }
    }

    void data_long(long long val, Object::Receiver out) {
      if (m_stack.size() > 0) {
        switch (m_stack.back().semantic) {
          case VALUE:
            data_begin(out);
            out(make_object<LongValue>(val));
            data_end(out);
            break;
          case COUNT:
            m_stack.back().count = val;
            data_end(out);
            break;
          case DEFINITION:
            if (0 <= val && val < m_class_map.size()) {
              m_stack.back().definition = val;
              data_end(out);
            } else {
              error("class def out of range");
            }
            break;
          default:
            error("unexpected long value");
            break;
        }
      } else {
        out(make_object<LongValue>(val));
        data_end(out);
      }
    }

    void data_double(double val, Object::Receiver out) {
      if (m_stack.size() > 0 && m_stack.back().semantic != VALUE) {
        error("unexpected double value");
      } else {
        data_begin(out);
        out(make_object<DoubleValue>(val));
        data_end(out);
      }
    }

    void data_string(const std::string &val, Object::Receiver out) {
      if (m_stack.size() > 0) {
        switch (m_stack.back().semantic) {
          case VALUE:
            data_begin(out);
            out(make_object<StringValue>(val));
            data_end(out);
            break;
          case KEY:
            if (m_stack.back().collection == CLASS) {
              m_class_map.back().push_back(val);
              data_end(out);
            } else {
              out(make_object<MapKey>(val));
              data_end(out);
            }
            break;
          case NAME:
            data_end(out);
            break;
          default:
            error("unexpected string value");
            break;
        }
      } else {
        out(make_object<StringValue>(val));
        data_end(out);
      }
    }

    void data_list(Object::Receiver out) {
      if (m_stack.size() > 0 && m_stack.back().semantic != VALUE) {
        error("unexpected list value");
      } else {
        data_begin(out);
        m_stack.push_back({ LIST, VALUE, 0, 0 });
        out(make_object<ListStart>());
      }
    }

    void data_map(Object::Receiver out) {
      if (m_stack.size() > 0 && m_stack.back().semantic != VALUE) {
        error("unexpected map value");
      } else {
        data_begin(out);
        m_stack.push_back({ MAP, KEY, 0, 0 });
        out(make_object<MapStart>());
      }
    }

    void data_class() {
      if (m_stack.size() > 0 && m_stack.back().semantic != VALUE) {
        error("unexpected class def");
      } else {
        m_class_map.push_back(std::vector<std::string>());
        m_stack.push_back({ CLASS, NAME, 0, 0 });
      }
    }

    void data_object(int definition, Object::Receiver out) {
      if (m_stack.size() > 0 && m_stack.back().semantic != VALUE) {
        error("unexpected object value");
      } else if (definition >= 0) {
        data_begin(out);
        m_stack.push_back({ OBJECT, VALUE, definition, 0 });
        out(make_object<MapStart>());
      } else {
        data_begin(out);
        m_stack.push_back({ OBJECT, DEFINITION, 0, 0 });
        out(make_object<MapStart>());
      }
    }

    void data_begin(Object::Receiver out) {
      if (m_stack.size() > 0) {
        const auto &top = m_stack.back();
        if (top.collection == OBJECT) {
          const auto &key = m_class_map[top.definition][top.count];
          out(make_object<MapKey>(key));
        }
      }
    }

    void data_end(Object::Receiver out) {
      if (m_stack.size() > 0) {
        auto &top = m_stack.back();
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
                  m_stack.pop_back();
                }
                break;
              case KEY:
                if (--top.count == 0) m_stack.pop_back();
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
                  out(make_object<MapEnd>());
                  m_stack.pop_back();
                  data_end(out);
                }
                break;
              case VALUE:
                if (++top.count == m_class_map[top.definition].size()) {
                  out(make_object<MapEnd>());
                  m_stack.pop_back();
                  data_end(out);
                }
                break;
              default:
                break;
            }
            break;
        }
      }
    }

    void parse(const char *buffer, int size, Object::Receiver out) {
      for (int i = 0; i < size; ++i) {
        auto ch = (unsigned char)buffer[i];

        // code
        if (m_state == CODE) {
          code(ch, out);

        // head
        } else if (m_state == HEAD) {
          head(ch, out);

        // data
        } else if (m_state == DATA) {
          data(ch, out);

        // utf8
        } else if (m_state == UTF8) {
          utf8(ch, out);

        // error
        } else {
          break;
        }
      }
    }

    void error(const char *msg) {
      Log::error("[hessian2] %s", msg);
      m_state = ERROR;
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

    State              m_state;
    int                m_head_size;
    int                m_data_size;
    int                m_char_size;
    ByteBuf<0x100>     m_head;
    ByteBuf<0x10000>   m_data;
    std::vector<Level> m_stack;

    std::vector<std::vector<std::string>> m_class_map;
  };

  //
  // Decoder
  //

  Decoder::Decoder() {
    m_parser = new Parser();
  }

  Decoder::~Decoder() {
    delete m_parser;
  }

  auto Decoder::help() -> std::list<std::string> {
    return {
      "Parses Hessian2 documents into abstract object streams",
    };
  }

  auto Decoder::clone() -> Module* {
    return new Decoder();
  }

  void Decoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {

    // Start parsing.
    if (obj->is<MessageStart>()) {
      m_is_body = true;
      m_parser->reset();
      out(std::move(obj));
      out(make_object<ListStart>());

    // End parsing.
    } else if (obj->is<MessageEnd>()) {
      m_is_body = false;
      out(make_object<ListEnd>());
      out(std::move(obj));

    // Parse.
    } else if (auto data = obj->as<Data>()) {
      if (m_is_body) {
        for (auto chunk : data->chunks()) {
          m_parser->parse(
            std::get<0>(chunk),
            std::get<1>(chunk),
            out
          );
        }
      }

    // Pass the other stuff.
    } else {
      out(std::move(obj));
    }
  }

  //
  // Encoder
  //

  Encoder::Encoder() {
  }

  Encoder::~Encoder() {
  }

  auto Encoder::help() -> std::list<std::string> {
    return {
      "Generates Hessian2 documents from abstract object streams",
    };
  }

  auto Encoder::clone() -> Module* {
    return new Encoder();
  }

  void Encoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {

    // Start encoding.
    if (obj->is<MessageStart>()) {
      m_buffer.clear();
      m_is_body = true;
      m_level = 0;
      out(std::move(obj));

    // Stop encoding.
    } else if (obj->is<MessageEnd>()) {
      if (!m_buffer.empty()) {
        out(make_object<Data>(std::move(m_buffer)));
      }
      m_is_body = false;
      out(std::move(obj));

    // Encode.
    } else if (m_is_body && obj->is<ValueObject>()) {
      if (obj->is<PrimitiveObject>()) {
        if (obj->is<NullValue>()) {
          m_buffer.push('N');

        } else if (auto v = obj->as<BoolValue>()) {
          m_buffer.push(v->value ? 'T' : 'F');

        } else if (auto v = obj->as<IntValue>()) {
          m_buffer.push('I');
          m_buffer.push((char)(v->value >> 24));
          m_buffer.push((char)(v->value >> 16));
          m_buffer.push((char)(v->value >> 8 ));
          m_buffer.push((char)(v->value >> 0 ));

        } else if (auto v = obj->as<LongValue>()) {
          m_buffer.push('L');
          m_buffer.push((char)(v->value >> 56));
          m_buffer.push((char)(v->value >> 48));
          m_buffer.push((char)(v->value >> 40));
          m_buffer.push((char)(v->value >> 32));
          m_buffer.push((char)(v->value >> 24));
          m_buffer.push((char)(v->value >> 16));
          m_buffer.push((char)(v->value >> 8 ));
          m_buffer.push((char)(v->value >> 0 ));

        } else if (auto v = obj->as<DoubleValue>()) {
          int64_t tmp; *(double*)&tmp = v->value;
          m_buffer.push('D');
          m_buffer.push((char)(tmp >> 56));
          m_buffer.push((char)(tmp >> 48));
          m_buffer.push((char)(tmp >> 40));
          m_buffer.push((char)(tmp >> 32));
          m_buffer.push((char)(tmp >> 24));
          m_buffer.push((char)(tmp >> 16));
          m_buffer.push((char)(tmp >> 8 ));
          m_buffer.push((char)(tmp >> 0 ));

        } else if (auto v = obj->as<StringValue>()) {
          if (std::strncmp(v->value.c_str(), "${{date}}", 9) == 0) {
            long long ll = std::stoll(v->value.substr(9, 13));

            m_buffer.push(0x4a);
            m_buffer.push((char)(ll >> 56));
            m_buffer.push((char)(ll >> 48));
            m_buffer.push((char)(ll >> 40));
            m_buffer.push((char)(ll >> 32));
            m_buffer.push((char)(ll >> 24));
            m_buffer.push((char)(ll >> 16));
            m_buffer.push((char)(ll >> 8 ));
            m_buffer.push((char)(ll >> 0 ));
          } else {
            push(v->value);
          }
        }

      } else if (obj->is<ListStart>()) {
        if (m_level++ > 0) m_buffer.push(0x57);

      } else if (obj->is<ListEnd>()) {
        if (--m_level > 0) m_buffer.push('Z');

      } else if (obj->is<MapStart>()) {
        m_buffer.push('H');
        m_level++;

      } else if (auto k = obj->as<MapKey>()) {
        push(k->key);

      } else if (obj->is<MapEnd>()) {
        m_buffer.push('Z');
        m_level--;
      }

      // Flush.
      if (m_buffer.size() >= 0x1000) {
        out(make_object<Data>(std::move(m_buffer)));
      }

    // Pass the other stuff.
    } else {
      out(std::move(obj));
    }
  }

  void Encoder::push(const std::string &str) {
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
      m_buffer.push((char)n);
      m_buffer.push(str.c_str());
    } else if (n < 1024) {
      m_buffer.push((char)(n >> 8) | 0x30);
      m_buffer.push((char)(n >> 0));
      m_buffer.push(str.c_str());
    } else if (n < 65536) {
      m_buffer.push('S');
      m_buffer.push((char)(n >> 8));
      m_buffer.push((char)(n >> 0));
      m_buffer.push(str.c_str());
    } else {
      Log::warn("[hessian2] string is too long (%d bytes)", (int)n);
      m_buffer.push('S');
      m_buffer.push(0xff);
      m_buffer.push(0xff);
      m_buffer.push(str.c_str(), 65536);
    }
  }

} // hessian2

NS_END
