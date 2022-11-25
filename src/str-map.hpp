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

#ifndef STR_MAP_H
#define STR_MAP_H

#include "pjs/pjs.hpp"

#include <cstring>
#include <list>
#include <string>

namespace pipy {

//
// StrMap
//

class StrMap {
private:

  //
  // StrMap::Node
  //

  struct Node {
    pjs::Ref<pjs::Str> str;
    uint8_t start;
    uint8_t end;
    Node* children[0];

    Node() : Node(nullptr, 0, 0) {}
    Node(pjs::Str *s, uint8_t a, uint8_t b);
  };

public:

  //
  // StrMap::Parser
  //

  class Parser {
  public:
    Parser(const StrMap &map)
      : m_current_node(map.m_root) {}

    auto parse(char c) -> pjs::Str* {
      if (!m_current_node) return pjs::Str::empty;
      auto n = m_current_node;
      auto s = n->start;
      auto e = n->end;
      auto i = uint8_t(c);
      if (s <= i && i < e) {
        m_current_node = n = n->children[i - s];
        return n ? n->str : pjs::Str::empty;
      } else {
        return pjs::Str::empty;
      }
    }

  private:
    Node* m_current_node;
  };

  StrMap(const std::list<std::string> &strings);
  ~StrMap();

private:
  void insert_string(const std::string &str);
  auto create_node(pjs::Str *str, uint8_t start, uint8_t end) -> Node*;
  auto insert_node(Node *parent, uint8_t i, Node *child) -> Node*;
  void delete_node(Node *node);

  Node* m_root;
};

} // namespace pipy

#endif // STR_MAP_H
