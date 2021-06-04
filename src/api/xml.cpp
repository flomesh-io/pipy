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
#include "data.hpp"
#include "utils.hpp"

#include <stack>

namespace pjs {

using namespace pipy;

//
// XML
//

template<> void ClassDef<XML>::init() {
  ctor();

  variable("Node", class_of<Constructor<XML::Node>>());

  method("parse", [](Context &ctx, Object *obj, Value &ret) {
    Str *str;
    if (!ctx.arguments(1, &str)) return;
    ret.set(XML::parse(str->str()));
  });

  method("stringify", [](Context &ctx, Object *obj, Value &ret) {
    XML::Node *doc;
    int space = 0;
    if (!ctx.arguments(1, &doc, &space)) return;
    ret.set(XML::stringify(doc, space));
  });

  method("decode", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    ret.set(XML::decode(*data));
  });

  method("encode", [](Context &ctx, Object *obj, Value &ret) {
    XML::Node *doc;
    int space = 0;
    if (!ctx.arguments(1, &doc, &space)) return;
    auto *data = pipy::Data::make();
    XML::encode(doc, space, *data);
    ret.set(data);
  });
}

//
// XML::Node
//

template<> void ClassDef<XML::Node>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *name;
    Object* attributes = nullptr;
    Array* children = nullptr;
    if (!ctx.arguments(1, &name, &attributes, &children)) return nullptr;
    return XML::Node::make(name, attributes, children);
  });

  accessor("name", [](Object *obj, Value &ret) { ret.set(obj->as<XML::Node>()->name()); });
  accessor("attributes", [](Object *obj, Value &ret) { ret.set(obj->as<XML::Node>()->attributes()); });
  accessor("children", [](Object *obj, Value &ret) { ret.set(obj->as<XML::Node>()->children()); });
}

template<> void ClassDef<Constructor<XML::Node>>::init() {
  super<Function>();
  ctor();
}

} // namespace pjs

namespace pipy {

//
// XMLParser
//

class XMLParser {
public:
  XMLParser() : m_parser(XML_ParserCreate(nullptr)) {
    XML_SetUserData(m_parser, this);
    XML_SetElementHandler(m_parser, xml_element_start, xml_element_end);
    XML_SetCharacterDataHandler(m_parser, xml_char_data);
    auto *root = XML::Node::make(pjs::Str::empty, pjs::Object::make(), pjs::Array::make());
    m_stack.push(root);
  }

  ~XMLParser() {
    while (!m_stack.empty()) {
      auto node = m_stack.top();
      node->retain()->release();
      m_stack.pop();
    }
    XML_ParserFree(m_parser);
  }

  auto parse(const std::string &str) -> XML::Node* {
    if (!XML_Parse(m_parser, str.c_str(), str.length(), true)) return nullptr;
    auto root = m_stack.top();
    m_stack.pop();
    return root;
  }

  auto parse(const Data &data) -> XML::Node* {
    for (const auto &c : data.chunks()) {
      if (!XML_Parse(m_parser, std::get<0>(c), std::get<1>(c), false)) {
        return nullptr;
      }
    }
    if (!XML_Parse(m_parser, nullptr, 0, true)) return nullptr;
    auto root = m_stack.top();
    m_stack.pop();
    return root;
  }

private:
  XML_Parser m_parser;
  std::stack<XML::Node*> m_stack;

  void element_start(const XML_Char *name, const XML_Char **attrs) {
    auto *attributes = attrs[0] ? pjs::Object::make() : nullptr;
    auto *children = pjs::Array::make();
    auto *node = XML::Node::make(pjs::Str::make(name), attributes, children);
    if (attributes) {
      for (int i = 0; attrs[i]; i += 2) {
        std::string k(attrs[i+0]);
        std::string v(attrs[i+1]);
        attributes->ht_set(k, v);
      }
    }
    append_child(node);
    m_stack.push(node);
  }

  void element_end(const XML_Char *name) {
    m_stack.pop();
  }

  void char_data(const XML_Char *str, int len) {
    append_child(std::string(str, len));
  }

  void append_child(const pjs::Value &v) {
    auto *parent = m_stack.top();
    auto *children = parent->children();
    children->push(v);
  }

  static void xml_element_start(void *userdata, const XML_Char *name, const XML_Char **attrs) {
    auto *parser = static_cast<XMLParser*>(userdata);
    parser->element_start(name, attrs);
  }

  static void xml_element_end(void *userdata, const XML_Char *name) {
    auto *parser = static_cast<XMLParser*>(userdata);
    parser->element_end(name);
  }

  static void xml_char_data(void *userdata, const XML_Char *str, int len) {
    int spaces = 0;
    while (spaces < len && std::isspace(str[spaces])) spaces++;
    if (spaces == len) return;
    auto *parser = static_cast<XMLParser*>(userdata);
    parser->char_data(str, len);
  }
};

//
// XML
//

auto XML::parse(const std::string &str) -> Node* {
  XMLParser parser;
  return parser.parse(str);
}

auto XML::stringify(Node *doc, int space) -> std::string {
  Data data;
  if (!encode(doc, space, data)) return "";
  return data.to_string();
}

auto XML::decode(const Data &data) -> Node* {
  XMLParser parser;
  return parser.parse(data);
}

bool XML::encode(Node *doc, int space, Data &data) {
  static std::string s_head("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");

  auto children = doc->children();
  if (!children || children->length() != 1) return false;

  pjs::Value root;
  children->get(0, root);
  if (!root.is_instance_of<XML::Node>()) return false;

  if (space < 0) space = 0;
  if (space > 10) space = 10;

  std::function<void(XML::Node *node, int)> write;

  write = [&](XML::Node *node, int l) {
    if (node->name() == 0) return;

    std::string padding(space * l, ' ');
    if (space) data.push(padding);
    data.push('<');
    data.push(node->name()->str());
    if (auto attrs = node->attributes()) {
      attrs->iterate_all([&](pjs::Str *k, pjs::Value &v) {
        auto *s = v.to_string();
        data.push(' ');
        data.push(k->str());
        data.push('=');
        data.push('"');
        data.push(s->str());
        data.push('"');
        s->release();
      });
    }

    bool is_closed = false;
    bool is_text = false;

    if (auto children = node->children()) {
      if (children->length() == 1) {
        pjs::Value front;
        children->get(0, front);
        if (!front.is_instance_of<XML::Node>()) {
          auto *s = front.to_string();
          data.push('>');
          data.push(s->str());
          s->release();
          is_closed = true;
          is_text = true;
        }
      }

      if (!is_text && children->length() > 0) {
        data.push('>');
        if (space) data.push('\n');
        is_closed = true;
        std::string padding(space * l + space, ' ');
        children->iterate_all([&](pjs::Value &v, int) {
          if (v.is_instance_of<XML::Node>()) {
            write(v.as<XML::Node>(), l + 1);
          } else {
            if (space) data.push(padding);
            auto *s = v.to_string();
            data.push(s->str());
            s->release();
            if (space) data.push('\n');
          }
        });
      }
    }

    if (is_closed) {
      if (space && !is_text) data.push(padding);
      data.push("</");
      data.push(node->name()->str());
      data.push('>');
      if (space) data.push('\n');
    } else {
      data.push("/>");
      if (space) data.push('\n');
    }
  };

  data.push(s_head);
  if (space) data.push('\n');
  write(root.as<XML::Node>(), 0);
  return true;
}

} // namespace pipy