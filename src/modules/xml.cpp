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

#include "xml.hpp"
#include "expat.h"
#include "utils.hpp"

NS_BEGIN

namespace xml {

  //
  // Parser
  //

  class Parser {
  public:
    Parser() {
      m_parser = XML_ParserCreate(nullptr);
      reset();
    }

    ~Parser() {
      XML_ParserFree(m_parser);
    }

    std::string m_array_hint;

    void reset() {
      XML_ParserReset(m_parser, nullptr);
      XML_SetUserData(m_parser, this);
      XML_SetElementHandler(m_parser, xml_element_start, xml_element_end);
      XML_SetCharacterDataHandler(m_parser, xml_char_data);
      m_stack.clear();
    }

    void parse(const char *str, int len, Object::Receiver out) {
      m_out = out;
      XML_Parse(m_parser, str, len, 0);
    }

  private:
    struct Node {
      std::string tag;
      std::string value;
      std::string array_tag;
      bool is_map;
      bool is_array_element;
    };

    XML_Parser m_parser;
    std::vector<Node> m_stack;
    Object::Receiver m_out;

    static void xml_element_start(void *userdata, const XML_Char *name, const XML_Char **attrs) {
      Parser *parser = static_cast<Parser*>(userdata);
      parser->element_start(name, attrs);
    }

    static void xml_element_end(void *userdata, const XML_Char *name) {
      Parser *parser = static_cast<Parser*>(userdata);
      parser->element_end(name);
    }

    static void xml_char_data(void *userdata, const XML_Char *str, int len) {
      int spaces = 0;
      while (spaces < len && (unsigned char)str[spaces] <= ' ') spaces++;
      if (spaces == len) return;
      Parser *parser = static_cast<Parser*>(userdata);
      parser->char_data(str, len);
    }

    void element_start(const XML_Char *name, const XML_Char **attrs) {
      bool is_array_element = false;
      const auto &array_hint = m_array_hint;
      if (!array_hint.empty()) {
        for (int i = 0; attrs[i]; i += 2) {
          if (array_hint == attrs[i]) {
            is_array_element = true;
            break;
          }
        }
      }

      if (!m_stack.empty()) {
        auto &parent = m_stack.back();
        if (!parent.is_map) {
          parent.is_map = true;
          if (!parent.is_array_element) {
            m_out(make_object<MapKey>(parent.tag));
          }
          m_out(make_object<MapStart>());
        }
        if (is_array_element) {
          if (parent.array_tag != name) {
            if (!parent.array_tag.empty()) {
              m_out(make_object<ListEnd>());
            }
            m_out(make_object<MapKey>(name));
            m_out(make_object<ListStart>());
            parent.array_tag = name;
          }
        } else if (!parent.array_tag.empty()) {
          m_out(make_object<ListEnd>());
          parent.array_tag.clear();
        }
      }

      m_stack.emplace_back();
      auto &node = m_stack.back();
      node.tag = name;
      node.is_array_element = is_array_element;
    }

    void element_end(const XML_Char *name) {
      const auto &node = m_stack.back();
      if (node.is_map) {
        if (!node.array_tag.empty()) {
          m_out(make_object<ListEnd>());
        }
        m_out(make_object<MapEnd>());
      } else if (node.is_array_element) {
        m_out(make_object<StringValue>(node.value));
      } else {
        m_out(make_object<MapKey>(node.tag));
        m_out(make_object<StringValue>(node.value));
      }
      m_stack.pop_back();
    }

    void char_data(const XML_Char *str, int len) {
      m_stack.back().value += std::string(str, len);
    }
  };

  //
  // Decoder
  //

  Decoder::Decoder() {
    m_parser.reset(new Parser);
  }

  auto Decoder::help() -> std::list<std::string> {
    return {
      "Parses XML documents into abstract object streams",
      "array_hint = Name of the attribute that indicates a tag being an array element",
    };
  }

  void Decoder::config(const std::map<std::string, std::string> &params) {
    m_parser->m_array_hint = utils::get_param(params, "array_hint", "");
  }

  auto Decoder::clone() -> Module* {
    auto decoder = new Decoder();
    decoder->m_parser->m_array_hint = m_parser->m_array_hint;
    return decoder;
  }

  void Decoder::pipe(
    std::shared_ptr<Context> ctx,
    std::unique_ptr<Object> obj,
    Object::Receiver out
  ) {

    // Start parsing.
    if (obj->is<MessageStart>()) {
      m_parser->reset();
      m_parsing = true;
      out(std::move(obj));
      out(make_object<MapStart>());

    // End parsing.
    } else if (obj->is<MessageEnd>()) {
      m_parsing = false;
      out(make_object<MapEnd>());
      out(std::move(obj));

    // Parse.
    } else if (auto data = obj->as<Data>()) {
      if (m_parsing) {
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
      "Generates XML documents from abstract object streams",
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
      m_current_key.clear();
      m_tag_stack.clear();
      m_buffer = make_object<Data>();
      m_buffer->push("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
      if (m_indent > 0) m_buffer->push("\r\n");
      out(std::move(obj));

    // Stop encoding.
    } else if (obj->is<MessageEnd>()) {
      if (m_buffer) {
        if (m_buffer->size() > 0) out(std::move(m_buffer));
        m_buffer.reset();
      }
      out(std::move(obj));

    // Encode.
    } else if (m_buffer) {
      if (obj->is<ValueObject>()) {
        if (obj->is<CollectionObject>()) {
          if (obj->is<ListStart>()) {
            // TODO: Handle array start.

          } else if (obj->is<ListEnd>()) {
            // TODO: Handle array end.

          } else if (obj->is<MapStart>()) {
            if (!m_current_key.empty()) {
              std::string str(m_indent > 0 ? m_tag_stack.size() * m_indent : 0, ' ');
              str += '<';
              str += m_current_key;
              str += ">";
              if (m_indent > 0) str += "\r\n";
              m_buffer->push(str);
              m_tag_stack.push_back(m_current_key);
            }

          } else if (auto k = obj->as<MapKey>()) {
            m_current_key = k->key;

          } else if (obj->is<MapEnd>()) {
            if (!m_tag_stack.empty()) {
              std::string str(m_indent > 0 ? m_tag_stack.size() * m_indent - m_indent : 0, ' ');
              str += "</";
              str += m_tag_stack.back();
              str += ">";
              if (m_indent > 0) str += "\r\n";
              m_buffer->push(str);
              m_tag_stack.pop_back();
            }
          }
        } else {
          std::string str(m_indent > 0 ? m_tag_stack.size() * m_indent : 0, ' ');
          str += '<';
          str += m_current_key;
          str += '>';

          if (obj->is<NullValue>()) {
            str += "null";

          } else if (auto val = obj->as<BoolValue>()) {
            str += val->value ? "true" : "false";

          } else if (auto val = obj->as<IntValue>()) {
            str += std::to_string(val->value);

          } else if (auto val = obj->as<LongValue>()) {
            str += std::to_string(val->value);

          } else if (auto val = obj->as<DoubleValue>()) {
            char buf[100];
            std::sprintf(buf, "%g", val->value);
            str += buf;

          } else if (auto val = obj->as<StringValue>()) {
            for (size_t i = 0; i < val->value.length(); i++) {
              auto c = val->value[i];
              switch (c) {
                case '<': str += "&lt;"; break;
                case '>': str += "&gt;"; break;
                default: str += c; break;
              }
            }
          }

          str += "</";
          str += m_current_key;
          str += ">";
          if (m_indent > 0) str += "\r\n";
          m_buffer->push(str);
        }
      }

    // Pass the other stuff.
    } else {
      out(std::move(obj));
    }
  }

} // namespace xml

NS_END
