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

namespace pipy {

template<typename T, int S = 3>
class ScarcePointerArray {
public:
  ~ScarcePointerArray() {
    if (m_root) {
      erase(m_root);
    }
  }

  T* get(size_t i) const {
    Node *node = m_root;
    LevelIterator it(i);
    int n, level = it.next(n);

    if (!node || node->level < level) {
      return nullptr;
    }

    while (node->level > level) {
      node = node->branches[0];
      if (!node) return nullptr;
    }

    while (level > 0) {
      node = node->branches[n];
      if (!node) return nullptr;
      level = it.next(n);
    }

    return node->leaves[n];
  }

  T* set(size_t i, T *v) {
    Node *node = m_root;
    LevelIterator it(i);
    int n, level = it.next(n);

    if (v) {
      if (node) {
        while (node->level < level) {
          auto *p = new Node(node->level + 1);
          p->branches[0] = node;
          p->count = 1;
          node = p;
        }
      } else {
        node = new Node(level);
      }
    } else if (!node || node->level < level) {
      return nullptr;
    }

    Node **pp = &m_root; *pp = node;
    Node **path[sizeof(int) * 8 / S];
    int depth = 1; path[0] = pp;

    while (node->level > level) {
      auto p = node;
      pp = p->branches;
      node = *pp;
      if (!node) {
        if (!v) return nullptr;
        node = *pp = new Node(p->level - 1);
        p->count++;
      }
      path[depth++] = pp;
    }

    while (level > 0) {
      auto p = node;
      pp = p->branches + n;
      node = *pp;
      if (!node) {
        if (!v) return nullptr;
        node = *pp = new Node(level - 1);
        p->count++;
      }
      path[depth++] = pp;
      level = it.next(n);
    }

    if (v) {
      auto old = node->leaves[n];
      node->leaves[n] = v;
      if (!old) node->count++;
      return old;
    } else if (auto old = node->leaves[n]) {
      node->leaves[n] = nullptr;
      while (depth > 0) {
        pp = path[--depth];
        node = *pp;
        if (--node->count) break;
        delete node;
        *pp = nullptr;
      }
      return old;
    } else {
      return nullptr;
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

  //
  // ScarcePointerArray::LevelIterator
  //

  class LevelIterator {
  public:
    LevelIterator(size_t i) : m_i(i) {
      auto bits = i ? sizeof(int) * 8 - __builtin_clz(int(i)) : 0;
      m_level = std::max(1, int(bits + S - 1) / S);
    }

    int next(int &i) {
      auto mask = (1<<S) - 1;
      auto level = --m_level;
      i = mask & (m_i >> (S*level));
      return level;
    }

  private:
    size_t m_i;
    int m_level;
  };

  Node* m_root = nullptr;

  void erase(Node *node) {
    if (node->level > 0) {
      auto n = node->count;
      auto &branches = node->branches;
      for (int i = 0; n > 0 && i < (1<<S); i++) {
        if (auto *branch = branches[i]) {
          erase(branch);
          n--;
        }
      }
    }
    delete node;
  }
};

} // namespace pipy

#endif // SCARCE_HPP
