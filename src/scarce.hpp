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

#ifndef SCARCE_HPP
#define SCARCE_HPP

#include "pjs/pjs.hpp"

#include <algorithm>
#include <string>
#include <functional>

namespace pipy {

template<typename T, int S = 3>
class ScarcePointerArray {
public:
  ScarcePointerArray()
    : m_root(new Node(0)) {}

  ~ScarcePointerArray() {
    erase(m_root);
  }

  auto get(size_t i) const -> T* {
    T *leaf = nullptr;
    Node *node = m_root;
    for_each_level(
      i, [&](int level, int i) {
        if (level > 0) {
          if (node->level < level) {
            if (i > 0) return false;
          } else {
            while (node->level > level) {
              node = node->branches[0];
              if (!node) return false;
            }
            node = node->branches[i];
          }
        } else {
          leaf = node->leaves[i];
        }
        return true;
      }
    );
    return leaf;
  }

  void set(size_t i, T *v) {
    Node *node = m_root;
    Node **pp = &m_root;
    Node **path[sizeof(int) * 8 / S];
    int depth = 0;
    for_each_level(
      i, [&](int level, int i) {
        if (node->level < level) {
          do {
            auto *p = new Node(node->level + 1);
            p->branches[0] = node;
            p->count = 1;
            node = p;
          } while (node->level < level);
        } else {
          while (node->level > level) {
            pp = node->branches;
            auto p = *pp;
            if (!p) {
              p = *pp = new Node(node->level - 1);
              node->count++;
            }
            node = p;
          }
        }
        if (level > 0) {
          path[depth++] = pp;
          pp = node->branches + i;
          auto next = *pp;
          if (!next) {
            next = *pp = new Node(level - 1);
            node->count++;
          }
          node = next;
        } else if (v) {
          if (!node->leaves[i]) node->count++;
          node->leaves[i] = v;
        } else if (node->leaves[i]) {
          node->leaves[i] = nullptr;
          node->count--;
        }
        return true;
      }
    );
    if (!v) {
      while (depth > 0) {
        auto pp = path[--depth];
        auto node = *pp;
        if (!node->count) {
          delete node;
          *pp = nullptr;
          if (depth > 0) {
            node = *path[depth-1];
            node->count--;
          }
        }
      }
    }
  }

private:

  //
  // ScarcePointerArray::Node
  //

  struct Node : public pjs::Pooled<Node> {
    int level;
    int count = 0;
    union {
      Node* branches[1<<S];
      T* leaves[1<<S];
    };

    Node(int l) : level(l) {
      std::memset(branches, 0, sizeof(branches));
    }
  };

  Node* m_root;

  void for_each_level(size_t i, const std::function<bool(int, int)> &f) const {
    auto mask = (1<<S) - 1;
    auto bits = i ? sizeof(int) * 8 - __builtin_clz(int(i)) : 0;
    auto segs = std::max(1, int(bits + S - 1) / S);
    for (
      int level = segs - 1;
      level >= 0 && f(level, mask & (i >> (S*level)));
      level--
    ) {}
  }

  void erase(Node *node) {
    if (node->level > 0) {
      auto &branches = node->branches;
      for (int i = 0; i < (1<<S); i++) {
        if (auto *branch = branches[i]) {
          erase(branch);
        }
      }
    }
    delete node;
  }
};

} // namespace pipy

#endif // SCARCE_HPP
