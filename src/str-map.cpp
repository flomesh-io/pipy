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

#include "str-map.hpp"

#include <cstdlib>

namespace pipy {

//
// StrMap::Node
//


StrMap::Node::Node(pjs::Str *s, uint8_t a, uint8_t b)
  : str(s), start(a), end(b)
{
  std::memset(children, 0, sizeof(Node*) * (b - a));
}

//
// StrMap
//

StrMap::StrMap(const std::list<std::string> &strings)
  : m_root(new Node)
{
  for (const auto &s : strings) {
    insert_string(s);
  }
}

StrMap::~StrMap() {
  delete_node(m_root);
}

void StrMap::insert_string(const std::string &str) {
  Node** r = &m_root;
  Node*  n = *r;
  size_t p = 0;
  while (p < str.length()) {
    auto a = n->start;
    auto b = n->end;
    auto i = uint8_t(str[p++]);
    if (a <= i && i < b && n->children[i-a]) {
      r = &n->children[i-a];
      n = *r;
    } else {
      auto parent = n;
      auto node = n = new Node;
      for (size_t i = str.length(); i > p; i--) {
        auto c = uint8_t(str[i-1]);
        auto n = create_node(nullptr, c, c+1);
        n->children[0] = node;
        node = n;
      }
      *r = insert_node(parent, i, node);
      break;
    }
  }
  n->str = pjs::Str::make(str);
}

auto StrMap::create_node(pjs::Str *str, uint8_t start, uint8_t end) -> Node* {
  auto p = static_cast<Node*>(std::malloc(sizeof(Node) + sizeof(Node*) * (end - start)));
  new (p) Node(str, start, end);
  return p;
}

auto StrMap::insert_node(Node *parent, uint8_t i, Node *child) -> Node* {
  auto a = parent->start;
  auto b = parent->end;

  if (a <= i && i < b) {
    parent->children[i-a] = child;
    return parent;

  } else if (i < a) {
    auto d = a - i;
    auto p = create_node(parent->str, i, b);
    std::memcpy(p->children + d, parent->children, sizeof(Node*) * (b - a));
    p->children[0] = child;
    delete parent;
    return p;

  } else {
    auto p = create_node(parent->str, a, i + 1);
    std::memcpy(p->children, parent->children, sizeof(Node*) * (b - a));
    p->children[i - a] = child;
    delete parent;
    return p;
  }
}

void StrMap::delete_node(Node *node) {
  auto a = node->start;
  auto b = node->end;
  auto n = b - a;
  for (int i = 0; i < n; i++) {
    if (auto *c = node->children[i]) {
      delete_node(c);
    }
  }
  delete node;
}

} // namespace pipy
