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

#include "json.hpp"
#include "yajl/yajl_parse.h"
#include "utils.hpp"

NS_BEGIN

namespace json {

  //
  // Parser
  //

  class Parser {
  public:
    Parser() {
      m_handle = yajl_alloc(&s_callbacks, nullptr, this);
    }

    ~Parser() {
      yajl_free(m_handle);
    }

    void parse(const char *str, int size, Object::Receiver out) {
      m_out = out;
      yajl_parse(m_handle, (const unsigned char*)str, size);
    }

    void complete() {
      yajl_complete_parse(m_handle);
    }

  private:
    yajl_handle m_handle;
    Object::Receiver m_out;

    static yajl_callbacks s_callbacks;

    static int yajl_null(void *ctx) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<NullValue>());
      return 1;
    }

    static int yajl_boolean(void *ctx, int val) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<BoolValue>(val));
      return 1;
    }

    static int yajl_integer(void *ctx, long long val) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<LongValue>(val));
      return 1;
    }

    static int yajl_double(void *ctx, double val) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<DoubleValue>(val));
      return 1;
    }

    static int yajl_string(void *ctx, const unsigned char *val, size_t len) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<StringValue>(std::string((const char*)val, len)));
      return 1;
    }

    static int yajl_start_map(void *ctx) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<MapStart>());
      return 1;
    }

    static int yajl_map_key(void *ctx, const unsigned char *key, size_t len) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<MapKey>(std::string((const char*)key, len)));
      return 1;
    }

    static int yajl_end_map(void *ctx) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<MapEnd>());
      return 1;
    }

    static int yajl_start_array(void *ctx) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<ListStart>());
      return 1;
    }

    static int yajl_end_array(void *ctx) {
      auto parser = static_cast<Parser*>(ctx);
      parser->m_out(make_object<ListEnd>());
      return 1;
    }
  };

  yajl_callbacks Parser::s_callbacks = {
    &Parser::yajl_null,
    &Parser::yajl_boolean,
    &Parser::yajl_integer,
    &Parser::yajl_double,
    nullptr,
    &Parser::yajl_string,
    &Parser::yajl_start_map,
    &Parser::yajl_map_key,
    &Parser::yajl_end_map,
    &Parser::yajl_start_array,
    &Parser::yajl_end_array,
  };

  //
  // Decoder
  //

  Decoder::Decoder() {
  }

  auto Decoder::help() -> std::list<std::string> {
    return {
      "Parses JSON documents into abstract object streams",
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
      m_parser.reset(new Parser);
      out(std::move(obj));

    // End parsing.
    } else if (obj->is<MessageEnd>()) {
      if (m_parser) {
        m_parser->complete();
        m_parser.reset();
      }
      out(std::move(obj));

    // Parse.
    } else if (auto data = obj->as<Data>()) {
      if (m_parser) {
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

  auto Encoder::help() -> std::list<std::string> {
    return {
      "Generates JSON documents from abstract object streams",
      "indent = Indent width",
    };
  }

  void Encoder::config(const std::map<std::string, std::string> &params) {
    m_indent = std::atoi(utils::get_param(params, "indent", "0").c_str());
  }

  auto Encoder::clone() -> Module* {
    auto clone = new Encoder();
    clone->m_indent = m_indent;
    return clone;
  }

  void Encoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {

    // Start encoding.
    if (obj->is<MessageStart>()) {
      m_stack.clear();
      m_buffer = make_object<Data>();
      out(std::move(obj));

    // Stop encoding.
    } else if (obj->is<MessageEnd>()) {
      if (m_buffer) {
        if (m_buffer->size() > 0) {
          m_buffer->push('\n');
          out(std::move(m_buffer));
        }
        m_buffer.reset();
      }
      out(std::move(obj));

    // Encode.
    } else if (m_buffer && obj->is<ValueObject>()) {

      // Pretty print.
      if (m_indent > 0) {

        if (obj->is<ListEnd>()) {
          if (!m_stack.empty()) m_stack.pop_back();
          std::string str("\r\n");
          str += std::string(m_stack.size() * m_indent, ' ');
          str += ']';
          m_buffer->push(str);

        } else if (obj->is<MapEnd>()) {
          if (!m_stack.empty()) m_stack.pop_back();
          std::string str("\r\n");
          str += std::string(m_stack.size() * m_indent, ' ');
          str += '}';
          m_buffer->push(str);

        } else {
          std::string str;
          char parent = m_stack.empty() ? 0 : m_stack.back();

          if (auto k = obj->as<MapKey>()) {
            switch (parent) {
              case 'm': str += ",\r\n"; break;
              case 'M': str += "\r\n"; m_stack[m_stack.size()-1] = 'm'; break;
            }
            str += std::string(m_stack.size() * m_indent, ' ');
            str += '"';
            str += k->key;
            str += "\": ";
            m_buffer->push(str);

          } else {
            switch (parent) {
              case 'l': str += ','; // fall through
              case 'L':
                str += "\r\n";
                str += std::string(m_stack.size() * m_indent, ' ');
                m_stack[m_stack.size()-1] = 'l';
                break;
            }

            if (obj->is<ListStart>()) {
              m_stack.push_back('L');
              str += '[';

            } else if (obj->is<MapStart>()) {
              m_stack.push_back('M');
              str += '{';

            } else if (obj->is<NullValue>()) {
              str += "null";

            } else if (auto val = obj->as<BoolValue>()) {
              str += val->value ? "true" : "false";

            } else if (auto val = obj->as<IntValue>()) {
              str += std::to_string(val->value);

            } else if (auto val = obj->as<LongValue>()) {
              str += std::to_string(val->value);

            } else if (auto val = obj->as<DoubleValue>()) {
              str += std::to_string(val->value);

            } else if (auto val = obj->as<StringValue>()) {
              str += '"';
              str += escape(val->value);
              str += '"';
            }

            m_buffer->push(str);
          }
        }

      // Compact print
      } else {

        if (obj->is<ListEnd>()) {
          if (!m_stack.empty()) m_stack.pop_back();
          m_buffer->push(']');

        } else if (obj->is<MapEnd>()) {
          if (!m_stack.empty()) m_stack.pop_back();
          m_buffer->push('}');

        } else {
          std::string str;
          char parent = m_stack.empty() ? 0 : m_stack.back();

          if (auto k = obj->as<MapKey>()) {
            switch (parent) {
              case 'm': str += ","; break;
              case 'M': m_stack[m_stack.size()-1] = 'm'; break;
            }
            str += '"';
            str += escape(k->key);
            str += "\":";
            m_buffer->push(str);

          } else {
            switch (parent) {
              case 'l': str += ','; break;
              case 'L': m_stack[m_stack.size()-1] = 'l'; break;
            }

            if (obj->is<ListStart>()) {
              m_stack.push_back('L');
              str += '[';

            } else if (obj->is<MapStart>()) {
              m_stack.push_back('M');
              str += '{';

            } else if (obj->is<NullValue>()) {
              str += "null";

            } else if (auto val = obj->as<BoolValue>()) {
              str += val->value ? "true" : "false";

            } else if (auto val = obj->as<IntValue>()) {
              str += std::to_string(val->value);

            } else if (auto val = obj->as<LongValue>()) {
              str += std::to_string(val->value);

            } else if (auto val = obj->as<DoubleValue>()) {
              str += std::to_string(val->value);

            } else if (auto val = obj->as<StringValue>()) {
              str += '"';
              str += escape(val->value);
              str += '"';
            }

            m_buffer->push(str);
          }
        }
      }

    // Pass the other stuff.
    } else {
      out(std::move(obj));
    }
  }

  auto Encoder::escape(const std::string str) -> std::string {
    std::string escaped;
    for (auto c : str) {
      switch (c) {
        case '"': escaped += "\\\""; break;
        case '\r': escaped += "\\r"; break;
        case '\n': escaped += "\\n"; break;
        default: escaped += c; break;
      }
    }
    return escaped;
  }

} // namespace json

NS_END
