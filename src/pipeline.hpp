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

#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include "pjs/pjs.hpp"

#include <list>
#include <memory>
#include <set>

namespace pipy {

class Module;
class Filter;
class ReusableSession;

//
// Pipeline
//

class Pipeline : public pjs::RefCount<Pipeline> {
public:
  enum Type {
    NAMED,
    LISTEN,
    TASK,
  };

  static auto make(Module *module, Type type, const std::string &name) -> Pipeline* {
    return new Pipeline(module, type, name);
  }

  static void for_each(std::function<void(Pipeline*)> callback) {
    for (const auto p : s_all_pipelines) {
      callback(p);
    }
  }

  auto module() const -> Module* { return m_module; }
  auto type() const -> Type { return m_type; }
  auto name() const -> const std::string& { return m_name; }
  auto allocated() const -> size_t { return m_allocated; }
  auto active() const -> size_t { return m_active; }
  auto filters() const -> const std::list<std::unique_ptr<Filter>>& { return m_filters; }
  void append(Filter *filter);
  void bind();
  auto alloc() -> ReusableSession*;
  void free(ReusableSession *session);

private:
  Pipeline(Module *module, Type type, const std::string &name);
  ~Pipeline();

  Module* m_module;
  Type m_type;
  std::string m_name;
  std::list<std::unique_ptr<Filter>> m_filters;
  ReusableSession* m_pool = nullptr;
  size_t m_allocated = 0;
  size_t m_active = 0;

  static std::set<Pipeline*> s_all_pipelines;

  friend class pjs::RefCount<Pipeline>;
};

} // namespace pipy

#endif // PIPELINE_HPP