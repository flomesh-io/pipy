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

#ifndef XML_HPP
#define XML_HPP

#include "pjs/pjs.hpp"

#include <functional>

namespace pipy {

class Data;

//
// XML
//

class XML : public pjs::ObjectTemplate<XML> {
public:
  class Node : public pjs::ObjectTemplate<Node> {
  public:
    auto name() const -> pjs::Str* { return m_name; }
    auto attributes() const -> pjs::Object* { return m_attributes; }
    auto children() const -> pjs::Array* { return m_children; }

  private:
    Node(pjs::Str *name, pjs::Object *attributes, pjs::Array *children)
      : m_name(name), m_attributes(attributes), m_children(children) {}

    pjs::Ref<pjs::Str> m_name;
    pjs::Ref<pjs::Object> m_attributes;
    pjs::Ref<pjs::Array> m_children;

    friend class pjs::ObjectTemplate<Node>;
  };

  static auto parse(const std::string &str) -> Node*;
  static auto stringify(Node *doc, int space) -> std::string;
  static auto decode(const Data &data) -> Node*;
  static bool encode(Node *doc, int space, Data &data);
};

} // namespace pipy

#endif // XML_HPP