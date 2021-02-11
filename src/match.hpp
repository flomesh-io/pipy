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

#ifndef MATCH_HPP
#define MATCH_HPP

#include "object.hpp"

#include <list>
#include <string>
#include <vector>

NS_BEGIN

//
// Match
//

class Match {
public:
  Match();
  Match(const std::string &path);
  Match(const Match &other);

  auto operator=(const Match &other) -> Match&;

  bool is_root() const { return m_path.size() == 0; }
  bool is_list() const { return !is_root() && m_path.back().is_array; }
  bool is_map() const { return !is_root() && !m_path.back().is_array; }
  auto key() const -> const std::string& { return m_path.back().key; }

  bool matching() const { return m_matched == m_path.size(); }

  void reset();
  void process(const Object *obj);

private:
  struct PathLevel {
    bool is_array;
    int index;
    std::string key;
  };

  struct StackLevel {
    bool is_array;
    int index;
  };

  std::vector<PathLevel> m_path;
  std::list<StackLevel> m_stack;
  int m_matched = 0;
};

NS_END

#endif // MATCH_HPP
