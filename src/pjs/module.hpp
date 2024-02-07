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

#ifndef PJS_MODULE_HPP
#define PJS_MODULE_HPP

#include "stmt.hpp"

namespace pjs {

//
// Module
//

class Module : public RefCount<Module> {
public:
  static auto make(Instance *instance, const std::string &name, const std::string &source) -> Module* {
    return new Module(instance, name, source);
  }

  auto instance() const -> Instance* { return m_instance; }
  auto id() const -> int { return m_id; }
  auto name() const -> const std::string& { return m_source.filename; }
  auto source() const -> const std::string& { return m_source.content; }
  auto tree() const -> Stmt* { return m_tree.get(); }

  auto add_fiber_variable() -> int;
  auto new_fiber_data() -> Data*;
  bool compile(std::string &error, int &error_line, int &error_column);
  void execute(Context &ctx, int l, Value &result);

private:
  Module(Instance *instance, const std::string &name, const std::string &source);

  Ref<Instance> m_instance;
  int m_id;
  int m_fiber_variable_count = 0;
  Source m_source;
  Tree::Scope m_scope;
  std::unique_ptr<Stmt> m_tree;
  std::unique_ptr<Tree::Imports> m_imports;

  friend class RefCount<Module>;
};

} // namespace pjs

#endif // PJS_MODULE_HPP
