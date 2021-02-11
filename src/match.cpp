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

#include "match.hpp"

NS_BEGIN

Match::Match() {
}

Match::Match(const std::string &path) {
  for (size_t i = 0; i < path.size(); ++i) {
    size_t j = i;
    while (j < path.size() && path[j] != '/') ++j;
    if (j > i) {
      PathLevel level;
      std::string name(path.c_str() + i, j - i);
      if (name.front() == '[' && name.back() == ']') {
        level.is_array = true;
        level.index = std::atoi(name.c_str() + 1);
      } else {
        level.is_array = false;
        level.index = -1;
        level.key = name;
      }
      m_path.push_back(level);
    }
    i = j;
  }
}

Match::Match(const Match &other) {
  m_path = other.m_path;
}

auto Match::operator=(const Match &other) -> Match& {
  m_path = other.m_path;
  reset();
  return *this;
}

void Match::reset() {
  m_stack.clear();
  m_matched = 0;
}

void Match::process(const Object *obj) {
  if (obj->is<MapEnd>() || obj->is<ListEnd>()) {
    if (m_stack.size() > 0) m_stack.pop_back();
    if (m_matched > m_stack.size()) m_matched = m_stack.size();

  } else {
    if (m_stack.size() > 0) {
      auto &top = m_stack.back();
      if (top.is_array) top.index++;
    }

    if (m_stack.size() > 0) {
      auto &top = m_stack.back();

      // Match with array indices.
      if (top.is_array) {

        // Start of the match.
        if (m_matched < m_path.size() &&
            m_matched + 1 == m_stack.size()
        ) {
          const auto &seg = m_path[m_matched];
          if (seg.is_array && top.index == seg.index) ++m_matched;

        // End of the match.
        } else if (m_matched == m_stack.size()) {
          if (top.index != m_path[m_matched - 1].index) {
            m_matched--;
          }
        }

      // Match with object keys.
      } else if (auto key = obj->as<MapKey>()) {

        // End of the match.
        if (m_matched == m_stack.size()) {
          m_matched--;
        }

        // Start of the match.
        if (m_matched < m_path.size() &&
            m_matched + 1 == m_stack.size()
        ) {
          const auto &seg = m_path[m_matched];
          if (!seg.is_array && key->key == seg.key) ++m_matched;
        }
      }
    }

    if (obj->is<MapStart>()) {
      m_stack.push_back({ false, -1 });
    } else if (obj->is<ListStart>()) {
      m_stack.push_back({ true, -1 });
    }
  }
}

NS_END